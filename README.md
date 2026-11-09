# unamed_game

Prototipo en C++17 + OpenGL 3.3 Core, sin motor y sin dependencias externas.
Fase actual: personaje humanoide con esqueleto, animación procedural de andar/correr y
render en "modo debug" (líneas y puntos, sin iluminación ni texturas).

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/game

# Debug: símbolos, UBSan, aserciones de libstdc++ y mensajes de KHR_debug del driver
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j4
```

Dependencias: `libOpenGL`/`libGLX` (glvnd + Mesa), `libX11` y `libXi` (`sudo apt install libxi-dev`),
esta última para leer el ratón en crudo (XInput2 raw motion, sin aceleración del sistema).

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Mover (relativo a la cámara): trote a 3,5 m/s |
| Shift | Sprint (8 m/s) |
| Espacio | Saltar |
| Clic izquierdo / F | Usar el objeto seleccionado (golpear, espadazo, disparar, lanzar) |
| Clic derecho (mantener) | Apuntar con la pistola: cámara al hombro, mira y láser |
| G | Lanzar una piedra sin cambiar de objeto |
| 1 / 2 / 3 / 4 / 5 | Puños / Espada / Pistola / Piedras / Cóctel molotov |
| Ratón | Orbitar la cámara (queda capturado al arrancar). Las acciones van hacia donde mira la cámara |
| Flechas / Q E | Orbitar la cámara con teclado |
| Rueda | Zoom |
| F12 / º | Modo debug: alambre sin shaders (huesos, articulaciones, cajas) |
| F1 / F2 / F3 / F4 | (solo en modo debug) Mostrar/ocultar huesos / articulaciones / cuerpo / cuadrícula |
| Esc / P | Menú de pausa (continuar, reiniciar nivel, sensibilidad del ratón, salir) |
| R | Reiniciar jugador (y recargar piedras) |

Al perder el foco (alt-tab) el ratón se suelta y se abre el menú de pausa.

## Jugabilidad

- **Salto y plataformas**: salto de ~1,2 m. Los bordillos de hasta 0,3 m se suben andando
  (escalinata al fondo a la izquierda); los bloques más altos requieren saltar (escalera
  hasta la plataforma alta a la derecha, plataformas flotantes a la izquierda).
- **Puños** (8 de daño) y **espada** (25): golpe en un sector delante del personaje.
- **Pistola** (12): balas a 45 m/s. Desde la cadera salen rectas al frente; apuntando
  (clic derecho) van exactamente al punto bajo la mira, también hacia arriba o abajo. La
  mira se pone roja sobre un enemigo. Al apuntar se camina más despacio (2,2 m/s).
- **Piedras** (15): parábola con gravedad y rebotes. Mirando hacia arriba con el ratón se
  lanzan más alto y más lejos. Se recogen al pasar por encima (máx. 20).
- **Cócteles molotov** (5, máx. 10; R los recarga): se lanzan como las piedras y se rompen
  al chocar. Explosión de hasta 25 de daño en 3 m con empuje (el escudo no la para) y un
  charco de fuego de ~2,6 m durante 7 s sobre la superficie de debajo (en una plataforma
  arde encima, sin flotar en el borde). Quien pisa el fuego arde: 20 de daño por segundo,
  y sigue ardiendo 2,5 s al salir.
- **Enemigos**: no se mueven, pero llevan **escudo**: los golpes que les llegan de frente
  hacen solo el 20 % del daño y la mitad de retroceso. Por la espalda o el costado reciben
  el golpe completo. Todo impacto les hace retroceder (se les puede tirar de una
  plataforma). Al llegar a 0 de vida caen y reaparecen a los 4 s.

## Render

Por defecto, todo se dibuja como cajas sólidas instanciadas (un cubo en la GPU y una
matriz + color por instancia: 3 draw calls para toda la escena) con un shader propio:
sol con sombras (shadow mapping 2048², PCF), ambiente hemisférico + relleno, especular,
luz de contorno en personajes, bisel en aristas, suelo con cuadrícula procedural, cielo
en degradado, niebla y tonemapping ACES. Las balas, el láser, las chispas y las barras de
vida siguen siendo líneas. En la HD 620: ~225 fps sin vsync (~4,4 ms por frame).

Fuego: llamas procedurales en billboards verticales (ruido fractal animado que sube y
deforma una silueta de lengua, rampa de color brasa→naranja→amarillo), bola de fuego en
la explosión, humo (alfa premultiplicado, mismo draw call que las llamas), ascuas, hasta 8
luces puntuales parpadeantes y suelo chamuscado con borde de ruido. Con tres fuegos
delante de la cámara: ~130 fps sin vsync.

El modo debug (F12) es el render original en alambre, sin shaders de iluminación.

## Hardware objetivo y decisiones

Máquina de desarrollo: i7-7500U (2C/4T), 12 GB RAM, Intel HD 620 (Mesa) + GeForce 920M (nouveau).

- **OpenGL 3.3 Core** en vez de Vulkan: la HD 620 soporta ambos, pero Vulkan exige
  ~1000 líneas de boilerplate (instancias, swapchain, sincronización, pipelines) antes de
  pintar una línea. 3.3 es la API moderna más baja: corre en casi cualquier GPU de 2010+.
- **Usar la Intel, no la 920M**: con `nouveau` la Kepler no hace reclocking y rinde menos
  que la integrada. Mesa elige la Intel por defecto (`DRI_PRIME=1` forzaría la NVIDIA).
- **X11 + GLX directo** en vez de GLFW/SDL: cero dependencias y nada oculto. Todo lo
  específico de plataforma vive en `platform_x11.cpp` detrás de `platform.h`, así que
  cambiar a GLFW/SDL (p. ej. para Windows) es reescribir un único fichero.
- **Simulación a paso fijo de 60 Hz** desacoplada del render, **vsync** activado (o un
  limitador a 60 fps si no hay vsync) para no quemar CPU/GPU en un portátil.
- **Un único VBO dinámico** con *orphaning* y 2 draw calls por frame para toda la escena.

Coste medido en Release: <0,1 ms de CPU por frame, ~120 cajas; la GPU usa ~27 % del
presupuesto de 60 fps.

## Estructura

```
src/
  platform.h / platform_x11.cpp  Ventana, contexto GL 3.3, teclado/ratón, reloj
  gl.h                           Cabeceras GL (sin loader: glvnd exporta todo en Linux)
  vmath.h                        Vec3/Mat4, perspectiva, lookAt (column-major, Y arriba)
  debug_draw.h/.cpp              Lote de líneas/puntos + shader mínimo pass-through
  skeleton.h/.cpp                19 articulaciones, jerarquía y cinemática directa
  physics.h/.cpp                 Cajas AABB, cuerpos con gravedad, rayos (segmento vs caja)
  character.h/.cpp               Movimiento, salto, animación procedural y keyframes de acciones
  game.h/.cpp                    Nivel, enemigos, combate, proyectiles, partículas
  game_draw.cpp                  Dibujado del mundo 3D, armas, barras de vida y HUD
  text.h/.cpp                    Fuente vectorial mínima (A-Z, 0-9) para HUD y menús
  menu.h/.cpp                    Menú de pausa
  renderer.h/.cpp                Render con shaders: instancing, sombras, cielo, niebla
  main.cpp                       Bucle principal, cámara orbital, entrada
```

### Cómo funciona el esqueleto

Cada articulación tiene un padre, un offset fijo respecto a él (pose de reposo) y una
rotación local que pone la animación. La matriz mundo se calcula recorriendo el array en
orden (los padres siempre van antes que los hijos):

```
world[i] = world[padre] * T(offset) * Ry * Rx * Rz
```

Cada hueso arrastra una caja (muslo, antebrazo...) con *skinning rígido*: cada vértice
pertenece a un solo hueso. Es lo que hacían muchos juegos de PS2/PC de la época de Vice
City para personajes secundarios. El siguiente paso es el *skinning suavizado* (cada
vértice con varios huesos y pesos), que se hace en el vertex shader.

## Próximos pasos sugeridos

1. Colisión de la cámara con el escenario (ahora puede atravesar plataformas).
2. IA de enemigos: girarse hacia el jugador, atacar, y vida/daño para el jugador.
3. Malla real del personaje cargada desde fichero (glTF) con skinning por pesos en GPU.
4. Clips de animación con keyframes e interpolación (cuaterniones + slerp) y blending.
5. Mallas con formas más orgánicas que cajas (cápsulas, o modelos glTF).
