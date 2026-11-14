#include "menu.h"


#include <algorithm>
#include <cstdio>

namespace {

constexpr float kPanelW = 520, kPanelH = 522;
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

    if (selected_ == QualityItem) {
        int dir = in.wasPressed(Key::A) || in.wasPressed(Key::Left) ? -1 : in.wasPressed(Key::D) || in.wasPressed(Key::Right) ? 1 : 0;
        if (clicked) {
            Rect r = itemRect(QualityItem, width, height);
            dir = in.mouseX < (r.x0 + r.x1) * 0.5f ? -1 : 1;
        }
        int q = std::max(0, std::min(2, quality + dir));
        if (q != quality) {
            quality = q;
            return Result::QualityChanged;
        }
        return Result::None;
    }

    if (clicked || in.wasPressed(Key::Enter) || in.wasPressed(Key::Space)) {
        switch (selected_) {
            case Continue: return Result::Resume;
            case Restart: return Result::Restart;
            case TestCombatItem: return Result::TestCombat;
            case TestLightItem: return Result::TestLight;
            case Quit: return Result::Quit;
        }
    }
    return Result::None;
}

void PauseMenu::draw(UiLayer& ui, int width, int height) const {
    DebugDraw& d = ui.shapes;
    const uint32_t cyan = rgba(40, 220, 255), text = rgba(240, 245, 255), dim = rgba(150, 165, 210);
    fillRect(d, 0, 0, float(width), float(height), rgba(8, 10, 30, 150));  // velo sobre la escena
    Rect p = panelRect(width, height);
    fillRect(d, p.x0, p.y0, p.x1, p.y1, rgba(14, 18, 46, 238));
    fillRect(d, p.x0, p.y0, p.x1, p.y0 + 4, cyan);
    strokeRect(d, p.x0, p.y0, p.x1, p.y1, rgba(70, 90, 160));
    TextStyle title;
    title.face = FontFace::Title;
    title.align = Align::Center;
    title.glow = 0.6f;
    drawText(ui, (p.x0 + p.x1) * 0.5f, p.y0 + 30, 40, "PAUSA", text, title);

    char sens[48];
    std::snprintf(sens, sizeof(sens), "Sensibilidad   ‹ %.2f ›", double(sensitivity));
    const char* qnames[3] = {"baja", "media", "alta"};
    char qual[48];
    std::snprintf(qual, sizeof(qual), "Calidad gráfica   ‹ %s ›", qnames[quality]);
    const char* labels[ItemCount] = {"Continuar", "Reiniciar nivel", "Prueba: Grises sin fin", "Prueba: viaje por la luz", sens, qual, "Salir"};
    for (int i = 0; i < ItemCount; ++i) {
        Rect r = itemRect(i, width, height);
        bool sel = i == selected_;
        if (sel) {
            fillRect(d, r.x0, r.y0, r.x1, r.y1, rgba(40, 220, 255, 40));
            fillRect(d, r.x0, r.y0, r.x0 + 5, r.y1, cyan);
        }
        TextStyle st;
        st.align = Align::Center;
        drawText(ui, (r.x0 + r.x1) * 0.5f, r.y0 + 8, 24, labels[i], sel ? cyan : text, st);
    }
    TextStyle hint;
    hint.align = Align::Center;
    drawText(ui, (p.x0 + p.x1) * 0.5f, p.y1 - 30, 15, "W/S elegir · Enter aceptar · A/D ajustar · Esc volver", dim, hint);
}
