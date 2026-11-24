# Criando pets no ChatGPT

Receita usada para os 8 pets da placa. O conversor (`backend/app/sprites.py`) cuida de reduzir, limpar e alinhar;
o que importa aqui é a IA manter o **mesmo personagem, no mesmo lugar**, entre os quadros.

## Passo a passo

1. **Conversa nova por pet** no ChatGPT, anexando `gato_folha.webp` (referência de estilo: mesmo tamanho de pixel,
   contorno e proporções; a coleção fica coerente).
2. **Prompt 1** (o personagem) → gere até gostar.
3. **Prompt 2** na mesma conversa → salve como `<nome>_base.webp` (normal, piscando, dormindo).
4. **Prompt 3** na mesma conversa → salve como `<nome>_alerta.webp` (alerta, feliz, trabalhando).
5. Confira: `backend\.venv\Scripts\python tools\pet_convert.py <nome>` → `pets\<nome>_previa.png`.
6. Rabo (opcional): `pets\<nome>.json` com `{"rabo": {"x": .., "y0": .., "y1": .., "desloca": 2}}`, coordenadas
   tiradas de `pet_convert.py <nome> --grade`.

Em até 20 s o backend converte e a placa baixa. WebP direto do ChatGPT serve (não precisa converter para PNG).

**Nomes:** sem acento, só letras/números, até 12 (`robo`, `fantasma`, `cachorro`). O que vem antes do `_` é o nome.

## Dicas

- Escolha a versão com **olhos mais limpos e contorno mais nítido**; cor dá para corrigir, forma não.
- Contorno **não preto** e cores vivas: o fundo da tela é escuro (pinguim em azul-ardósia, não preto).
- Se vier chão, sombra ou fundo xadrez: *"remove the floor and shadow, background must be one flat magenta color"*.
- Peça **"about 70% of the image height"** para todos terem tamanho parecido na tela.
- Se um quadro novo vier muito diferente, dá para ficar só com o normal: o conversor completa com o que tiver.

---

## Prompt 1: o personagem

Abertura (igual para todos):

```text
The attached orange cat is my style reference. Create a NEW character in exactly the same pixel art style: same pixel size, same outline thickness, same chibi proportions, same overall size in the image (about 70% of the image height), front view, centered, full body.
```

Trecho do personagem (um destes):

| Pet | Trecho |
|---|---|
| Blob | `Character: a cute round green slime blob (#80ED99 body, darker green #57CC99 shading at the bottom, a small white shine spot on top-left), soft dome shape slightly wider at the base, two big simple black eyes with white highlight, small happy mouth. Outline in dark green (#1B4332), not black.` |
| Robô | `Character: a cute small robot with a boxy rounded head and a smaller boxy body, light gray metal (#B0BEC5) with darker gray panels (#78909C), one short antenna with a red light on top, a dark screen-like face with two glowing cyan eyes (#4DD0E1) and a small cyan mouth line, short arms and small feet, a few bolt details. Outline in dark blue-gray (#37474F), not black.` |
| Fantasminha | `Character: a cute little ghost, white body with a very light lavender tint (#F3EEFF) and soft lavender shading (#C9B8F0) on one side, round head and a wavy bottom edge with 3 small points, floating (no feet), two big simple black eyes with white highlight, small pink blush cheeks, small "o" mouth, tiny arms raised. Outline in medium purple-gray (#6A5A8C), not black.` |
| Cachorro | `Character: a cute puppy sitting, light brown fur (#D9A066) with cream muzzle and belly (#F5DEB3), long floppy darker brown ears (#8B5A2B), a small black nose, big simple black eyes with white highlight, happy mouth with a little pink tongue, short tail on the right side curving up. Outline in dark brown (#5C3A1E), not black.` |
| TV | `Character: a cute retro television with personality: wooden cabinet body (#8D6E63) with lighter wood trim (#A1887F), two thin antennas in a V shape on top, a rounded screen in dark green-gray (#263238) showing a simple glowing green face (#69F0AE: two eyes and a small smile), two small knobs on the right side, short little legs. Outline in dark brown (#3E2723), not black.` |
| Pinguim | `Character: a cute chubby penguin standing, body in dark slate blue (#34495E) — NOT pure black — with a big white belly and white face, small orange beak and orange feet (#FFA726), big simple black eyes with white highlight, small pink blush, little flippers at the sides. Outline in very dark navy (#1C2833).` |
| Dino | `Character: a cute baby dinosaur sitting, green body (#66BB6A) with light yellow-green belly (#DCEDC8), small rounded back plates in darker green (#388E3C), short tail on the right side, tiny arms, big simple black eyes with white highlight, happy mouth. Outline in dark green (#1B5E20), not black.` |

