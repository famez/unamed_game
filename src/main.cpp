#include "debug_draw.h"
#include "game.h"
#include "gl.h"
#include "menu.h"
#include "platform.h"
#include "renderer.h"
#include "vmath.h"

#include <cstdio>

namespace {

constexpr double kFixedDt = 1.0 / 60.0;  // la simulación siempre avanza en pasos de 60 Hz
constexpr int kMaxStepsPerFrame = 5;     // evita la "espiral de la muerte" si un frame tarda
constexpr float kMouseSensitivity = 0.003f;  // radianes por píxel (x el ajuste del menú)
constexpr float kAimSensitivity = 0.6f;      // factor extra al apuntar, para afinar

struct OrbitCamera {
    Vec3 target;
    float yaw = 0;       // 0 = detrás de un personaje que mira a +Z
    float pitch = 0.35f; // radianes por encima del horizonte
    float distance = 5.0f;

    Vec3 forwardFlat() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
    Vec3 rightFlat() const { return {-std::cos(yaw), 0, std::sin(yaw)}; }
    Vec3 eye() const {
        Vec3 dir{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        return target - dir * distance;
    }
    Mat4 view() const { return lookAt(eye(), target, {0, 1, 0}); }
};

// Cámara real del frame: la orbital o, al apuntar (aim 0..1), acercada sobre el hombro
// derecho para dejar al personaje a la izquierda y la mira libre en el centro. Para lanzar
// (`wide`) se acerca menos: interesa ver la parábola entera.
OrbitCamera aimedCamera(const OrbitCamera& cam, float aim, bool wide) {
    float a = smoothstep01(aim);
    OrbitCamera v = cam;
    v.distance = lerpf(cam.distance, wide ? 3.2f : 1.5f, a);
    v.target = cam.target + Vec3{0, (wide ? 0.4f : 0.5f) * a, 0} + cam.rightFlat() * ((wide ? 0.8f : 0.55f) * a);
    return v;
}

// Pulsaciones acumuladas hasta el siguiente paso de simulación. Sin esto, un clic que
// cae en un frame en el que no toca ningún paso fijo (pasa a veces a 60 Hz) se perdería.
struct PendingInput {
    bool jump = false, use = false, throwRock = false;
    int slot = -1;
};

}  // namespace

int main() {
    WindowDesc desc;
    desc.title = "unamed_game";
#ifdef GAME_DEBUG_GL
    desc.debugContext = true;
#endif
    if (!platform::init(desc)) return 1;

    std::printf("GL_RENDERER : %s\nGL_VERSION  : %s\nvsync=%s msaa=%s\n",
                (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION),
                platform::vsyncEnabled() ? "on" : "off", platform::msaaEnabled() ? "on" : "off");

    DebugDraw dd, hud;
    Renderer renderer;
    if (!dd.init() || !hud.init() || !renderer.init()) return 1;

    glEnable(GL_PROGRAM_POINT_SIZE);
    if (platform::msaaEnabled()) glEnable(GL_MULTISAMPLE);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);

    Game game;
    game.init();
    OrbitCamera cam;
    cam.target = game.hero().body.pos + Vec3{0, 1.0f, 0};
    float aimBlend = 0;
    bool aimWide = false;  // el último apuntado fue para lanzar
    DrawFlags flags;
    bool debugView = false;  // alambre sin shaders (F12 / º)
    PauseMenu menu;
    DebugDraw overlay;  // capa del menú, por encima del HUD
    if (!overlay.init()) return 1;
    Input input;
    PendingInput pending;
    platform::setMouseCaptured(true);  // se hace efectiva en cuanto la ventana tenga foco
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

        // Menú de pausa: Esc/P lo abren y cierran. También se abre solo si se pierde el
        // ratón (alt-tab), para que el juego no siga corriendo sin control.
        bool menuWasOpen = menu.open;
        bool captured = platform::mouseCaptured();
        if (input.wasPressed(Key::Escape) || input.wasPressed(Key::P)) {
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
        } else if (wasCaptured && !captured) {
            openMenu();
        }
        wasCaptured = captured;
        bool playing = !menu.open && !menuWasOpen;  // el clic que cierra el menú no dispara

        if (input.wasPressed(Key::DebugView)) debugView = !debugView;
        if (debugView) {  // F1-F4 solo tienen sentido en el modo debug
            if (input.wasPressed(Key::F1)) flags.bones = !flags.bones;
            if (input.wasPressed(Key::F2)) flags.joints = !flags.joints;
            if (input.wasPressed(Key::F3)) flags.body = !flags.body;
            if (input.wasPressed(Key::F4)) flags.grid = !flags.grid;
        }

        bool aimHeld = false;    // apuntado que acerca la cámara (pistola, lanzamientos)
        bool rightHeld = false;  // botón derecho (con la espada: defensa)
        if (playing) {
            if (input.wasPressed(Key::R)) game.resetHero();
            pending.jump |= input.wasPressed(Key::Space);
            pending.use |= input.mouseLeftPressed || input.wasPressed(Key::F);
            pending.throwRock |= input.wasPressed(Key::G);
            const Key slotKeys[] = {Key::N1, Key::N2, Key::N3, Key::N4, Key::N5, Key::N6, Key::N7, Key::N8};
            for (int i = 0; i < 8; ++i)
                if (input.wasPressed(slotKeys[i])) pending.slot = i;
            Item item = game.hero().item;
            rightHeld = captured && input.mouseRight;
            bool gun = item == Item::Pistol || item == Item::Shotgun;
            aimHeld = rightHeld && (gun || item == Item::Rocks || item == Item::Molotov || item == Item::Grenade);
            if (aimHeld) aimWide = !gun;
        }

