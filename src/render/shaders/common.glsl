// Constantes y funciones comunes a los shaders de color (en espacio lineal).
// Dos paletas: la ciudad parada (gris de verdad: cielo cubierto, luz pobre y fría, niebla
// espesa) y la ciudad viva (tarde cálida). El croma del mundo pasa de una a otra; las
// macros usan uWorldColor, así que solo valen en los shaders que lo declaran.
const vec3 kSunWarm = vec3(1.0, 0.92, 0.80) * 1.25, kSunGrey = vec3(0.80, 0.83, 0.88) * 0.7;
const vec3 kFillWarm = vec3(0.55, 0.65, 0.95) * 0.4, kFillGrey = vec3(0.50, 0.53, 0.58) * 0.25;
const vec3 kZenithWarm = vec3(0.07, 0.22, 0.70), kZenithGrey = vec3(0.17, 0.18, 0.21);
const vec3 kHorizonWarm = vec3(0.80, 0.60, 0.68), kHorizonGrey = vec3(0.36, 0.37, 0.40);
const vec3 kBounceWarm = vec3(0.28, 0.24, 0.17), kBounceGrey = vec3(0.13, 0.13, 0.14);
#define kSunColor mix(kSunGrey, kSunWarm, uWorldColor)
#define kFillColor mix(kFillGrey, kFillWarm, uWorldColor)
#define kZenith mix(kZenithGrey, kZenithWarm, uWorldColor)
#define kHorizon mix(kHorizonGrey, kHorizonWarm, uWorldColor)
#define kBounce mix(kBounceGrey, kBounceWarm, uWorldColor)
#define kWorldFog mix(0.024, 0.007, uWorldColor)  // niebla de la ciudad parada
const float kFogDensity = 0.007;  // humo y volúmenes

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Ruido de valor 2D y fbm (suma de octavas): base de llamas, humo y chamuscado.
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { v += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return v;
}

// Tonemapping ACES (aproximación de Narkowicz) + gamma 2.2.
vec3 finish(vec3 c) {
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}
