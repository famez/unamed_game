#include "menu.h"

#include "text.h"

#include <cstdio>

namespace {

constexpr float kPanelW = 520, kPanelH = 360;
constexpr float kItemH = 42, kItemGap = 12, kItemsTop = 110;
constexpr float kMinSensitivity = 0.25f, kMaxSensitivity = 3.0f, kSensitivityStep = 0.25f;

void fillRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}

void strokeRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.line({x0, y0, 0}, {x1, y0, 0}, c);
    hud.line({x1, y0, 0}, {x1, y1, 0}, c);
    hud.line({x1, y1, 0}, {x0, y1, 0}, c);
    hud.line({x0, y1, 0}, {x0, y0, 0}, c);
}

}  // namespace

PauseMenu::Rect PauseMenu::panelRect(int width, int height) {
    float x = (float(width) - kPanelW) * 0.5f, y = (float(height) - kPanelH) * 0.5f;
    return {x, y, x + kPanelW, y + kPanelH};
}

PauseMenu::Rect PauseMenu::itemRect(int item, int width, int height) {
    Rect p = panelRect(width, height);
    float y = p.y0 + kItemsTop + float(item) * (kItemH + kItemGap);
    return {p.x0 + 40, y, p.x1 - 40, y + kItemH};
}

void PauseMenu::show() {
    open = true;
    selected_ = Continue;
}

void PauseMenu::changeSensitivity(int dir) {
    sensitivity = clampf(sensitivity + float(dir) * kSensitivityStep, kMinSensitivity, kMaxSensitivity);
}

PauseMenu::Result PauseMenu::update(const Input& in, int width, int height) {
    if (in.wasPressed(Key::W) || in.wasPressed(Key::Up)) selected_ = (selected_ + ItemCount - 1) % ItemCount;
    if (in.wasPressed(Key::S) || in.wasPressed(Key::Down)) selected_ = (selected_ + 1) % ItemCount;

    // El ratón selecciona al pasar por encima y activa con clic.
    bool clicked = false;
    for (int i = 0; i < ItemCount; ++i) {
        Rect r = itemRect(i, width, height);
        if (!r.contains(in.mouseX, in.mouseY)) continue;
        if (in.mouseDX != 0 || in.mouseDY != 0 || in.mouseLeftPressed) selected_ = i;
        if (in.mouseLeftPressed) clicked = true;
    }

    if (selected_ == Sensitivity) {
        if (in.wasPressed(Key::A) || in.wasPressed(Key::Left)) changeSensitivity(-1);
        if (in.wasPressed(Key::D) || in.wasPressed(Key::Right)) changeSensitivity(+1);
        if (clicked) {  // clic en la mitad izquierda baja, en la derecha sube
            Rect r = itemRect(Sensitivity, width, height);
            changeSensitivity(in.mouseX < (r.x0 + r.x1) * 0.5f ? -1 : +1);
        }
        return Result::None;
    }

    if (clicked || in.wasPressed(Key::Enter) || in.wasPressed(Key::Space)) {
        switch (selected_) {
            case Continue: return Result::Resume;
            case Restart: return Result::Restart;
            case Quit: return Result::Quit;
        }
    }
    return Result::None;
}

void PauseMenu::draw(DebugDraw& hud, int width, int height) const {
    const uint32_t yellow = rgba(255, 220, 80), gray = rgba(170, 170, 180), dim = rgba(110, 110, 120);

    fillRect(hud, 0, 0, float(width), float(height), rgba(0, 0, 0, 140));  // velo sobre la escena
    Rect p = panelRect(width, height);
    fillRect(hud, p.x0, p.y0, p.x1, p.y1, rgba(18, 20, 26, 235));
    strokeRect(hud, p.x0, p.y0, p.x1, p.y1, dim);

    const char* title = "PAUSA";
    drawText(hud, (p.x0 + p.x1 - textWidth(title, 6)) * 0.5f, p.y0 + 34, 6, title, rgba(240, 240, 245));

    char sens[48];
    std::snprintf(sens, sizeof(sens), "SENSIBILIDAD  < %.2f >", double(sensitivity));
    const char* labels[ItemCount] = {"CONTINUAR", "REINICIAR NIVEL", sens, "SALIR"};
    for (int i = 0; i < ItemCount; ++i) {
        Rect r = itemRect(i, width, height);
        bool sel = i == selected_;
        if (sel) {
            fillRect(hud, r.x0, r.y0, r.x1, r.y1, rgba(255, 220, 80, 35));
            strokeRect(hud, r.x0, r.y0, r.x1, r.y1, yellow);
        }
        const float scale = 3.0f;
        drawText(hud, (r.x0 + r.x1 - textWidth(labels[i], scale)) * 0.5f, (r.y0 + r.y1) * 0.5f - 3 * scale,
                 scale, labels[i], sel ? yellow : gray);
    }

    const char* hint = "W/S: ELEGIR   ENTER: ACEPTAR   A/D: AJUSTAR   ESC: VOLVER";
    drawText(hud, (p.x0 + p.x1 - textWidth(hint, 1.2f)) * 0.5f, p.y1 - 26, 1.2f, hint, dim);
}