        // Cámara: ratón capturado, flechas o Q/E para orbitar; rueda para zoom.
        float fd = float(frameDt);
        if (playing && captured) {
            float sens = kMouseSensitivity * menu.sensitivity * (aimHeld ? kAimSensitivity : 1.0f);
            cam.yaw -= input.mouseDX * sens;
            cam.pitch += input.mouseDY * sens;
        }
        if (playing) {
            float orbitKeys = float(input.isDown(Key::Left) || input.isDown(Key::Q)) -
                              float(input.isDown(Key::Right) || input.isDown(Key::E));
            cam.yaw += orbitKeys * 2.0f * fd;
            cam.pitch += (float(input.isDown(Key::Up)) - float(input.isDown(Key::Down))) * 1.5f * fd;
            cam.distance = clampf(cam.distance * std::pow(0.9f, input.wheel), 1.5f, 30.0f);
        }
        cam.pitch = clampf(cam.pitch, -0.6f, 1.45f);
        aimBlend = moveTowards(aimBlend, aimHeld ? 1.0f : 0.0f, 7.0f * fd);

        // Simulación a paso fijo.
        OrbitCamera view = aimedCamera(cam, aimBlend, aimWide);
        accumulator += frameDt;
        int steps = 0;
        while (accumulator >= kFixedDt && steps < kMaxStepsPerFrame) {
            if (!menu.open) {
                Controls c;
                if (playing) {
                    c.move = normalize(cam.forwardFlat() * (float(input.isDown(Key::W)) - float(input.isDown(Key::S))) +
                                       cam.rightFlat() * (float(input.isDown(Key::D)) - float(input.isDown(Key::A))));
                    c.run = input.isDown(Key::Shift);
                }
                c.jump = pending.jump;
                c.use = pending.use;
                c.throwRock = pending.throwRock;
                c.selectSlot = pending.slot;
                c.aimYaw = cam.yaw;
                c.aimPitch = cam.pitch;
                c.aim = rightHeld;
                c.aimOrigin = view.eye();
                c.aimDir = normalize(view.target - view.eye());
                game.update(float(kFixedDt), c);
            }
            pending = PendingInput{};
            accumulator -= kFixedDt;
            ++steps;
        }
        if (steps == kMaxStepsPerFrame) accumulator = 0;

        // La cámara sigue al personaje con suavizado exponencial (independiente del fps).
        // Más rígida que antes: a 8 m/s un seguimiento blando deja al personaje atrás.
        Vec3 desired = game.hero().body.pos + Vec3{0, 1.0f, 0};
        cam.target += (desired - cam.target) * (1.0f - std::exp(-14.0f * fd));
        view = aimedCamera(cam, aimBlend, aimWide);

        // Render. Primero generamos la geometría en CPU y medimos ese tiempo: la primera
        // llamada GL del frame (glClear) puede bloquear esperando al vsync y falsearía la cifra.
        game.draw(dd, debugView ? nullptr : &renderer, flags, cam.rightFlat());
        game.drawHud(hud, w, h);
        if (menu.open) menu.draw(overlay, w, h);
        size_t verts = dd.vertexCount() + hud.vertexCount() + overlay.vertexCount();
        size_t boxes = renderer.instanceCount();
        double cpuTime = platform::timeSeconds() - frameStart;

        RenderCamera rc;
        rc.fovY = 60.0f * kPi / 180.0f;
        rc.aspect = float(w) / float(h > 0 ? h : 1);
        rc.view = view.view();
        rc.proj = perspective(rc.fovY, rc.aspect, rc.zNear, rc.zFar);
        rc.position = view.eye();
        rc.forward = normalize(view.target - rc.position);
        rc.right = normalize(cross(rc.forward, {0, 1, 0}));
        rc.up = cross(rc.right, rc.forward);

        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (!debugView) renderer.render(rc, game.hero().body.pos, w, h, game.time());
        glEnable(GL_DEPTH_TEST);
        dd.flush(rc.proj * rc.view, 6.0f);  // en modo normal: solo efectos (balas, láser...)
        // HUD y menú: encima de todo, en píxeles y con transparencia.
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Mat4 pixels = ortho(0, float(w), float(h), 0, -1, 1);
        hud.flush(pixels, 3.0f);
        overlay.flush(pixels, 3.0f);
        glDisable(GL_BLEND);
        platform::swapBuffers();
        if (!platform::vsyncEnabled())  // sin vsync, limitamos a ~60 fps para no quemar CPU/GPU
            platform::sleepSeconds(kFixedDt - (platform::timeSeconds() - frameStart));

        // Estadísticas en el título de la ventana cada medio segundo.
        statsTimer += frameDt;
        cpuTimeSum += cpuTime;
        ++statsFrames;
        if (statsTimer >= 0.5) {
            const Character& hero = game.hero();
            char title[256];
            std::snprintf(title, sizeof(title),
                          "unamed_game | %s | %.0f fps | cpu %.3f ms | %zu cajas %zu verts | %s | piedras %d | molotov %d | "
                          "derribos %d | pos (%.1f, %.1f, %.1f) | vel %.1f m/s%s",
                          debugView ? "DEBUG" : "shaders", statsFrames / statsTimer,
                          1000.0 * cpuTimeSum / statsFrames, boxes, verts,
                          itemName(hero.item), game.rocks(), game.molotovs(), game.kills(), hero.body.pos.x,
                          hero.body.pos.y, hero.body.pos.z, hero.speed, menu.open ? " | PAUSA" : "");
            platform::setTitle(title);
            statsTimer = cpuTimeSum = 0;
            statsFrames = 0;
        }
    }

    overlay.shutdown();
    renderer.shutdown();
    hud.shutdown();
    dd.shutdown();
    platform::shutdown();
    return 0;
}
