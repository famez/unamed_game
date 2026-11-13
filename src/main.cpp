#include "debug_draw.h"
#include "game.h"
#include "gl.h"
#include "inventory_menu.h"
#include "menu.h"
#include "platform.h"
#include "renderer.h"
#include "vmath.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    bool jump = false, attack = false, dodge = false, lockOn = false, unlock = false, interact = false;
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

}  // namespace

int main(int argc, char** argv) {
    WindowDesc desc;
    desc.title = "unamed_game";
#ifdef GAME_DEBUG_GL
    desc.debugContext = true;
#endif
    if (!platform::init(desc)) return 1;

    std::printf("GL_RENDERER : %s\nGL_VERSION  : %s\nvsync=%s msaa=%s\n",
                (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION),
                platform::vsyncEnabled() ? "on" : "off", platform::msaaEnabled() ? "on" : "off");

    DebugDraw dd, glow, hud, overlay;  // líneas, efectos aditivos, HUD y menús
    Renderer renderer;
    if (!dd.init() || !glow.init() || !hud.init() || !overlay.init() || !renderer.init()) return 1;

    glEnable(GL_PROGRAM_POINT_SIZE);
    if (platform::msaaEnabled()) glEnable(GL_MULTISAMPLE);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);

    Game game;
    game.init();
    OrbitCamera cam;
    cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
    Vec3 camVel;  // velocidad del muelle de la cámara
    DrawFlags flags;
    // Modo debug (F12 / º, o --debug al arrancar): solo esqueletos y cajas en alambre.
    bool debugView = false;
    // Opciones de desarrollo: --autoplay (un bot juega solo, sin capturar el ratón) y
    // --screenshot fichero.ppm segundos (guarda un frame en ese instante y sale).
    bool autoplay = false;
    float camYaw0 = 0;  // --camyaw: orientación inicial de la cámara (radianes)
    const char* shotPath = nullptr;
    double shotTime = 0;
    int shotCount = 1, shotIndex = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) debugView = true;
        if (std::strcmp(argv[i], "--autoplay") == 0) autoplay = true;
        if (std::strcmp(argv[i], "--camyaw") == 0 && i + 1 < argc) camYaw0 = float(std::atof(argv[++i]));
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            shotTime = std::atof(argv[i + 2]);
            i += 2;
            if (i + 1 < argc && argv[i + 1][0] != '-') shotCount = std::atoi(argv[++i]);  // frames seguidos
        }
    }
    cam.yaw = camYaw0;
    const double startTime = platform::timeSeconds();
    int botStep = 0;
    PauseMenu menu;
    InventoryMenu inventory;
    Input input;
    PendingInput pending;
    platform::setMouseCaptured(!autoplay);  // se hace efectiva en cuanto la ventana tenga foco
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
    double accumulator = 0;
    double statsTimer = 0, cpuTimeSum = 0;
    int statsFrames = 0;
    bool quit = false;

    while (!quit) {
        double frameStart = platform::timeSeconds();
        double frameDt = frameStart - prevTime;
        prevTime = frameStart;
        int w = platform::width(), h = platform::height();

        if (!platform::pumpEvents(input)) break;

        // Menús: Esc/P abren y cierran la pausa; I el inventario. La pausa también se abre
        // sola si se pierde el ratón (alt-tab), para que el juego no siga sin control.
        bool menuWasOpen = menu.open || inventory.open;
        bool captured = platform::mouseCaptured();
        if (inventory.open) {
            if (inventory.update(input, game, w, h)) {
                inventory.open = false;
                platform::setMouseCaptured(true);
            }
        } else if (input.wasPressed(Key::Escape) || input.wasPressed(Key::P)) {
            if (menu.open)
                closeMenu();
            else
                openMenu();
        } else if (menu.open) {
            switch (menu.update(input, w, h)) {
                case PauseMenu::Result::Resume: closeMenu(); break;
                case PauseMenu::Result::Restart:
                    game = Game{};
                    game.init();
                    cam = OrbitCamera{};
                    cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
                    closeMenu();
                    break;
                case PauseMenu::Result::Quit: quit = true; break;
                case PauseMenu::Result::None: break;
            }
        } else if (input.wasPressed(Key::I)) {
            inventory.show();
            platform::setMouseCaptured(false);
        } else if (wasCaptured && !captured && !autoplay) {
            openMenu();
        }
        wasCaptured = captured;
        bool paused = menu.open || inventory.open;
        bool playing = !paused && !menuWasOpen;  // el clic que cierra el menú no ataca

        if (input.wasPressed(Key::DebugView)) debugView = !debugView;
        if (debugView) {  // F1-F4 solo tienen sentido en el modo debug
            if (input.wasPressed(Key::F1)) flags.bones = !flags.bones;
            if (input.wasPressed(Key::F2)) flags.joints = !flags.joints;
            if (input.wasPressed(Key::F3)) flags.body = !flags.body;
            if (input.wasPressed(Key::F4)) flags.grid = !flags.grid;
        }

        bool guardHeld = false;
        if (playing) {
            pending.jump |= input.wasPressed(Key::Space);
            pending.attack |= input.mouseLeftPressed;
            pending.dodge |= input.wasPressed(Key::Shift);
            pending.lockOn |= input.wasPressed(Key::Tab);
            pending.unlock |= input.mouseMiddlePressed || input.wasPressed(Key::X);
            pending.interact |= input.wasPressed(Key::F);
            pending.runCommand |= input.wasPressed(Key::E);
            pending.scroll -= int(input.wheel);  // rueda arriba = entrada de arriba
            const Key slotKeys[] = {Key::N1, Key::N2, Key::N3, Key::N4, Key::N5, Key::N6};
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
                c.unlock = pending.unlock;
                c.command = pending.command;
                c.runCommand = pending.runCommand;
                c.commandScroll = pending.scroll;
                c.camForward = cam.forwardFlat();
                if (autoplay) {
                    // Bot: fija al enemigo más cercano, se acerca y encadena combos; de vez
                    // en cuando lanza Fuego o Hielo.
                    ++botStep;
                    Vec3 target;
                    if (!game.lockTarget(target)) {
                        c.lockOn = botStep % 20 == 0;
                    } else {
                        Vec3 to = flat(target - game.hero().body.pos);
                        if (length(to) > 2.2f) c.move = normalize(to);
                        c.attack = botStep % 9 == 0 && length(to) < 6.0f;
                        if (botStep % 400 == 200) c.command = botStep % 800 < 400 ? 0 : 1;
                        if (botStep % 300 == 150) c.jump = true;
                    }
                    c.interact = botStep % 30 == 0;
                }
                game.update(float(kFixedDt), c);
                pending = PendingInput{};
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
        // La cámara se acerca si hay una pared entre ella y el personaje.
        float clear = game.cameraClear(cam.target, cam.eye());
        if (clear < 1.0f) view.distance = std::max(1.0f, cam.distance * clear - 0.3f);
        float shake = game.cameraShake();
        if (shake > 0) {  // temblor en los golpes fuertes
            float t = game.time();
            view.target += Vec3{std::sin(t * 61.0f), std::sin(t * 47.0f + 1.0f), std::sin(t * 53.0f + 2.0f)} *
                           (0.12f * shake * shake);
        }

        RenderCamera rc;
        rc.fovY = 60.0f * kPi / 180.0f + game.fovKick();
        rc.aspect = float(w) / float(h > 0 ? h : 1);
        rc.view = view.view();
        rc.proj = perspective(rc.fovY, rc.aspect, rc.zNear, rc.zFar);
        rc.position = view.eye();
        rc.forward = normalize(view.target - rc.position);
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
        game.drawHud(hud, vi, debugView);
        if (menu.open) menu.draw(overlay, w, h);
        if (inventory.open) inventory.draw(overlay, game, w, h);
        size_t verts = dd.vertexCount() + glow.vertexCount() + hud.vertexCount() + overlay.vertexCount();
        size_t instances = renderer.instanceCount();
        double cpuTime = platform::timeSeconds() - frameStart;

        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (!debugView) renderer.render(rc, heroPos, w, h, game.time());
        glEnable(GL_DEPTH_TEST);
        dd.flush(vi.viewProj, 6.0f);  // en modo normal: casi nada; en debug, toda la escena
        // Efectos translúcidos (estelas, destellos, halos): prueban la profundidad pero no
        // la escriben, así no se tapan entre ellos. Mezcla alfa normal y no aditiva: sobre
        // un escenario claro la suma satura a blanco y se pierde el color.
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glow.flush(vi.viewProj, 4.0f);
        glDepthMask(GL_TRUE);
        // HUD y menús: encima de todo, en píxeles y con transparencia.
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Mat4 pixels = ortho(0, float(w), float(h), 0, -1, 1);
        hud.flush(pixels, 3.0f);
        overlay.flush(pixels, 3.0f);
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
        platform::swapBuffers();
        if (!platform::vsyncEnabled())  // sin vsync, limitamos a ~60 fps para no quemar CPU/GPU
            platform::sleepSeconds(kFixedDt - (platform::timeSeconds() - frameStart));

        // Estadísticas en el título de la ventana cada medio segundo.
        statsTimer += frameDt;
        cpuTimeSum += cpuTime;
        ++statsFrames;
        if (statsTimer >= 0.5) {
            char title[256];
            std::snprintf(title, sizeof(title),
                          "unamed_game | %s | %.0f fps | cpu %.3f ms | %zu instancias %zu verts | oleada %d | "
                          "enemigos %zu | botin %zu%s",
                          debugView ? "DEBUG" : "shaders", statsFrames / statsTimer, 1000.0 * cpuTimeSum / statsFrames,
                          instances, verts, game.wave(), game.enemyCount(), game.pickupCount(),
                          paused ? " | PAUSA" : "");
            platform::setTitle(title);
            statsTimer = cpuTimeSum = 0;
            statsFrames = 0;
        }
    }

    overlay.shutdown();
    renderer.shutdown();
    hud.shutdown();
    glow.shutdown();
    dd.shutdown();
    platform::shutdown();
    return 0;
}
