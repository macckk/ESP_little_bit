"""Backend do CYD (FastAPI): a mesma placa mostra varios "apps" (app/apps/); cada placa tem um app ativo.

  POST /events/claude                 <- hooks do Claude Code (Bearer HOOK_TOKEN) -> app "pet"
  POST /events/statusline             <- linha de status do Claude Code de terminal (Bearer HOOK_TOKEN)
  GET  /device/{id}/screen            -> JSON da tela do app ativo; ?wait=N faz long-poll (ate 30 s) com If-None-Match
  POST /device/{id}/action            <- toque na placa: {"action", "value"}; action "app" troca o app (Bearer DEVICE_TOKEN)
  GET  /device/{id}/sprite/{nome}     -> pacote do pet em pixel art (pets/)
  POST /device/{id}/app/{app}         <- troca o app pelo PC (Bearer HOOK_TOKEN)
  GET  /apps                          -> apps disponiveis
  GET  /events/recent                 -> ultimos eventos (Bearer HOOK_TOKEN)
  GET  /health

Rodar:  .venv\\Scripts\\python serve.py   (dentro de backend/)
"""
import asyncio
import hashlib
import json
import secrets
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI, Header, HTTPException, Request, Response

from . import config
from .apps import build_apps, load_config
from .apps.pet_app import PetApp

APPS_CFG = load_config(config.APPS_CONFIG)
APPS = build_apps(APPS_CFG)
DEFAULT_APP = APPS_CFG.get("default", "pet") if APPS_CFG.get("default", "pet") in APPS else next(iter(APPS))
pet_app: PetApp | None = APPS.get("pet")  # type: ignore[assignment]
SLOW_SECONDS = 20  # trabalho lento dos apps (rede, arquivos)

devices_seen: dict[str, float] = {}
_changed = asyncio.Condition()
_screens: dict[str, dict] = {a: {"body": b"", "etag": ""} for a in APPS}


# ------------------------------------------------------------------ placas


class Devices:
    """App ativo de cada placa, salvo em data/devices.json."""

    def __init__(self, path):
        self.path = path
        try:
            self.data: dict[str, dict] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.data = {}

    def app_of(self, device_id: str) -> str:
        a = self.data.get(device_id, {}).get("app", DEFAULT_APP)
        return a if a in APPS else DEFAULT_APP

    def set_app(self, device_id: str, app_id: str) -> None:
        self.data.setdefault(device_id, {})["app"] = app_id
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, indent=1), encoding="utf-8")


devices = Devices(config.DEVICES_PATH)


def _check(authorization: str | None, expected: str) -> None:
    if expected and not secrets.compare_digest(authorization or "", f"Bearer {expected}"):
        raise HTTPException(status_code=401, detail="token invalido")


def _menu() -> list[dict]:
    return [{"id": a.id, "name": a.name} for a in APPS.values()]


async def _refresh(app_ids=None) -> None:
    """Recalcula as telas; se alguma mudou, acorda quem esta em long-poll."""
    changed = False
    for a in app_ids or APPS:
        try:
            screen = APPS[a].screen()
        except Exception as e:  # app com defeito nao derruba os outros
            screen = {"layout": "message", "title": APPS[a].name, "icon": "err", "text": f"Erro no app: {e}"}
        screen["app"] = a
        screen["apps"] = _menu()  # menu de apps na placa (toque no titulo)
        body = json.dumps(screen, ensure_ascii=False, separators=(",", ":")).encode()
        etag = '"' + hashlib.sha1(body).hexdigest()[:16] + '"'
        if etag != _screens[a]["etag"]:
            _screens[a] = {"body": body, "etag": etag}
            changed = True
    if changed:
        await _notify()


async def _notify() -> None:
    async with _changed:
        _changed.notify_all()


async def _slow_work() -> None:
    for a in APPS.values():
        try:
            await asyncio.to_thread(a.refresh)  # fora do loop async
        except Exception as e:
            print(f"app {a.id}: refresh falhou: {e}")


async def _ticker() -> None:
    # transicoes por tempo (pet: terminou -> ocioso -> dormindo) e trabalho lento dos apps
    last_slow = 0.0
    while True:
        await asyncio.sleep(5)
        if time.time() - last_slow >= SLOW_SECONDS:
            last_slow = time.time()
            await _slow_work()
        await _refresh()


@asynccontextmanager
async def lifespan(_app: FastAPI):
    await _slow_work()
    await _refresh()
    task = asyncio.create_task(_ticker())
    yield
    task.cancel()


app = FastAPI(title="CYD backend", lifespan=lifespan)


