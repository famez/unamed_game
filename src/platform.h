#pragma once

// Capa de plataforma: ventana, contexto OpenGL, entrada y tiempo.
// La implementación actual es X11 + GLX (platform_x11.cpp). Para portar a otro
// sistema (o pasar a GLFW/SDL) solo hay que reimplementar este fichero.

enum class Key {
    W, A, S, D, Q, E,
    Shift, Space, Escape, Enter,
    Left, Right, Up, Down,
    N1, N2, N3, N4, N5, N6, N7, N8,
    F1, F2, F3, F4,
    DebugView,  // F12 o la tecla º (a la izquierda del 1 en teclado español)
    F, G, P, R, I, X, Tab, C, Ctrl, V, T, Z,
    Count
};

struct Input {
    bool down[(int)Key::Count] = {};     // tecla mantenida
    bool pressed[(int)Key::Count] = {};  // flanco de bajada en este frame
    float mouseDX = 0, mouseDY = 0;      // píxeles movidos en este frame
    float wheel = 0;                     // +1 arriba / -1 abajo
    bool mouseLeft = false, mouseRight = false, mouseMiddle = false;
    bool mouseLeftPressed = false;       // clic izquierdo en este frame
    bool mouseMiddlePressed = false;     // clic central en este frame
    float mouseX = 0, mouseY = 0;        // posición del cursor en la ventana (píxeles)

    bool isDown(Key k) const { return down[(int)k]; }
    bool wasPressed(Key k) const { return pressed[(int)k]; }
};

struct WindowDesc {
    const char* title = "game";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool debugContext = false;
    int msaaSamples = 4;  // 0 = desactivado
};

namespace platform {

bool init(const WindowDesc& desc);
void shutdown();

// Procesa la cola de eventos. Devuelve false si el usuario cierra la ventana.
bool pumpEvents(Input& input);
void swapBuffers();
void setTitle(const char* title);

// Captura del ratón estilo FPS: cursor oculto y confinado, y los deltas llegan sin
// límite de bordes. Se suelta sola al perder el foco y vuelve al recuperarlo.
void setMouseCaptured(bool captured);
bool mouseCaptured();

int width();
int height();
bool vsyncEnabled();
bool msaaEnabled();

double timeSeconds();  // reloj monotónico
void sleepSeconds(double s);

}  // namespace platform
