# Audio: Vela y Hollín

## voces/
Voces sintetizadas por formantes, en estilo balbuceo anime. Sirven como placeholder jugable o como estilo final.
- `*/combate/`: ataques ligeros, medio, fuerte, remate de combo, habilidades (Hollín: púas, cadenas, drenar, mina y agujero negro), salto, esquiva, daño, KO, victoria y cambio de forma.
- `*/blips/`: sílabas sueltas para el texto que aparece letra a letra. Pon una cada 2–3 letras, elegida al azar.
- `*/frases/`: frases de muestra.
- `*/efectos/`: risa, «hmm», «¿eh?», esfuerzo y dolor. Vela tiene además suspiro y bostezo; Hollín, carcajada malvada.
- `dialogo_demo.wav`: un intercambio entre las dos.

## Tus grabaciones
- `procesar_voz.py`: convierte tu voz en la de Vela o la de Hollín (sube tono y formantes por separado, con el vocoder WORLD).
- `GUION_GRABACION.md`: lista de tomas con los mismos nombres de archivo, consejos para grabar e integración en el juego.

## musica/: «Farolas»
- 72 BPM, 32 compases y **106,67 s exactos**, en re dórico. Bucle sin costura: reproduce el archivo entero en bucle, sin puntos de loop internos.
- `farolas_capa_luz_loop`: Vela. Pads cálidos, caja de música con eco y destellos agudos.
- `farolas_capa_sombra_loop`: Hollín. Los mismos acordes una octava abajo, dron grave, un motivo travieso de juguete roto con una nota cromática, campanas invertidas y un latido.
- **Uso:** arranca las dos capas a la vez, sincronizadas, y cambia solo el volumen. Al cambiar de forma, haz un fundido de potencia constante de 1,5–2 s: `vol_luz = cos(g·π/2)` y `vol_sombra = sin(g·π/2)`. Como comparten tempo y acordes, el cambio suena musical.
- `farolas_demo_cambio_de_forma`: dos vueltas del bucle pasando de luz a sombra y vuelta.
- Hay versión WAV (máxima calidad y bucle exacto) y OGG (ligera). En C++, miniaudio o stb_vorbis reproducen OGG sin huecos.
- `fuente/`: los scripts que lo generan todo (`music.py`, `voices.py` y `combate.py`), por si quieres cambiar tempo, acordes o melodía.
