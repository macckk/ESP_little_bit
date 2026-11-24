// Pets em pixel art: o backend converte as imagens de pets/ e lista {"n":nome,"v":versao} em "sprites".
// A task do backend (core 0) baixa o que falta para LittleFS (/sprites/<nome>-<versao>.bin) e apaga o que saiu.
// O pet (core 1) le a pasta e carrega o pacote escolhido na RAM (~1 KB por quadro).
// Formato do pacote: ver backend/app/sprites.py.
#include <HTTPClient.h>
#include <LittleFS.h>
#include <sys/stat.h>

#include "app.h"
#include "pet_builtin.h"  // pet reserva (tools/pet_embed.py)

static constexpr const char* DIR_PATH = "/sprites";
static constexpr size_t MAX_PACK = 16 * 1024;
static constexpr int MAX_LIST = 12;
static volatile uint32_t generation = 1;

uint32_t sprites_generation() { return generation; }

// mkdir POSIX direto: o LittleFS.exists/mkdir do Arduino loga erro quando o caminho ainda nao existe
static void ensureDir() {
  static bool done = false;
  if (!done) done = ::mkdir("/littlefs/sprites", 0755) == 0 || errno == EEXIST;
}

static bool download(const char* name, const String& path) {
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(gNet.url + "/device/" + gDeviceId + "/sprite/" + name)) return false;
  if (gNet.token.length()) http.addHeader("Authorization", "Bearer " + gNet.token);
  int code = http.GET();
  int len = http.getSize();
  bool ok = false;
  if (code == 200 && len > 30 && len <= (int)MAX_PACK) {
    String tmp = String(DIR_PATH) + "/tmp";
    File f = LittleFS.open(tmp, "w");
    if (f) {
      int n = http.writeToStream(&f);
      f.close();
      if (n == len && LittleFS.rename(tmp, path)) ok = true;
      else LittleFS.remove(tmp);
    }
  }
  http.end();
  Serial.printf("SPRITE %s: %s (HTTP %d, %d B)\n", name, ok ? "baixado" : "falhou", code, len);
  return ok;
}

void sprites_sync(const String& body) {
  JsonDocument filter;
  filter["sprites"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
  if (doc["sprites"].isNull()) return;  // outro layout: nao mexe nos pets guardados
  JsonArrayConst list = doc["sprites"].as<JsonArrayConst>();

  // o que deve ficar: <nome>-<versao>.bin
  String keep[MAX_LIST], names[MAX_LIST];
  int nkeep = 0;
  for (JsonObjectConst s : list) {
    const char* n = s["n"] | "";
    const char* v = s["v"] | "";
    if (!*n || strlen(n) > 12 || strlen(v) > 8 || nkeep >= MAX_LIST) continue;
    names[nkeep] = n;
    keep[nkeep++] = String(n) + "-" + v + ".bin";
  }

  // 1) apaga primeiro o que saiu ou mudou de versao (a particao e pequena: nao da para ter as duas versoes)
  ensureDir();
  bool changed = false;
  String have[MAX_LIST], del[MAX_LIST * 2];
  int nhave = 0, ndel = 0;
  File dir = LittleFS.open(DIR_PATH);
  for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
    String fn = f.name();
    bool wanted = false;
    for (int i = 0; i < nkeep; i++) wanted |= fn == keep[i];
    if (wanted && nhave < MAX_LIST) have[nhave++] = fn;
    else if (!wanted && ndel < MAX_LIST * 2) del[ndel++] = String(DIR_PATH) + "/" + fn;
  }
  if (dir) dir.close();
  for (int i = 0; i < ndel; i++) {
    LittleFS.remove(del[i]);
    Serial.printf("SPRITE removido %s\n", del[i].c_str());
    changed = true;
  }

  // 2) baixa o que falta, se couber (sem espaco, pula: o LittleFS cheio chegou a travar a placa)
  for (int i = 0; i < nkeep; i++) {
    bool exists = false;
    for (int j = 0; j < nhave; j++) exists |= have[j] == keep[i];
    if (exists) continue;
    size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeB < MAX_PACK + 8192) {
      Serial.printf("SPRITE %s: sem espaco (%u B livres)\n", names[i].c_str(), (unsigned)freeB);
      continue;
    }
    if (download(names[i].c_str(), String(DIR_PATH) + "/" + keep[i])) changed = true;
  }
  if (changed) generation = generation + 1;
}

// nome do arquivo "gato-1a2b3c4d.bin" -> "gato"
static String baseName(const String& fn) {
  int dash = fn.lastIndexOf('-');
  return dash > 0 && fn.endsWith(".bin") ? fn.substring(0, dash) : String();
}

