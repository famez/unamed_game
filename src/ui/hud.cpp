// HUD y textos en pantalla: barras, retrato, nivel, comandos, objetivo, avisos y los
// textos de la historia y las cinemáticas. Todo en 2D sobre UiLayer.

#include "game/game.h"
#include "render/hero_model.h"

#include "render/text.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

const uint32_t kUiPanel = rgba(14, 14, 18, 200), kUiCyan = rgba(222, 214, 198), kUiPink = rgba(196, 92, 100);
const uint32_t kUiText = rgba(236, 234, 228), kUiDim = rgba(140, 140, 146), kUiAmber = rgba(232, 196, 140);
const uint32_t kUiShadow = rgba(6, 6, 8, 200);

void fillRect(DebugDraw& d, float x0, float y0, float x1, float y1, uint32_t c) {
    d.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
// Paralelogramo: la parte de arriba desplazada `skew` píxeles a la derecha.
void skewRect(DebugDraw& d, float x0, float y0, float x1, float y1, float skew, uint32_t c) {
    d.quad({x0 + skew, y0, 0}, {x1 + skew, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
void fillCircle(DebugDraw& d, float cx, float cy, float r, uint32_t c, int n = 28, float rot = 0) {
    for (int i = 0; i < n; ++i) {
        float a0 = rot + 2.0f * kPi * float(i) / float(n), a1 = rot + 2.0f * kPi * float(i + 1) / float(n);
        d.triangle({cx, cy, 0}, {cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0},
                   {cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0}, c);
    }
}
void diamond(DebugDraw& d, float cx, float cy, float r, uint32_t c) {
    d.quad({cx, cy - r, 0}, {cx + r, cy, 0}, {cx, cy + r, 0}, {cx - r, cy, 0}, c);
}
// Barra inclinada y segmentada (cada `segment` unidades una muesca), con lo recién
// perdido en otro color.
void segBar(DebugDraw& d, float xRight, float y, float length, float thick, float frac, float ghost, uint32_t fill,
            uint32_t ghostCol, int segments) {
    const float skew = thick * 0.6f, x0 = xRight - length;
    skewRect(d, x0 - 4, y - 3, xRight + 4, y + thick + 3, skew, kUiPanel);
    auto seg = [&](float a, float b, uint32_t c) {
        if (b <= a) return;
        skewRect(d, x0 + length * a, y, x0 + length * b, y + thick, skew, c);
    };
    seg(0, 1, rgba(40, 36, 70, 230));
    seg(frac, ghost, ghostCol);
    seg(0, frac, fill);
    if (frac > 0) skewRect(d, x0, y, x0 + length * frac, y + thick * 0.35f, skew * 0.35f, withAlpha(rgba(255, 255, 255), 0.3f));
    for (int i = 1; i < segments; ++i) {  // muescas
        float x = x0 + length * float(i) / float(segments);
        d.quad({x + skew - 1, y, 0}, {x + skew + 1, y, 0}, {x + 1, y + thick, 0}, {x - 1, y + thick, 0}, kUiPanel);
    }
}

void uiText(UiLayer& ui, float x, float y, float size, const char* s, uint32_t c, FontFace face = FontFace::Body,
            Align align = Align::Left, float glow = 0) {
    TextStyle st;
    st.face = face;
    st.align = align;
    st.glow = glow;
    st.outline = rgba(8, 10, 30, 190);
    drawText(ui, x, y, size, s, c, st);
}

bool project(const ViewInfo& v, Vec3 p, float& sx, float& sy) {
    const float* m = v.viewProj.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw < 0.1f) return false;
    sx = (cx / cw * 0.5f + 0.5f) * float(v.width);
    sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * float(v.height);
    return true;
}

}  // namespace

namespace {

// Parte un texto en líneas que caben en `maxW` píxeles (por palabras).
std::vector<std::string> wrapText(const std::string& text, float size, float maxW) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && textWidth(tryLine.c_str(), size) > maxW) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = tryLine;
        }
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else word += ch;
    }
    flushWord();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

uint32_t speakerColor(const std::string& id) {
    if (id == "ilse") return rgba(180, 140, 230);
    if (id == "voz") return rgba(236, 222, 196);
    return rgba(214, 210, 200);
}

}  // namespace

