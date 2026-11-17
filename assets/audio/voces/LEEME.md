# Voces de la protagonista

El juego busca aquí las voces grabadas de Vela (luz) y Hollín (sombra). Cada grito puede tener
varias tomas (`_1`, `_2`…, hasta 12) y el juego elige una al azar sin repetir la anterior.

| Fichero | Cuándo suena |
|---|---|
| `vela_golpe_N.wav` / `hollin_golpe_N.wav` | golpes normales y técnicas |
| `vela_remate_N.wav` / `hollin_remate_N.wav` | remates, embestida, contraataque, transformación (si falta: `golpe`) |
| `vela_salto_N.wav` / `hollin_salto_N.wav` | saltos |
| `vela_esquiva_N.wav` / `hollin_esquiva_N.wav` | esquivas (si falta: `salto`) |
| `vela_dolor_N.wav` / `hollin_dolor_N.wav` | al recibir daño |
| `hollin_risa_N.wav` | Hollín al reventar una farola o rematar el súper contraataque |

Formato: WAV de cualquier frecuencia, mono o estéreo, 16/24/32 bits o coma flotante. El
volumen se iguala solo. Si un personaje no tiene ninguna grabación, se usa su voz sintetizada.

## Créditos

Voces de **Cici Fyre** («Female RPG Voice Starter Pack», CC0):
https://opengameart.org/content/female-rpg-voice-starter-pack · https://cicifyre.itch.io/
Vela usa el tipo 1 (la voz dulce) y Hollín el tipo 3 (la madura).

## abuelo/: barks del Abuelo Candil
45 tomas en inglés (OGG mono, 44,1 kHz), tres por tipo: saludo, despedida, sí, no, hmm, pregunta, sorpresa, enfado, ay, risa, suspiro, uf, farola, vela y hollín. El guion está en `guion_abuelo_tts.csv` (español) y `guion_abuelo_tts_en.csv` (inglés); la lista, en `src/audio/barks.h`.
- Son la **voz de entrada al diálogo**, como en Zelda: al hablarle (F) suelta un bark y aparece su frase de texto, que no se dobla.
- La primera vez (o tras un minuto sin hablarle) saluda; si es Hollín quien le habla, recela («You again?», «Hm. Shadow eyes…»); si no, va alternando murmullos, risas, «sí», suspiros, preguntas y «uf».
- Para dar barks a otro vecino: sus OGG con el mismo esquema de nombres y una entrada en `Game::entryBark` (`src/game/npcs.cpp`).
