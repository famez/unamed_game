#include "text.h"

// stb_truetype (dominio público): lectura de TTF y generación de SDF. Sus avisos no son
// nuestros: se silencian solo aquí.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kAtlasSize = 1024;
constexpr float kBakeSize = 48.0f;  // tamaño de fuente al generar el SDF
constexpr int kPadding = 6;         // margen alrededor de cada letra (en texels)
constexpr float kDistScale = 128.0f / kPadding;  // 1 texel = 128/6 unidades de distancia

struct Glyph {
    int codepoint = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float xoff = 0, yoff = 0, w = 0, h = 0;  // en píxeles a kBakeSize
    float advance = 0;
};

struct FontData {
    std::vector<unsigned char> file;
    stbtt_fontinfo info{};
    float scale = 0, ascent = 0;
    std::vector<Glyph> glyphs;  // ordenados por codepoint
    const Glyph* find(int cp) const {
        size_t lo = 0, hi = glyphs.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (glyphs[mid].codepoint < cp) lo = mid + 1;
            else hi = mid;
        }
        return lo < glyphs.size() && glyphs[lo].codepoint == cp ? &glyphs[lo] : nullptr;
    }
};

FontData gFonts[int(FontFace::Count)];
GLuint gAtlas = 0;

// Decodifica el siguiente carácter UTF-8 (devuelve '?' si la secuencia no es válida).
int nextCodepoint(const char*& p) {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    int cp = n == 1 ? c & 0x1F : n == 2 ? c & 0x0F : c & 0x07;
    for (int i = 0; i < n; ++i) {
        if ((*p & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (*p++ & 0x3F);
    }
    return n ? cp : '?';
}

bool readFile(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n));
    bool ok = std::fread(out.data(), 1, size_t(n), f) == size_t(n);
    std::fclose(f);
    return ok;
}

const char* kVs = R"(#version 330 core
layout(location = 0) in vec4 aPosUv;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aOutline;
layout(location = 3) in float aGlow;
uniform mat4 uProj;
out vec2 vUv;
out vec4 vColor;
out vec4 vOutline;
out float vGlow;
void main() {
    vUv = aPosUv.zw;
    vColor = aColor;
    vOutline = aOutline;
    vGlow = aGlow;
    gl_Position = uProj * vec4(aPosUv.xy, 0.0, 1.0);
}
)";

// La distancia 0,5 es el borde de la letra. El suavizado es de ~1 píxel de pantalla sea
// cual sea el tamaño (fwidth). El contorno es otra franja más afuera; el halo, una caída
// suave todavía más lejos.
const char* kFs = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
in vec4 vOutline;
in float vGlow;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main() {
    float d = texture(uAtlas, vUv).r;
    float w = max(fwidth(d) * 0.7, 1e-4);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    vec4 c = vec4(vColor.rgb, vColor.a * fill);
    if (vOutline.a > 0.0) {
        float o = smoothstep(0.36 - w, 0.36 + w, d);
        c = vec4(mix(vOutline.rgb, vColor.rgb, fill), max(fill * vColor.a, o * vOutline.a));
    }
    if (vGlow > 0.0) {
        float g = smoothstep(0.08, 0.5, d) * vGlow * 0.7;
        c = vec4(mix(vColor.rgb, c.rgb, c.a), max(c.a, g * vColor.a));
    }
    fragColor = c;
}
)";

GLuint compileProgram() {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Error en el shader de texto:\n%s\n", log);
        }
        return s;
    };
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, kVs), f = compile(GL_FRAGMENT_SHADER, kFs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

}  // namespace

