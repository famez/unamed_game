#include "platform.h"

#include "gl.h"

#include <GL/glx.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

struct State {
    Display* dpy = nullptr;
    Window win = 0;
    Colormap cmap = 0;
    GLXContext ctx = nullptr;
    Atom wmDelete = 0;
    int width = 0, height = 0;
    bool vsync = false;
    bool msaa = false;
    bool havePointer = false;
    int lastX = 0, lastY = 0;
    Cursor blankCursor = 0;
    bool hasFocus = false;
    bool wantCapture = false;  // lo que pide el juego
    bool captured = false;     // si el grab está realmente activo
    int xiOpcode = -1;         // XInput2 disponible si >= 0
} g;

bool grabPointer() {
    // confine_to = nuestra ventana: el cursor no puede salir mientras dure el grab.
    int r = XGrabPointer(g.dpy, g.win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, g.win, g.blankCursor, CurrentTime);
    return r == GrabSuccess;
}

void releasePointer() {
    if (g.captured) XUngrabPointer(g.dpy, CurrentTime);
    g.captured = false;
}

bool mapKey(KeySym ks, Key& out) {
    switch (ks) {
        case XK_w: out = Key::W; return true;
        case XK_a: out = Key::A; return true;
        case XK_s: out = Key::S; return true;
        case XK_d: out = Key::D; return true;
        case XK_q: out = Key::Q; return true;
        case XK_e: out = Key::E; return true;
        case XK_f: out = Key::F; return true;
        case XK_g: out = Key::G; return true;
        case XK_F1: out = Key::F1; return true;
        case XK_F2: out = Key::F2; return true;
        case XK_F3: out = Key::F3; return true;
        case XK_F4: out = Key::F4; return true;
        case XK_F12:
        case XK_masculine:
        case XK_grave: out = Key::DebugView; return true;
        case XK_p: out = Key::P; return true;
        case XK_r: out = Key::R; return true;
        case XK_i: out = Key::I; return true;
        case XK_x: out = Key::X; return true;
        case XK_Tab: out = Key::Tab; return true;
        case XK_c: out = Key::C; return true;
        case XK_v: out = Key::V; return true;
        case XK_Control_L:
        case XK_Control_R: out = Key::Ctrl; return true;
        case XK_1: out = Key::N1; return true;
        case XK_2: out = Key::N2; return true;
        case XK_3: out = Key::N3; return true;
        case XK_4: out = Key::N4; return true;
        case XK_5: out = Key::N5; return true;
        case XK_6: out = Key::N6; return true;
        case XK_7: out = Key::N7; return true;
        case XK_8: out = Key::N8; return true;
        case XK_Shift_L:
        case XK_Shift_R: out = Key::Shift; return true;
        case XK_space: out = Key::Space; return true;
        case XK_Escape: out = Key::Escape; return true;
        case XK_Return:
        case XK_KP_Enter: out = Key::Enter; return true;
        case XK_Left: out = Key::Left; return true;
        case XK_Right: out = Key::Right; return true;
        case XK_Up: out = Key::Up; return true;
        case XK_Down: out = Key::Down; return true;
        default: return false;
    }
}

GLXFBConfig chooseFBConfig(int screen, int samples) {
    const int attrs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        GLX_SAMPLE_BUFFERS, samples > 0 ? 1 : 0,
        GLX_SAMPLES, samples,
        None,
    };
    int count = 0;
    GLXFBConfig* list = glXChooseFBConfig(g.dpy, screen, attrs, &count);
    if (!list || count == 0) return nullptr;
    GLXFBConfig cfg = list[0];
    XFree(list);
    return cfg;
}

bool hasExtension(const char* list, const char* name) {
    if (!list) return false;
    size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

#ifdef GAME_DEBUG_GL
void GLAPIENTRY glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                                const GLchar* msg, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::fprintf(stderr, "[GL %s] %s\n", type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "info", msg);
}
#endif

}  // namespace

