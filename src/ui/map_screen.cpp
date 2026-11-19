#include "ui/map_screen.h"

#include "game/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr float kLegendW = 230;  // la columna de la derecha
const uint32_t kText = rgba(236, 234, 228), kDim = rgba(150, 148, 150), kAmber = rgba(232, 196, 140);
const uint32_t kInk = rgba(14, 12, 16, 230), kFrame = rgba(222, 214, 198), kObjective = rgba(240, 120, 110);
const uint32_t kDoor = rgba(240, 206, 120), kCable = rgba(150, 200, 235), kChest = rgba(236, 180, 70);
const uint32_t kLanternOn = rgba(255, 200, 110), kPortal = rgba(178, 120, 255), kInn = rgba(120, 220, 150);
const uint32_t kShop = rgba(240, 214, 90), kFolk = rgba(200, 214, 236);

void fillRect(DebugDraw& d, float x0, float y0, float x1, float y1, uint32_t c) {
    d.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
void strokeRect(DebugDraw& d, float x0, float y0, float x1, float y1, uint32_t c) {
    d.line({x0, y0, 0}, {x1, y0, 0}, c);
    d.line({x1, y0, 0}, {x1, y1, 0}, c);
    d.line({x1, y1, 0}, {x0, y1, 0}, c);
    d.line({x0, y1, 0}, {x0, y0, 0}, c);
}
void disc(DebugDraw& d, float cx, float cy, float r, uint32_t c, int n = 18) {
    for (int i = 0; i < n; ++i) {
        float a0 = 2.0f * kPi * float(i) / float(n), a1 = 2.0f * kPi * float(i + 1) / float(n);
        d.triangle({cx, cy, 0}, {cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0}, {cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0}, c);
    }
}
void ring(DebugDraw& d, float cx, float cy, float r, float thick, uint32_t c, int n = 28) {
    for (int i = 0; i < n; ++i) {
        float a0 = 2.0f * kPi * float(i) / float(n), a1 = 2.0f * kPi * float(i + 1) / float(n);
        Vec3 o0{cx + std::cos(a0) * (r + thick * 0.5f), cy + std::sin(a0) * (r + thick * 0.5f), 0};
        Vec3 o1{cx + std::cos(a1) * (r + thick * 0.5f), cy + std::sin(a1) * (r + thick * 0.5f), 0};
        Vec3 i0{cx + std::cos(a0) * (r - thick * 0.5f), cy + std::sin(a0) * (r - thick * 0.5f), 0};
        Vec3 i1{cx + std::cos(a1) * (r - thick * 0.5f), cy + std::sin(a1) * (r - thick * 0.5f), 0};
        d.quad(i0, o0, o1, i1, c);
    }
}
void diamond(DebugDraw& d, float cx, float cy, float r, uint32_t c) {
    d.quad({cx, cy - r, 0}, {cx + r, cy, 0}, {cx, cy + r, 0}, {cx - r, cy, 0}, c);
}
// Arco de puerta: la entrada a otra zona.
void arch(DebugDraw& d, float cx, float cy, float r, uint32_t c) {
    fillRect(d, cx - r - 1.5f, cy - r * 0.2f - 1.5f, cx + r + 1.5f, cy + r + 1.5f, kInk);
    disc(d, cx, cy - r * 0.2f, r + 1.5f, kInk, 14);
    fillRect(d, cx - r, cy - r * 0.2f, cx + r, cy + r, c);
    disc(d, cx, cy - r * 0.2f, r, c, 14);
    fillRect(d, cx - r * 0.45f, cy, cx + r * 0.45f, cy + r, kInk);
    disc(d, cx, cy, r * 0.45f, kInk, 10);
}
// Casita: descanso.
void house(DebugDraw& d, float cx, float cy, float r, uint32_t c) {
    d.triangle({cx, cy - r * 1.2f, 0}, {cx + r * 1.15f, cy - r * 0.1f, 0}, {cx - r * 1.15f, cy - r * 0.1f, 0}, kInk);
    fillRect(d, cx - r * 0.85f, cy - r * 0.2f, cx + r * 0.85f, cy + r * 0.95f, kInk);
    d.triangle({cx, cy - r * 0.9f, 0}, {cx + r * 0.85f, cy - r * 0.15f, 0}, {cx - r * 0.85f, cy - r * 0.15f, 0}, c);
    fillRect(d, cx - r * 0.6f, cy - r * 0.15f, cx + r * 0.6f, cy + r * 0.75f, c);
}
// Flecha de la protagonista: punta hacia `dir` (en la pantalla).
void arrow(DebugDraw& d, float cx, float cy, float dx, float dy, float r, uint32_t fill, uint32_t edge) {
    float px = -dy, py = dx;
    auto tri = [&](float s, uint32_t c) {
        Vec3 tip{cx + dx * r * 1.25f * s, cy + dy * r * 1.25f * s, 0};
        Vec3 l{cx - dx * r * 0.8f * s + px * r * 0.85f * s, cy - dy * r * 0.8f * s + py * r * 0.85f * s, 0};
        Vec3 rr{cx - dx * r * 0.8f * s - px * r * 0.85f * s, cy - dy * r * 0.8f * s - py * r * 0.85f * s, 0};
        Vec3 notch{cx - dx * r * 0.35f * s, cy - dy * r * 0.35f * s, 0};
        d.triangle(tip, l, notch, c);
        d.triangle(tip, notch, rr, c);
    };
    tri(1.35f, edge);
    tri(1.0f, fill);
}

// Etiquetas del mapa: si una pisaría a otra ya puesta, baja una línea (hasta tres veces).
struct Placed {
    float x0, y0, x1, y1;
};
std::vector<Placed> gPlaced;

void placeLabel(UiLayer& ui, float x, float y, float size, const char* s, uint32_t c) {
    float w = textWidth(s, size);
    for (int tries = 0; tries < 4; ++tries) {
        bool clash = false;
        for (const Placed& p : gPlaced)
            if (x < p.x1 && x + w > p.x0 && y < p.y1 && y + size > p.y0) clash = true;
        if (!clash) break;
        y += size + 2;
    }
    gPlaced.push_back({x - 2, y - 1, x + w + 2, y + size + 1});
    TextStyle st;
    st.outline = rgba(8, 8, 12, 230);
    drawText(ui, x, y, size, s, c, st);
}

void label(UiLayer& ui, float x, float y, float size, const char* s, uint32_t c, Align a = Align::Left) {
    TextStyle st;
    st.align = a;
    st.outline = rgba(8, 8, 12, 230);
    drawText(ui, x, y, size, s, c, st);
}

// «Entrar · Gruta de las Peñas» → «Gruta de las Peñas».
std::string placeName(const std::string& doorLabel) {
    const char* dot = std::strstr(doorLabel.c_str(), "· ");
    std::string s = dot ? std::string(dot + std::strlen("· ")) : doorLabel;
    if (s.rfind("al ", 0) == 0) s = s.substr(3);
    else if (s.rfind("a la ", 0) == 0) s = s.substr(5);
    else if (s.rfind("por la ", 0) == 0) s = s.substr(7);
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

// Un número redondo de metros para la barra de escala (1, 2, 5, 10, 20, 50...).
float niceMeters(float target) {
    float p = std::pow(10.0f, std::floor(std::log10(std::max(target, 0.01f))));
    for (float m : {1.0f, 2.0f, 5.0f, 10.0f})
        if (m * p >= target) return m * p;
    return 10 * p;
}

}  // namespace

MapScreen::Rect MapScreen::viewport(int width, int height) {
    return {28, 78, float(width) - kLegendW - 40, float(height) - 44};
}

void MapScreen::show(const Game& game, int width, int height) {
    open = true;
    dragging_ = false;
    if (map_.empty() || map_.serial != game.levelSerial()) {
        game.buildLevelMap(map_);
        quad_.upload(map_.w, map_.h, map_.pixels.data());
    }
    fit(width, height);
}

void MapScreen::fit(int width, int height) {
    zoom_ = minZoom(width, height) * 1.0f / 0.92f;
    cu_ = float(map_.w) * 0.5f;
    cv_ = float(map_.h) * 0.5f;
    clampView(width, height);
}

float MapScreen::minZoom(int width, int height) const {
    Rect r = viewport(width, height);
    if (map_.w <= 0 || map_.h <= 0) return 1;
    return std::min((r.x1 - r.x0) / float(map_.w), (r.y1 - r.y0) / float(map_.h)) * 0.92f;
}

float MapScreen::maxZoom() const { return 14.0f * map_.cell; }  // 14 px por metro

void MapScreen::clampView(int width, int height) {
    zoom_ = clampf(zoom_, minZoom(width, height), std::max(maxZoom(), minZoom(width, height)));
    // Que siempre quede algo del mapa en la vista: el centro, dentro de la imagen.
    cu_ = clampf(cu_, 0, float(map_.w));
    cv_ = clampf(cv_, 0, float(map_.h));
}

void MapScreen::zoomAt(float factor, float px, float py, int width, int height) {
    Rect r = viewport(width, height);
    float mx = (r.x0 + r.x1) * 0.5f, my = (r.y0 + r.y1) * 0.5f;
    // El punto del mapa bajo el cursor se queda bajo el cursor.
    float u = cu_ + (px - mx) / zoom_, v = cv_ + (py - my) / zoom_;
    zoom_ *= factor;
    zoom_ = clampf(zoom_, minZoom(width, height), std::max(maxZoom(), minZoom(width, height)));
    cu_ = u - (px - mx) / zoom_;
    cv_ = v - (py - my) / zoom_;
    clampView(width, height);
}

float MapScreen::sx(Vec3 p, int width, int height) const {
    Rect r = viewport(width, height);
    return (r.x0 + r.x1) * 0.5f + (map_.u(p) - cu_) * zoom_;
}
float MapScreen::sy(Vec3 p, int width, int height) const {
    Rect r = viewport(width, height);
    return (r.y0 + r.y1) * 0.5f + (map_.v(p) - cv_) * zoom_;
}

bool MapScreen::update(const Input& in, const Game& game, int width, int height, float dt) {
    time_ += dt;
    if (in.wasPressed(Key::M) || in.wasPressed(Key::Escape)) return true;
    if (map_.serial != game.levelSerial()) show(game, width, height);  // ha cambiado de zona (no debería, en pausa)
    Rect r = viewport(width, height);
    bool inside = in.mouseX >= r.x0 && in.mouseX <= r.x1 && in.mouseY >= r.y0 && in.mouseY <= r.y1;
    if (in.wheel != 0 && inside) zoomAt(std::pow(1.25f, in.wheel), in.mouseX, in.mouseY, width, height);
    float mx = (r.x0 + r.x1) * 0.5f, my = (r.y0 + r.y1) * 0.5f;
    if (in.wasPressed(Key::E)) zoomAt(1.5f, mx, my, width, height);
    if (in.wasPressed(Key::Q)) zoomAt(1.0f / 1.5f, mx, my, width, height);
    if (in.mouseLeftPressed && inside) dragging_ = true;
    if (!in.mouseLeft) dragging_ = false;
    if (dragging_) {
        cu_ -= in.mouseDX / zoom_;
        cv_ -= in.mouseDY / zoom_;
    }
    float pan = 520.0f * dt / zoom_;  // px de la pantalla por segundo
    cu_ += (float(in.isDown(Key::D) || in.isDown(Key::Right)) - float(in.isDown(Key::A) || in.isDown(Key::Left))) * pan;
    cv_ += (float(in.isDown(Key::S) || in.isDown(Key::Down)) - float(in.isDown(Key::W) || in.isDown(Key::Up))) * pan;
    if (in.wasPressed(Key::Space)) {  // centrar en la protagonista (y acercar si está muy lejos)
        Vec3 p = game.hero().body.pos;
        cu_ = map_.u(p);
        cv_ = map_.v(p);
        zoom_ = std::max(zoom_, std::min(maxZoom(), 3.0f * map_.cell));
    }
    if (in.wasPressed(Key::F)) fit(width, height);
    clampView(width, height);
    return false;
}

void MapScreen::drawBack(UiLayer& ui, int width, int height) const {
    DebugDraw& d = ui.shapes;
    fillRect(d, 0, 0, float(width), float(height), rgba(6, 6, 9, 238));
    Rect r = viewport(width, height);
    fillRect(d, r.x0, r.y0, r.x1, r.y1, rgba(20, 19, 22, 255));
    // Una trama tenue en el vacío (donde no hay suelo), para que se lea como «nada».
    for (float x = r.x0 - std::fmod(r.x0, 24.0f); x < r.x1; x += 24)
        d.line({std::max(x, r.x0), r.y0, 0}, {std::max(x, r.x0), r.y1, 0}, rgba(255, 255, 255, 8));
    for (float y = r.y0 - std::fmod(r.y0, 24.0f) + 24; y < r.y1; y += 24) d.line({r.x0, y, 0}, {r.x1, y, 0}, rgba(255, 255, 255, 8));
}

void MapScreen::drawImage(const Mat4& pixels, float scale, int windowHeight, int width, int height) const {
    Rect r = viewport(width, height);
    float mx = (r.x0 + r.x1) * 0.5f, my = (r.y0 + r.y1) * 0.5f;
    float x0 = mx - cu_ * zoom_, y0 = my - cv_ * zoom_;
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(r.x0 * scale), windowHeight - int(r.y1 * scale), int((r.x1 - r.x0) * scale), int((r.y1 - r.y0) * scale));
    quad_.draw(pixels, x0, y0, x0 + float(map_.w) * zoom_, y0 + float(map_.h) * zoom_, 1.0f);
    glDisable(GL_SCISSOR_TEST);
}

