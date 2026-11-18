#include "audio/audio.h"
#include "audio/synth.h"
#include "render/debug_draw.h"
#include "game/game.h"
#include "game/props.h"
#include "core/gl.h"
#include "ui/inventory_menu.h"
#include "dev/showcase.h"
#include "dev/viewer.h"
#include "ui/level_select.h"
#include "ui/menu.h"
#include "render/hero_model.h"
#include "render/models.h"
#include "core/platform.h"
#include "render/renderer.h"
#include "render/text.h"
#include "core/vmath.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

constexpr double kFixedDt = 1.0 / 60.0;  // la simulación siempre avanza en pasos de 60 Hz
constexpr int kMaxStepsPerFrame = 5;     // evita la "espiral de la muerte" si un frame tarda
constexpr float kMouseSensitivity = 0.003f;  // radianes por píxel (x el ajuste del menú)

struct OrbitCamera {
    Vec3 target;
    float yaw = 0;       // 0 = detrás de un personaje que mira a +Z
    float pitch = 0.32f; // radianes por encima del horizonte
    float distance = 6.0f;

    Vec3 forwardFlat() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
    Vec3 rightFlat() const { return {-std::cos(yaw), 0, std::sin(yaw)}; }
    Vec3 eye() const {
        Vec3 dir{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        return target - dir * distance;
    }
    Mat4 view() const { return lookAt(eye(), target, {0, 1, 0}); }
};

// Pulsaciones acumuladas hasta el siguiente paso de simulación. Sin esto, un clic que
// cae en un frame en el que no toca ningún paso fijo (pasa a veces a 60 Hz) se perdería.
struct PendingInput {
    bool jump = false, attack = false, dodge = false, lockOn = false, nextTarget = false, unlock = false, interact = false;
    bool advance = false;  // pasar la línea de diálogo
    bool light = false, transform = false, summon = false, swapMode = false;
    bool runCommand = false;
    int command = -1, scroll = 0;
};

// Guarda el framebuffer en PPM (P6), de arriba a abajo.
void saveScreenshot(const char* path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * size_t(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(px.data() + size_t(y) * size_t(w) * 3, 1, size_t(w) * 3, f);
    std::fclose(f);
}

// Si el juego se cae (segfault, aborto...), imprime la pila de llamadas antes de salir: así
// cualquier cuelgue deja rastro en la terminal aunque el sistema no guarde el volcado.
void crashHandler(int sig) {
    const char msg[] = "\n*** El juego se ha caído. Pila de llamadas (pásala para depurar):\n";
    ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    std::signal(sig, SIG_DFL);
    std::raise(sig);  // y el volcado normal, si el sistema lo guarda
}

}  // namespace

const char kHelp[] =
    "Uso: game [opciones]\n"
    "\n"
    "Juego\n"
    "  --calidad baja|media|alta     calidad gráfica (también en el menú de pausa)\n"
    "  --resolucion ANCHOxALTO       tamaño de la ventana, p. ej. 1920x1080\n"
    "  --volumen 0..1                volumen general\n"
    "  --silencio                    sin audio\n"
    "\n"
    "Niveles (sin opciones, se elige en el selector del principio)\n"
    "  --nivel NOMBRE                entra directamente en ese nivel: plaza, mercado, puente,\n"
    "                                selva, cueva, callejones o los mapas grandes: campo, neon, cafetero, rio\n"
    "  --prueba [semilla]            patio de combate: enemigos sin fin; F1 y F2 hacen infinitos\n"
    "                                la vida y el Poder; Z cambia de modo\n"
    "  --historia                    la historia (capítulo 1), aparcada por ahora\n"
    "  --escena ID                   historia: empieza reproduciendo esa escena del guion\n"
    "\n"
    "Desarrollo\n"
    "  --debug                       solo esqueletos y cajas en alambre (también F12)\n"
    "  --autoplay                    un bot juega solo\n"
    "  --giros                       el bot solo corre haciendo curvas (para ver el giro)\n"
    "  --cambios                     el bot cambia de luz a sombra y vuelta (para ver el cambio)\n"
    "  --demo                        el bot enseña técnicas, formas y armas de luz y de sombra\n"
    "  --retrato | --cuerpo          cámara frente a la cara o al cuerpo del protagonista\n"
    "  --camyaw RADIANES             orientación inicial de la cámara\n"
    "  --visor FICHERO.glb [--anim N] ver un modelo como lo pinta el juego, animado; se recarga\n"
    "                                al volver a exportarlo (o con R)\n"
    "  --exportar-modelos [CARPETA]  escribe en .glb los enemigos, Ilse y el lobo de sombra que falten\n"
    "                                y sale (--forzar: también sustituye los que ya hay)\n"
    "  --sombra                      empezar en modo sombra\n"
    "  --medir                       sin vsync: escribe los fps reales por la consola\n"
    "  --vuelo                       la cámara sobrevuela el escenario (para ver los mapas grandes)\n"
    "  --vitrina [N,M...]            guion que enseña ataques, técnicas, formas y mecánicas (en el patio);\n"
    "                                con números, solo esos actos (6: romper farolas, 17: polillas)\n"
    "  --video FICHERO.mp4 [SEG]     graba a velocidad real (30 fps, con ffmpeg); sale a los SEG segundos\n"
    "  --screenshot FICHERO SEG [N]  guarda N fotogramas (.ppm) desde SEG segundos y sale\n"
    "  --efectos-golpe LISTA         efectos al golpear, separados por comas: tajo, destello, materia\n"
    "                                (esquirlas o tinta), sacudida, camara, remate; o todos / ninguno\n"
    "  -h, --help                    esta ayuda\n";

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::fputs(kHelp, stdout);
            return 0;
        }
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL}) std::signal(sig, crashHandler);
    for (int i = 1; i < argc; ++i)  // --exportar-modelos [carpeta]: los personajes generados a .glb (sin ventana)
        if (std::strcmp(argv[i], "--exportar-modelos") == 0) {
            std::string dir = i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : std::string(GAME_ASSET_DIR) + "/models";
            bool force = false;
            for (int k = 1; k < argc; ++k) force |= std::strcmp(argv[k], "--forzar") == 0;
            exportCharacterGlbs(dir, force);
            return 0;
        }
    WindowDesc desc;
    desc.title = "unamed_game";
    desc.msaaSamples = 0;  // el antialiasing lo hace el buffer de la escena (según la calidad)
    // Calidad gráfica (--calidad baja|media|alta); se puede cambiar en el menú de pausa.
    Quality quality = Quality::Medium;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--resolucion") == 0) std::sscanf(argv[i + 1], "%dx%d", &desc.width, &desc.height);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--calidad") == 0)
            quality = std::strcmp(argv[i + 1], "baja") == 0   ? Quality::Low
                      : std::strcmp(argv[i + 1], "alta") == 0 ? Quality::High
                                                              : Quality::Medium;
    bool measure = false;  // --medir: sin vsync ni límite, y los fps por la consola
    for (int i = 1; i < argc; ++i) {  // grabando no se espera al monitor (con la pantalla apagada se para)
        if (std::strcmp(argv[i], "--video") == 0) desc.vsync = false;
        if (std::strcmp(argv[i], "--medir") == 0) desc.vsync = false, measure = true;
    }
