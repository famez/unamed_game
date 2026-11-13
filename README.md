# unamed_game

Action-RPG en tercera persona en C++17 + OpenGL 3.3 Core, sin motor ni dependencias
externas, inspirado en la filosofía de los *Kingdom Hearts* (sin copiar nada suyo):
personajes de dibujo animado, combos vistosos con un arma, magias, enemigos que sueltan
orbes y objetos de colores, inventario y cofres. Primer escenario: una plaza soleada con
fuente, casas, terraza y plataformas, con oleadas de enemigos.

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/game              # --debug arranca en modo debug

# Debug: símbolos, UBSan, aserciones de libstdc++ y mensajes de KHR_debug del driver
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j4

# Tests de la lógica (sin ventana, con AddressSanitizer + UBSan)
ctest --test-dir build --output-on-failure     # o ./build/logic_test para ver cada caso
```

Dependencias: `libOpenGL`/`libGLX` (glvnd + Mesa), `libX11` y `libXi` (`sudo apt install libxi-dev`),
esta última para leer el ratón en crudo (XInput2 raw motion, sin aceleración del sistema).

Opciones de desarrollo: `--autoplay` (un bot fija objetivos y combate solo, sin capturar el
ratón), `--screenshot fichero.ppm segundos` (guarda un frame y sale) y `--camyaw radianes`.

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Correr (relativo a la cámara; el personaje gira hacia donde va) |
| Espacio | Saltar |
| Clic izquierdo | Atacar: combo de suelo o aéreo, con acercamiento automático al objetivo |
| Clic derecho (mantener) | Guardia. Si el golpe llega justo al levantarla: **bloqueo perfecto** |
| Shift | Voltereta de esquiva (invulnerable mientras dura) |
| Tab | Fijar objetivo / pasar al siguiente. Clic central o X: soltarlo |
| Rueda + E | Elegir y usar una entrada del menú de comandos |
| 1 2 3 | Magias: Fuego, Hielo, Cura |
| 4 5 6 | Atajos de objetos (se asignan en el inventario) |
| F | Abrir cofres |
| I | Inventario (pausa): usar objetos, equipar armas, asignar atajos |
| Ratón / flechas | Orbitar la cámara (con objetivo fijado gira sola para encuadrarlo) |
| F12 / º | Modo debug: solo esqueletos y cajas en alambre |
| F1 / F2 / F3 / F4 | (en modo debug) Huesos / articulaciones / cajas / cuadrícula |
| Esc / P | Menú de pausa (continuar, reiniciar, sensibilidad, salir) |

## Jugabilidad

- **Combo de suelo**: tajo, revés, giro de 360° y **remate** con salto y voltereta que lanza
  a los enemigos por los aires y deja una onda en el suelo. El arma decide cuántos golpes
  hay antes del remate. Un clic durante un golpe queda en cola y se encadena.
- **Combo aéreo**: dos golpes y un remate en remolino; mientras golpea, el personaje casi
  no cae. Un remate aéreo por salto.
- **Acercamiento**: si el objetivo está a menos de 7 m, el golpe empieza con un impulso
  hacia él. Sin objetivo fijado se elige el enemigo más a mano en la dirección pulsada.
- **Peso de los golpes**: parón de 50–90 ms al impactar, destellos, temblor de cámara en
  los remates y estela del color del arma.
- **Guardia**: para todo lo que llegue de frente. En los primeros 0,35 s es un bloqueo
  perfecto: rechaza al atacante y lo deja tambaleándose.
- **Magias** (PM, se regeneran despacio y con orbes azules): **Fuego** (bola que persigue
  al objetivo y explota en área quemando), **Hielo** (tres esquirlas que congelan y
  ralentizan) y **Cura**.
- **Enemigos** (cada uno con su esqueleto, animación procedural e IA por estados:
  aparece → persigue → avisa → ataca → se recupera; un golpe le hace tambalearse):
  - *Duende*: rápido, se agacha con las garras en alto y se lanza con un zarpazo.
  - *Seta saltarina*: salta y cae aplastando con una onda (se esquiva saltando). Mientras
    salta solo la detiene un remate.
  - *Polilla lunar*: vuela en círculos y dispara orbes de luz que se pueden **devolver**
    de un golpe.
- **Oleadas**: salen de portales en el suelo; al limpiar una llega la siguiente, más dura.
- **Botín**: monedas (munny), orbes verdes (PV) y azules (PM) y, a veces, objetos (cristales
  que giran con un haz de luz). Saltan del enemigo, rebotan y vuelan hacia el jugador al
  acercarse. Las **cajas** se rompen a golpes y sueltan botín; los **cofres** guardan
  objetos y armas nuevas (uno está en la plataforma alta: hay que subir saltando).
- **Inventario**: consumibles (Poción, Hipoción, Éter, Elixir), materiales que sueltan los
  enemigos y armas: *Espada de madera* (equilibrada), *Báculo estelar* (magia fuerte,
  combo de 5) y *Hoja aurora* (golpes fuertes y largos).
- **Derrota**: a 0 PV el personaje cae y reaparece en el inicio con todo recuperado.

## Modo debug (F12)

No usa el render con shaders: ni sombras, ni iluminación, ni instancias. Se dibuja en
alambre con un único buffer de líneas: los esqueletos de todos los personajes (huesos con
la izquierda en azul y la derecha en rojo, articulaciones y cajas de cada segmento), el
escenario, cajas y cofres, el sector del último golpe, el botín como cruces, y encima de
cada enemigo su estado de IA y su vida.

## Render

Todo se dibuja instanciado (un cubo y una esfera en la GPU y una matriz + color por
instancia) con un shader propio. Los personajes son elipsoides pegados a sus
articulaciones, con proporciones de dibujo animado (cabeza grande, pelo de punta,
zapatones). Estilo visual:

- Paleta luminosa: cielo azul saturado con nubes procedurales, sol cálido y relleno frío.
- Sombreado de animación en los personajes (la luz pasa a sombra en una franja estrecha),
  sombras proyectadas suaves y luz de contorno.
- Materiales: adoquines procedurales, césped, agua con ondas y reflejo del cielo, metal y
  **emisivo** (ojos, cristales, orbes, farolas, ventanas).
- Efectos translúcidos en una capa aparte: estelas del arma, destellos de 4 puntas, halos,
  ondas en el suelo y retícula del objetivo fijado.
- Del prototipo anterior: sombras (shadow mapping 2048², PCF), fuego volumétrico por
  raymarching (la magia Fuego y los enemigos ardiendo), luces puntuales, niebla y ACES.

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

Coste medido en Release (HD 620, 1280×720): ~0,5 ms de CPU por frame con ~600 instancias;
sin vsync, ~90 fps con shaders y ~365 fps en modo debug.

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
  game_draw.cpp                  Dibujado del mundo, efectos y HUD (y su versión debug)
  inventory_menu.h/.cpp          Menú de inventario
  text.h/.cpp                    Fuente vectorial mínima para HUD y menús
  menu.h/.cpp                    Menú de pausa
  renderer.h/.cpp                Render con shaders: instancing, sombras, cielo, materiales
  main.cpp                       Bucle principal, cámara, entrada
tests/
  logic_test.cpp                 Tests de la lógica sin ventana (combate, IA, botín...)
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

1. Más habilidades estilo género: planear, doble salto, contraataque tras guardia.
2. Tienda / síntesis con los materiales que sueltan los enemigos.
3. Un jefe con varias fases al final de las oleadas.
4. Sonido (golpes, recoger botín, música) y vibración de cámara más elaborada.
