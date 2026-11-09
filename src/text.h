#pragma once

// Fuente vectorial mínima: cada carácter son segmentos en una rejilla de 4x6 unidades.
// Solo mayúsculas sin acentos, dígitos y algo de puntuación (las minúsculas se dibujan
// como mayúsculas). Pensada para el HUD en píxeles (y hacia abajo).

#include "debug_draw.h"

// `scale` = píxeles por unidad de rejilla: la altura del texto es 6 * scale.
void drawText(DebugDraw& dd, float x, float y, float scale, const char* text, uint32_t color);
float textWidth(const char* text, float scale);