namespace platform {

bool init(const WindowDesc& desc) {
    g.dpy = XOpenDisplay(nullptr);
    if (!g.dpy) {
        std::fprintf(stderr, "No se pudo abrir el display X11 (¿DISPLAY definido?)\n");
        return false;
    }
    int screen = DefaultScreen(g.dpy);

    int glxMajor = 0, glxMinor = 0;
    if (!glXQueryVersion(g.dpy, &glxMajor, &glxMinor) || (glxMajor == 1 && glxMinor < 3)) {
        std::fprintf(stderr, "Se necesita GLX >= 1.3\n");
        return false;
    }

    GLXFBConfig cfg = desc.msaaSamples > 0 ? chooseFBConfig(screen, desc.msaaSamples) : nullptr;
    g.msaa = cfg != nullptr;
    if (!cfg) cfg = chooseFBConfig(screen, 0);
    if (!cfg) {
        std::fprintf(stderr, "No hay ningún framebuffer GLX compatible\n");
        return false;
    }

    XVisualInfo* vi = glXGetVisualFromFBConfig(g.dpy, cfg);
    Window root = RootWindow(g.dpy, vi->screen);
    g.cmap = XCreateColormap(g.dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes swa{};
    swa.colormap = g.cmap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                     ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    g.win = XCreateWindow(g.dpy, root, 0, 0, desc.width, desc.height, 0, vi->depth, InputOutput,
                          vi->visual, CWColormap | CWEventMask, &swa);
    XFree(vi);
    if (!g.win) {
        std::fprintf(stderr, "XCreateWindow falló\n");
        return false;
    }
    g.width = desc.width;
    g.height = desc.height;

    XStoreName(g.dpy, g.win, desc.title);
    g.wmDelete = XInternAtom(g.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g.dpy, g.win, &g.wmDelete, 1);
    XMapWindow(g.dpy, g.win);

    // Sin esto, mantener una tecla genera pares KeyRelease/KeyPress falsos.
    Bool detectable = False;
    XkbSetDetectableAutoRepeat(g.dpy, True, &detectable);

    // XInput2 "raw motion": deltas directos del dispositivo, sin aceleración del sistema
    // y sin depender de la posición del cursor. Solo se entregan en la ventana raíz.
    int xiEvent = 0, xiError = 0, xiMajor = 2, xiMinor = 0;
    if (XQueryExtension(g.dpy, "XInputExtension", &g.xiOpcode, &xiEvent, &xiError) &&
        XIQueryVersion(g.dpy, &xiMajor, &xiMinor) == Success) {
        unsigned char mask[XIMaskLen(XI_RawMotion)] = {};
        XISetMask(mask, XI_RawMotion);
        XIEventMask em{XIAllMasterDevices, sizeof(mask), mask};
        XISelectEvents(g.dpy, root, &em, 1);
    } else {
        g.xiOpcode = -1;
    }

    // Cursor invisible para el modo capturado: un pixmap 1x1 sin bits.
    char empty = 0;
    Pixmap pm = XCreateBitmapFromData(g.dpy, g.win, &empty, 1, 1);
    XColor black{};
    g.blankCursor = XCreatePixmapCursor(g.dpy, pm, pm, &black, &black, 0, 0);
    XFreePixmap(g.dpy, pm);

    // Contexto OpenGL 3.3 Core: la versión más baja con pipeline moderno. Funciona
    // en prácticamente cualquier GPU de los últimos 15 años.
    auto createContextAttribs = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB(
        (const GLubyte*)"glXCreateContextAttribsARB");
    if (!createContextAttribs) {
        std::fprintf(stderr, "Falta GLX_ARB_create_context\n");
        return false;
    }
    const int ctxAttrs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, desc.debugContext ? GLX_CONTEXT_DEBUG_BIT_ARB : 0,
        None,
    };
    g.ctx = createContextAttribs(g.dpy, cfg, nullptr, True, ctxAttrs);
    XSync(g.dpy, False);
    if (!g.ctx || !glXMakeCurrent(g.dpy, g.win, g.ctx)) {
        std::fprintf(stderr, "No se pudo crear el contexto OpenGL 3.3 Core\n");
        return false;
    }

    if (desc.vsync) {
        const char* exts = glXQueryExtensionsString(g.dpy, screen);
        if (hasExtension(exts, "GLX_EXT_swap_control")) {
            auto fn = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddressARB(
                (const GLubyte*)"glXSwapIntervalEXT");
            if (fn) { fn(g.dpy, g.win, 1); g.vsync = true; }
        } else if (hasExtension(exts, "GLX_MESA_swap_control")) {
            auto fn = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddressARB(
                (const GLubyte*)"glXSwapIntervalMESA");
            if (fn && fn(1) == 0) g.vsync = true;
        }
    }

#ifdef GAME_DEBUG_GL
    if (desc.debugContext) {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }
#endif
    return true;
}

void shutdown() {
    if (!g.dpy) return;
    releasePointer();
    if (g.blankCursor) XFreeCursor(g.dpy, g.blankCursor);
    if (g.ctx) {
        glXMakeCurrent(g.dpy, None, nullptr);
        glXDestroyContext(g.dpy, g.ctx);
    }
    if (g.win) XDestroyWindow(g.dpy, g.win);
    if (g.cmap) XFreeColormap(g.dpy, g.cmap);
    XCloseDisplay(g.dpy);
    g = State{};
}

bool pumpEvents(Input& in) {
    std::memset(in.pressed, 0, sizeof(in.pressed));
    in.mouseDX = in.mouseDY = in.wheel = 0;
    in.mouseLeftPressed = false;
    in.mouseMiddlePressed = false;

    while (XPending(g.dpy)) {
        XEvent ev;
        XNextEvent(g.dpy, &ev);
        switch (ev.type) {
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == g.wmDelete) return false;
                break;
            case ConfigureNotify:
                g.width = ev.xconfigure.width;
                g.height = ev.xconfigure.height;
                break;
            case KeyPress:
            case KeyRelease: {
                Key k;
                if (!mapKey(XLookupKeysym(&ev.xkey, 0), k)) break;
                bool isDown = ev.type == KeyPress;
                if (isDown && !in.down[(int)k]) in.pressed[(int)k] = true;
                in.down[(int)k] = isDown;
                break;
            }
            case ButtonPress:
            case ButtonRelease: {
                bool isDown = ev.type == ButtonPress;
                in.mouseX = float(ev.xbutton.x);
                in.mouseY = float(ev.xbutton.y);
                switch (ev.xbutton.button) {
                    case Button1:
                        if (isDown && !in.mouseLeft) in.mouseLeftPressed = true;
                        in.mouseLeft = isDown;
                        break;
                    case Button2:
                        if (isDown && !in.mouseMiddle) in.mouseMiddlePressed = true;
                        in.mouseMiddle = isDown;
                        break;
                    case Button3: in.mouseRight = isDown; break;
                    case Button4: if (isDown) in.wheel += 1; break;
                    case Button5: if (isDown) in.wheel -= 1; break;
                }
                break;
            }
            case GenericEvent:
                if (ev.xcookie.extension == g.xiOpcode && XGetEventData(g.dpy, &ev.xcookie)) {
                    auto* raw = (XIRawEvent*)ev.xcookie.data;
                    // Solo nos interesan mientras el ratón está capturado (llegan siempre,
                    // porque se escuchan en la raíz). Los valores vienen empaquetados:
                    // uno por cada bit activo de la máscara (eje 0 = X, eje 1 = Y).
                    if (raw->evtype == XI_RawMotion && g.captured) {
                        const double* v = raw->raw_values;
                        for (int axis = 0; axis < 2 && axis < raw->valuators.mask_len * 8; ++axis)
                            if (XIMaskIsSet(raw->valuators.mask, axis)) {
                                (axis == 0 ? in.mouseDX : in.mouseDY) += float(*v);
                                ++v;
                            }
                    }
                    XFreeEventData(g.dpy, &ev.xcookie);
                }
                break;
            case MotionNotify:
                in.mouseX = float(ev.xmotion.x);
                in.mouseY = float(ev.xmotion.y);
                // Con XI2 y ratón capturado los deltas ya llegan por GenericEvent.
                if (g.havePointer && !(g.captured && g.xiOpcode >= 0)) {
                    in.mouseDX += float(ev.xmotion.x - g.lastX);
                    in.mouseDY += float(ev.xmotion.y - g.lastY);
                }
                g.lastX = ev.xmotion.x;
                g.lastY = ev.xmotion.y;
                g.havePointer = true;
                break;
            case FocusIn:
                if (ev.xfocus.mode == NotifyNormal || ev.xfocus.mode == NotifyWhileGrabbed)
                    g.hasFocus = true;
                break;
            case FocusOut:
                if (ev.xfocus.mode != NotifyNormal && ev.xfocus.mode != NotifyWhileGrabbed) break;
                // Al perder el foco no recibimos los KeyRelease: soltamos todo, y el ratón
                // también (alt-tab no debe dejar el puntero atrapado en el juego).
                g.hasFocus = false;
                std::memset(in.down, 0, sizeof(in.down));
                in.mouseLeft = in.mouseRight = false;
                releasePointer();
                break;
        }
    }

