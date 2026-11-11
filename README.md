# unamed_game

Prototipo en C++17 + OpenGL 3.3 Core, sin motor y sin dependencias externas: personaje
con esqueleto y animación procedural, combate cuerpo a cuerpo con combos y defensa, armas
de fuego y arrojadizas, enemigos con escudo, fuego volumétrico y render con sombras.

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/game

# Debug: símbolos, UBSan, aserciones de libstdc++ y mensajes de KHR_debug del driver
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j4

# Tests de la lógica (sin ventana, con AddressSanitizer + UBSan)
ctest --test-dir build --output-on-failure     # o ./build/logic_test para ver cada caso
```

Dependencias: `libOpenGL`/`libGLX` (glvnd + Mesa), `libX11` y `libXi` (`sudo apt install libxi-dev`),
esta última para leer el ratón en crudo (XInput2 raw motion, sin aceleración del sistema).

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Mover (el personaje mira siempre hacia la cámara: A/D se desplazan de lado, S retrocede) |
| Shift | Sprint hacia delante (8 m/s) |
| Espacio | Saltar. **Yendo de lado: voltereta** (esquiva los golpes) |
| Clic izquierdo / F | Usar el objeto: golpe (combos de 3 con puños y espada), disparo, lanzamiento |
| Clic derecho (mantener) | Pistola/escopeta: apuntar (cámara al hombro, mira). Piedras/molotov/granada: apuntar el lanzamiento (se ve la trayectoria; el clic izquierdo lanza). Espada: **defenderse** |
| G | Lanzar una piedra sin cambiar de objeto |
| 1 … 8 | Puños / Espada / Pistola / Piedras / Molotov / Escopeta / Granada / Martillo |
| Ratón | Orbitar la cámara (queda capturado al arrancar). Las acciones van hacia donde mira la cámara |
| Flechas / Q E | Orbitar la cámara con teclado |
| Rueda | Zoom |
| F12 / º | Modo debug: alambre sin shaders (huesos, articulaciones, cajas) |
| F1 / F2 / F3 / F4 | (solo en modo debug) Mostrar/ocultar huesos / articulaciones / cuerpo / cuadrícula |
| Esc / P | Menú de pausa (continuar, reiniciar nivel, sensibilidad del ratón, salir) |
| R | Reiniciar jugador (vida y munición) |

Al perder el foco (alt-tab) el ratón se suelta y se abre el menú de pausa.

## Jugabilidad

- **Salto y plataformas**: salto de ~1,2 m. Los bordillos de hasta 0,3 m se suben andando
  (escalinata al fondo a la izquierda); los bloques más altos requieren saltar (escalera
  hasta la plataforma alta a la derecha, plataformas flotantes a la izquierda).
- **Movimiento relativo a la cámara**: el cuerpo mira hacia donde mira la cámara y las
  piernas se animan según la dirección (paso lateral, marcha atrás a 0,75x).
- **Voltereta** (espacio yendo de lado): 0,55 s a 7,5 m/s; durante ella no te alcanzan.
- **Combos**: clics seguidos dentro de 0,45 s encadenan golpes (un clic durante un golpe
  queda en cola). Puños: directo (8), izquierdo (8), gancho (15, con impulso). Espada:
  tajo (25), barrido horizontal (20, arco muy ancho), estocada (35, alcance largo).
- **Defensa con espada** (clic derecho): un golpe de frente hace el 20 % del daño y casi no
  empuja. Te mueves más despacio y no puedes atacar mientras bloqueas.
- **Pistola** (12): balas a 45 m/s. Desde la cadera salen rectas al frente; apuntando
  (clic derecho) van exactamente al punto bajo la mira, también hacia arriba o abajo. La
  mira se pone roja sobre un enemigo. Al apuntar se camina más despacio (2,2 m/s).
- **Escopeta** (16 cartuchos): 8 perdigones de 7 en un cono de ~8,5°, alcance ~18 m y
  0,8 s entre disparos. Devastadora de cerca.
- **Piedras** (15): parábola con gravedad y rebotes. Mirando hacia arriba con el ratón se
  lanzan más alto y más lejos. Se recogen al pasar por encima (máx. 20).
- **Lanzamiento apuntado** (piedra, molotov, granada): con clic derecho se ve la parábola
  exacta (la misma simulación que el proyectil) y dónde caerá; el clic izquierdo lanza.
- **Granada** (4): rebota y explota a los 2 s: hasta 55 de daño en 4 m con empuje; el
  escudo cubre el 40 % si mira a la explosión. También te daña a ti si estás cerca.
- **Martillo**: golpe lento (0,85 s) de 40 que lanza por los aires; el escudo solo para el
  40 %. La onda contra el suelo empuja y daña a los que estén alrededor.
- **Cócteles molotov** (5, máx. 10; R los recarga): se lanzan como las piedras y se rompen
  al chocar. Explosión de hasta 25 de daño en 3 m con empuje (el escudo no la para) y un
  charco de fuego de ~2,6 m durante 7 s sobre la superficie de debajo (en una plataforma
  arde encima, sin flotar en el borde). Quien pisa el fuego arde: 20 de daño por segundo,
  y sigue ardiendo 2,5 s al salir.
- **Enemigos**: no se mueven, pero llevan **escudo**: los golpes que les llegan de frente
  hacen solo el 20 % del daño y la mitad de retroceso. Por la espalda o el costado reciben
  el golpe completo. Todo impacto les hace retroceder (se les puede tirar de una
  plataforma). Al llegar a 0 de vida caen y reaparecen a los 4 s. Si estás delante y a
  menos de 1,7 m levantan la **porra** (0,55 s de aviso) y golpean: 15 de daño y empujón.
  Golpearles durante la preparación les interrumpe.
- **Vida del jugador** (barra abajo a la izquierda): a 0 caes y reapareces en el inicio.

## Render

Por defecto, todo se dibuja instanciado (un cubo y una esfera en la GPU y una matriz +
color por instancia; unos pocos draw calls para toda la escena) con un shader propio. Los
personajes son ~27 elipsoides solapados (estilo maniquí: cabeza con pelo, ojos y nariz,
deltoides, músculos, articulaciones); el escenario son cajas. Iluminación:
sol con sombras (shadow mapping 2048², PCF), ambiente hemisférico + relleno, especular,
luz de contorno en personajes, bisel en aristas, suelo con cuadrícula procedural, cielo
en degradado, niebla y tonemapping ACES. Las balas, el láser, las chispas y las barras de
vida siguen siendo líneas. En la HD 620: ~210 fps sin vsync (~4,8 ms por frame).

Fuego volumétrico: cada fuego (charco, cuerpo ardiendo, explosión, mecha) es una caja que
el fragment shader recorre con raymarching (hasta 22 pasos) evaluando ruido fractal 3D animado:
la densidad forma lenguas que suben y la "temperatura" da el color (brasa → naranja →
amarillo). Es 3D desde cualquier ángulo y, sobre una plataforma, se recorta a su cara de
arriba. Además: humo (billboards con alfa premultiplicado), ascuas, hasta 8 luces
puntuales parpadeantes y suelo chamuscado. Optimizaciones (dentro del fuego de un molotov:
de ~68 a ~130 fps):

- El ruido fractal está precalculado en una **textura 3D** de 128³ (R16, periódica): una
  lectura con filtrado trilineal sustituye a 3 octavas × 8 hashes por paso.
- Humo y fuego se dibujan a **media resolución** en un buffer aparte que luego se compone
  encima, y solo en el rectángulo de pantalla que ocupan (scissor).
- Cada rayo se recorta al **cilindro o esfera real** (no a la caja) y a la profundidad de
  la escena, y el número de pasos depende del tramo recorrido.

Objetivo apuntado: al apuntar con la pistola, el enemigo bajo la mira lleva una silueta
roja que late. Se hace con el stencil buffer: se dibuja marcando el stencil y luego se
vuelve a dibujar inflado, con color plano, solo donde el stencil no está marcado.

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
  game_draw.cpp                  Dibujado del mundo 3D (cuerpos de elipsoides), armas, HUD
  text.h/.cpp                    Fuente vectorial mínima (A-Z, 0-9) para HUD y menús
  menu.h/.cpp                    Menú de pausa
  renderer.h/.cpp                Render con shaders: instancing, sombras, cielo, niebla,
                                 fuego volumétrico, humo, silueta por stencil
  main.cpp                       Bucle principal, cámara orbital, entrada
tests/
  logic_test.cpp                 Tests de la lógica sin ventana (combate, físicas, armas...)
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

1. Físicas con Jolt Physics: ragdolls al derribar enemigos y objetos dinámicos.
2. Colisión de la cámara con el escenario (ahora puede atravesar plataformas).
3. IA de enemigos que se muevan: patrullar, perseguir, cubrirse con el escudo.
4. Malla real del personaje cargada desde fichero (glTF) con skinning por pesos en GPU.
5. Clips de animación con keyframes e interpolación (cuaterniones + slerp) y blending.
