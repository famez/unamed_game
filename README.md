# unamed_game

Action-RPG en tercera persona en C++17 + OpenGL 3.3 Core, sin motor. **El color es
energía**: los *Grises*, criaturas de estática, han robado el color de una ciudad
flotante. El protagonista, un *Prismero*, lleva un arma de luz sólida que cambia de
forma; cada Gris que disipa devuelve su color al mundo, y la ciudad pasa del gris a los
neones a medida que se limpian oleadas.

## Historia

Capítulo 1, *La primera luz*: el Prismero despierta junto a la fuente sin recordar su
nombre. Le acompaña **Brizna**, una chispa de luz que parece saber más de lo que dice.
Los Grises fueron personas; al disiparlos su color vuelve al mundo. Tres **ecos de
memoria** escondidos por la plaza dejan pistas sobre el origen del protagonista.

Los guiones son ficheros de texto en `assets/story/` (formato explicado en `src/story.h`):
escenas que detienen el juego, avisos que pasan como subtítulos, recuerdos y órdenes
(`!levantarse`, `!nombre`, `!oleadas`, `!eco_activa`, `!fin_capitulo`). Las escenas se
disparan una sola vez: el despertar, el primer Gris disipado, la primera aparición de cada
especie, la primera oleada, el croma, la primera caída y los ecos.

## Música y sonido

Todo está sintetizado por código (`src/synth.cpp`), sin ficheros de audio en el
repositorio. La música (Si menor / Re mayor, 84 BPM, 16 compases en bucle) son cuatro capas
sincronizadas cuyo volumen decide el juego: el **colchón** siempre, la **melodía** cuando
vuelve el color, el **pulso** (bajo y percusión) en combate y el **brillo** (arpegio) cuando
la ciudad ya brilla. Se genera la primera vez en segundo plano (~2 s) y se guarda en
`~/.cache/unamed_game/`. Los 26 efectos (golpes de cristal, látigo, martillo, guardia,
magias, botín, ecos, el "tecleo" de los diálogos...) se generan al arrancar.

La salida usa PulseAudio/PipeWire cargado con `dlopen` en tiempo de ejecución: no hace falta
ninguna cabecera para compilar y, si no está, el juego va en silencio.

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/game                     # --calidad baja|media|alta  --resolucion 1920x1080  --debug

# Debug: símbolos, UBSan, aserciones de libstdc++ y mensajes de KHR_debug del driver
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j4

