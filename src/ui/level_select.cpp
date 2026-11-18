#include "ui/level_select.h"

namespace {

constexpr float kPanelW = 760, kItemH = 44, kItemGap = 4, kTop = 120;

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
    {"Mapa · Ciudad de la Bahía", "Diorama al estilo de Río: la caleta, la plaza de los arcos y el mirador", false, LevelId::Rio},
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

LevelSelect::Rect LevelSelect::itemRect(int item, int width, int /*height*/) {
    float x = (float(width) - kPanelW) * 0.5f, y = kTop + float(item) * (kItemH + kItemGap);
    return {x, y, x + kPanelW, y + kItemH};
}

int LevelSelect::update(const Input& in, int width, int height) {
    if (in.wasPressed(Key::W) || in.wasPressed(Key::Up)) selected_ = (selected_ + kCount - 1) % kCount;
    if (in.wasPressed(Key::S) || in.wasPressed(Key::Down)) selected_ = (selected_ + 1) % kCount;
    bool clicked = false;
    for (int i = 0; i < kCount; ++i) {
        if (!itemRect(i, width, height).contains(in.mouseX, in.mouseY)) continue;
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
        bool sel = i == selected_;
        fillRect(d, r.x0, r.y0, r.x1, r.y1, sel ? rgba(222, 214, 198, 36) : rgba(16, 16, 20, 220));
        if (sel) fillRect(d, r.x0, r.y0, r.x0 + 6, r.y1, accent);
        drawText(ui, r.x0 + 28, r.y0 + 3, 21, kEntries[i].name, sel ? accent : ink);
        drawText(ui, r.x0 + 28, r.y0 + 26, 15, kEntries[i].about, dim);
    }
    drawText(ui, float(width) * 0.5f, float(height) - 46, 15,
             "W/S elegir · Enter jugar · Esc salir · En el juego: Z luz/sombra, F1 vida y F2 Poder infinitos", dim, sub);
}