O gato (o primeiro, sem referência) usou:
`Pixel art sprite of a cute chubby orange cat, chibi proportions (big round head, small body), sitting, facing the viewer, full body visible, centered in a square image with empty space around it.`
com cores `#F4A261` (corpo), creme na barriga/focinho, rosa nas orelhas e nariz, olhos 2×3 com brilho e contorno `#6B3E26`.

Fechamento (igual para todos):

```text
Style: true pixel art, every pixel a perfectly square block of the same size as in the reference, flat colors only, maximum 12 colors, no gradients, no anti-aliasing, no blur, no dithering, no glow.
Background: solid flat magenta (#FF00FF), one single color, no checkerboard, no floor, no shadow, no text, no border.
```

## Prompt 2: normal, piscando, dormindo → `<nome>_base.webp`

```text
Using exactly the same character, same size, same colors and same position, create a sprite sheet of 3 frames side by side, in a single row, equally spaced, on the same solid magenta (#FF00FF) background:
1) normal, eyes open
2) blinking, eyes as closed horizontal lines
3) sleeping, eyes closed as small downward curves, slightly relaxed, body 1 pixel lower
Everything else identical between frames. No text, no numbers, no separators.
```

## Prompt 3: alerta, feliz, trabalhando → `<nome>_alerta.webp`

```text
Using exactly the same character as before — same size, same proportions, same colors, same outline, same pose and same position — create a new sprite sheet with 3 frames side by side, in a single row, equally spaced, on the same solid flat magenta (#FF00FF) background:

1) ALERT: eyes wide open and round (bigger pupils with white highlight), mouth small and open in an "o", [ALERT DETAIL].
2) HAPPY: eyes closed as upward curves (^ ^), big open smile, small pink blush on both cheeks, [HAPPY DETAIL].
3) WORKING: focused expression, eyes half-closed looking slightly to the side, small determined mouth, [WORKING DETAIL].

Same pixel grid as before: true pixel art, every pixel a perfectly square block of the same size, flat colors only, no gradients, no anti-aliasing, no blur, no shadow, no floor, no text, no numbers, no separators. The body must stay in the same place in all 3 frames; only the face and the small details change.
```

| Pet | [ALERT DETAIL] | [HAPPY DETAIL] | [WORKING DETAIL] |
|---|---|---|---|
| Gato | ears perked straight up, one front paw slightly raised | tail raised | tongue tip slightly out |
| Blob | body slightly stretched taller | body slightly squished wider | a small sweat drop on the side |
| Robô | antenna light blinking bright red | antenna light green, small hearts on the face screen | antenna light yellow, eyes shown as small lines |
| Fantasminha | tiny arms raised high | tiny arms open wide | a tiny quill pen held in one arm |
| Cachorro | ears perked up, one front paw raised | tongue out, tail raised | tongue tip out, ears slightly back |
| TV | antennas straight up, screen face in yellow | screen face with hearts, antennas wiggly | screen face shows small loading dots |
| Pinguim | flippers raised up | flippers open wide | one flipper on the chin, thinking |
| Dino | tiny arms raised, back plates standing up | tail raised, tiny arms open | tongue tip out, tiny arms together |

A IA às vezes acrescenta efeitos (estrelinhas, engrenagens) ao lado do personagem; o conversor mantém, e eles
convivem bem com os efeitos da placa.