void MapScreen::drawFront(UiLayer& ui, const Game& game, float camYaw, int width, int height) const {
    DebugDraw& d = ui.shapes;
    Rect r = viewport(width, height);
    char buf[160];
    auto in = [&](float x, float y, float m = 0) { return x >= r.x0 + m && x <= r.x1 - m && y >= r.y0 + m && y <= r.y1 - m; };
    auto X = [&](Vec3 p) { return sx(p, width, height); };
    auto Y = [&](Vec3 p) { return sy(p, width, height); };
    const float metersPx = zoom_ / map_.cell;  // píxeles de la interfaz por metro
    gPlaced.clear();

    // Las farolas, en puntos pequeños (de cerca): por donde se viaja por la luz y dónde hay sombra.
    if (metersPx > 1.2f)
        for (const Lamp& l : game.lamps()) {
            float x = X(l.pos), y = Y(l.pos);
            if (!in(x, y)) continue;
            disc(d, x, y, 2.6f, kInk, 8);
            disc(d, x, y, 1.7f, l.on && !l.broken ? rgba(255, 226, 150) : rgba(110, 108, 112), 8);
        }
    for (const Portal& p : game.portals()) {
        float x = X(p.pos), y = Y(p.pos);
        if (!in(x, y)) continue;
        ring(d, x, y, 6.5f, 4.5f, kInk);
        ring(d, x, y, 6.5f, 2.4f, kPortal);
    }
    for (const Interactable& it : game.interactables()) {
        if (it.used) continue;
        float x = X(it.pos), y = Y(it.pos);
        if (!in(x, y)) continue;
        disc(d, x, y, 5, kInk, 12);
        ring(d, x, y, 3.2f, 1.6f, kText, 12);
    }
    for (const TreasureChest& c : game.chests()) {
        if (c.open) continue;
        float x = X(c.pos), y = Y(c.pos);
        if (!in(x, y)) continue;
        fillRect(d, x - 5.5f, y - 4.5f, x + 5.5f, y + 4.5f, kInk);
        fillRect(d, x - 4, y - 3, x + 4, y + 3, kChest);
        fillRect(d, x - 4, y - 0.6f, x + 4, y + 0.4f, kInk);
    }
    for (const ColorNode& c : game.colorNodes()) {
        float x = X(c.pos), y = Y(c.pos);
        if (!in(x, y)) continue;
        diamond(d, x, y, 9, kInk);
        diamond(d, x, y, 6.5f, c.lit ? (c.color | 0xFF000000u) : rgba(70, 68, 74));
        if (!c.lit) diamond(d, x, y, 3, c.color | 0xFF000000u);
    }
    for (const Lantern& l : game.lanterns()) {
        float x = X(l.pos), y = Y(l.pos);
        if (!in(x, y)) continue;
        disc(d, x, y, 6, kInk, 12);
        disc(d, x, y, 4.2f, l.lit ? kLanternOn : rgba(90, 84, 78), 12);
        if (l.lit) disc(d, x, y - 0.8f, 1.8f, rgba(255, 250, 220), 8);
    }
    // Las puertas a otras zonas, con adónde llevan.
    for (const ZoneDoor& z : game.zoneDoors()) {
        float x = X(z.pos), y = Y(z.pos);
        if (!in(x, y)) continue;
        arch(d, x, y, 6.5f, kDoor);
        placeLabel(ui, x + 11, y - 9, 14, placeName(z.label).c_str(), kDoor);
    }

    // Gente: quien da descanso, quien vende y, de cerca, los vecinos con nombre.
    for (const Npc& n : game.npcs()) {
        float x = X(n.body.body.pos), y = Y(n.body.body.pos);
        if (!in(x, y)) continue;
        bool inn = n.role == NpcRole::Inn, shop = n.role == NpcRole::Shop || !n.stock.empty();
        if (inn) house(d, x, y, 6.5f, kInn);
        else if (shop) {
            disc(d, x, y, 6.5f, kInk, 14);
            disc(d, x, y, 4.8f, kShop, 14);
            fillRect(d, x - 1, y - 3, x + 1, y + 3, kInk);
        } else if (!n.name.empty() && metersPx > 1.5f) {
            disc(d, x, y, 4, kInk, 10);
            disc(d, x, y, 2.6f, kFolk, 10);
        } else
            continue;
        if (!n.name.empty() && (inn || shop || metersPx > 3.0f)) placeLabel(ui, x + 9, y - 9, 13, n.name.c_str(), inn ? kInn : shop ? kShop : kFolk);
    }
    // El teleférico: los andenes y el cable.
    for (const CableLine& c : game.cableLines()) {
        float ax = X(c.stopA), ay = Y(c.stopA), bx = X(c.stopB), by = Y(c.stopB);
        // El cable, recortado al marco.
        float x0 = X(c.a), y0 = Y(c.a), x1 = X(c.b), y1 = Y(c.b), t0 = 0, t1 = 1;
        float dx = x1 - x0, dy = y1 - y0;
        for (auto [p, q] : {std::pair{-dx, x0 - r.x0}, std::pair{dx, r.x1 - x0}, std::pair{-dy, y0 - r.y0}, std::pair{dy, r.y1 - y0}}) {
            if (std::fabs(p) < 1e-6f) {
                if (q < 0) t0 = 2;
            } else if (p < 0) t0 = std::max(t0, q / p);
            else t1 = std::min(t1, q / p);
        }
        if (t0 < t1) d.line({x0 + dx * t0, y0 + dy * t0, 0}, {x0 + dx * t1, y0 + dy * t1, 0}, withAlpha(kCable, 0.7f));
        for (auto [x, y, name] : {std::tuple{ax, ay, &c.nameA}, std::tuple{bx, by, &c.nameB}}) {
            if (!in(x, y)) continue;
            fillRect(d, x - 6, y - 6, x + 6, y + 6, kInk);
            fillRect(d, x - 4.5f, y - 4.5f, x + 4.5f, y + 4.5f, kCable);
            fillRect(d, x - 4.5f, y - 1, x + 4.5f, y + 1, kInk);
            if (!name->empty()) placeLabel(ui, x + 10, y - 9, 13, name->c_str(), kCable);
        }
    }
    // El objetivo: un anillo que late (en el borde, si queda fuera de la vista).
    Vec3 goal;
    const LevelPart* part = game.currentPart();
    if (game.objective(goal)) {
        float x = X(goal), y = Y(goal);
        float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
        if (in(x, y, 10)) {
            ring(d, x, y, 9 + 5 * pulse, 2.2f, withAlpha(kObjective, 0.35f + 0.4f * (1 - pulse)));
            ring(d, x, y, 7, 3.6f, kInk);
            ring(d, x, y, 7, 2.2f, kObjective);
            disc(d, x, y, 2.2f, kObjective, 10);
        } else {
            float mx = (r.x0 + r.x1) * 0.5f, my = (r.y0 + r.y1) * 0.5f, dx = x - mx, dy = y - my;
            float k = std::min((r.x1 - r.x0) * 0.5f - 16, (r.y1 - r.y0) * 0.5f - 16) / std::max(std::fabs(dx), std::fabs(dy));
            float len = std::sqrt(dx * dx + dy * dy);
            arrow(d, mx + dx * k, my + dy * k, dx / len, dy / len, 7, kObjective, kInk);
        }
    }

    // La protagonista: hacia dónde mira la cámara (un abanico tenue) y hacia dónde mira ella.
    {
        Vec3 p = game.hero().body.pos;
        float x = clampf(X(p), r.x0 + 12, r.x1 - 12), y = clampf(Y(p), r.y0 + 12, r.y1 - 12);
        // En la imagen, +u es −x y +v es −z: una dirección del mundo (sx, sz) va a (−sx, −sz).
        auto dir = [](float yaw, float& dx, float& dy) { dx = -std::sin(yaw), dy = -std::cos(yaw); };
        float cx, cy;
        dir(camYaw, cx, cy);
        const int kFan = 10;
        for (int i = 0; i < kFan; ++i) {
            float a0 = camYaw - 0.5f + float(i) / kFan, a1 = camYaw - 0.5f + float(i + 1) / kFan;
            float x0, y0, x1, y1;
            dir(a0, x0, y0);
            dir(a1, x1, y1);
            d.triangle({x, y, 0}, {x + x0 * 46, y + y0 * 46, 0}, {x + x1 * 46, y + y1 * 46, 0}, rgba(255, 246, 220, 40));
        }
        float hx, hy;
        dir(game.hero().yaw, hx, hy);
        float pulse = 0.5f + 0.5f * std::sin(time_ * 3.0f);
        ring(d, x, y, 11 + 3 * pulse, 1.6f, rgba(255, 255, 255, uint8_t(60 + 60 * (1 - pulse))));
        arrow(d, x, y, hx, hy, 7.5f, rgba(255, 252, 240), kInk);
    }

    // El marco, la rosa (solo el norte: arriba es +z) y la escala.
    strokeRect(d, r.x0, r.y0, r.x1, r.y1, withAlpha(kFrame, 0.6f));
    {
        float x = r.x1 - 26, y = r.y0 + 34;
        disc(d, x, y, 15, rgba(10, 10, 14, 190), 20);
        d.triangle({x, y - 12, 0}, {x + 5, y + 2, 0}, {x - 5, y + 2, 0}, kFrame);
        d.triangle({x, y + 10, 0}, {x + 5, y + 2, 0}, {x - 5, y + 2, 0}, rgba(110, 106, 104));
        TextStyle st;
        st.face = FontFace::Title;
        st.align = Align::Center;
        st.outline = rgba(8, 8, 12, 230);
        drawText(ui, x, y - 32, 13, "N", kFrame, st);
    }
    {
        float m = niceMeters(110.0f / metersPx), len = m * metersPx;
        float x = r.x0 + 16, y = r.y1 - 18;
        fillRect(d, x - 6, y - 20, x + len + 6, y + 8, rgba(10, 10, 14, 170));
        fillRect(d, x, y - 1, x + len, y + 1, kFrame);
        fillRect(d, x, y - 5, x + 1.5f, y + 3, kFrame);
        fillRect(d, x + len - 1.5f, y - 5, x + len, y + 3, kFrame);
        std::snprintf(buf, sizeof(buf), "%g m", double(m));
        label(ui, x + len * 0.5f, y - 19, 12, buf, kFrame, Align::Center);
    }

    // Arriba: el nivel y lo que toca hacer.
    TextStyle title;
    title.face = FontFace::Title;
    title.glow = 0.3f;
    drawText(ui, r.x0, 18, 24, game.levelName().c_str(), kText, title);
    if (part) {
        if (part->title == game.levelName()) std::snprintf(buf, sizeof(buf), "%s", part->objective.c_str());
        else std::snprintf(buf, sizeof(buf), "%s  ·  %s", part->title.c_str(), part->objective.c_str());
        label(ui, r.x0, 50, 16, buf, kObjective);
    }
    fillRect(d, r.x0, r.y0 - 6, r.x1, r.y0 - 5, withAlpha(kFrame, 0.5f));

    // A la derecha: la leyenda y lo que queda por encontrar.
    float lx = r.x1 + 22, ly = r.y0 + 4;
    TextStyle head;
    head.face = FontFace::Title;
    drawText(ui, lx, ly, 14, "LEYENDA", kDim, head);
    ly += 30;
    auto row = [&](const char* text, auto icon) {
        icon(lx + 9, ly + 9);
        label(ui, lx + 26, ly, 15, text, kText);
        ly += 26;
    };
    row("Tú", [&](float x, float y) { arrow(d, x, y, 0, -1, 6, rgba(255, 252, 240), kInk); });
    row("Objetivo", [&](float x, float y) {
        ring(d, x, y, 6, 3.4f, kInk);
        ring(d, x, y, 6, 2, kObjective);
    });
    row("Paso a otra zona", [&](float x, float y) { arch(d, x, y, 6, kDoor); });
    if (!game.cableLines().empty())
        row("Teleférico", [&](float x, float y) {
            fillRect(d, x - 5, y - 5, x + 5, y + 5, kCable);
            fillRect(d, x - 5, y - 1, x + 5, y + 1, kInk);
        });
    row("Descanso", [&](float x, float y) { house(d, x, y, 6, kInn); });
    row("Tienda", [&](float x, float y) {
        disc(d, x, y, 5, kShop, 14);
        fillRect(d, x - 1, y - 3, x + 1, y + 3, kInk);
    });
    row("Cápsula", [&](float x, float y) {
        fillRect(d, x - 4.5f, y - 3.5f, x + 4.5f, y + 3.5f, kChest);
        fillRect(d, x - 4.5f, y - 0.6f, x + 4.5f, y + 0.4f, kInk);
    });
    row("Nodo de color", [&](float x, float y) { diamond(d, x, y, 6.5f, rgba(120, 200, 255)); });
    row("Farol (descansar)", [&](float x, float y) { disc(d, x, y, 4.5f, kLanternOn, 12); });
    if (!game.portals().empty())
        row("Portal luz / sombra", [&](float x, float y) { ring(d, x, y, 5, 2.2f, kPortal); });
    row("Farola", [&](float x, float y) { disc(d, x, y, 2.4f, rgba(255, 226, 150), 8); });

    // Lo que queda.
    ly += 12;
    drawText(ui, lx, ly, 14, "EN ESTA ZONA", kDim, head);
    ly += 28;
    auto count = [&](const char* what, int have, int total) {
        if (total == 0) return;
        std::snprintf(buf, sizeof(buf), "%s", what);
        label(ui, lx, ly, 15, buf, kText);
        std::snprintf(buf, sizeof(buf), "%d / %d", have, total);
        label(ui, float(width) - 30, ly, 15, buf, have == total ? kInn : kAmber, Align::Right);
        ly += 24;
    };
    int chestsOpen = 0, nodesLit = 0, lanternsLit = 0;
    for (const TreasureChest& c : game.chests()) chestsOpen += c.open;
    for (const ColorNode& c : game.colorNodes()) nodesLit += c.lit;
    for (const Lantern& l : game.lanterns()) lanternsLit += l.lit;
    count("Cápsulas abiertas", chestsOpen, int(game.chests().size()));
    count("Nodos encendidos", nodesLit, int(game.colorNodes().size()));
    count("Faroles encendidos", lanternsLit, int(game.lanterns().size()));
    if (game.partCount() > 0) count("Partes", std::min(game.partIndex(), game.partCount()), game.partCount());

    label(ui, r.x0, float(height) - 32, 14,
          "Rueda o Q/E · acercar      Arrastrar o WASD · mover      Espacio · centrar en ti      F · todo el mapa      M o Esc · cerrar",
          kDim);
}