void Game::drawStoryHud(UiLayer& ui, const ViewInfo& view) const {
    DebugDraw& d = ui.shapes;
    const float w = float(view.width), h = float(view.height);
    if (fade_ > 0) fillRect(d, 0, 0, w, h, withAlpha(rgba(0, 0, 0), smoothstep01(fade_)));

    // Carteles de capítulo.
    if (chapterBanner_ > 0) {
        float total = chapterEnded_ ? 7.0f : 5.0f;
        float a = clampf(std::min((total - chapterBanner_) / 0.8f, chapterBanner_ / 1.0f), 0, 1);
        float y = h * 0.4f;
        fillRect(d, 0, y - 20, w, y + 110, withAlpha(rgba(4, 4, 6), 0.55f * a));
        uiText(ui, w * 0.5f, y, 22, chapterEnded_ ? "FIN DEL CAPÍTULO 1" : "CAPÍTULO 1", withAlpha(kUiCyan, a), FontFace::Title,
               Align::Center, 0.5f * a);
        uiText(ui, w * 0.5f, y + 34, 46, chapterEnded_ ? "Continuará…" : "La plaza parada", withAlpha(kUiText, a),
               FontFace::Body, Align::Center, 0.6f * a);
    }

    const StoryLine* l = dialogue_.line();
    if (!l) return;
    std::string shown = utf8Prefix(l->text, dialogue_.visibleChars());
    std::string name = speakerName(l->speaker);
    uint32_t col = speakerColor(l->speaker);

    if (dialogue_.memory()) {
        // Recuerdo: la imagen se vela en blanco y la voz aparece en el centro.
        fillRect(d, 0, 0, w, h, withAlpha(rgba(20, 18, 16), 0.35f));
        fillRect(d, 0, h * 0.38f, w, h * 0.62f, withAlpha(rgba(6, 6, 8), 0.4f));
        std::vector<std::string> lines = wrapText(shown, 30, w * 0.6f);
        float y = h * 0.5f - float(lines.size()) * 20.0f;
        if (!name.empty()) uiText(ui, w * 0.5f, y - 34, 16, name.c_str(), withAlpha(col, 0.9f), FontFace::Title, Align::Center);
        for (const std::string& line : lines) {
            uiText(ui, w * 0.5f, y, 30, line.c_str(), rgba(255, 255, 255), FontFace::Body, Align::Center, 0.6f);
            y += 40;
        }
        return;
    }
    if (!dialogue_.blocking()) {
        // Aviso: subtítulo arriba, sin detener el juego.
        std::vector<std::string> lines = wrapText(shown, 21, 640);
        float y = 128;
        float bw = 700;
        fillRect(d, (w - bw) * 0.5f, y - 8, (w + bw) * 0.5f, y + 30 + float(lines.size()) * 28.0f, withAlpha(rgba(6, 6, 8), 0.6f));
        if (!name.empty()) uiText(ui, (w - bw) * 0.5f + 16, y - 2, 14, name.c_str(), col, FontFace::Title);
        y += 22;
        for (const std::string& line : lines) {
            uiText(ui, (w - bw) * 0.5f + 16, y, 21, line.c_str(), kUiText);
            y += 28;
        }
        return;
    }
    // Diálogo: caja abajo con el nombre en una pestaña y el texto letra a letra.
    const float bw = 880, bx = (w - bw) * 0.5f, by = h - 200, bh = 158;
    skewRect(d, bx, by, bx + bw, by + bh, 12, withAlpha(rgba(12, 12, 14), 0.88f));
    fillRect(d, bx + 12, by, bx + bw + 12, by + 3, col);
    if (!name.empty()) {
        float nw = textWidth(name.c_str(), 18, FontFace::Title) + 40;
        skewRect(d, bx + 20, by - 30, bx + 20 + nw, by, 8, col);
        uiText(ui, bx + 40, by - 26, 18, name.c_str(), rgba(14, 14, 18), FontFace::Title);
    }
    std::vector<std::string> lines = wrapText(shown, 25, bw - 80);
    float y = by + 26;
    for (const std::string& line : lines) {
        uiText(ui, bx + 40, y, 25, line.c_str(), l->speaker == "sistema" ? kUiAmber : kUiText);
        y += 34;
    }
    if (dialogue_.lineComplete() && std::fmod(time_, 0.8f) < 0.5f) {
        float tx = bx + bw - 30, ty = by + bh - 30;
        d.triangle({tx - 9, ty - 6, 0}, {tx + 9, ty - 6, 0}, {tx, ty + 5, 0}, col);
    }
    uiText(ui, bx + bw - 6, by + bh + 10, 14, "Clic / E / Espacio · continuar", kUiDim, FontFace::Body, Align::Right);
}

