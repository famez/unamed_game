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

## musica/: el resto de pistas
Todas a 44,1 kHz en estéreo. Las `_loop` se repiten enteras; los `stinger_` suenan una vez sobre la música, que baja mientras tanto.
- `ciudad_loop`: el mercado, los callejones y la Ciudad Neón.
- `combate_loop`: cuando algún enemigo va a por Vela cerca (aguanta 4 s tras la pelea). También en el modo prueba.
- `fonda_loop`: junto a la fonda de la Señora Mecha, en la plaza.
- `jefe_loop`: mientras hay un jefe vivo.
- `piano_nocturno_de_las_farolas`: la portada (el selector de niveles).
- `piano_vela_y_hollin`: las escenas de recuerdo (`!recuerdo` en el guion).
- `piano_el_ultimo_farol`: el fin del capítulo.
- `stinger_victoria`: al derrotar a un jefe. `stinger_derrota`: al caer Vela.
- Prioridad: portada > fin del capítulo > recuerdo > jefe > combate > fonda > música de la zona. Si una pista no se puede leer, suena una sintetizada parecida.