# Tests de la lógica (sin ventana, con AddressSanitizer + UBSan)
ctest --test-dir build --output-on-failure     # o ./build/logic_test para ver cada caso
```

Dependencias del sistema: `libOpenGL`/`libGLX` (glvnd + Mesa), `libX11` y `libXi`
(`sudo apt install libxi-dev`). Incluidas en el repositorio: `stb_truetype.h` y las
fuentes Orbitron y Exo 2.

Opciones de desarrollo: `--autoplay` (un bot combate solo, sin capturar el ratón),
`--screenshot fichero.ppm segundos [frames]` (guarda frames y sale) y `--camyaw radianes`.

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Correr (relativo a la cámara; el personaje gira hacia donde va) |
| Espacio | Saltar (también cancela la recuperación de un golpe) |
| Clic izquierdo | Atacar: combo de suelo o aéreo, con acercamiento automático al objetivo |
| Clic derecho (mantener) | Guardia. Si el golpe llega justo al levantarla: **bloqueo perfecto** |
| Shift | Voltereta de esquiva (invulnerable mientras dura) |
| Tab | Fijar objetivo / pasar al siguiente. Clic central o X: soltarlo |
| Rueda + E | Elegir y usar una entrada del menú de comandos |
| 1 2 3 | Magias: Fuego, Hielo, Cura |
| 4 5 6 | Atajos de objetos (se asignan en el inventario) |
| F | Abrir cápsulas |
| I | Inventario (pausa): usar objetos, equipar prismas, asignar atajos |
| Ratón / flechas | Orbitar la cámara (con objetivo fijado gira sola para encuadrarlo) |
| F12 / º | Modo debug: solo esqueletos y cajas en alambre |
| F1 / F2 / F3 / F4 | (en modo debug) Huesos / articulaciones / cajas / cuadrícula |
| Esc / P | Pausa: continuar, reiniciar, sensibilidad, **calidad gráfica**, salir |
| Clic / E / Espacio / Enter | En las escenas: pasar el diálogo |

## Jugabilidad

- **Croma**: el mundo empieza casi gris (12 %). Cada Gris disipado devuelve un 3,5 % y deja
  una salpicadura de su color en el suelo; cada oleada superada, un 10 % más. Los neones
  se encienden y el cielo se satura según el croma (medidor arcoíris arriba a la izquierda).
- **Prisma modular**: tajos con la hoja de luz, el giro y el remolino aéreo con un látigo
  más largo y el remate con un martillo de luz que lanza por los aires.
- **Combos**: tajo, revés, giro de 360° y remate con salto y voltereta; combo aéreo de dos
  golpes y remolino flotando. Un clic durante un golpe queda en cola.
- **Peso y fluidez**: golpes con preparación, impacto seco y rebote; paso adelante;
  parón al impactar; mezcla de poses entre estados; inclinación al acelerar y girar;
  pelo y cinta con inercia; cámara con muelle; render interpolado entre pasos.
- **Guardia, esquiva y magias** (Fuego, Hielo, Cura) como antes; los orbes de los Eco se
  devuelven de un golpe.
- **Grises**: *Rastrero* (se lanza con las garras), *Domo* (salta y aplasta con una onda;
  se esquiva saltando) y *Eco* (vuela y dispara orbes). IA por estados con aviso del ataque.
- **Botín**: lúmenes (moneda), orbes de PV y PM y objetos; cápsulas con prismas nuevos;
  células de energía que se rompen.

## Modo debug (F12)

No usa el render con shaders: se dibuja en alambre con un único buffer de líneas
(esqueletos con la izquierda en azul y la derecha en rojo, articulaciones, cajas, escenario,
sector del último golpe, botín como cruces) y encima de cada enemigo su estado de IA y vida.

## Render

- **Cuerpos continuos**: cada personaje se define con elipsoides y cápsulas pegados al
  esqueleto; al arrancar se funden (unión suave de funciones de distancia), se convierten en
  malla (*surface nets*) con pesos de hueso y la GPU la deforma (skinning). Contorno por
  casco invertido. Los detalles nítidos o luminosos son piezas rígidas aparte.
- **Post-proceso**: la escena se dibuja en un buffer propio a resolución interna escalada;
  el alfa es la máscara de brillo y un bloom de cadena de reducciones da halo a neones,
  efectos y destellos.
- **Estilo**: sombreado de animación, materiales emisivo, agua, césped y suelo con líneas de
  luz; cielo con nubes; desaturación del escenario según el croma y salpicaduras de color.
- **Texto**: fuentes reales en un atlas SDF (nítidas a cualquier tamaño, con contorno y
  halo, tildes y ñ); la interfaz se diseña a 720 px de alto y escala con la ventana.

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
- **Un único VBO dinámico** con *orphaning* y pocos draw calls por frame para toda la escena.
- **Sin motor de físicas externo**: los personajes son cuerpos verticales contra cajas
  (AABB), que es todo lo que necesita este tipo de combate. Jolt Physics (ragdolls) se
  quitó al cambiar de género: los enemigos se desvanecen en luz al morir.

Coste medido en Release (HD 620, 1920×1080, sin vsync): ~74 fps en calidad media y ~97 en
baja; ~0,5 ms de CPU por frame. La calidad alta (resolución interna completa, MSAA 8x,
sombras 4096²) es para un equipo más potente.

## Estructura

```
src/
  platform.h / platform_x11.cpp  Ventana, contexto GL 3.3, teclado/ratón, reloj
  gl.h                           Cabeceras GL (sin loader: glvnd exporta todo en Linux)
  vmath.h                        Vec3/Mat4, perspectiva, lookAt (column-major, Y arriba)
  debug_draw.h/.cpp              Lote de líneas/triángulos/puntos + shader mínimo
  skeleton.h/.cpp                Esqueletos genéricos (jerarquía y cinemática directa)
  physics.h/.cpp                 Cajas AABB, cuerpos con gravedad, rayos (segmento vs caja)
  character.h/.cpp               Protagonista: carrera, salto, combos, esquiva, guardia
  enemy.h/.cpp                   Especies de enemigos: esqueletos, formas, animación
  items.h/.cpp                   Objetos, armas e inventario
  game.h/.cpp                    Escenario, oleadas, IA, combate, magias, botín, cofres
  game_story.cpp                 Escenas del guion, órdenes, Brizna y ecos de memoria
  story.h/.cpp                   Formato de guion y reproductor de diálogos (UTF-8)
  synth.h/.cpp                   Síntesis de la música (capas) y los efectos; WAV
  audio.h/.cpp                   Salida (PulseAudio con dlopen) y mezclador en un hilo
  sfx.h                          Lista de efectos y eventos de sonido de la lógica
  game_draw.cpp                  Dibujado del mundo, efectos y HUD (y su versión debug)
  inventory_menu.h/.cpp          Menú de inventario
  text.h/.cpp                    Fuentes SDF (Orbitron, Exo 2) con stb_truetype: HUD y menús
  models.h/.cpp                  Cuerpos continuos: primitivas → malla con skinning
  menu.h/.cpp                    Menú de pausa
  renderer.h/.cpp                Render: instancing, skinning, sombras, bloom, calidad, croma
  main.cpp                       Bucle principal, cámara, entrada
tests/
  logic_test.cpp                 Tests de la lógica sin ventana (combate, IA, botín, croma...)
assets/fonts/                    Orbitron y Exo 2 (licencia SIL OFL, ver OFL-*.txt)
assets/story/                    Guiones de la historia
third_party/stb/                 stb_truetype.h (dominio público)
```

### Cómo funcionan los esqueletos

Cada articulación tiene un padre, un offset fijo respecto a él (pose de reposo) y una
rotación local que pone la animación. La matriz mundo se calcula recorriendo el array en
orden (los padres siempre van antes que los hijos):

```
world[i] = world[padre] * T(offset) * Ry * Rx * Rz
```

El protagonista usa un esqueleto humanoide de 19 articulaciones; cada enemigo tiene el
suyo (orejas y cola del duende, sombrero de la seta, alas de la polilla). Las formas del
cuerpo son elipsoides pegados a una articulación (*skinning rígido*).

## Próximos pasos sugeridos

1. Capítulo 2: el puente y la luz encendida al otro lado; más especies de Grises.
2. Un jefe con varias fases y su propia variación de la música.
3. Retratos animados en los diálogos y escenas con cámara guionizada.
4. Síntesis de mejoras para el prisma con los materiales.
