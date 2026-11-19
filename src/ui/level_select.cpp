#include "ui/level_select.h"

namespace {

constexpr float kPanelW = 760, kItemH = 42, kItemGap = 3, kTop = 120, kBottomMargin = 70;

const LevelSelect::Entry kEntries[] = {
    {"Patio de combate", "Grises sin fin de las dos familias, farolas, portales y oscuridad", true, LevelId::Prueba},
    {"1 · Plaza del Reloj", "Combates de zona, nodos de color y faroles", false, LevelId::Plaza},
    {"2 · Mercado Quemado", "Calles estrechas, rompecabezas de luz con espejos", false, LevelId::Mercado},
    {"3 · Puente de los Faroles", "Faroles que encender y el Gris mayor al final", false, LevelId::Puente},
    {"4 · Selva de los Puentes", "De noche: puentes colgantes entre árboles, antorchas", false, LevelId::Selva},
    {"5 · Cueva de Cristal", "Salas de roca alumbradas por cristales y un río subterráneo", false, LevelId::Cueva},
    {"6 · Callejones del Farol", "Calles estrechas con farolillos de papel y letreros de neón", false, LevelId::Callejones},
    {"Mapa · Campo abierto", "Valle de día: colinas, río, granja, molinos y un pinar", false, LevelId::Campo},
    {"Mapa · Ciudad Neón", "De noche: torres, letreros, vía elevada y un santuario", false, LevelId::Neon},
    {"Mapa · Pueblo cafetero", "Plaza, casas de bahareque de colores y cafetales en la loma", false, LevelId::Cafetero},
    {"Mapa · Ciudad de la Bahía", "Al estilo de Río: playas salvajes, el centro colonial, el morro y el teleférico", false, LevelId::Rio},
    {"Mapa · Las Copas", "Una aldea en lo alto de árboles gigantes: pasarelas, la posada y vértigo", false, LevelId::Copas},
    {"Mapa · Cumbres Nevadas", "Un valle alto entre picos: el refugio, el lago helado, el barranco y la cruz de la cima", false, LevelId::Cumbres},
    {"Puzles · Los Andamios", "Saltos sobre el vacío: escaleras de gato, paredes a las que agarrarse y saltos de pared", false, LevelId::Andamios},
};
constexpr int kCount = int(sizeof(kEntries) / sizeof(kEntries[0]));

void fillRect(DebugDraw& d, float x0, float y0, float x1, float y1, uint32_t c) {
    d.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}

}  // namespace

const LevelSelect::Entry* LevelSelect::entries(int& count) {
    count = kCount;
    return kEntries;
}

LevelSelect::Rect LevelSelect::itemRect(int item, int width, int /*height*/) const {
    float x = (float(width) - kPanelW) * 0.5f, y = kTop + float(item) * (kItemH + kItemGap) - scroll_;
    return {x, y, x + kPanelW, y + kItemH};
}

float LevelSelect::listBottom(int height) const { return float(height) - kBottomMargin; }

float LevelSelect::maxScroll(int height) const {
    float total = float(kCount) * (kItemH + kItemGap) - kItemGap;
    float view = listBottom(height) - kTop;
    return total > view ? total - view : 0;
}

void LevelSelect::keepVisible(int height) {
    float y0 = float(selected_) * (kItemH + kItemGap), y1 = y0 + kItemH;
    float view = listBottom(height) - kTop;
    if (y0 < scroll_) scroll_ = y0;
    if (y1 > scroll_ + view) scroll_ = y1 - view;
}

int LevelSelect::update(const Input& in, int width, int height) {
    bool moved = false;
    if (in.wasPressed(Key::W) || in.wasPressed(Key::Up)) selected_ = (selected_ + kCount - 1) % kCount, moved = true;
    if (in.wasPressed(Key::S) || in.wasPressed(Key::Down)) selected_ = (selected_ + 1) % kCount, moved = true;
    if (moved) keepVisible(height);
    if (in.wheel != 0) scroll_ -= in.wheel * (kItemH + kItemGap) * 2;
    scroll_ = scroll_ < 0 ? 0 : (scroll_ > maxScroll(height) ? maxScroll(height) : scroll_);
    bool clicked = false;
    for (int i = 0; i < kCount; ++i) {
        Rect r = itemRect(i, width, height);
        if (r.y0 < kTop || r.y1 > listBottom(height)) continue;  // fuera de la vista
        if (!r.contains(in.mouseX, in.mouseY)) continue;
        if (in.mouseDX != 0 || in.mouseDY != 0 || in.mouseLeftPressed) selected_ = i;
        if (in.mouseLeftPressed) clicked = true;
    }
    if (in.wasPressed(Key::Escape)) {
        open = false;
        return -2;
    }
    if (clicked || in.wasPressed(Key::Enter) || in.wasPressed(Key::Space)) {
        open = false;
        return selected_;
    }
    return -1;
}

void LevelSelect::draw(UiLayer& ui, int width, int height) const {
    DebugDraw& d = ui.shapes;
    const uint32_t ink = rgba(236, 234, 228), dim = rgba(150, 150, 156), accent = rgba(222, 214, 198);
    fillRect(d, 0, 0, float(width), float(height), rgba(4, 4, 6, 200));
    TextStyle title;
    title.face = FontFace::Title;
    title.align = Align::Center;
    title.glow = 0.6f;
    drawText(ui, float(width) * 0.5f, 52, 44, "LA HORA DETENIDA", ink, title);
    TextStyle sub;
    sub.align = Align::Center;
    drawText(ui, float(width) * 0.5f, 104, 20, "Elige un nivel", dim, sub);
    for (int i = 0; i < kCount; ++i) {
        Rect r = itemRect(i, width, height);
        if (r.y0 < kTop || r.y1 > listBottom(height)) continue;  // fuera de la vista
        bool sel = i == selected_;
        fillRect(d, r.x0, r.y0, r.x1, r.y1, sel ? rgba(222, 214, 198, 36) : rgba(16, 16, 20, 220));
        if (sel) fillRect(d, r.x0, r.y0, r.x0 + 6, r.y1, accent);
        drawText(ui, r.x0 + 28, r.y0 + 3, 21, kEntries[i].name, sel ? accent : ink);
        drawText(ui, r.x0 + 28, r.y0 + 26, 15, kEntries[i].about, dim);
    }
    if (float ms = maxScroll(height); ms > 0) {
        // Barra de desplazamiento a la derecha de la lista.
        float x = (float(width) + kPanelW) * 0.5f + 10, top = kTop, bottom = listBottom(height);
        float view = bottom - top, total = view + ms;
        float t0 = top + view * (scroll_ / total), t1 = top + view * ((scroll_ + view) / total);
        fillRect(d, x, top, x + 4, bottom, rgba(40, 40, 46, 200));
        fillRect(d, x, t0, x + 4, t1, accent);
    }
    drawText(ui, float(width) * 0.5f, float(height) - 46, 15,
             "W/S elegir · rueda desplazar · Enter jugar · Esc salir · En el juego: Z luz/sombra, F1 vida y F2 Poder infinitos", dim, sub);
}