# ------------------------------------------------------ eventos do Claude Code


@app.post("/events/claude", status_code=204)
async def claude_event(request: Request, authorization: str | None = Header(default=None)):
    _check(authorization, config.HOOK_TOKEN)
    try:
        ev = await request.json()
    except ValueError:
        raise HTTPException(status_code=400, detail="json invalido")
    if not isinstance(ev, dict):
        raise HTTPException(status_code=400, detail="esperado objeto")
    if pet_app and pet_app.handle_event(ev):
        await _refresh(["pet"])
    return Response(status_code=204)


@app.post("/events/statusline", status_code=204)
async def statusline_event(request: Request, authorization: str | None = Header(default=None)):
    _check(authorization, config.HOOK_TOKEN)
    try:
        s = await request.json()
    except ValueError:
        raise HTTPException(status_code=400, detail="json invalido")
    if isinstance(s, dict) and pet_app:
        pet_app.handle_statusline(s)
        await _refresh(["pet"])
    return Response(status_code=204)


# ------------------------------------------------------------------- placa


@app.get("/device/{device_id}/screen")
async def device_screen(
    device_id: str,
    wait: int = 0,
    authorization: str | None = Header(default=None),
    if_none_match: str | None = Header(default=None),
):
    _check(authorization, config.DEVICE_TOKEN)
    devices_seen[device_id] = time.time()

    def current() -> dict:  # muda se a tela do app mudar ou se a placa trocar de app
        return _screens[devices.app_of(device_id)]

    wait = max(0, min(wait, 30))
    if wait and if_none_match and if_none_match == current()["etag"]:
        try:
            async with _changed:
                await asyncio.wait_for(_changed.wait_for(lambda: current()["etag"] != if_none_match), wait)
        except asyncio.TimeoutError:
            pass
    sc = current()
    if if_none_match == sc["etag"]:
        return Response(status_code=304, headers={"ETag": sc["etag"]})
    return Response(content=sc["body"], media_type="application/json; charset=utf-8", headers={"ETag": sc["etag"]})


async def _switch(device_id: str, app_id: str) -> None:
    if app_id not in APPS:
        raise HTTPException(status_code=404, detail="app nao existe")
    devices.set_app(device_id, app_id)
    await _refresh([app_id])
    await _notify()  # o long-poll dessa placa passa a olhar o outro app


@app.post("/device/{device_id}/action", status_code=204)
async def device_action(device_id: str, request: Request, authorization: str | None = Header(default=None)):
    _check(authorization, config.DEVICE_TOKEN)
    try:
        body = await request.json()
    except ValueError:
        raise HTTPException(status_code=400, detail="json invalido")
    if not isinstance(body, dict):
        raise HTTPException(status_code=400, detail="esperado objeto")
    action = str(body.get("action") or "")[:32]
    value = str(body.get("value") or "")[:64]
    if action == "app":
        await _switch(device_id, value)
    else:
        a = devices.app_of(device_id)
        if await asyncio.to_thread(APPS[a].action, device_id, action, value):
            await _refresh([a])
    return Response(status_code=204)


@app.post("/device/{device_id}/app/{app_id}", status_code=204)
async def device_set_app(device_id: str, app_id: str, authorization: str | None = Header(default=None)):
    _check(authorization, config.HOOK_TOKEN)
    await _switch(device_id, app_id)
    return Response(status_code=204)


@app.get("/device/{device_id}/sprite/{name}")
async def device_sprite(device_id: str, name: str, authorization: str | None = Header(default=None)):
    _check(authorization, config.DEVICE_TOKEN)
    data = pet_app.sprites.get(name) if pet_app else None
    if data is None:
        raise HTTPException(status_code=404, detail="pet nao encontrado")
    return Response(content=data, media_type="application/octet-stream")


# -------------------------------------------------------------- consultas


@app.get("/apps")
async def list_apps():
    return {"default": DEFAULT_APP, "apps": _menu()}


@app.get("/events/recent")
async def events_recent(limit: int = 30, authorization: str | None = Header(default=None)):
    _check(authorization, config.HOOK_TOKEN)
    return pet_app.store.recent(max(1, min(limit, 200))) if pet_app else []


@app.get("/health")
async def health():
    now = time.time()
    return {
        "ok": True,
        "devices": {d: {"visto_ha_s": round(now - t, 1), "app": devices.app_of(d)} for d, t in devices_seen.items()},
        "screens": {a: json.loads(s["body"] or b"{}") for a, s in _screens.items()},
        "apps": {a.id: a.summary() for a in APPS.values()},
    }