// --- HUD -------------------------------------------------------------------------------

void Game::drawHud(UiLayer& ui, const ViewInfo& view, bool debug) const {
    DebugDraw& d = ui.shapes;
    const float w = float(view.width), h = float(view.height);
    char buf[128];
    // En las escenas que detienen el juego (y al despertar) solo se ve la historia.
    if (dialogueBlocking() || hero_.lying > 0.01f) {
        drawStoryHud(ui, view);
        drawCinemaHud(ui, view);
        return;
    }
    drawCinemaHud(ui, view);

    // Vitalidad abajo a la derecha: retrato hexagonal y barras de PV y PM inclinadas y
    // segmentadas (su longitud crece con el máximo).
    {
        float cx = w - kPortraitInset, cy = h - kPortraitInset;
        fillCircle(d, cx, cy, 58, kUiPanel, 6, kPi / 6);
        // Retrato sobrio: silueta (cabeza y hombros del abrigo) y la veta de luz del pecho.
        fillCircle(d, cx, cy, 52, hp_ / maxHp_ < 0.25f ? rgba(200, 60, 70) : rgba(150, 148, 144), 6, kPi / 6);
        fillCircle(d, cx, cy, 47, rgba(22, 22, 26), 6, kPi / 6);
        bool face = heroGlb(shadowMode_ && shadowUnlocked_ ? HeroLook::Shadow : HeroLook::Light) != nullptr;
        if (face) {  // fondo del retrato (la cara la pinta drawPortrait encima): luz cálida o violeta
            fillCircle(d, cx, cy, 47, shadowMode_ ? rgba(40, 24, 60) : rgba(70, 58, 44), 6, kPi / 6);
            fillCircle(d, cx, cy - 6, 30, shadowMode_ ? rgba(64, 38, 100) : rgba(120, 100, 70), 24);
        } else {  // sin modelo: silueta sobria (cabeza y hombros del abrigo) y la veta de luz del pecho
            d.triangle({cx - 34, cy + 40, 0}, {cx + 34, cy + 40, 0}, {cx, cy + 6, 0}, rgba(44, 45, 52));  // hombros
            fillRect(d, cx - 30, cy + 22, cx + 30, cy + 40, rgba(44, 45, 52));
            fillRect(d, cx - 12, cy + 14, cx + 12, cy + 22, rgba(104, 58, 50));  // bufanda
            fillCircle(d, cx, cy - 4, 17, rgba(150, 134, 122), 20);              // cara en penumbra
            fillCircle(d, cx, cy - 13, 16, rgba(64, 62, 66), 20);                // pelo
            fillRect(d, cx - 16, cy - 13, cx + 16, cy - 8, rgba(64, 62, 66));
            float litP = clampf(mp_ / maxMp_, 0, 1);
            fillRect(d, cx - 1.5f, cy + 24, cx + 1.5f, cy + 38, withAlpha(rgba(255, 222, 170), 0.25f + 0.75f * litP));
        }
        if (heroFlash_ > 0) fillCircle(d, cx, cy, 52, withAlpha(rgba(255, 60, 90), heroFlash_ * 2.5f), 6, kPi / 6);

        float right = cx - 66;
        float hpW = std::min(maxHp_, 160.0f) * 2.6f;  // la barra crece con el nivel hasta un tope
        uiText(ui, right - hpW - 6, cy - 64, 15, "PV", kUiCyan, FontFace::Title);
        std::snprintf(buf, sizeof(buf), "%d / %d", int(std::ceil(hp_)), int(maxHp_));
        uiText(ui, right, cy - 66, 18, buf, kUiText, FontFace::Title, Align::Right);
        segBar(d, right, cy - 40, hpW, 18, hp_ / maxHp_, hpGhost_ / maxHp_, rgba(214, 206, 192),
               rgba(255, 60, 120), int(maxHp_ / 10));
        // Nivel (sobre el retrato) y la experiencia: un hilo bajo la barra de vida.
        {
            float pulse = 1.0f + 0.4f * std::min(1.0f, levelFlash_);
            std::snprintf(buf, sizeof(buf), "NV %d", heroLevel_);
            uiText(ui, cx, cy + 62, 16 * pulse, buf, levelFlash_ > 0 ? rgba(190, 240, 255) : kUiText, FontFace::Title,
                   Align::Center, std::min(1.0f, levelFlash_));
            float frac = heroLevel_ >= kMaxLevel ? 1.0f : float(xp_) / float(xpToNext(heroLevel_));
            fillRect(d, right - hpW, cy - 20, right, cy - 17, rgba(30, 32, 40));
            fillRect(d, right - hpW, cy - 20, right - hpW + hpW * frac, cy - 17, rgba(190, 240, 255));
        }
        // Poder: la única barra de las técnicas, del color de la mitad activa.
        if (powers_.light || shadowUnlocked_) {
            uint32_t pc = shadowMode_ ? rgba(170, 100, 255) : rgba(255, 220, 130);
            segBar(d, right - 14, cy - 2, maxMp_ * 1.8f, 12, mp_ / maxMp_, mp_ / maxMp_, pc, 0, 10);
            uiText(ui, right - 14 - maxMp_ * 1.8f - 12, cy - 6, 14, "PODER", pc, FontFace::Title, Align::Right);
        }
        if (!shadowMode_ && mp_ >= maxMp_ && powers_.nova) {  // la Supernova está lista
            float pulse = 0.6f + 0.4f * std::sin(time_ * 6.0f);
            uiText(ui, right - 60, cy + 30, 15, "5 · SUPERNOVA", withAlpha(rgba(255, 230, 150), pulse), FontFace::Title, Align::Right, pulse);
        }
        // La mitad activa (Z cambia, cuando la sombra ya ha despertado).
        if (shadowUnlocked_) {
            float pulse = modeFlash_ > 0 ? 1.0f + modeFlash_ : 1.0f;
            uiText(ui, cx, cy - 76, 15 * pulse, shadowMode_ ? "SOMBRA" : "LUZ",
                   shadowMode_ ? rgba(180, 130, 255) : rgba(255, 226, 160), FontFace::Title, Align::Center, 0.5f);
            uiText(ui, cx, cy - 92, 11, "Z · cambiar", kUiDim, FontFace::Title, Align::Center);
        }
        // Dónde está: en la sombra (se puede fundir) o en la luz.
        if (shadowMode_ && shadowAt(hero_.body.pos) > 0.6f)
            uiText(ui, right - 14, cy + 16, 13, "EN LA SOMBRA · Q fundirse", rgba(180, 130, 255), FontFace::Title, Align::Right, 0.6f);
        else if (std::max(lightAt(hero_.body.pos), lampLightAt(hero_.body.pos)) > 0.3f)
            uiText(ui, right - 14, cy + 16, 13, "EN LA LUZ", rgba(255, 245, 200), FontFace::Title, Align::Right, 0.6f);
        // Forma activa: nombre y tiempo restante.
        if (form_ != Form::None) {
            uint32_t fc = formColor(form_);
            std::snprintf(buf, sizeof(buf), "%s  %.0f s", formName(form_), double(formTime_));
            uiText(ui, right, cy - 98, 16, buf, fc, FontFace::Title, Align::Right, 0.6f);
            segBar(d, right, cy - 78, 200, 6, formTime_ / kFormDuration, formTime_ / kFormDuration, fc, 0, 20);
        }
        // Lúmenes.
        diamond(d, w - 196, cy + 46, 9, kUiAmber);
        diamond(d, w - 196, cy + 46, 4, rgba(255, 250, 210));
        std::snprintf(buf, sizeof(buf), "%d", inv_.munny);
        uiText(ui, w - 180, cy + 34, 22, buf, kUiAmber, FontFace::Title);
        uiText(ui, w - 180 + textWidth(buf, 22, FontFace::Title) + 8, cy + 40, 14, "LUM", kUiDim, FontFace::Title);
    }

    // Menú de comandos abajo a la izquierda: técnicas de la mitad activa (1-5) y atajos de
    // objetos (6-8).
    if (powers_.techniques) {
        const float x = 28, rowH = 34, width = 300;
        float y0 = h - 30 - rowH * float(kCommandCount) - 40;
        skewRect(d, x, y0, x + 170, y0 + 30, 10, rgba(222, 214, 198, 220));
        uiText(ui, x + 16, y0 + 6, 17, "COMANDOS", rgba(18, 18, 22), FontFace::Title);
        for (int i = 0; i < kCommandCount; ++i) {
            float y = y0 + 36 + float(i) * rowH;
            bool sel = i == commandSel_;
            char name[64], value[32];
            bool usable;
            uint32_t accent;
            if (i < kSpellSlots) {
                Spell sp = modeSpell(shadowMode_, i);
                const SpellDef& s = spellDef(sp);
                std::snprintf(name, sizeof(name), "%s", s.name);
                if (sp == Spell::Nova) std::snprintf(value, sizeof(value), "todo el poder");
                else std::snprintf(value, sizeof(value), "%d poder", int(s.cost));
                usable = sp == Spell::Nova ? mp_ >= maxMp_ && powers_.nova : mp_ >= s.cost;
                accent = s.color;
            } else {
                ItemId id = inv_.quick[i - kSpellSlots];
                std::snprintf(name, sizeof(name), "%s", itemName(id));
                std::snprintf(value, sizeof(value), "×%d", inv_.count(id));
                usable = inv_.count(id) > 0;
                accent = itemDef(id).color;
            }
            float x0 = x + (sel ? 16.0f : 0.0f);
            skewRect(d, x0, y, x0 + width, y + rowH - 5, 8, sel ? rgba(228, 222, 210, 235) : kUiPanel);
            skewRect(d, x0, y, x0 + 6, y + rowH - 5, 8, usable ? mixRgb(accent, rgba(200, 196, 186), 0.6f) : rgba(70, 70, 76));
            if (sel) d.triangle({x0 - 14, y + 5, 0}, {x0 - 14, y + rowH - 10, 0}, {x0 - 4, y + (rowH - 5) * 0.5f, 0}, kUiCyan);
            char key[4];
            std::snprintf(key, sizeof(key), "%d", i + 1);
            uint32_t tc = !usable ? rgba(110, 110, 116) : sel ? rgba(20, 20, 24) : kUiText;
            uiText(ui, x0 + 18, y + 6, 15, key, sel ? rgba(90, 84, 76) : kUiCyan, FontFace::Title);
            uiText(ui, x0 + 40, y + 4, 19, name, tc);
            uiText(ui, x0 + width - 10, y + 6, 15, value, sel ? rgba(60, 70, 110) : kUiDim, FontFace::Title, Align::Right);
        }
        const char* help = inLight_ ? "DENTRO DE LA LUZ · Q saltar a la fuente señalada · R elegir fuente · Clic emboscada · Espacio salir"
                           : lock_ >= 0 ? "Clic atacar · R soltar objetivo · Clic central cambiar de objetivo · C esquivar · Shift correr"
                           : shadowMode_ ? "SOMBRA · Q en la oscuridad: fundirte · Rompe farolas a golpes o apágalas (F en su interruptor) · Z luz"
                                        : "Clic atacar · Q junto a una farola: entrar en la luz · R fijar · C esquivar · Shift correr";
        skewRect(d, x - 6, h - 30, x + 20 + textWidth(help, 14), h - 8, 6, kUiPanel);
        uiText(ui, x, h - 28, 14, help, kUiDim);
    }

    // Objetivo fijado arriba a la derecha.
    if (lock_ >= 0) {
        const Enemy& e = enemies_[size_t(lock_)];
        const EnemyDef& ed = e.def();
        float right = w - 34;
        uiText(ui, right, 22, 22, e.name(), kUiPink, FontFace::Title, Align::Right, 0.5f);
        float f = std::max(0.0f, e.health) / (ed.maxHealth * (e.boss ? 7.0f : 1.0f));
        segBar(d, right, 56, 240, 12, f, f, kUiPink, 0, 10);
    }

    if (novaFlash_ > 0) fillRect(d, 0, 0, w, h, withAlpha(rgba(255, 250, 235), novaFlash_ * 0.9f));
    // Jefe: barra grande arriba en el centro.
    for (const Enemy& e : enemies_)
        if (e.boss && e.alive()) {
            float f = std::max(0.0f, e.health) / (e.def().maxHealth * 7.0f);
            uiText(ui, w * 0.5f, 16, 20, e.name(), rgba(255, 170, 90), FontFace::Title, Align::Center, 0.5f);
            segBar(d, w * 0.5f + 260, 44, 520, 14, f, f, rgba(255, 150, 80), 0, 20);
        }
    // Avisos de objetos obtenidos (a la derecha, entran deslizándose y se desvanecen).
    for (size_t i = 0; i < notices_.size(); ++i) {
        const Notice& n = notices_[i];
        float a = clampf(std::min(n.age / 0.15f, (4.0f - n.age) / 0.6f), 0, 1);
        float slide = (1.0f - smoothstep01(n.age / 0.25f)) * 80.0f;
        float tw = textWidth(n.text.c_str(), 19);
        float y = 96 + float(i) * 40, x1 = w - 30 + slide, x0 = x1 - tw - 52;
        skewRect(d, x0, y, x1, y + 32, 8, withAlpha(rgba(12, 16, 42), 0.85f * a));
        diamond(d, x0 + 22, y + 16, 8, withAlpha(n.color, a));
        uiText(ui, x0 + 38, y + 5, 19, n.text.c_str(), withAlpha(n.color, a));
    }

    // Escenario, parte y objetivo arriba a la izquierda, con el croma del escenario.
    {
        const LevelPart* p = currentPart();
        skewRect(d, 18, 14, 380, 96, 10, kUiPanel);
        fillRect(d, 18, 14, 22, 96, kUiCyan);
        uiText(ui, 30, 18, 15, levelName_.c_str(), kUiCyan, FontFace::Title);
        if (p) {
            std::snprintf(buf, sizeof(buf), "Parte %d/%d · %s", int(part_) + 1, int(parts_.size()), p->title.c_str());
            uiText(ui, 30, 40, 17, buf, kUiText);
            std::string obj = "› " + p->objective;
            if (p->kind == PartKind::Nodes || p->kind == PartKind::Lanterns) {
                int n = 0;
                if (p->kind == PartKind::Nodes)
                    for (const ColorNode& c : nodes_) n += c.lit;
                else
                    for (const Lantern& l : lanterns_) n += l.lit;
                obj += " (" + std::to_string(n) + "/" + std::to_string(p->count) + ")";
            }
            uiText(ui, 30, 66, 16, obj.c_str(), kUiAmber);
        } else {
            uiText(ui, 30, 44, 17, "Escenario completado", rgba(110, 255, 190));
        }
        // Croma: barra arcoíris (cada segmento de un color, apagados los que faltan).
        skewRect(d, 18, 102, 250, 140, 10, kUiPanel);
        std::snprintf(buf, sizeof(buf), "CROMA %d%%", int(cromaShown_ * 100.0f + 0.5f));
        uiText(ui, 30, 105, 14, buf, kUiText, FontFace::Title);
        const uint32_t rainbow[] = {rgba(255, 70, 120), rgba(255, 150, 50), rgba(255, 220, 60), rgba(110, 255, 140),
                                    rgba(40, 220, 255), rgba(120, 110, 255), rgba(230, 90, 255)};
        const int segs = 14;
        for (int i = 0; i < segs; ++i) {
            float x0 = 30 + float(i) * 15.0f, lit = clampf(cromaShown_ * segs - float(i), 0, 1);
            uint32_t c = rainbow[i * 7 / segs];
            skewRect(d, x0, 125, x0 + 12, 133, 3, rgba(50, 50, 70, 220));
            if (lit > 0) skewRect(d, x0, 125, x0 + 12 * lit, 133, 3, c);
        }
        // Cartel con el nombre de la parte al empezarla.
        float by = h * 0.2f;
        if (partBanner_ > 0 && p && chapterBanner_ <= 0) {
            float a = clampf(std::min((4.0f - partBanner_) / 0.4f, partBanner_ / 0.6f), 0, 1);
            fillRect(d, 0, by - 14, w, by + 92, withAlpha(rgba(12, 16, 42), 0.5f * a));
            uiText(ui, w * 0.5f, by - 4, 16, levelName_.c_str(), withAlpha(kUiCyan, a), FontFace::Title, Align::Center);
            uiText(ui, w * 0.5f, by + 18, 44, p->title.c_str(), withAlpha(kUiText, a), FontFace::Body, Align::Center, 0.6f * a);
        }
        // Indicador del objetivo: rombo sobre su posición o, si está fuera de la pantalla,
        // flecha en el borde hacia él.
        if (p && hero_.downTimer < 0 && testMode_ == TestMode::None) {
            float sx, sy;
            Vec3 m = objectivePos(*p);
            float dist = length(m - hero_.body.pos);
            bool on = project(view, m, sx, sy) && sx > 40 && sx < w - 40 && sy > 40 && sy < h - 40;
            if (!on) {
                if (!project(view, m, sx, sy)) {  // detrás de la cámara: al borde inferior
                    sx = w - sx;
                    sy = h - 40;
                }
                sx = clampf(sx, 40, w - 40);
                sy = clampf(sy, 160, h - 40);
            }
            float pulse = 1.0f + 0.15f * std::sin(time_ * 5.0f);
            diamond(d, sx, sy, 11 * pulse, withAlpha(kUiAmber, 0.9f));
            diamond(d, sx, sy, 5, rgba(255, 250, 220));
            std::snprintf(buf, sizeof(buf), "%.0f m", double(dist));
            uiText(ui, sx, sy + 14, 14, buf, kUiAmber, FontFace::Title, Align::Center);
        }
    }

    // Fuente de luz señalada: se puede viajar hasta ella con Q.
    if (lightTarget_ >= 0 && hero_.downTimer < 0) {
        float sx, sy;
        Vec3 lp = lightTargetPos();
        if (project(view, lp, sx, sy)) {
            float pulse = 1.0f + 0.12f * std::sin(time_ * 8.0f);
            fillCircle(d, sx, sy, 22 * pulse, withAlpha(rgba(255, 240, 190), 0.25f), 20);
            fillCircle(d, sx, sy, 6, rgba(255, 250, 225), 12);
            if (inLight_)
                std::snprintf(buf, sizeof(buf), "Q · saltar  %.0f m", double(length(lp - hostPos_)));
            else if (shadowMode_)
                std::snprintf(buf, sizeof(buf), "Q · deslizarte  %.0f m", double(length(lp - hero_.body.pos)));
            else
                std::snprintf(buf, sizeof(buf), "Q · entrar en la luz");
            uiText(ui, sx, sy + 26, 15, buf, rgba(255, 240, 190), FontFace::Body, Align::Center, 0.4f);
        }
    }
    // Dentro de la luz: el Gris sobre el que caería la emboscada.
    if (int a = ambushTarget(); a >= 0) {
        float sx, sy;
        if (project(view, enemies_[size_t(a)].center() + Vec3{0, 1.0f, 0}, sx, sy)) {
            float pulse = 1.0f + 0.12f * std::sin(time_ * 10.0f);
            diamond(d, sx, sy, 12 * pulse, withAlpha(kUiPink, 0.85f));
            uiText(ui, sx, sy + 16, 15, "Clic · emboscada", kUiPink, FontFace::Body, Align::Center, 0.4f);
        }
    }
    InteractTarget it = nearbyInteract();
    if (it.kind >= 0 && hero_.downTimer < 0 && !inLight_) {
        const char* msg = it.label.c_str();
        float tw = textWidth(msg, 22) + 50;
        float x0 = (w - tw) * 0.5f, y0 = h * 0.7f;
        skewRect(d, x0, y0, x0 + tw, y0 + 38, 10, kUiPanel);
        fillRect(d, x0 + 12, y0 + 7, x0 + 36, y0 + 31, kUiCyan);
        uiText(ui, x0 + 24, y0 + 8, 18, "F", rgba(10, 20, 50), FontFace::Title, Align::Center);
        uiText(ui, x0 + 46, y0 + 7, 22, msg, kUiText);
    }
    if (hero_.downTimer >= 0) {
        float a = clampf(hero_.downTimer / 0.8f, 0, 1);
        fillRect(d, 0, 0, w, h, withAlpha(rgba(20, 10, 40), 0.45f * a));
        uiText(ui, w * 0.5f, h * 0.38f, 56, "SEÑAL PERDIDA", withAlpha(kUiPink, a), FontFace::Title, Align::Center, a);
        uiText(ui, w * 0.5f, h * 0.38f + 72, 22, "Reconstruyendo el prisma...", withAlpha(kUiDim, a), FontFace::Body,
               Align::Center);
    }

    // Modo debug: estado de la IA y vida encima de cada enemigo, y recuentos.
    if (debug) {
        for (const Enemy& e : enemies_) {
            float sx, sy;
            if (!project(view, e.center() + Vec3{0, e.def().height * 0.5f + 0.3f, 0}, sx, sy)) continue;
            std::snprintf(buf, sizeof(buf), "%s · %s %.0f", aiStateName(e.state), intentName(e.intent),
                          double(std::max(0.0f, e.health)));
            uiText(ui, sx, sy, 15, buf, kUiAmber, FontFace::Body, Align::Center);
        }
        std::snprintf(buf, sizeof(buf), "DEBUG · enemigos %zu · botín %zu · partículas %zu · proyectiles %zu",
                      enemies_.size(), pickups_.size(), particles_.size(), projectiles_.size());
        uiText(ui, 28, 74, 15, buf, rgba(120, 255, 160));
        uiText(ui, 28, 94, 15, "F1 huesos · F2 articulaciones · F3 cajas · F4 rejilla", rgba(120, 200, 160));
    }
    drawStoryHud(ui, view);
}