static String findFile(const char* name) {
  ensureDir();
  File dir = LittleFS.open(DIR_PATH);
  if (!dir) return String();
  String found;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String fn = f.name();
    if (baseName(fn) == name) found = String(DIR_PATH) + "/" + fn;
  }
  dir.close();
  return found;
}

int sprites_names(String* out, int max) {
  ensureDir();
  int n = 0;
  bool hasBuiltin = false;
  File dir = LittleFS.open(DIR_PATH);
  for (File f = dir ? dir.openNextFile() : File(); f && n < max; f = dir.openNextFile()) {
    String b = baseName(f.name());
    bool dup = false;
    for (int i = 0; i < n; i++) dup |= out[i] == b;
    if (b.length() && !dup) out[n++] = b;
    hasBuiltin |= b == BUILTIN_PET_NAME;
  }
  if (dir) dir.close();
  if (!hasBuiltin && n < max) out[n++] = BUILTIN_PET_NAME;  // o embutido sempre esta na lista
  for (int i = 1; i < n; i++)  // ordem alfabetica (estavel entre boots)
    for (int j = i; j > 0 && out[j] < out[j - 1]; j--) std::swap(out[j], out[j - 1]);
  return n;
}

void sprites_free(SpritePack& p) {
  free(p.data);
  p = SpritePack{};
}

static bool parsePack(const char* name, uint8_t* buf, size_t len, SpritePack& p, int maxFrames);

bool sprites_load(const char* name, SpritePack& p, int maxFrames) {
  sprites_free(p);
  String path = findFile(name);
  if (!path.length()) {  // nao baixado: o embutido no firmware, se for ele
    if (strcmp(name, BUILTIN_PET_NAME)) return false;
    uint8_t* buf = (uint8_t*)malloc(sizeof BUILTIN_PET);
    if (buf) memcpy(buf, BUILTIN_PET, sizeof BUILTIN_PET);
    return parsePack(name, buf, sizeof BUILTIN_PET, p, maxFrames);
  }
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t len = f.size();
  uint8_t* buf = len > 30 && len <= MAX_PACK ? (uint8_t*)malloc(len) : nullptr;
  bool ok = buf && f.read(buf, len) == len;
  f.close();
  if (!ok) {
    free(buf);
    return false;
  }
  return parsePack(name, buf, len, p, maxFrames);
}

// Valida o pacote e fica com buf, encolhido para so os quadros (ou so os maxFrames primeiros: miniaturas).
// Libera buf se invalido.
static bool parsePack(const char* name, uint8_t* buf, size_t len, SpritePack& p, int maxFrames) {
  bool ok = buf != nullptr;
  // cabecalho: "CYDP" ver w h nframes ncolors label[16]
  if (ok) ok = !memcmp(buf, "CYDP", 4) && buf[4] == 1 && buf[5] && buf[6] && buf[7] && buf[8] && buf[8] <= 16;
  if (ok) {
    p.w = buf[5];
    p.h = buf[6];
    p.nframes = buf[7];
    p.ncolors = buf[8];
    size_t off = 9 + 16;
    size_t frameBytes = (size_t)((p.w + 1) / 2) * p.h;
    size_t need = off + 3 * p.ncolors + p.nframes + frameBytes * p.nframes;
    ok = len >= need;
    if (ok) {
      memcpy(p.label, buf + 9, 15);
      p.label[15] = 0;
      for (int i = 0; i < p.ncolors; i++, off += 3) p.pal[i] = (buf[off] << 16) | (buf[off + 1] << 8) | buf[off + 2];
      memcpy(p.roles, buf + off, min<int>(p.nframes, sizeof p.roles));
      off += p.nframes;
      if (maxFrames > 0 && p.nframes > maxFrames) p.nframes = maxFrames;
      memmove(buf, buf + off, frameBytes * p.nframes);  // so os quadros ficam na RAM
      uint8_t* small = (uint8_t*)realloc(buf, frameBytes * p.nframes);
      p.data = small ? small : buf;
      strlcpy(p.name, name, sizeof p.name);
    }
  }
  if (!ok) {
    free(buf);
    p = SpritePack{};
    Serial.printf("SPRITE %s: arquivo invalido\n", name);
  }
  return ok;
}

int sprites_frame_for(const SpritePack& p, uint8_t role) {
  for (int i = 0; i < p.nframes && i < (int)sizeof p.roles; i++)
    if (p.roles[i] == role) return i;
  return -1;
}