bool loadFonts(const char* assetDir) {
    const char* files[int(FontFace::Count)] = {"fonts/Exo2.ttf", "fonts/Orbitron.ttf"};
    // ASCII imprimible + lo necesario para el castellano y algunos signos.
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c : {0xA1, 0xBF, 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xF1, 0xD1, 0xFC, 0xDC, 0xBA,
                  0xAA, 0xB7, 0xAB, 0xBB, 0xB0, 0xD7, 0x2013, 0x2014, 0x2026, 0x201C, 0x201D, 0x2018, 0x2019, 0x2039, 0x203A, 0x25B2, 0x25BC})
        cps.push_back(c);
    std::sort(cps.begin(), cps.end());

    std::vector<unsigned char> atlas(size_t(kAtlasSize) * kAtlasSize, 0);
    int penX = 1, penY = 1, rowH = 0;
    for (int f = 0; f < int(FontFace::Count); ++f) {
        FontData& fd = gFonts[f];
        std::string path = std::string(assetDir) + "/" + files[f];
        if (!readFile(path, fd.file) || !stbtt_InitFont(&fd.info, fd.file.data(), 0)) {
            std::fprintf(stderr, "No se pudo cargar la fuente %s\n", path.c_str());
            return false;
        }
        fd.scale = stbtt_ScaleForPixelHeight(&fd.info, kBakeSize);
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&fd.info, &asc, &desc, &gap);
        fd.ascent = float(asc) * fd.scale;
        fd.glyphs.clear();
        for (int cp : cps) {
            Glyph g;
            g.codepoint = cp;
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&fd.info, cp, &adv, &lsb);
            g.advance = float(adv) * fd.scale;
            int w = 0, h = 0, xo = 0, yo = 0;
            unsigned char* sdf = stbtt_GetCodepointSDF(&fd.info, fd.scale, cp, kPadding, 128, kDistScale, &w, &h, &xo, &yo);
            if (sdf) {
                if (penX + w + 1 >= kAtlasSize) {  // fila nueva
                    penX = 1;
                    penY += rowH + 1;
                    rowH = 0;
                }
                if (penY + h + 1 >= kAtlasSize) {
                    std::fprintf(stderr, "El atlas de fuentes se ha llenado\n");
                    stbtt_FreeSDF(sdf, nullptr);
                    break;
                }
                for (int y = 0; y < h; ++y) std::memcpy(&atlas[size_t(penY + y) * kAtlasSize + size_t(penX)], sdf + y * w, size_t(w));
                g.u0 = float(penX) / kAtlasSize;
                g.v0 = float(penY) / kAtlasSize;
                g.u1 = float(penX + w) / kAtlasSize;
                g.v1 = float(penY + h) / kAtlasSize;
                g.xoff = float(xo);
                g.yoff = float(yo);
                g.w = float(w);
                g.h = float(h);
                penX += w + 1;
                rowH = std::max(rowH, h);
                stbtt_FreeSDF(sdf, nullptr);
            }
            fd.glyphs.push_back(g);
        }
    }
    glGenTextures(1, &gAtlas);
    glBindTexture(GL_TEXTURE_2D, gAtlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void unloadFonts() {
    glDeleteTextures(1, &gAtlas);
    gAtlas = 0;
}

bool TextBatch::init() {
    program_ = compileProgram();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, outline));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, glow));
    glBindVertexArray(0);
    return program_ != 0;
}

void TextBatch::shutdown() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
}

void TextBatch::flush(const Mat4& proj) {
    if (verts_.empty() || !gAtlas) {
        verts_.clear();
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (verts_.size() > capacity_) capacity_ = verts_.size() * 2;
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(capacity_ * sizeof(Vertex)), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data());
    glUseProgram(program_);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProj"), 1, GL_FALSE, proj.m);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gAtlas);
    glUniform1i(glGetUniformLocation(program_, "uAtlas"), 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
    glBindVertexArray(0);
    verts_.clear();
}

float textWidth(const char* utf8, float size, FontFace face) {
    const FontData& fd = gFonts[int(face)];
    if (fd.glyphs.empty()) return 0;
    float k = size / kBakeSize, w = 0;
    for (const char* p = utf8; *p;) {
        const Glyph* g = fd.find(nextCodepoint(p));
        if (g) w += g->advance * k;
    }
    return w;
}

float drawText(UiLayer& ui, float x, float y, float size, const char* utf8, uint32_t color, const TextStyle& st) {
    const FontData& fd = gFonts[int(st.face)];
    if (fd.glyphs.empty()) return 0;
    float width = textWidth(utf8, size, st.face);
    if (st.align == Align::Center) x -= width * 0.5f;
    if (st.align == Align::Right) x -= width;
    float k = size / kBakeSize;
    float baseline = y + fd.ascent * k;
    float pen = x;
    auto& v = ui.text.verts_;
    for (const char* p = utf8; *p;) {
        const Glyph* g = fd.find(nextCodepoint(p));
        if (!g) continue;
        if (g->w > 0) {
            float x0 = pen + g->xoff * k, y0 = baseline + g->yoff * k;
            float x1 = x0 + g->w * k, y1 = y0 + g->h * k;
            TextBatch::Vertex a{x0, y0, g->u0, g->v0, color, st.outline, st.glow, 0};
            TextBatch::Vertex b{x1, y0, g->u1, g->v0, color, st.outline, st.glow, 0};
            TextBatch::Vertex c{x1, y1, g->u1, g->v1, color, st.outline, st.glow, 0};
            TextBatch::Vertex d{x0, y1, g->u0, g->v1, color, st.outline, st.glow, 0};
            v.insert(v.end(), {a, b, c, a, c, d});
        }
        pen += g->advance * k;
    }
    return width;
}
