#pragma once

// Texto con fuentes reales (Orbitron para títulos y cifras, Exo 2 para el resto) en un
// atlas de campos de distancia (SDF): cada texel guarda la distancia al borde de la
// letra, así se dibuja nítida a cualquier tamaño y el contorno y el brillo salen gratis.
// Admite UTF-8 (tildes, ñ, ¿¡). Pensado para el HUD en píxeles (y hacia abajo).

#include "debug_draw.h"
#include "gl.h"
#include "vmath.h"

#include <cstdint>
#include <vector>

enum class FontFace { Body, Title, Count };
enum class Align { Left, Center, Right };

struct TextStyle {
    FontFace face = FontFace::Body;
    Align align = Align::Left;
    uint32_t outline = 0;  // color del contorno (alfa 0 = sin contorno)
    float glow = 0;        // 0..1: halo suave del color del texto
};

// Carga las fuentes y crea el atlas (necesita contexto GL). `assetDir` = carpeta assets.
bool loadFonts(const char* assetDir);
void unloadFonts();

class TextBatch {
public:
    bool init();
    void shutdown();
    void flush(const Mat4& proj);
    size_t vertexCount() const { return verts_.size(); }

    struct Vertex {
        float x, y, u, v;
        uint32_t color, outline;
        float glow, pad;
    };
    std::vector<Vertex> verts_;

private:
    GLuint vao_ = 0, vbo_ = 0, program_ = 0;
    size_t capacity_ = 0;
};

// Capa de interfaz: formas (paneles, barras) y encima el texto.
struct UiLayer {
    DebugDraw shapes;
    TextBatch text;
    bool init() { return shapes.init() && text.init(); }
    void shutdown() {
        shapes.shutdown();
        text.shutdown();
    }
    void flush(const Mat4& pixels) {
        shapes.flush(pixels, 3.0f);
        text.flush(pixels);
    }
    size_t vertexCount() const { return shapes.vertexCount() + text.vertexCount(); }
};

// `y` es la parte de arriba de la línea; `size` el tamaño de la fuente en píxeles.
// Devuelve el ancho dibujado.
float drawText(UiLayer& ui, float x, float y, float size, const char* utf8, uint32_t color, const TextStyle& st = {});
float textWidth(const char* utf8, float size, FontFace face = FontFace::Body);