#ifdef GAME_DEBUG_GL
    desc.debugContext = true;
#endif
    if (!platform::init(desc)) return 1;

    std::printf("GL_RENDERER : %s\nGL_VERSION  : %s\nvsync=%s msaa=%s\n",
                (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION),
                platform::vsyncEnabled() ? "on" : "off", platform::msaaEnabled() ? "on" : "off");

    DebugDraw dd, glow;   // líneas (modo debug) y efectos translúcidos
    UiLayer hud, overlay;  // HUD y menús (formas + texto)
    Renderer renderer;
    if (!dd.init() || !glow.init() || !hud.init() || !overlay.init() || !renderer.init(quality)) return 1;
    // Fuentes: carpeta assets junto al directorio de trabajo o la del código fuente.
    // Carpeta assets: junto al directorio de trabajo o, si no, la del código fuente.
    const char* assetDir = "assets";
    if (FILE* f = std::fopen("assets/story/capitulo1.txt", "r")) std::fclose(f);
    else assetDir = GAME_ASSET_DIR;
    if (!loadFonts(assetDir)) return 1;
    // Audio: música y efectos sintetizados; la música se guarda en ~/.cache/unamed_game.
    Audio audio;
    float masterVolume = 1.0f;
    // Datos que se guardan entre partidas (música generada, lo aprendido por los Grises).
    std::string dataDir;
    {
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        const char* home = std::getenv("HOME");
        std::string cache = xdg && *xdg ? std::string(xdg) : std::string(home ? home : ".") + "/.cache";
        dataDir = cache + "/unamed_game";
        std::string mk = "mkdir -p '" + dataDir + "'";
        if (std::system(mk.c_str()) != 0) std::fprintf(stderr, "No se pudo crear %s\n", dataDir.c_str());
        // --silencio: sin audio (pruebas de noche); --volumen 0..1: volumen general.
        bool silent = false;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--silencio") == 0 || std::strcmp(argv[i], "--visor") == 0) silent = true;
            if (std::strcmp(argv[i], "--volumen") == 0 && i + 1 < argc) masterVolume = clampf(float(std::atof(argv[i + 1])), 0, 1);
        }
        if (!silent) audio.init(cache + "/unamed_game", std::string(assetDir) + "/audio/voces", std::string(assetDir) + "/audio/musica");
    }
    double modelStart = platform::timeSeconds();
    buildModels(renderer);
    loadHeroModels(renderer, assetDir);  // Vela y Hollín (.glb); sin ellos, el modelo de siempre
    loadCharacterGlbs(renderer, std::string(assetDir) + "/models");  // enemigos, Ilse y el lobo
    propLibrary().loadDir(std::string(assetDir) + "/props");          // piezas del escenario
    loadInhabitants(renderer, std::string(assetDir) + "/models/habitantes");
    uploadPropMeshes(renderer);
    std::printf("modelos generados en %.0f ms\n", 1000.0 * (platform::timeSeconds() - modelStart));
    for (int i = 1; i + 1 < argc; ++i)  // --visor FICHERO.glb: solo el modelo, sin partida
        if (std::strcmp(argv[i], "--visor") == 0) {
            int code = runViewer(argv[i + 1], renderer, dd, overlay, argc, argv);
            audio.shutdown();
            unloadFonts();
            overlay.shutdown();
            renderer.shutdown();
            hud.shutdown();
            glow.shutdown();
            dd.shutdown();
            platform::shutdown();
            return code;
        }

    glEnable(GL_PROGRAM_POINT_SIZE);
    if (platform::msaaEnabled()) glEnable(GL_MULTISAMPLE);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);

    // --prueba [semilla]: modo prueba en un escenario aleatorio; sin semilla, una distinta cada vez.
    TestMode testMode = TestMode::None;
    uint32_t testSeed = uint32_t(platform::timeSeconds() * 1000.0) ^ 0x5bd1e995u;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--prueba") == 0) {
            testMode = TestMode::Combat;
            if (i + 1 < argc && std::strcmp(argv[i + 1], "combate") == 0) ++i;  // (nombre antiguo)
            if (i + 1 < argc && argv[i + 1][0] != '-') testSeed = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        }
    bool showcase = false;  // --vitrina: el guion de grabación (en el patio de combate)
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--vitrina") == 0) {
            showcase = true;
            if (testMode == TestMode::None) testMode = TestMode::Combat, testSeed = 7;
        }
    // Nivel: el del selector, o el que se pida al arrancar (--nivel, --prueba, --historia).
    bool story = false, levelGiven = testMode != TestMode::None;
    LevelId freeLevel = LevelId::Plaza;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--historia") == 0 || std::strcmp(argv[i], "--escena") == 0) story = levelGiven = true;
        if (std::strcmp(argv[i], "--nivel") == 0 && i + 1 < argc) {
            const char* n = argv[++i];
            freeLevel = std::strcmp(n, "mercado") == 0      ? LevelId::Mercado
                        : std::strcmp(n, "puente") == 0     ? LevelId::Puente
                        : std::strcmp(n, "selva") == 0      ? LevelId::Selva
                        : std::strcmp(n, "cueva") == 0      ? LevelId::Cueva
                        : std::strcmp(n, "callejones") == 0 ? LevelId::Callejones
                        : std::strcmp(n, "campo") == 0      ? LevelId::Campo
                        : std::strcmp(n, "neon") == 0       ? LevelId::Neon
                        : std::strcmp(n, "cafetero") == 0   ? LevelId::Cafetero
                        : std::strcmp(n, "rio") == 0        ? LevelId::Rio
                        : std::strcmp(n, "copas") == 0      ? LevelId::Copas
                                                            : LevelId::Plaza;
            levelGiven = true;
        }
    }
    Game game;
    auto startGame = [&] {
        game.saveBrains();
        game = Game{};
        if (testMode != TestMode::None) {
            game.initTest(testMode, testSeed);
            std::printf("modo prueba, semilla %u\n", testSeed);
        } else if (story) {
            game.init(assetDir);
        } else {
            game.initLevel(freeLevel);
        }
        game.loadBrains(dataDir);
        for (int i = 1; i + 1 < argc; ++i)  // --escena id: reproduce ya esa escena (para escribirlas)
            if (std::strcmp(argv[i], "--escena") == 0) game.jumpToScene(argv[i + 1]);
    };
    startGame();
    for (int i = 1; i + 1 < argc; ++i)  // --efectos-golpe tajo,destello,...: solo esos efectos de golpe
        if (std::strcmp(argv[i], "--efectos-golpe") == 0) {
            unsigned mask = kFxAll;
            if (parseHitFx(argv[i + 1], mask)) game.setHitFx(mask);
            else std::fprintf(stderr, "--efectos-golpe: no entiendo \"%s\" (tajo, destello, materia, sacudida, camara, remate, todos, ninguno)\n", argv[i + 1]);
        }
    for (int i = 1; i < argc; ++i)  // --sombra: empezar en modo sombra (niveles sueltos y patio)
        if (std::strcmp(argv[i], "--sombra") == 0) {
            Controls z;
            z.swapMode = true;
            game.update(float(kFixedDt), z);
        }
    OrbitCamera cam;
    cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
    Vec3 camVel;  // velocidad del muelle de la cámara
    float sceneBlend = 0;  // 0 = cámara de juego, 1 = plano de diálogo
    float cineBlend = 0;   // 0 = cámara normal, 1 = plano de escena del guion
    float camDist = 6.0f;  // distancia de la cámara tras apartarse de las paredes
    Vec3 cineEye, cineFocus;
    DrawFlags flags;
    // Modo debug (F12 / º, o --debug al arrancar): solo esqueletos y cajas en alambre.
    bool debugView = false;
    // Opciones de desarrollo: --autoplay (un bot juega solo, sin capturar el ratón) y
    // --screenshot fichero.ppm segundos (guarda un frame en ese instante y sale).
    bool autoplay = false;
    float flyFloor = -1;    // altura del relieve bajo el vuelo (suavizada)
    bool flyover = false;   // --vuelo: la cámara sobrevuela el mapa (para ver los mapas grandes)
    for (int i = 1; i < argc; ++i) flyover |= std::strcmp(argv[i], "--vuelo") == 0;
    bool portrait = false;  // --retrato: cámara frente a la cara del protagonista (para modelarlo)
    bool bodyShot = false;  // --cuerpo: lo mismo, con el cuerpo entero
    bool turnsBot = false;  // --giros: el bot solo corre haciendo curvas y medias vueltas
    bool swapBot = false;   // --cambios: el bot se queda quieto y cambia de luz a sombra y vuelta
    bool demoBot = false;   // --demo: el bot enseña técnicas, formas y armas de las dos mitades
    float camYaw0 = 0;  // --camyaw: orientación inicial de la cámara (radianes)
    const char* shotPath = nullptr;
    double shotTime = 0;
    int shotCount = 1, shotIndex = 0;
    // --video: cada fotograma avanza 1/30 s de juego exacto y se manda a ffmpeg, así el vídeo
    // va a velocidad real aunque grabar sea más lento que jugar.
    const char* videoPath = nullptr;
    double videoSeconds = 0;
    FILE* video = nullptr;
    int videoFrames = 0, videoW = 0, videoH = 0;
    std::vector<unsigned char> videoPx;
    Showcase vitrina;
    for (int i = 1; i + 1 < argc; ++i)  // --vitrina 5,16: solo esos actos (contados desde 1)
        if (std::strcmp(argv[i], "--vitrina") == 0 && argv[i + 1][0] != '-') {
            std::vector<int> acts;
            for (const char* p = argv[i + 1]; *p;) {
                acts.push_back(std::atoi(p) - 1);
                while (*p && *p != ',') ++p;
                if (*p == ',') ++p;
            }
            vitrina.only(acts);
        }
    struct VideoSound {  // efectos que han sonado durante la grabación (se mezclan al acabar)
        double time;
        Sfx id;
        float gain, pan, pitch;
        int take;
    };
    std::vector<VideoSound> videoSounds;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) debugView = true;
        if (std::strcmp(argv[i], "--autoplay") == 0) autoplay = true;
        if (std::strcmp(argv[i], "--retrato") == 0) portrait = true;
        if (std::strcmp(argv[i], "--cuerpo") == 0) portrait = bodyShot = true;
        if (std::strcmp(argv[i], "--giros") == 0) autoplay = turnsBot = true;
        if (std::strcmp(argv[i], "--cambios") == 0) autoplay = swapBot = true;
        if (std::strcmp(argv[i], "--demo") == 0) autoplay = demoBot = true;
        if (std::strcmp(argv[i], "--vitrina") == 0) autoplay = true;
        if (std::strcmp(argv[i], "--video") == 0 && i + 1 < argc) {
            videoPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') videoSeconds = std::atof(argv[++i]);
        }
        if (std::strcmp(argv[i], "--camyaw") == 0 && i + 1 < argc) camYaw0 = float(std::atof(argv[++i]));
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            shotTime = std::atof(argv[i + 2]);
            i += 2;
            if (i + 1 < argc && argv[i + 1][0] != '-') shotCount = std::atoi(argv[++i]);  // frames seguidos
        }
    }
    cam.yaw = camYaw0 + game.hero().yaw;  // detrás de la protagonista
    const double startTime = platform::timeSeconds();
    int botStep = 0;
    PauseMenu menu;
    menu.quality = int(quality);
    InventoryMenu inventory;
    LevelSelect selector;
    bool fromPause = false;  // el selector se abrió desde la pausa (Esc vuelve a ella)
    if (!levelGiven && !autoplay) selector.show();
    Input input;
    PendingInput pending;
    platform::setMouseCaptured(!autoplay && !selector.open);  // se hace efectiva en cuanto la ventana tenga foco
    bool wasCaptured = false;

    auto openMenu = [&] {
        menu.show();
        platform::setMouseCaptured(false);
    };
    auto closeMenu = [&] {
        menu.open = false;
        platform::setMouseCaptured(true);
    };

    double prevTime = platform::timeSeconds();
    float farolasMix = 0;  // el tema «Farolas»: 0 la capa de luz, 1 la de sombra (cruce suave)
    float combatHold = 0, memoryHold = 0;  // la música de combate y la del recuerdo aguantan un poco al acabar
    bool atInn = false;                    // junto a la fonda (con histéresis, para no ir y venir)
    bool wasDown = false, hadBoss = false;
    int lastKills = 0;
    double accumulator = 0;
    double statsTimer = 0, cpuTimeSum = 0;
    int statsFrames = 0;
    bool quit = false;

    while (!quit) {
        double frameStart = platform::timeSeconds();
        double frameDt = frameStart - prevTime;
        if (videoPath) frameDt = 2 * kFixedDt + 1e-9;  // grabando: 30 fps de juego por fotograma
        prevTime = frameStart;
        int w = platform::width(), h = platform::height();

        if (!platform::pumpEvents(input)) break;
        // La interfaz se diseña para 720 px de alto y se escala con la ventana: a 1080p
        // todo es 1,5 veces mayor. `uw`×`uh` es el tamaño virtual; el ratón se pasa a él.
        const float uiScale = float(h > 0 ? h : 720) / 720.0f;
        const int uw = int(float(w) / uiScale + 0.5f), uh = 720;
        Input uiInput = input;
        uiInput.mouseX /= uiScale;
        uiInput.mouseY /= uiScale;
        uiInput.mouseDX /= uiScale;
        uiInput.mouseDY /= uiScale;

        // Menús: Esc/P abren y cierran la pausa; I el inventario. La pausa también se abre
        // sola si se pierde el ratón (alt-tab), para que el juego no siga sin control.
        bool menuWasOpen = menu.open || inventory.open || selector.open;
        bool captured = platform::mouseCaptured();
        if (selector.open) {
            int pick = selector.update(uiInput, uw, uh);
            if (pick == -2) {  // Esc: al arrancar sale del juego; desde la pausa, vuelve a ella
                if (fromPause) openMenu();
                else quit = true;
            } else if (pick >= 0) {
                int n = 0;
                const LevelSelect::Entry& e = LevelSelect::entries(n)[pick];
                testMode = e.test ? TestMode::Combat : TestMode::None;
                testSeed = uint32_t(platform::timeSeconds() * 1000.0) ^ 0x5bd1e995u;
                freeLevel = e.level;
                story = false;
                startGame();
                game.setTestCheats(menu.infHp, menu.infPower);
                cam = OrbitCamera{};
                cam.yaw = game.hero().yaw;
                cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
                platform::setMouseCaptured(true);
            }
        } else if (inventory.open) {
            if (inventory.update(uiInput, game, uw, uh)) {
                inventory.open = false;
                platform::setMouseCaptured(true);
            }
        } else if (input.wasPressed(Key::Escape) || input.wasPressed(Key::P)) {
            if (menu.open)
                closeMenu();
            else
                openMenu();
        } else if (menu.open) {
            PauseMenu::Result result = menu.update(uiInput, uw, uh);
            switch (result) {
                case PauseMenu::Result::Resume: closeMenu(); break;
                case PauseMenu::Result::ChooseLevel:
                    menu.open = false;
                    fromPause = true;
                    selector.show();
                    break;
                case PauseMenu::Result::Restart:
                    startGame();
                    game.setTestCheats(menu.infHp, menu.infPower);
                    cam = OrbitCamera{};
                    cam.yaw = game.hero().yaw;
                    cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
                    closeMenu();
                    break;
                case PauseMenu::Result::CheatsChanged: game.setTestCheats(menu.infHp, menu.infPower); break;
                case PauseMenu::Result::Quit: quit = true; break;
                case PauseMenu::Result::QualityChanged: renderer.setQuality(Quality(menu.quality)); break;
                case PauseMenu::Result::None: break;
            }
        } else if (input.wasPressed(Key::I) || input.wasPressed(Key::Tab)) {
            inventory.show();
            platform::setMouseCaptured(false);
        } else if (wasCaptured && !captured && !autoplay) {
            openMenu();
        }
        wasCaptured = captured;
        bool paused = menu.open || inventory.open || selector.open;
        bool playing = !paused && !menuWasOpen;  // el clic que cierra el menú no ataca

        if (input.wasPressed(Key::DebugView)) debugView = !debugView;
        if (debugView) {  // F1-F4 solo tienen sentido en el modo debug
            if (input.wasPressed(Key::F1)) flags.bones = !flags.bones;
            if (input.wasPressed(Key::F2)) flags.joints = !flags.joints;
            if (input.wasPressed(Key::F3)) flags.body = !flags.body;
            if (input.wasPressed(Key::F4)) flags.grid = !flags.grid;
        } else if (game.sandbox() && !menu.open && !selector.open) {  // sin historia: F1-F2, qué es infinito
            bool changed = true;
            if (input.wasPressed(Key::F1)) menu.infHp = !menu.infHp;
            else if (input.wasPressed(Key::F2)) menu.infPower = !menu.infPower;
            else changed = false;
            if (changed) game.setTestCheats(menu.infHp, menu.infPower);
        }

        bool guardHeld = false;
        if (playing) {
            if (game.dialogueBlocking()) {  // en una escena, todo sirve para avanzar
                pending.advance |= input.mouseLeftPressed || input.wasPressed(Key::E) || input.wasPressed(Key::Space) ||
                                   input.wasPressed(Key::Enter);
            } else {
                pending.jump |= input.wasPressed(Key::Space);
                pending.attack |= input.mouseLeftPressed;
                pending.dodge |= input.wasPressed(Key::C) || input.wasPressed(Key::Ctrl);
                pending.lockOn |= input.wasPressed(Key::R);
                pending.nextTarget |= input.mouseMiddlePressed;
                pending.unlock |= input.wasPressed(Key::X);
                pending.interact |= input.wasPressed(Key::F);
                pending.runCommand |= input.wasPressed(Key::E);
                pending.transform |= input.wasPressed(Key::G);
                pending.summon |= input.wasPressed(Key::T);
                pending.swapMode |= input.wasPressed(Key::Z);
                // Q: entrar en la luz / saltar a otra fuente / fundirse en la sombra.
                pending.light |= input.wasPressed(Key::Q);
            }
            pending.scroll -= int(input.wheel);  // rueda arriba = entrada de arriba
            const Key slotKeys[] = {Key::N1, Key::N2, Key::N3, Key::N4, Key::N5, Key::N6, Key::N7, Key::N8};
            for (int i = 0; i < Game::kCommandCount; ++i)
                if (input.wasPressed(slotKeys[i])) pending.command = i;
            guardHeld = captured && input.mouseRight;
        }

        // Cámara: ratón capturado o flechas para orbitar.
        float fd = float(frameDt);
        if (playing && captured) {
            float sens = kMouseSensitivity * menu.sensitivity;
            cam.yaw -= input.mouseDX * sens;
            cam.pitch += input.mouseDY * sens;
        }
        if (playing) {
            cam.yaw += (float(input.isDown(Key::Left)) - float(input.isDown(Key::Right))) * 2.0f * fd;
            cam.pitch += (float(input.isDown(Key::Up)) - float(input.isDown(Key::Down))) * 1.5f * fd;
        }
        cam.pitch = clampf(cam.pitch, -0.3f, 1.3f);

        // Simulación a paso fijo.
        accumulator += frameDt;
        int steps = 0;
        while (accumulator >= kFixedDt && steps < kMaxStepsPerFrame) {
            if (!paused) {
                Controls c;
                if (playing)
                    c.move = normalize(cam.forwardFlat() * (float(input.isDown(Key::W)) - float(input.isDown(Key::S))) +
                                       cam.rightFlat() * (float(input.isDown(Key::D)) - float(input.isDown(Key::A))));
                c.jump = pending.jump;
                c.attack = pending.attack;
                c.dodge = pending.dodge;
                c.guard = guardHeld;
                c.interact = pending.interact;
                c.lockOn = pending.lockOn;
                c.nextTarget = pending.nextTarget;
                c.unlock = pending.unlock;
                c.command = pending.command;
                c.runCommand = pending.runCommand;
                c.advance = pending.advance;
                c.sprint = playing && input.isDown(Key::Shift);
                c.light = pending.light;
                c.transform = pending.transform;
                c.summon = pending.summon;
                c.swapMode = pending.swapMode;
                c.attackHeld = playing && captured && input.mouseLeft && !game.dialogueBlocking();
                c.commandScroll = pending.scroll;
                c.camForward = cam.forwardFlat();
                c.aimOrigin = cam.eye();
                c.aimDir = normalize(cam.target - cam.eye());
                if (autoplay) {
                    c.advance = botStep % 25 == 0;  // el bot pasa los diálogos
                    // Bot: fija al enemigo más cercano, se acerca y encadena combos; de vez
                    // en cuando lanza Fuego o Hielo.
                    ++botStep;
                    Vec3 target;
                    if (!game.lockTarget(target)) {
                        c.lockOn = botStep % 20 == 0;
                        Vec3 goal;  // sin nadie a mano, hacia el objetivo
                        if (game.objective(goal) && length(flat(goal - game.hero().body.pos)) > 2.0f)
                            c.move = normalize(flat(goal - game.hero().body.pos));
                    } else {
                        Vec3 to = flat(target - game.hero().body.pos);
                        if (length(to) > 2.2f) c.move = normalize(to);
                        c.attack = botStep % 9 == 0 && length(to) < 6.0f;
                        if (botStep % 400 == 200) c.command = botStep % 800 < 400 ? 0 : 1;
                        if (botStep % 300 == 150) c.jump = true;
                    }
                    c.interact = botStep % 30 == 0;
                    if (turnsBot) {  // tramos: recto, curva de 90°, media vuelta, giro parado
                        int t = botStep % 360;
                        c = Controls{};
                        c.camForward = cam.forwardFlat();
                        c.advance = botStep % 25 == 0;
                        float a = t < 60 ? 0.0f : t < 120 ? kPi * 0.5f : t < 180 ? kPi * 1.5f : t < 220 ? kPi * 1.5f : kPi * 0.5f;
                        if (t >= 180 && t < 220) a = -1;  // parado
                        if (t >= 220 && t < 250) a = kPi * 0.5f, c.move = Vec3{std::sin(a), 0, std::cos(a)} * 0.15f;
                        else if (a >= 0) c.move = Vec3{std::sin(a), 0, std::cos(a)};
                    }
                    if (swapBot) {  // quieto; cambia de modo y, entre medias, da un par de golpes
                        c = Controls{};
                        c.camForward = cam.forwardFlat();
                        int t = botStep % 150;
                        c.swapMode = t == 20;
                        c.attack = t == 80 || t == 92 || t == 104;
                    }
                    if (demoBot) {  // guion: luz (estallido, Forma Solar), sombra (mazo, técnicas, lobo)
                        if (botStep == 1) game.setTestCheats(true, true);
                        Controls d;
                        d.camForward = cam.forwardFlat();
                        Vec3 foe;
                        bool has = game.lockTarget(foe);
                        if (!has) d.lockOn = botStep % 15 == 0;
                        Vec3 to = has ? flat(foe - game.hero().body.pos) : Vec3{};
                        int t = botStep;
                        bool fight = (t > 110 && t < 230) || (t > 290 && t < 390) || (t > 650 && t < 800);
                        if (has && fight && length(to) > 2.0f) d.move = normalize(to);
                        if (fight && t % 11 == 0 && has && length(to) < 4.0f) d.attack = true;
                        d.command = t == 40 ? 3 : t == 420 ? 0 : t == 490 ? 1 : t == 560 ? 3 : -1;
                        d.transform = t == 90 || t == 240 || t == 630;
                        d.swapMode = t == 260;
                        c = d;
                    }
                    if (showcase) {
                        c = vitrina.step(game, cam.yaw, c);
                        if (vitrina.done()) quit = true;
                    }
                    // En los modos de prueba también cambia a sombra y lanza agujeros negros.
                    if (turnsBot || swapBot || demoBot || showcase) {
                    } else if (game.testMode() != TestMode::None) {
                        c.swapMode = botStep == 30;
                        if (botStep % 240 == 120) c.command = 4;  // la técnica grande de cada mitad
                    }
                }
                game.update(float(kFixedDt), c);
                pending = PendingInput{};
                if (game.takeShopRequest() && !inventory.open) {  // la Doctora Umbra abre el taller
                    inventory.showWorkshop();
                    platform::setMouseCaptured(false);
                }
            }
            accumulator -= kFixedDt;
            ++steps;
        }
        if (steps == kMaxStepsPerFrame) accumulator = 0;
        // Fracción del siguiente paso ya transcurrida: se dibuja entre el estado anterior y
        // el actual, así el movimiento es suave aunque el monitor no vaya a 60 Hz.
        float alpha = paused ? 1.0f : clampf(float(accumulator / kFixedDt), 0, 1);

        // La cámara sigue al personaje con un muelle amortiguado (independiente del fps) y
        // se adelanta un poco hacia donde corre. Con un objetivo fijado gira sola para
        // encuadrar a los dos.
        Vec3 heroPos = game.heroRenderPos(alpha);
        if (vitrina.takeCut()) {  // el guion ha cambiado de sitio al protagonista: corte de plano
            cam.target = heroPos + Vec3{0, 1.1f, 0};
            camVel = {};
            camDist = cam.distance;
        }
        Vec3 heroVel = game.hero().body.vel;
        Vec3 desired = heroPos + Vec3{0, 1.1f, 0} + flat(heroVel) * 0.12f;
        Vec3 lockPos;
        if (!paused && game.lockTarget(lockPos)) {
            Vec3 to = flat(lockPos - heroPos);
            if (length(to) > 1.0f) {
                float yaw = std::atan2(to.x, to.z);
                cam.yaw = lerpAngle(cam.yaw, yaw, 1.0f - std::exp(-3.0f * fd));
            }
            desired += (lockPos - heroPos) * 0.2f;
        }
        {
            const float omega = 11.0f;  // muelle críticamente amortiguado
            float x = omega * fd, k = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            Vec3 change = cam.target - desired;
            Vec3 temp = (camVel + change * omega) * fd;
            camVel = (camVel - temp * omega) * k;
            cam.target = desired + (change + temp) * k;
        }
        OrbitCamera view = cam;
        // Escenas: la cámara se pone de cara al personaje, más cerca y más baja (plano de
        // diálogo); al terminar vuelve a la orbital con suavidad.
        sceneBlend = moveTowards(sceneBlend, game.dialogueBlocking() ? 1.0f : 0.0f, 2.0f * fd);
        if (sceneBlend > 0) {
            float k = smoothstep01(sceneBlend);
            float faceYaw = game.hero().yaw + kPi + 0.35f;
            view.yaw = lerpAngle(cam.yaw, faceYaw, k);
            view.pitch = lerpf(cam.pitch, 0.12f, k);
            view.distance = lerpf(cam.distance, 3.4f, k);
            view.target = cam.target + Vec3{0, 0.25f * k, 0};
            if (sceneBlend >= 1.0f) cam.yaw = faceYaw;  // al salir, se parte de este encuadre
        }
        // La cámara se acerca si hay una pared entre ella y el personaje.
        // Si algo se interpone, la cámara se acerca enseguida; cuando deja de haberlo, se aleja
        // despacio (sin saltos de un fotograma a otro).
        float clear = game.cameraClear(cam.target, cam.eye());
        float wantDist = clear < 1.0f ? std::max(1.0f, view.distance * clear - 0.3f) : view.distance;
        camDist = wantDist < camDist ? wantDist : moveTowards(camDist, wantDist, 4.0f * fd);
        view.distance = camDist;
        float shake = game.cameraShake();
        if (shake > 0) {  // temblor en los golpes fuertes
            float t = game.time();
            view.target += Vec3{std::sin(t * 61.0f), std::sin(t * 47.0f + 1.0f), std::sin(t * 53.0f + 2.0f)} *
                           (0.12f * shake * shake);
        }
        view.target += game.cameraKick();  // tirón hacia donde empuja el último golpe

        RenderCamera rc;
        rc.fovY = 60.0f * kPi / 180.0f + game.fovKick();
        rc.aspect = float(w) / float(h > 0 ? h : 1);
        // Planos de escena: la cámara se funde hacia el plano y vuelve al terminar.
        Vec3 eye = view.eye(), look = view.target;
        Vec3 ce, cf;
        bool cine = game.cinematic(ce, cf);
        if (cine) {
            cineEye = cineBlend <= 0 ? ce : cineEye + (ce - cineEye) * (1.0f - std::exp(-4.0f * fd));
            cineFocus = cineBlend <= 0 ? cf : cineFocus + (cf - cineFocus) * (1.0f - std::exp(-4.0f * fd));
        }
        cineBlend = moveTowards(cineBlend, cine ? 1.0f : 0.0f, (cine ? 1.2f : 1.6f) * fd);
        if (cineBlend > 0) {
            float k = smoothstep01(cineBlend);
            eye = eye + (cineEye - eye) * k;
            look = look + (cineFocus - look) * k;
        }
        if (portrait) {
            Vec3 f = game.hero().forward(), side{f.z, 0, -f.x};
            look = translationOf(game.hero().joints[Head]) + Vec3{0, 0.11f, 0};
            float turn = 0.3f + 0.5f * std::sin(game.time() * 0.4f);  // gira despacio para verla de lado
            eye = look + f * 0.95f + side * turn + Vec3{0, 0.02f, 0};
            if (bodyShot) {
                look = game.heroRenderPos(alpha) + Vec3{0, 0.95f, 0};
                eye = look + f * 3.4f + side * (turn * 2.5f) + Vec3{0, 0.3f, 0};
            }
        }
        if (flyover) {  // --vuelo: vueltas alrededor del centro, acercándose y alejándose
            float tt = game.time();
            float r = 95.0f + 55.0f * std::sin(tt * 0.09f), hgt = 22.0f + 16.0f * std::sin(tt * 0.13f + 1.0f), a = tt * 0.11f;
            if (game.level() == LevelId::Neon) hgt += 34;  // por encima de las torres
            look = {0, 6.0f, 0};
            if (game.level() == LevelId::Rio) {  // el diorama de la bahía: de cerca
                r = 46.0f + 12.0f * std::sin(tt * 0.09f), hgt = 20.0f + 8.0f * std::sin(tt * 0.13f + 1.0f);
                look = {-6, 4.0f, -8};
            }
            if (game.level() == LevelId::Copas) {  // la aldea de las copas: de cerca, entre los troncos
                r = 38.0f + 10.0f * std::sin(tt * 0.09f), hgt = 10.0f + 8.0f * std::sin(tt * 0.13f + 1.0f);
                look = {0, 2.0f, 12};
            }
            a += camYaw0;  // --camyaw: desde qué lado empieza
            eye = look + Vec3{std::cos(a) * r, hgt, std::sin(a) * r};
            // Siempre por encima del relieve (los morros pasan de 80 m), con la transición suave.
            float floor = 0;
            for (float k : {0.0f, 6.0f, 12.0f, 18.0f}) {
                float ak = (tt + k * 0.25f) * 0.11f;
                Vec3 ahead = look + Vec3{std::cos(ak) * r, 0, std::sin(ak) * r};
                floor = std::max(floor, game.world().groundAt(ahead.x, ahead.z));
            }
            flyFloor = flyFloor < 0 ? floor : flyFloor + (floor - flyFloor) * std::min(1.0f, fd * 1.5f);
            eye.y = std::max(eye.y, flyFloor + 18.0f);
        }
        rc.view = lookAt(eye, look, {0, 1, 0});
        rc.zFar = game.viewDistance();
        rc.proj = perspective(rc.fovY, rc.aspect, rc.zNear, rc.zFar);
        rc.position = eye;
        rc.forward = normalize(look - rc.position);
        rc.right = normalize(cross(rc.forward, {0, 1, 0}));
        rc.up = cross(rc.right, rc.forward);
        ViewInfo vi;
        vi.viewProj = rc.proj * rc.view;
        vi.eye = rc.position;
        vi.right = rc.right;
        vi.up = rc.up;
        vi.width = w;
        vi.height = h;

        // Render. Primero generamos la geometría en CPU y medimos ese tiempo: la primera
        // llamada GL del frame (glClear) puede bloquear esperando al vsync y falsearía la cifra.
        game.draw(dd, glow, debugView ? nullptr : &renderer, flags, vi, alpha);
        ViewInfo uiView = vi;
        uiView.width = uw;
        uiView.height = uh;
        if (!portrait && !flyover && !selector.open) game.drawHud(hud, uiView, debugView);
        if (menu.open) menu.draw(overlay, uw, uh);
        if (selector.open) selector.draw(overlay, uw, uh);
        if (inventory.open) inventory.draw(overlay, game, uw, uh);
        if (showcase && vitrina.captionAlpha() > 0) {  // rótulo del acto
            float a = vitrina.captionAlpha();
            TextStyle st;
            st.align = Align::Center;
            st.glow = 0.5f;
            float y = float(uh) * 0.16f;
            overlay.shapes.quad({0, y - 14, 0}, {float(uw), y - 14, 0}, {float(uw), y + 86, 0}, {0, y + 86, 0},
                                rgba(0, 0, 0, uint8_t(150 * a)));
            TextStyle tt = st;
            tt.face = FontFace::Title;
            drawText(overlay, float(uw) * 0.5f, y, 40, vitrina.title(), rgba(240, 236, 226, uint8_t(255 * a)), tt);
            drawText(overlay, float(uw) * 0.5f, y + 50, 20, vitrina.subtitle(), rgba(200, 196, 190, uint8_t(255 * a)), st);
        }
        size_t verts = dd.vertexCount() + glow.vertexCount() + hud.vertexCount() + overlay.vertexCount();
        size_t instances = renderer.instanceCount(), propsDrawn = renderer.propCount();
        double cpuTime = platform::timeSeconds() - frameStart;

        glViewport(0, 0, w, h);
        if (debugView) {
            glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        } else {
            renderer.beginFrame(w, h);
            renderer.render(rc, heroPos, renderer.sceneWidth(), renderer.sceneHeight(), game.time());
        }
        glEnable(GL_DEPTH_TEST);
        dd.flush(vi.viewProj, 6.0f);  // en modo normal: casi nada; en debug, toda la escena
        // Efectos translúcidos (estelas, destellos, halos): prueban la profundidad pero no
        // la escriben, así no se tapan entre ellos. Mezcla alfa normal y no aditiva: sobre
        // un escenario claro la suma satura a blanco y se pierde el color.
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        // El alfa se acumula aparte: es la máscara de bloom (los efectos brillan).
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
        glow.flush(vi.viewProj, 4.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (!debugView) renderer.endFrame();  // resolver, bloom y componer en pantalla
        glEnable(GL_BLEND);
        // HUD y menús: encima de todo, en píxeles y con transparencia.
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Mat4 pixels = ortho(0, float(uw), float(uh), 0, -1, 1);
        hud.flush(pixels);
        if (!debugView && !portrait && !flyover && !selector.open) {  // la cara de la protagonista en el hexágono del HUD
            float k = float(w) / float(uw);
            float cxp = (float(uw) - Game::kPortraitInset) * k, cyp = Game::kPortraitInset * k;  // desde abajo
            int hw = int(31.0f * k), hh = int(29.0f * k);
            RenderCamera pc;
            if (game.drawPortrait(renderer, pc, float(hw) / float(hh)))
                renderer.portrait(pc, int(cxp) - hw, int(cyp) - hh + int(2 * k), 2 * hw, 2 * hh);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        overlay.flush(pixels);
        glDisable(GL_BLEND);
        if (shotPath && platform::timeSeconds() - startTime >= shotTime) {
            if (shotCount == 1) {
                saveScreenshot(shotPath, w, h);
            } else {
                char name[512];
                std::snprintf(name, sizeof(name), "%s_%03d.ppm", shotPath, shotIndex);
                saveScreenshot(name, w, h);
            }
            quit = ++shotIndex >= shotCount;
        }
        if (videoPath) {
            if (!video) {
                videoW = w & ~1, videoH = h & ~1;
                char cmd[1024];
                std::snprintf(cmd, sizeof(cmd),
                              "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - -vf vflip "
                              "-c:v libx264 -pix_fmt yuv420p -crf 22 -preset fast '%s'",
                              videoW, videoH, videoPath);
                video = popen(cmd, "w");
                videoPx.resize(size_t(videoW) * size_t(videoH) * 3);
            }
            if (video) {
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, videoW, videoH, GL_RGB, GL_UNSIGNED_BYTE, videoPx.data());
                std::fwrite(videoPx.data(), 1, videoPx.size(), video);
                ++videoFrames;
                if (videoSeconds > 0 && videoFrames >= int(videoSeconds * 30)) quit = true;
            }
        }
        platform::swapBuffers();
        if (videoPath || measure) {
        } else if (!platform::vsyncEnabled())  // sin vsync, limitamos a ~60 fps para no quemar CPU/GPU
            platform::sleepSeconds(kFixedDt - (platform::timeSeconds() - frameStart));

        // Sonido: los efectos que ha emitido el juego, con panorámica y distancia desde la
        // cámara; y el volumen de cada capa de la música según cómo va la partida.
        for (const SoundEvent& e : game.takeSounds()) {
            float gain = e.gain, pan = 0;
            if (e.positional) {
                Vec3 rel = e.pos - rc.position;
                float dist = length(rel);
                pan = dist > 0.1f ? dot(rel * (1.0f / dist), rc.right) * 0.8f : 0.0f;
                gain /= 1.0f + 0.06f * dist;
            }
            audio.play(e.id, gain, pan, e.pitch, e.take);
            if (videoPath) videoSounds.push_back({double(videoFrames) / 30.0, e.id, gain, pan, e.pitch, e.take});
        }
        if (menu.open || inventory.open || selector.open) {
            if (input.wasPressed(Key::W) || input.wasPressed(Key::S) || input.wasPressed(Key::Up) ||
                input.wasPressed(Key::Down) || input.wasPressed(Key::A) || input.wasPressed(Key::D))
                audio.play(Sfx::MenuMove, 0.5f);
            if (input.wasPressed(Key::Enter) || input.mouseLeftPressed) audio.play(Sfx::MenuOk, 0.5f);
        }
        {
            float croma = game.croma();
            bool memory = game.dialogue().memory();
            float stems[int(Stem::Count)];
            stems[int(Stem::Pad)] = 0.85f;
            stems[int(Stem::Melody)] = memory ? 0.0f : smoothstep01((croma - 0.2f) / 0.15f) * 0.9f;
            stems[int(Stem::Pulse)] = (game.inCombat() && !game.dialogueBlocking()) || game.bossAlive() ? 0.8f : 0.0f;
            stems[int(Stem::Shimmer)] = memory ? 0.3f : smoothstep01((croma - 0.55f) / 0.2f) * 0.75f;
            // Qué suena, de más a menos importante: la portada (el selector), el fin del capítulo,
            // los recuerdos, el jefe, el combate, la fonda y, si no, la música de la zona.
            LevelId lv = game.level();
            float dt = float(frameDt);
            combatHold = game.fighting() && !game.dialogueBlocking() ? 4.0f : std::max(0.0f, combatHold - dt);
            memoryHold = memory ? 3.0f : std::max(0.0f, memoryHold - dt);
            float innDist = 1e9f;
            for (const Npc& n : game.npcs())
                if (n.role == NpcRole::Inn) innDist = std::min(innDist, length(n.body.body.pos - game.hero().body.pos));
            atInn = innDist < (atInn ? 10.0f : 7.0f);
            bool farolas = lv == LevelId::Plaza || lv == LevelId::Campo || lv == LevelId::Cafetero || lv == LevelId::Rio || lv == LevelId::Copas;
            TrackId track = selector.open ? TrackId::PianoNocturno
                            : game.chapterEnded() ? TrackId::PianoUltimoFarol
                            : memoryHold > 0 ? TrackId::PianoVelaHollin
                            : game.bossAlive() ? TrackId::JefeGrabado
                            : combatHold > 0 || lv == LevelId::Prueba ? TrackId::Combate
                            : atInn ? TrackId::Fonda
                            : farolas ? TrackId::Farolas
                            : lv == LevelId::Mercado || lv == LevelId::Callejones || lv == LevelId::Neon ? TrackId::Ciudad
                            : lv == LevelId::Puente || lv == LevelId::Selva || lv == LevelId::Cueva ? TrackId::Puente
                                                                                                     : TrackId::Plaza;
            // «Farolas» (la plaza y los mapas abiertos): sus dos capas suenan a la vez y se cruzan
            // al cambiar de forma, con potencia constante (~1,7 s). Las otras grabadas, en una capa.
            farolasMix = moveTowards(farolasMix, game.shadowMode() ? 1.0f : 0.0f, dt / 1.7f);
            if (track == TrackId::Farolas) {
                float g = smoothstep01(farolasMix);
                for (float& s : stems) s = 0;
                stems[int(Stem::Pad)] = 0.9f * std::cos(g * kPi * 0.5f);
                stems[int(Stem::Melody)] = 0.9f * std::sin(g * kPi * 0.5f);
            } else if (int(track) > int(TrackId::Farolas)) {
                for (float& s : stems) s = 0;
                stems[int(Stem::Pad)] = 0.9f;
            }
            audio.setStemGains(stems);
            audio.playTrack(track);
            // Fragmentos: la derrota al caer; la victoria al tumbar al jefe (no si desaparece
            // porque se reinicia el encuentro o se cambia de escenario).
            bool down = game.hero().downTimer >= 0, boss = game.bossAlive();
            if (down && !wasDown) audio.playStinger(Stinger::Derrota);
            if (hadBoss && !boss && !down && game.kills() > lastKills) audio.playStinger(Stinger::Victoria);
            wasDown = down;
            hadBoss = boss;
            lastKills = game.kills();
            audio.setMasterVolume((paused && !selector.open ? 0.35f : 0.8f) * masterVolume);
        }

        // Estadísticas en el título de la ventana cada medio segundo.
        statsTimer += frameDt;
        cpuTimeSum += cpuTime;
        ++statsFrames;
        if (statsTimer >= 0.5) {
            char title[256];
            std::snprintf(title, sizeof(title),
                          "unamed_game | %s | %.0f fps | cpu %.3f ms | %zu instancias %zu verts | %s | "
                          "enemigos %zu | botin %zu%s",
                          debugView ? "DEBUG" : "shaders", statsFrames / statsTimer, 1000.0 * cpuTimeSum / statsFrames,
                          instances, verts, game.levelName().c_str(), game.enemyCount(), game.pickupCount(),
                          paused ? " | PAUSA" : "");
            platform::setTitle(title);
            if (measure)
                std::printf("%.0f fps (%.2f ms) | cpu %.2f ms | %zu piezas | %s\n", statsFrames / statsTimer, 1000.0 * statsTimer / statsFrames,
                            1000.0 * cpuTimeSum / statsFrames, propsDrawn, game.levelName().c_str());
            statsTimer = cpuTimeSum = 0;
            statsFrames = 0;
        }
    }

    game.saveBrains();
    if (video) {
        pclose(video);  // ffmpeg termina de escribir el vídeo
        // Sonido: los efectos mezclados en su instante (con su tono y su panorámica) y unidos al
        // vídeo. La música no va (es aleatoria por capas).
        size_t frames = size_t(double(videoFrames) / 30.0 * kSampleRate) + kSampleRate;
        std::vector<float> mix(frames * 2, 0.0f);
        std::vector<std::vector<StereoBuffer>> cache(size_t(Sfx::Count));
        std::vector<bool> loaded(size_t(Sfx::Count), false);
        size_t pick = 0;
        for (const VideoSound& s : videoSounds) {
            std::vector<StereoBuffer>& takes = cache[size_t(s.id)];
            if (!loaded[size_t(s.id)]) takes = sfxVariants(s.id, std::string(assetDir) + "/audio/voces"), loaded[size_t(s.id)] = true;
            if (takes.empty()) continue;
            const StereoBuffer& b = takes[size_t(s.take >= 0 ? s.take : pick++) % takes.size()];
            float a = (s.pan + 1.0f) * 0.25f * 3.14159f, gl = std::cos(a) * 1.41f, gr = std::sin(a) * 1.41f;
            size_t start = size_t(s.time * kSampleRate), n = b.frames();
            for (size_t i = 0; start + i < frames; ++i) {
                double src = double(i) * double(s.pitch);  // tono: se lee más deprisa o más despacio
                size_t k = size_t(src);
                if (k + 1 >= n) break;
                float f = float(src - double(k));
                for (int ch = 0; ch < 2; ++ch) {
                    float v = (float(b.data[k * 2 + size_t(ch)]) * (1 - f) + float(b.data[(k + 1) * 2 + size_t(ch)]) * f) / 32768.0f;
                    mix[(start + i) * 2 + size_t(ch)] += v * s.gain * (ch ? gr : gl) * 0.7f;
                }
            }
        }
        StereoBuffer out;
        out.data.resize(mix.size());
        for (size_t i = 0; i < mix.size(); ++i) out.data[i] = int16_t(std::tanh(mix[i]) * 32000.0f);
        std::string wav = std::string(videoPath) + ".wav", tmp = std::string(videoPath) + ".tmp.mp4";
        if (saveWav(wav, out)) {
            std::string cmd = "ffmpeg -loglevel error -y -i '" + std::string(videoPath) + "' -i '" + wav +
                              "' -c:v copy -c:a aac -b:a 160k -shortest '" + tmp + "' && mv '" + tmp + "' '" + videoPath + "'";
            if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "No se pudo añadir el sonido al vídeo\n");
            std::remove(wav.c_str());
        }
    }
    audio.shutdown();
    unloadFonts();
    overlay.shutdown();
    renderer.shutdown();
    hud.shutdown();
    glow.shutdown();
    dd.shutdown();
    platform::shutdown();
    return 0;
}