// Escenas: bandas negras arriba y abajo durante los planos. Tutoriales: una tecla y una
// palabra, abajo en el centro, que se apagan al hacer la acción.
void Game::drawCinemaHud(UiLayer& ui, const ViewInfo& view) const {
    DebugDraw& d = ui.shapes;
    const float w = float(view.width), h = float(view.height);
    if (cineBars_ > 0) {
        float k = smoothstep01(cineBars_);
        fillRect(d, 0, 0, w, 70 * k, rgba(0, 0, 0));
        fillRect(d, 0, h - 70 * k, w, h, rgba(0, 0, 0));
    }
    if (tutorials_.empty() || dialogueBlocking()) return;
    const Tutorial& t = tutorials_.front();
    float a = clampf(std::min(t.age / 0.4f, t.doneTime >= 0 ? 1.0f - (t.age - t.doneTime) / 0.8f : 1.0f), 0, 1);
    if (a <= 0) return;
    float kw = std::max(34.0f, textWidth(t.key.c_str(), 18, FontFace::Title) + 20);
    float ww = textWidth(t.word.c_str(), 22);
    float x0 = (w - kw - 14 - ww) * 0.5f, y0 = h * 0.78f;
    uint32_t done = t.doneTime >= 0 ? rgba(150, 200, 150) : kUiCyan;
    fillRect(d, x0 - 1.5f, y0 - 1.5f, x0 + kw + 1.5f, y0 + 35.5f, withAlpha(done, a));
    fillRect(d, x0, y0, x0 + kw, y0 + 34, withAlpha(rgba(14, 14, 18), 0.9f * a));
    uiText(ui, x0 + kw * 0.5f, y0 + 7, 18, t.key.c_str(), withAlpha(done, a), FontFace::Title, Align::Center);
    uiText(ui, x0 + kw + 14, y0 + 5, 22, t.word.c_str(), withAlpha(kUiText, a), FontFace::Body, Align::Left, 0.5f * a);
}