    // Solo capturamos con foco: nunca robamos el puntero a otra aplicación.
    if (g.wantCapture && !g.captured && g.hasFocus) {
        g.captured = grabPointer();
        g.havePointer = false;  // descarta el salto del primer delta
    }

    // Recentrado: devolvemos el puntero al centro para que nunca toque los bordes. El
    // MotionNotify que genera el warp llega con delta 0 porque actualizamos lastX/lastY.
    if (g.captured) {
        int cx = g.width / 2, cy = g.height / 2;
        if (!g.havePointer || std::abs(g.lastX - cx) > g.width / 4 || std::abs(g.lastY - cy) > g.height / 4) {
            XWarpPointer(g.dpy, None, g.win, 0, 0, 0, 0, cx, cy);
            g.lastX = cx;
            g.lastY = cy;
            g.havePointer = true;
        }
    }
    return true;
}

void setMouseCaptured(bool captured) {
    g.wantCapture = captured;
    if (!captured) releasePointer();
}

bool mouseCaptured() { return g.captured; }

void swapBuffers() { glXSwapBuffers(g.dpy, g.win); }
void setTitle(const char* title) { XStoreName(g.dpy, g.win, title); }
int width() { return g.width; }
int height() { return g.height; }
bool vsyncEnabled() { return g.vsync; }
bool msaaEnabled() { return g.msaa; }

double timeSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

void sleepSeconds(double s) {
    if (s <= 0) return;
    timespec ts;
    ts.tv_sec = time_t(s);
    ts.tv_nsec = long((s - double(ts.tv_sec)) * 1e9);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

}  // namespace platform
