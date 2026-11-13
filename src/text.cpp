#include "text.h"

#include <cstring>

namespace {

// Cada segmento son 4 dígitos "x0 y0 x1 y1" en la rejilla (x 0..4, y 0..6, y hacia abajo).
const char* glyph(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    switch (c) {
        // clang-format off
        case 'A': return "0602 0220 2042 4246 0343";
        case 'B': return "0006 0030 3041 4142 4233 0333 3344 4445 4536 3606";
        case 'C': return "4000 0006 0646";
        case 'D': return "0006 0020 2042 4244 4426 2606";
        case 'E': return "4000 0006 0646 0333";
        case 'F': return "4000 0006 0333";
        case 'G': return "4000 0006 0646 4643 4323";
        case 'H': return "0006 4046 0343";
        case 'I': return "0040 2026 0646";
        case 'J': return "4046 4606 0604";
        case 'K': return "0006 0340 0346";
        case 'L': return "0006 0646";
        case 'M': return "0600 0023 2340 4046";
        case 'N': return "0600 0046 4640";
        case 'O': return "0040 4046 4606 0600";
        case 'P': return "0600 0040 4043 4303";
        case 'Q': return "0040 4046 4606 0600 2446";
        case 'R': return "0600 0040 4043 4303 0346";
        case 'S': return "4000 0003 0343 4346 4606";
        case 'T': return "0040 2026";
        case 'U': return "0006 0646 4640";
        case 'V': return "0026 2640";
        case 'W': return "0016 1623 2336 3640";
        case 'X': return "0046 4006";
        case 'Y': return "0023 4023 2326";
        case 'Z': return "0040 4006 0646";
        case '0': return "0040 4046 4606 0600 0640";
        case '1': return "2026 1120 1636";
        case '2': return "0040 4043 4303 0306 0646";
        case '3': return "0040 4046 4606 1343";
        case '4': return "0003 0343 4046";
        case '5': return "4000 0003 0343 4346 4606";
        case '6': return "4000 0006 0646 4643 4303";
        case '7': return "0040 4016";
        case '8': return "0040 4046 4606 0600 0343";
        case '9': return "4303 0300 0040 4046 4606";
        case '.': return "2526";
        case ':': return "2122 2425";
        case '-': return "1333";
        case '/': return "0640";
        case '<': return "3013 1336";
        case '>': return "1033 3316";
        case '%': return "0640 0011 3546";
        case '+': return "0343 2125";
        case '!': return "2023 2526";
        case ',': return "2527";
        case '\'': return "2021";
        case '(': return "2111 1115 1525";
        case ')': return "2131 3135 3525";
        case '?': return "0040 4043 4323 2324 2526";
        case '=': return "0242 0444";
        default:  return "";  // espacio y caracteres sin glifo
        // clang-format on
    }
}

constexpr float kAdvance = 6.0f;  // 4 de ancho + 2 de separación

}  // namespace

void drawText(DebugDraw& dd, float x, float y, float scale, const char* text, uint32_t color) {
    for (const char* p = text; *p; ++p, x += kAdvance * scale) {
        const char* g = glyph(*p);
        for (; std::strlen(g) >= 4; g += (g[4] == ' ' ? 5 : 4)) {
            Vec3 a{x + float(g[0] - '0') * scale, y + float(g[1] - '0') * scale, 0};
            Vec3 b{x + float(g[2] - '0') * scale, y + float(g[3] - '0') * scale, 0};
            dd.line(a, b, color);
        }
    }
}

float textWidth(const char* text, float scale) {
    size_t n = std::strlen(text);
    return n == 0 ? 0 : (float(n) * kAdvance - 2.0f) * scale;
}
