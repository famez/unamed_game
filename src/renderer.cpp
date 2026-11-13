#include "renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

namespace {

constexpr int kShadowSize = 2048;
constexpr float kShadowExtent = 30.0f;  // semilado (m) del área con sombras alrededor del jugador
const Vec3 kSunDir = normalize(Vec3{0.45f, 0.8f, 0.35f});  // hacia el sol

// Constantes y funciones comunes a los shaders de color (en espacio lineal).
const char* kCommonGlsl = R"(
// Paleta luminosa y saturada, de tarde soleada.
const vec3 kSunColor = vec3(1.0, 0.92, 0.78) * 1.8;
const vec3 kFillColor = vec3(0.55, 0.65, 0.95) * 0.45;  // relleno frío opuesto al sol
const vec3 kZenith   = vec3(0.10, 0.32, 0.85);
const vec3 kHorizon  = vec3(0.62, 0.80, 0.98);
const vec3 kBounce   = vec3(0.30, 0.26, 0.18);
const float kFogDensity = 0.008;

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
)";

const char* kMeshVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aModel;  // ocupa las posiciones 2..5
layout(location = 6) in vec4 aColor;
layout(location = 7) in vec3 aParams; // x = material, y = destello, z = forma (1 = elipsoide)
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;  // > 0: pasada de silueta, la caja crece estos metros por lado
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec3 vParams;
void main() {
    vec3 size = vec3(length(aModel[0].xyz), length(aModel[1].xyz), length(aModel[2].xyz));
    vec3 local = uInflate > 0.0 ? aPos * (size + 2.0 * uInflate) / size : aPos;
    vec4 world = aModel * vec4(local, 1.0);
    vWorldPos = world.xyz;
    // Matriz de normales para rotación * escala: R * S^-1 = M * S^-2.
    vNormal = mat3(aModel) * (aNormal / (size * size));
    vLocal = aPos;
    vSize = size;
    vColor = aColor;
    vParams = aParams;
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
)";

const char* kMeshFs = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocal;
in vec3 vSize;
in vec4 vColor;
in vec4 vLightPos;
flat in vec3 vParams;
uniform vec3 uSunDir;
uniform vec3 uCameraPos;
uniform sampler2DShadow uShadowMap;
uniform int uLightCount;
uniform vec4 uLightPos[8];    // xyz, radio
uniform vec3 uLightColor[8];
uniform int uScorchCount;
uniform vec4 uScorch[8];      // xyz, radio
uniform float uScorchAmount[8];
uniform vec4 uOutlineColor;  // a > 0: pasada de silueta (color plano, sin iluminación)
uniform float uTime;
out vec4 fragColor;

float shadowFactor(float ndl) {
    vec3 p = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = mix(0.0012, 0.0003, ndl);  // más sesgo en superficies rasantes
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    // Cada muestra ya filtra 2x2 por hardware (comparación + bilineal).
    float s = texture(uShadowMap, vec3(p.xy + vec2(-0.6, -0.3) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2( 0.4, -0.8) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2(-0.3,  0.8) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2( 0.8,  0.4) * texel * 1.5, p.z - bias));
    return s * 0.25;
}

// Distancia (en píxeles) a la línea de una rejilla de paso 1, antialiasada con fwidth.
float gridLine(vec2 p) {
    vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
    return 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);
}

void main() {
    if (uOutlineColor.a > 0.0) {
        fragColor = vec4(uOutlineColor.rgb, 1.0);
        return;
    }
    int material = int(vParams.x + 0.5);
    vec3 n = normalize(vNormal);
    vec3 base = toLinear(vColor.rgb);

    if (material == 3) {  // plaza: adoquines de 0,8 m en hileras desplazadas, cada uno de un tono
        vec2 w = vWorldPos.xz / 0.8;
        w.x += 0.5 * mod(floor(w.y), 2.0);
        vec2 cell = floor(w), f = fract(w);
        float tone = hash(cell);
        base *= mix(0.86, 1.08, tone);
        vec2 e = min(f, 1.0 - f) / fwidth(w);  // junta antialiasada
        base *= mix(0.72, 1.0, clamp(min(e.x, e.y) * 0.5, 0.0, 1.0));
        // Rosetón de piedra más clara alrededor de la fuente.
        float r = length(vWorldPos.xz);
        base *= 1.0 + 0.12 * (1.0 - smoothstep(3.6, 3.8, r)) * step(2.4, r);
    } else if (material == 5) {  // césped: manchas de ruido en dos tonos
        // Dos lecturas de ruido (no fbm): el césped ocupa media pantalla y es barato así.
        float n = 0.65 * noise(vWorldPos.xz * 0.35) + 0.35 * noise(vWorldPos.xz * 1.3);
        base *= mix(0.8, 1.15, n);
        base = mix(base, base * vec3(1.15, 1.1, 0.7), smoothstep(0.6, 0.8, noise(vWorldPos.xz * 0.5 + 7.0)));
    }
    if (material == 4) {  // emisivo: el color es luz propia, sin sombras
        vec3 Ve = normalize(uCameraPos - vWorldPos);
        float rimE = pow(1.0 - max(dot(n, Ve), 0.0), 2.0);
        vec3 glow = base * (1.6 + 0.8 * rimE) + vec3(0.25) * rimE;
        glow = mix(glow, vec3(2.0), vParams.y);
        float distE = length(uCameraPos - vWorldPos);
        glow = mix(glow, kHorizon, 1.0 - exp(-pow(distE * kFogDensity, 2.0)));
        fragColor = vec4(finish(glow), 1.0);
        return;
    }
    if (material == 6) {  // agua: ondas que se mueven y reflejo del cielo según el ángulo
        vec2 q = vWorldPos.xz * 3.0;
        float t = uTime * 1.5;
        vec3 wn = normalize(vec3(sin(q.x + t) * 0.15 + sin(q.y * 1.3 - t * 0.8) * 0.1, 1.0,
                                 cos(q.y + t * 1.1) * 0.15 + cos(q.x * 0.7 + t) * 0.1));
        n = wn;
    }

    // Chamuscado: oscurece suelo y plataformas (caras de arriba) bajo los fuegos, con
    // borde irregular gracias al ruido.
    if ((material == 1 || material == 3 || material == 5) && n.y > 0.5) {
        for (int i = 0; i < uScorchCount; ++i) {
            if (abs(vWorldPos.y - uScorch[i].y) > 0.4) continue;
            float d = length(vWorldPos.xz - uScorch[i].xz) + (fbm(vWorldPos.xz * 2.0) - 0.5) * 1.2;
            float k = 1.0 - smoothstep(uScorch[i].w * 0.3, uScorch[i].w, d);
            base *= 1.0 - 0.85 * k * uScorchAmount[i];
        }
    }

    // Bisel: oscurece cerca de las aristas. En una cara, una de las tres distancias al
    // borde es ~0 (la del eje de la cara); la arista más cercana es la mediana.
    float edge = 1.0;
    if (material != 3 && material != 5 && material != 6 && vParams.z < 0.5) {  // los elipsoides no tienen aristas
        vec3 d = (0.5 - abs(vLocal)) * vSize;
        float mid = d.x + d.y + d.z - min(d.x, min(d.y, d.z)) - max(d.x, max(d.y, d.z));
        float width = material == 1 ? 0.06 : 0.012;
        edge = mix(0.5, 1.0, smoothstep(0.0, width, mid));
    }

    vec3 V = normalize(uCameraPos - vWorldPos);
    float ndl = max(dot(n, uSunDir), 0.0);
    float shadow = shadowFactor(ndl);
    // Sombreado de animación: la luz pasa a sombra en una franja estrecha (no a cel puro),
    // y las sombras proyectadas son más suaves y algo azuladas.
    if (material == 0) ndl = mix(ndl, smoothstep(0.05, 0.3, ndl), 0.7);
    shadow = mix(0.25, 1.0, shadow);

    // Ambiente hemisférico (cielo arriba, rebote cálido del suelo abajo) + relleno suave
    // desde el lado contrario al sol, para que las caras en sombra no queden planas.
    vec3 skyAmbient = mix(kHorizon, kZenith, 0.35) * 1.3;
    vec3 ambient = mix(kBounce * 1.6, skyAmbient, n.y * 0.5 + 0.5) * 0.62;
    vec3 fillDir = normalize(vec3(-uSunDir.x, 0.3, -uSunDir.z));
    ambient += kFillColor * max(dot(n, fillDir), 0.0);
    float specPower = material == 2 ? 80.0 : 20.0;
    // La tela de los cuerpos redondeados (normales suaves) brilla menos que la de las cajas.
    float specAmount = material == 2 ? 1.2 : (material == 6 ? 2.0 : (material == 3 || material == 5 ? 0.04 : (vParams.z > 0.5 ? 0.12 : 0.15)));
    if (material == 6) specPower = 120.0;
    vec3 H = normalize(uSunDir + V);
    float spec = pow(max(dot(n, H), 0.0), specPower) * specAmount * step(0.0, dot(n, uSunDir));

    vec3 color = base * (ambient + kSunColor * ndl * shadow) + kSunColor * spec * shadow;
    // Luces puntuales (fuego): atenuación cuadrática hasta el radio y algo de "wrap" para
    // que la luz rodee un poco las caras.
    for (int i = 0; i < uLightCount; ++i) {
        vec3 Lv = uLightPos[i].xyz - vWorldPos;
        float d = length(Lv);
        float att = clamp(1.0 - d / uLightPos[i].w, 0.0, 1.0);
        float wrap = max(dot(n, Lv / max(d, 1e-3)) * 0.7 + 0.3, 0.0);
        color += base * uLightColor[i] * wrap * att * att;
    }
    if (material == 0 || material == 2) {  // luz de contorno: separa la silueta del fondo
        float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
        color += rim * kHorizon * (vParams.z > 0.5 ? 0.8 : 0.5);
    }
    if (material == 6) {  // reflejo del cielo (Fresnel)
        float fr = pow(1.0 - max(dot(n, V), 0.0), 4.0);
        color = mix(color, mix(kHorizon, kZenith, 0.3) * 1.4, 0.3 + 0.6 * fr);
    }
    color *= edge;
    color = mix(color, vec3(1.6), vParams.y);  // destello de impacto

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-pow(dist * kFogDensity, 2.0));
    color = mix(color, kHorizon, fog);
    fragColor = vec4(finish(color), 1.0);
}
)";

// Skinning: cada vértice se mueve con la mezcla de las matrices de sus dos huesos. Las
// salidas son las mismas que las del shader de instancias, así se reutiliza su fragment
// shader (iluminación, sombras, contorno).
const char* kSkinVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in float aMaterial;
uniform mat4 uBones[20];
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;
uniform float uFlash;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec3 vParams;
void main() {
    mat4 skin = uBones[int(aJoints.x)] * aWeights.x + uBones[int(aJoints.y)] * aWeights.y;
    vec3 n = normalize(mat3(skin) * aNormal);
    vec4 world = skin * vec4(aPos, 1.0);
    world.xyz += n * uInflate;
    vWorldPos = world.xyz;
    vNormal = n;
    vLocal = vec3(0.0);
    vSize = vec3(1.0);
    vColor = aColor;
    vParams = vec3(aMaterial, uFlash, 1.0);  // forma "redondeada": sin bisel
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
)";

const char* kSkinShadowVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform mat4 uBones[20];
uniform mat4 uLightViewProj;
void main() {
    mat4 skin = uBones[int(aJoints.x)] * aWeights.x + uBones[int(aJoints.y)] * aWeights.y;
    gl_Position = uLightViewProj * skin * vec4(aPos, 1.0);
}
)";

const char* kShadowVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in mat4 aModel;
uniform mat4 uLightViewProj;
void main() { gl_Position = uLightViewProj * aModel * vec4(aPos, 1.0); }
)";

const char* kShadowFs = R"(
void main() {}
)";

// Triángulo que cubre toda la pantalla, generado sin buffers a partir de gl_VertexID.
const char* kSkyVs = R"(
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";

const char* kSkyFs = R"(
in vec2 vNdc;
uniform vec3 uCamForward, uCamRight, uCamUp;
uniform vec2 uTanHalf;  // tan(fov/2) * aspect, tan(fov/2)
uniform vec3 uSunDir;
uniform float uTime;
out vec4 fragColor;
void main() {
    vec3 dir = normalize(uCamForward + vNdc.x * uTanHalf.x * uCamRight + vNdc.y * uTanHalf.y * uCamUp);
    vec3 color = mix(kHorizon, kZenith, smoothstep(0.0, 0.5, dir.y));
    // Nubes: ruido sobre un plano alto, más densas hacia el horizonte.
    if (dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.15) * 1.6;
        float c = smoothstep(0.5, 0.75, fbm(uv + vec2(uTime * 0.01, 0.0)));
        color = mix(color, vec3(1.0, 0.98, 0.96) * 1.25, c * smoothstep(0.0, 0.15, dir.y) * 0.9);
    }
    float sun = max(dot(dir, uSunDir), 0.0);
    color += kSunColor * (pow(sun, 600.0) * 4.0 + pow(sun, 12.0) * 0.08);  // disco + halo
    fragColor = vec4(finish(color), 1.0);
}
)";

// Humo: quads mirando a cámara generados con gl_VertexID; los datos van por instancia.
const char* kSmokeVs = R"(
layout(location = 0) in vec4 aPosSeed;
layout(location = 1) in vec4 aSize;  // tamaño, opacidad
uniform mat4 uViewProj;
uniform vec3 uCamRight, uCamUp, uCamPos;
out vec2 vUv;
flat out vec2 vParams;  // semilla, opacidad
out float vFog;
const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
void main() {
    vec2 c = kCorners[gl_VertexID];
    vec3 world = aPosSeed.xyz + (uCamRight * c.x + uCamUp * c.y) * aSize.x * 0.5;
    vUv = c;
    vParams = vec2(aPosSeed.w, aSize.y);
    vFog = 1.0 - exp(-pow(length(world - uCamPos) * kFogDensity, 2.0));
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";

const char* kSmokeFs = R"(
in vec2 vUv;
flat in vec2 vParams;
in float vFog;
uniform float uTime;
uniform sampler2D uSceneDepth;
uniform vec2 uFxSize;    // tamaño del buffer de efectos (media resolución)
uniform vec2 uNearFar;
out vec4 fragColor;
float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
    // El buffer de efectos no tiene profundidad: se compara a mano con la de la escena y
    // se desvanece cerca de la superficie (partículas suaves, sin cortes contra el suelo).
    float scene = linearDepth(texture(uSceneDepth, gl_FragCoord.xy / uFxSize).r);
    float soft = clamp((scene - linearDepth(gl_FragCoord.z)) / 0.4, 0.0, 1.0);
    if (soft <= 0.0) discard;
    // Mancha redonda con borde de ruido que se desplaza lentamente.
    float seed = vParams.x;
    float n = fbm(vUv * 1.8 + vec2(seed * 3.1, seed * 1.7 - uTime * 0.35));
    float a = (1.0 - smoothstep(0.25, 1.0, length(vUv) + (n - 0.5) * 0.7)) * vParams.y * (1.0 - vFog) * soft;
    vec3 col = vec3(0.10, 0.09, 0.09) + n * 0.08;
    fragColor = vec4(col * a, a);  // alfa premultiplicado
}
)";

// Fuego volumétrico. La geometría es solo la caja que lo contiene; el fragment shader
// lanza un rayo desde la cámara, calcula dónde entra y sale de la caja y la recorre en
// pasos acumulando la luz que emite el fuego en cada punto (raymarching).
const char* kVolumeVs = R"(
layout(location = 0) in vec3 aPos;      // cubo unidad
layout(location = 1) in vec4 aBoxMin;   // xyz, semilla
layout(location = 2) in vec4 aBoxMax;   // xyz, intensidad
layout(location = 3) in vec4 aShape;    // centro xyz, radio
layout(location = 4) in vec4 aShape2;   // altura, tipo
uniform mat4 uViewProj;
out vec3 vWorld;
flat out vec4 vBoxMin, vBoxMax, vShape, vShape2;
void main() {
    vWorld = mix(aBoxMin.xyz, aBoxMax.xyz, aPos + 0.5);
    vBoxMin = aBoxMin; vBoxMax = aBoxMax; vShape = aShape; vShape2 = aShape2;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";

const char* kVolumeFs = R"(
in vec3 vWorld;
flat in vec4 vBoxMin, vBoxMax, vShape, vShape2;
uniform vec3 uCamPos, uCamForward;
uniform float uTime;
uniform sampler3D uNoise;  // fbm de 3 octavas precalculado, periódico cada 16 unidades
uniform sampler2D uSceneDepth;
uniform vec2 uFxSize;
uniform vec2 uNearFar;
out vec4 fragColor;

// Una lectura con filtrado trilineal sustituye a 3 octavas x 8 hashes calculados.
float fbm3(vec3 p) { return texture(uNoise, p * (1.0 / 16.0)).r; }

// Densidad (x) y temperatura (y) del fuego en un punto del mundo.
vec2 fireAt(vec3 w) {
    vec3 p = w - vShape.xyz;
    float R = vShape.w, seed = vBoxMin.w;
    if (vShape2.y < 0.5) {
        // Columna: en cada punto hay fuego si el ruido supera un umbral que crece con la
        // altura y hacia el borde. Abajo arde casi todo; arriba solo sobreviven las
        // "lenguas". El ruido está estirado en vertical para que sean alargadas.
        float h = p.y / vShape2.x;
        float d = length(p.xz) / R;
        if (h < 0.0 || h > 1.0 || d > 1.0) return vec2(0.0);
        vec3 q = vec3(p.x / R * 3.2, h * 1.5 - uTime * 1.9, p.z / R * 3.2) + seed;
        float n = clamp((fbm3(q) - 0.5) * 1.9 + 0.5, 0.0, 1.0);  // más contraste
        float dens = clamp((n - 0.28 - h * 0.75 - d * d * 0.25) * 4.0, 0.0, 1.0);
        dens *= smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.85, 1.0, d));
        return vec2(dens, clamp(dens * (1.0 - h * 0.8) * 0.9, 0.0, 1.0));
    }
    // Esfera (explosión): bola turbulenta que sube.
    float d = length(p) / R;
    if (d > 1.0) return vec2(0.0);
    float n = fbm3(p / R * 2.0 + vec3(0.0, -uTime * 1.6, 0.0) + seed) * 0.875;
    float dens = clamp((n * 1.3 - d * 1.1 + 0.3) * 4.0, 0.0, 1.0);
    return vec2(dens, clamp(dens * (1.25 - d * 0.6), 0.0, 1.0));
}

vec3 fireRamp(float t) {  // brasa → naranja → amarillo → núcleo casi blanco
    vec3 c = mix(vec3(0.5, 0.05, 0.0), vec3(1.0, 0.33, 0.02), smoothstep(0.05, 0.35, t));
    c = mix(c, vec3(1.0, 0.7, 0.18), smoothstep(0.5, 0.85, t));
    return mix(c, vec3(1.0, 0.95, 0.75), smoothstep(0.93, 1.0, t));
}

float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}

void main() {
    vec3 ro = uCamPos, rd = normalize(vWorld - uCamPos);
    // Entrada y salida del rayo en la caja (método de los slabs).
    vec3 inv = 1.0 / rd;
    vec3 t0 = (vBoxMin.xyz - ro) * inv, t1 = (vBoxMax.xyz - ro) * inv;
    vec3 tMin = min(t0, t1), tMax = max(t0, t1);
    float tNear = max(max(max(tMin.x, tMin.y), tMin.z), 0.0);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);

    // Saltar el espacio vacío: la forma real (cilindro o esfera) es más estrecha que la
    // caja, así que el rayo se recorta también a ella.
    vec3 o = ro - vShape.xyz;
    float R = vShape.w;
    if (vShape2.y < 0.5) {
        float qa = dot(rd.xz, rd.xz), qb = dot(o.xz, rd.xz), qc = dot(o.xz, o.xz) - R * R;
        if (qa > 1e-6) {
            float disc = qb * qb - qa * qc;
            if (disc <= 0.0) discard;
            float sq = sqrt(disc);
            tNear = max(tNear, (-qb - sq) / qa);
            tFar = min(tFar, (-qb + sq) / qa);
        } else if (qc > 0.0) discard;
    } else {
        float qb = dot(o, rd), qc = dot(o, o) - R * R, disc = qb * qb - qc;
        if (disc <= 0.0) discard;
        float sq = sqrt(disc);
        tNear = max(tNear, -qb - sq);
        tFar = min(tFar, -qb + sq);
    }
    // Lo que tapa la escena (suelo, personajes) corta el rayo.
    float scene = linearDepth(texture(uSceneDepth, gl_FragCoord.xy / uFxSize).r);
    tFar = min(tFar, scene / max(dot(rd, uCamForward), 1e-3));
    if (tFar <= tNear) discard;

    // Pasos proporcionales al tramo recorrido: un rayo que roza el borde da pocos pasos.
    int steps = clamp(int(ceil((tFar - tNear) / (R * 0.12))), 4, 22);
    float dt = (tFar - tNear) / float(steps);
    // Desfase por píxel (ruido de gradiente intercalado, que reparte el grano de forma
    // más uniforme que un hash): cambia el bandeado de los pasos por un grano fino.
    vec2 px = gl_FragCoord.xy + fract(uTime * 7.0) * vec2(47.0, 17.0);
    float t = tNear + dt * fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
    float k = dt / R * 2.2;  // densidad óptica relativa al tamaño del fuego
    vec3 color = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < steps; ++i) {
        vec2 s = fireAt(ro + rd * t);
        if (s.x > 0.001) {
            float a = s.x * k;
            color += fireRamp(s.y) * (0.35 + s.y) * a * (1.0 - alpha);
            alpha += a * 0.35 * (1.0 - alpha);
            if (alpha > 0.95) break;
        }
        t += dt;
    }
    float intensity = vBoxMax.w;
    float fog = 1.0 - exp(-pow(tNear * kFogDensity, 2.0));
    color *= intensity * (1.0 - fog);
    alpha *= min(intensity, 1.0) * (1.0 - fog);
    fragColor = vec4(color, alpha);  // alfa premultiplicado: el fuego suma luz
}
)";

// Composición: el buffer de efectos (media resolución) se escala con filtrado bilineal
// sobre la imagen final.
const char* kCompositeVs = R"(
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kCompositeFs = R"(
in vec2 vUv;
uniform sampler2D uFx;
out vec4 fragColor;
void main() { fragColor = texture(uFx, vUv); }
)";

// Ruido de valor 3D periódico (para que la textura se repita sin costuras).
float latticeHash(int x, int y, int z, int period) {
    uint32_t h = uint32_t(x & (period - 1)) * 73856093u ^ uint32_t(y & (period - 1)) * 19349663u ^
                 uint32_t(z & (period - 1)) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFu) / 65535.0f;
}

// fbm de 3 octavas en una textura de kNoiseSize³ que cubre 16 celdas de la primera octava.
constexpr int kNoiseSize = 128;
std::vector<uint16_t> makeNoiseVolume() {
    const int n = kNoiseSize;
    std::vector<float> acc(size_t(n) * n * n, 0.0f);
    float amp = 0.5f;
    for (int period = 16; period <= 64; period *= 2, amp *= 0.5f) {
        const int cell = n / period;  // texels por celda
        std::vector<float> lattice(size_t(period) * period * period);
        for (int z = 0; z < period; ++z)
            for (int y = 0; y < period; ++y)
                for (int x = 0; x < period; ++x)
                    lattice[(size_t(z) * period + y) * period + x] = latticeHash(x, y, z, period);
        auto L = [&](int x, int y, int z) {
            x &= period - 1; y &= period - 1; z &= period - 1;
            return lattice[(size_t(z) * period + y) * period + x];
        };
        for (int z = 0; z < n; ++z)
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    int ix = x / cell, iy = y / cell, iz = z / cell;
                    auto fade = [&](int v) { float f = float(v % cell) / float(cell); return f * f * (3 - 2 * f); };
                    float fx = fade(x), fy = fade(y), fz = fade(z);
                    float v = lerpf(lerpf(lerpf(L(ix, iy, iz), L(ix + 1, iy, iz), fx),
                                          lerpf(L(ix, iy + 1, iz), L(ix + 1, iy + 1, iz), fx), fy),
                                    lerpf(lerpf(L(ix, iy, iz + 1), L(ix + 1, iy, iz + 1), fx),
                                          lerpf(L(ix, iy + 1, iz + 1), L(ix + 1, iy + 1, iz + 1), fx), fy),
                                    fz);
                    acc[(size_t(z) * n + y) * n + x] += amp * v;
                }
    }
    std::vector<uint16_t> out(acc.size());
    for (size_t i = 0; i < acc.size(); ++i) out[i] = uint16_t(clampf(acc[i] / 0.875f, 0, 1) * 65535.0f + 0.5f);
    return out;
}

GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error compilando shader:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs, bool common) {
    std::string header = "#version 330 core\n";
    GLuint v = compile(GL_VERTEX_SHADER, header + (common ? kCommonGlsl : "") + vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, header + (common ? kCommonGlsl : "") + fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error enlazando programa:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void setVec3(GLuint prog, const char* name, Vec3 v) { glUniform3f(glGetUniformLocation(prog, name), v.x, v.y, v.z); }
void setMat4(GLuint prog, const char* name, const Mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, m.m);
}

struct CubeVertex {
    float p[3], n[3];
};


}  // namespace

bool Renderer::init() {
    meshProgram_ = link(kMeshVs, kMeshFs, true);
    skinProgram_ = link(kSkinVs, kMeshFs, true);
    skinShadowProgram_ = link(kSkinShadowVs, kShadowFs, false);
    if (!skinProgram_ || !skinShadowProgram_) return false;
    shadowProgram_ = link(kShadowVs, kShadowFs, false);
    skyProgram_ = link(kSkyVs, kSkyFs, true);
    fxProgram_ = link(kSmokeVs, kSmokeFs, true);
    volumeProgram_ = link(kVolumeVs, kVolumeFs, true);
    compositeProgram_ = link(kCompositeVs, kCompositeFs, false);
    if (!meshProgram_ || !shadowProgram_ || !skyProgram_ || !fxProgram_ || !volumeProgram_ || !compositeProgram_)
        return false;

    // Textura 3D de ruido (R16, 4 MB), generada una vez al arrancar.
    std::vector<uint16_t> noise = makeNoiseVolume();
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_3D, noiseTex_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16, kNoiseSize, kNoiseSize, kNoiseSize, 0, GL_RED, GL_UNSIGNED_SHORT,
                 noise.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_3D, wrap, GL_REPEAT);

    // Cubo unidad (-0.5..0.5) con normales por cara. Para cada cara, u × v = normal, así
    // los triángulos quedan en sentido antihorario vistos desde fuera (para el culling).
    std::vector<CubeVertex> verts;
    const Vec3 faces[6][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (const auto& f : faces)
        for (const auto& c : corners) {
            Vec3 p = (f[0] + f[1] * c[0] + f[2] * c[1]) * 0.5f;
            verts.push_back({{p.x, p.y, p.z}, {f[0].x, f[0].y, f[0].z}});
        }

    // Esfera de radio 0,5 (mismo tamaño que el cubo unidad: la escala de la instancia da
    // directamente los ejes del elipsoide). 12 meridianos x 8 paralelos = 168 triángulos,
    // con normales suaves (la del punto de la esfera).
    const GLint sphereFirst = GLint(verts.size());
    const int kSlices = 12, kStacks = 8;
    auto spherePoint = [&](int stack, int slice) {
        float theta = kPi * float(stack) / float(kStacks), phi = 2.0f * kPi * float(slice) / float(kSlices);
        Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
        return CubeVertex{{n.x * 0.5f, n.y * 0.5f, n.z * 0.5f}, {n.x, n.y, n.z}};
    };
    for (int st = 0; st < kStacks; ++st)
        for (int sl = 0; sl < kSlices; ++sl) {
            CubeVertex a = spherePoint(st, sl), b = spherePoint(st + 1, sl);
            CubeVertex c = spherePoint(st + 1, sl + 1), d = spherePoint(st, sl + 1);
            // Antihorario visto desde fuera; en los polos uno de los triángulos degenera.
            if (st > 0) verts.insert(verts.end(), {a, d, c});
            if (st < kStacks - 1) verts.insert(verts.end(), {a, c, b});
        }
    const GLsizei sphereCount = GLsizei(verts.size()) - sphereFirst;

    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &cubeVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(CubeVertex), verts.data(), GL_STATIC_DRAW);
    setupBatch(boxes_, 0, 36);
    setupBatch(highlightBoxes_, 0, 36);
    setupBatch(spheres_, sphereFirst, sphereCount);
    setupBatch(highlightSpheres_, sphereFirst, sphereCount);

    glGenVertexArrays(1, &fxVao_);
    glGenBuffers(1, &fxVbo_);
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(FxInstance), (void*)offsetof(FxInstance, pos));
    glVertexAttribDivisor(0, 1);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FxInstance), (void*)offsetof(FxInstance, size));
    glVertexAttribDivisor(1, 1);

    // Volúmenes de fuego: el mismo cubo + 4 vec4 por instancia.
    glGenVertexArrays(1, &volumeVao_);
    glGenBuffers(1, &volumeVbo_);
    glBindVertexArray(volumeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, p));
    glBindBuffer(GL_ARRAY_BUFFER, volumeVbo_);
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(VolumeInstance), (void*)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(1 + i, 1);
    }
    glBindVertexArray(0);

    // Mapa de sombras: textura de profundidad con comparación por hardware.
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1, 1, 1, 1};  // fuera del mapa: iluminado
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        std::fprintf(stderr, "El framebuffer de sombras no está completo\n");
        return false;
    }
    boxes_.items.reserve(512);
    spheres_.items.reserve(512);
    return true;
}

void Renderer::shutdown() {
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTex_);
    glDeleteBuffers(1, &cubeVbo_);
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) {
        glDeleteBuffers(1, &b->vbo);
        glDeleteVertexArrays(1, &b->vao);
    }
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteProgram(meshProgram_);
    glDeleteProgram(skinProgram_);
    glDeleteProgram(skinShadowProgram_);
    for (SkinMesh& m : skinMeshes_) {
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ebo);
        glDeleteVertexArrays(1, &m.vao);
    }
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(fxProgram_);
    glDeleteBuffers(1, &fxVbo_);
    glDeleteVertexArrays(1, &fxVao_);
    glDeleteProgram(volumeProgram_);
    glDeleteBuffers(1, &volumeVbo_);
    glDeleteVertexArrays(1, &volumeVao_);
    glDeleteProgram(compositeProgram_);
    glDeleteTextures(1, &noiseTex_);
    releaseFxTargets();
}

// Proyecta las cajas de humo y fuego y devuelve el rectángulo de píxeles que ocupan.
// Falso si no se ve ninguna. Si alguna cruza el plano de la cámara, toda la pantalla.
bool Renderer::fxScreenRect(const RenderCamera& cam, int width, int height, int& x0, int& y0, int& x1,
                            int& y1) const {
    Mat4 vp = cam.proj * cam.view;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool full = false;
    auto addBox = [&](Vec3 lo, Vec3 hi) {
        for (int i = 0; i < 8 && !full; ++i) {
            Vec3 p{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
            float cx = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
            float cy = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
            float cw = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
            if (cw < cam.zNear) { full = true; break; }
            minX = std::min(minX, cx / cw); maxX = std::max(maxX, cx / cw);
            minY = std::min(minY, cy / cw); maxY = std::max(maxY, cy / cw);
        }
    };
    for (const VolumeInstance& v : volumes_)
        addBox({v.boxMin[0], v.boxMin[1], v.boxMin[2]}, {v.boxMax[0], v.boxMax[1], v.boxMax[2]});
    for (const FxInstance& f : smoke_) {
        float r = f.size * 0.5f;
        addBox(Vec3{f.pos[0] - r, f.pos[1] - r, f.pos[2] - r}, Vec3{f.pos[0] + r, f.pos[1] + r, f.pos[2] + r});
    }
    if (full) { minX = minY = -1; maxX = maxY = 1; }
    minX = std::max(minX, -1.0f); minY = std::max(minY, -1.0f);
    maxX = std::min(maxX, 1.0f); maxY = std::min(maxY, 1.0f);
    if (minX >= maxX || minY >= maxY) return false;
    // A píxeles, redondeando a pares (el buffer de efectos tiene la mitad) y con margen.
    x0 = std::max(0, (int(std::floor((minX * 0.5f + 0.5f) * float(width))) - 2) & ~1);
    y0 = std::max(0, (int(std::floor((minY * 0.5f + 0.5f) * float(height))) - 2) & ~1);
    x1 = std::min(width, int(std::ceil((maxX * 0.5f + 0.5f) * float(width))) + 2);
    y1 = std::min(height, int(std::ceil((maxY * 0.5f + 0.5f) * float(height))) + 2);
    return x1 > x0 && y1 > y0;
}

void Renderer::releaseFxTargets() {
    glDeleteFramebuffers(1, &depthFbo_);
    glDeleteTextures(1, &depthTex_);
    glDeleteFramebuffers(1, &fxFbo_);
    glDeleteTextures(1, &fxTex_);
    depthFbo_ = depthTex_ = fxFbo_ = fxTex_ = 0;
    fxWidth_ = fxHeight_ = 0;
}

// Copia de la profundidad de la escena (resolución completa, sin MSAA) y buffer de color
// a media resolución para los efectos. Se recrean si cambia el tamaño de la ventana.
void Renderer::ensureFxTargets(int width, int height) {
    if (width == fullWidth_ && height == fullHeight_ && fxFbo_) return;
    releaseFxTargets();
    fullWidth_ = width;
    fullHeight_ = height;
    fxWidth_ = std::max(1, (width + 1) / 2);
    fxHeight_ = std::max(1, (height + 1) / 2);

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    // Mismo formato que el framebuffer de la ventana: lo exige glBlitFramebuffer.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &depthFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, depthFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glGenTextures(1, &fxTex_);
    glBindTexture(GL_TEXTURE_2D, fxTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fxWidth_, fxHeight_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fxFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fxFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fxTex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "El framebuffer de efectos no está completo\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setupBatch(Batch& b, GLint first, GLsizei vertexCount) {
    b.first = first;
    b.vertexCount = vertexCount;
    glGenVertexArrays(1, &b.vao);
    glGenBuffers(1, &b.vbo);
    glBindVertexArray(b.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, n));
    // Atributos por instancia (divisor 1): matriz (4 columnas), color y parámetros.
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              (void*)(offsetof(Instance, model) + i * 4 * sizeof(float)));
        glVertexAttribDivisor(2 + i, 1);
    }
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Instance), (void*)offsetof(Instance, color));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Instance), (void*)offsetof(Instance, material));
    glVertexAttribDivisor(7, 1);
    glBindVertexArray(0);
}

// Sube las instancias del frame. El buffer crece al doble cuando se queda corto y se
// "huérfana" cada frame (glBufferData con nullptr) para no esperar a la GPU.
void Renderer::uploadBatch(Batch& b) {
    if (b.items.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    if (b.items.size() > b.capacity) b.capacity = b.items.size() * 2;
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.capacity * sizeof(Instance)), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(b.items.size() * sizeof(Instance)), b.items.data());
}

void Renderer::drawBatch(const Batch& b) {
    if (b.items.empty()) return;
    glBindVertexArray(b.vao);
    glDrawArraysInstanced(GL_TRIANGLES, b.first, b.vertexCount, GLsizei(b.items.size()));
}

void Renderer::fireColumn(Vec3 base, float radius, float height, float seed, float intensity, Vec3 clipMin,
                          Vec3 clipMax) {
    if (radius <= 0 || height <= 0 || intensity <= 0) return;
    VolumeInstance v{};
    Vec3 lo{std::max(base.x - radius, clipMin.x), std::max(base.y, clipMin.y), std::max(base.z - radius, clipMin.z)};
    Vec3 hi{std::min(base.x + radius, clipMax.x), std::min(base.y + height, clipMax.y), std::min(base.z + radius, clipMax.z)};
    if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z) return;
    v.boxMin[0] = lo.x; v.boxMin[1] = lo.y; v.boxMin[2] = lo.z; v.seed = seed;
    v.boxMax[0] = hi.x; v.boxMax[1] = hi.y; v.boxMax[2] = hi.z; v.intensity = intensity;
    v.center[0] = base.x; v.center[1] = base.y; v.center[2] = base.z; v.radius = radius;
    v.height = height;
    v.kind = 0;
    volumes_.push_back(v);
}

void Renderer::fireBall(Vec3 center, float radius, float seed, float intensity) {
    if (radius <= 0 || intensity <= 0) return;
    VolumeInstance v{};
    v.boxMin[0] = center.x - radius; v.boxMin[1] = center.y - radius; v.boxMin[2] = center.z - radius;
    v.boxMax[0] = center.x + radius; v.boxMax[1] = center.y + radius; v.boxMax[2] = center.z + radius;
    v.seed = seed;
    v.intensity = intensity;
    v.center[0] = center.x; v.center[1] = center.y; v.center[2] = center.z; v.radius = radius;
    v.height = radius;
    v.kind = 1;
    volumes_.push_back(v);
}

void Renderer::beginHighlight(uint32_t color) {
    highlighting_ = true;
    highlightColor_ = color;
}

void Renderer::endHighlight() { highlighting_ = false; }

void Renderer::smoke(Vec3 center, float size, float seed, float opacity) {
    smoke_.push_back({{center.x, center.y, center.z}, seed, size, opacity, 0, 0});
}

void Renderer::light(Vec3 pos, Vec3 color, float radius) { lights_.push_back({pos, color, radius}); }

void Renderer::scorch(Vec3 pos, float radius, float strength) {
    if (int(scorches_.size() / 5) >= kMaxScorches) return;
    scorches_.insert(scorches_.end(), {pos.x, pos.y, pos.z, radius, strength});
}

void Renderer::addInstance(const Mat4& model, uint32_t color, Material m, float flash, bool sphere) {
    Instance inst;
    for (int i = 0; i < 16; ++i) inst.model[i] = model.m[i];
    inst.color = color;
    inst.material = float(int(m));
    inst.flash = flash;
    inst.shape = sphere ? 1.0f : 0.0f;
    Batch& b = sphere ? (highlighting_ ? highlightSpheres_ : spheres_) : (highlighting_ ? highlightBoxes_ : boxes_);
    b.items.push_back(inst);
}

void Renderer::box(const Mat4& transform, Vec3 center, Vec3 half, uint32_t color, Material m, float flash) {
    addInstance(transform * translate(center) * scale(half * 2.0f), color, m, flash, false);
}

void Renderer::ellipsoid(const Mat4& transform, Vec3 center, Vec3 radii, uint32_t color, Material m, float flash) {
    addInstance(transform * translate(center) * scale(radii * 2.0f), color, m, flash, true);
}

int Renderer::createSkinnedMesh(const std::vector<SkinVertex>& verts, const std::vector<uint32_t>& indices) {
    SkinMesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(SkinVertex)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    const GLsizei st = sizeof(SkinVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(SkinVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, st, (void*)offsetof(SkinVertex, joints));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(SkinVertex, weights));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, material));
    glBindVertexArray(0);
    m.count = GLsizei(indices.size());
    skinMeshes_.push_back(m);
    return int(skinMeshes_.size()) - 1;
}

void Renderer::skinned(int mesh, const Mat4* bones, int boneCount, float flash, uint32_t outline) {
    if (mesh < 0 || mesh >= int(skinMeshes_.size())) return;
    SkinDraw d;
    d.mesh = mesh;
    d.boneCount = std::min(boneCount, kMaxBones);
    for (int i = 0; i < d.boneCount; ++i)
        for (int k = 0; k < 16; ++k) d.bones[i * 16 + k] = bones[i].m[k];
    d.flash = flash;
    d.outline = outline;
    skinDraws_.push_back(d);
}

// Dibuja las mallas encoladas con el programa ya activo y sus uniforms de escena puestos.
// En la pasada de contorno: solo las que lo llevan, infladas y con color plano.
void Renderer::drawSkinned(GLuint prog, bool outlinePass) {
    GLint bonesLoc = glGetUniformLocation(prog, "uBones");
    GLint flashLoc = glGetUniformLocation(prog, "uFlash");
    GLint outlineLoc = glGetUniformLocation(prog, "uOutlineColor");
    for (const SkinDraw& d : skinDraws_) {
        if (outlinePass && !d.outline) continue;
        glUniformMatrix4fv(bonesLoc, d.boneCount, GL_FALSE, d.bones);
        if (flashLoc >= 0) glUniform1f(flashLoc, d.flash);
        if (outlinePass)
            glUniform4f(outlineLoc, float(d.outline & 0xFF) / 255.0f, float((d.outline >> 8) & 0xFF) / 255.0f,
                        float((d.outline >> 16) & 0xFF) / 255.0f, 1.0f);
        const SkinMesh& m = skinMeshes_[size_t(d.mesh)];
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
    }
}

void Renderer::render(const RenderCamera& cam, Vec3 shadowCenter, int width, int height, float time) {
    // Cámara de la luz: orientación fija y centro ajustado a la rejilla de texels, para
    // que las sombras no "tiemblen" al moverse el jugador.
    Mat4 lightView = lookAt(kSunDir * 100.0f, {0, 0, 0}, {0, 1, 0});
    Vec3 c = transformPoint(lightView, shadowCenter);
    float texel = 2.0f * kShadowExtent / float(kShadowSize);
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    Mat4 lightProj = ortho(c.x - kShadowExtent, c.x + kShadowExtent, c.y - kShadowExtent, c.y + kShadowExtent,
                           -c.z - 80.0f, -c.z + 80.0f);
    Mat4 lightViewProj = lightProj * lightView;

    // Las instancias se suben una vez y sirven para las dos pasadas.
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) uploadBatch(*b);
    bool highlight = !highlightBoxes_.items.empty() || !highlightSpheres_.items.empty();
    auto drawHighlighted = [&] {
        drawBatch(highlightBoxes_);
        drawBatch(highlightSpheres_);
    };

    // 1) Sombras: profundidad desde el sol. Se dibujan las caras traseras y con offset
    //    de polígono para evitar el "acné" (auto-sombreado por falta de precisión).
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    glUseProgram(shadowProgram_);
    setMat4(shadowProgram_, "uLightViewProj", lightViewProj);
    drawBatch(boxes_);
    drawBatch(spheres_);
    drawHighlighted();
    if (!skinDraws_.empty()) {
        glUseProgram(skinShadowProgram_);
        setMat4(skinShadowProgram_, "uLightViewProj", lightViewProj);
        drawSkinned(skinShadowProgram_, false);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    // 2) Cielo, detrás de todo (sin escribir profundidad).
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProgram_);
    setVec3(skyProgram_, "uCamForward", cam.forward);
    setVec3(skyProgram_, "uCamRight", cam.right);
    setVec3(skyProgram_, "uCamUp", cam.up);
    setVec3(skyProgram_, "uSunDir", kSunDir);
    glUniform1f(glGetUniformLocation(skyProgram_, "uTime"), time);
    float t = std::tan(cam.fovY * 0.5f);
    glUniform2f(glGetUniformLocation(skyProgram_, "uTanHalf"), t * cam.aspect, t);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 3) Escena.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // Luces: si hay más de las que admite el shader, se quedan las más intensas.
    std::sort(lights_.begin(), lights_.end(), [](const PointLight& a, const PointLight& b) {
        return a.color.x + a.color.y + a.color.z > b.color.x + b.color.y + b.color.z;
    });
    int lightCount = int(std::min<size_t>(lights_.size(), kMaxLights));
    float lightPos[kMaxLights * 4], lightColor[kMaxLights * 3];
    for (int i = 0; i < lightCount; ++i) {
        const PointLight& l = lights_[size_t(i)];
        float* p = lightPos + i * 4;
        float* col = lightColor + i * 3;
        p[0] = l.pos.x; p[1] = l.pos.y; p[2] = l.pos.z; p[3] = l.radius;
        col[0] = l.color.x; col[1] = l.color.y; col[2] = l.color.z;
    }
    int scorchCount = int(scorches_.size() / 5);
    float scorchPos[kMaxScorches * 4], scorchAmount[kMaxScorches];
    for (int i = 0; i < scorchCount; ++i) {
        for (int k = 0; k < 4; ++k) scorchPos[i * 4 + k] = scorches_[size_t(i) * 5 + size_t(k)];
        scorchAmount[i] = scorches_[size_t(i) * 5 + 4];
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    // Uniforms comunes a los dos programas de la escena (instancias y skinning).
    auto sceneUniforms = [&](GLuint prog) {
        glUseProgram(prog);
        setMat4(prog, "uViewProj", cam.proj * cam.view);
        setMat4(prog, "uLightViewProj", lightViewProj);
        setVec3(prog, "uSunDir", kSunDir);
        setVec3(prog, "uCameraPos", cam.position);
        glUniform1f(glGetUniformLocation(prog, "uTime"), time);
        glUniform1i(glGetUniformLocation(prog, "uShadowMap"), 0);
        glUniform1i(glGetUniformLocation(prog, "uLightCount"), lightCount);
        if (lightCount > 0) {
            glUniform4fv(glGetUniformLocation(prog, "uLightPos"), lightCount, lightPos);
            glUniform3fv(glGetUniformLocation(prog, "uLightColor"), lightCount, lightColor);
        }
        glUniform1i(glGetUniformLocation(prog, "uScorchCount"), scorchCount);
        if (scorchCount > 0) {
            glUniform4fv(glGetUniformLocation(prog, "uScorch"), scorchCount, scorchPos);
            glUniform1fv(glGetUniformLocation(prog, "uScorchAmount"), scorchCount, scorchAmount);
        }
        glUniform1f(glGetUniformLocation(prog, "uInflate"), 0.0f);
        glUniform4f(glGetUniformLocation(prog, "uOutlineColor"), 0, 0, 0, 0);
    };
    // Personajes: malla con skinning y, detrás, su contorno (la malla inflada con las
    // caras frontales descartadas: solo asoma el borde).
    if (!skinDraws_.empty()) {
        sceneUniforms(skinProgram_);
        drawSkinned(skinProgram_, false);
        glCullFace(GL_FRONT);
        glUniform1f(glGetUniformLocation(skinProgram_, "uInflate"), 0.011f);
        drawSkinned(skinProgram_, true);
        glCullFace(GL_BACK);
    }
    sceneUniforms(meshProgram_);
    drawBatch(boxes_);
    drawBatch(spheres_);

    // 4) Objetivo resaltado con el stencil buffer:
    //    a) se dibuja normal, escribiendo 1 en el stencil donde queda visible;
    //    b) se vuelve a dibujar inflado, con color plano, solo donde el stencil NO es 1:
    //       lo único que sobrevive es un borde alrededor de la silueta completa.
    if (highlight) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        drawHighlighted();

        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glDepthMask(GL_FALSE);
        float pulse = 0.5f + 0.5f * std::sin(time * 9.0f);  // la silueta "late"
        float r = float(highlightColor_ & 0xFF) / 255.0f, g = float((highlightColor_ >> 8) & 0xFF) / 255.0f,
              b = float((highlightColor_ >> 16) & 0xFF) / 255.0f;
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.03f + 0.015f * pulse);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), lerpf(r, 1.0f, 0.35f * pulse),
                    lerpf(g, 1.0f, 0.35f * pulse), lerpf(b, 1.0f, 0.35f * pulse), 1.0f);
        drawHighlighted();
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.0f);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), 0, 0, 0, 0);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glDisable(GL_STENCIL_TEST);
    }
    glDisable(GL_CULL_FACE);

    // 5) Efectos con alfa premultiplicado: primero el humo (oscurece) y después el fuego
    //    volumétrico (suma luz). Son caros por píxel y blandos, así que se dibujan a media
    //    resolución (4 veces menos píxeles) en un buffer aparte que luego se compone encima.
    //    Ese buffer no tiene profundidad propia: los shaders leen la de la escena.
    int sx0, sy0, sx1, sy1;  // rectángulo de pantalla que cubren los efectos
    if ((!smoke_.empty() || !volumes_.empty()) && fxScreenRect(cam, width, height, sx0, sy0, sx1, sy1)) {
        ensureFxTargets(width, height);
        // Todo el trabajo se limita a ese rectángulo: una mecha encendida no cuesta como
        // un incendio a pantalla completa. El del buffer pequeño lleva 2 texels de margen
        // para que el filtrado bilineal de la composición no lea restos de otro frame.
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, fxFbo_);
        glViewport(0, 0, fxWidth_, fxHeight_);
        int fx0 = std::max(sx0 / 2 - 2, 0), fy0 = std::max(sy0 / 2 - 2, 0);
        glScissor(fx0, fy0, std::min(sx1 / 2 + 3, fxWidth_) - fx0, std::min(sy1 / 2 + 3, fxHeight_) - fy0);
        const float transparent[4] = {0, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, transparent);  // sin tocar el color de borrado global
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        auto fxUniforms = [&](GLuint prog) {
            glUniform1i(glGetUniformLocation(prog, "uSceneDepth"), 1);
            glUniform2f(glGetUniformLocation(prog, "uFxSize"), float(fxWidth_), float(fxHeight_));
            glUniform2f(glGetUniformLocation(prog, "uNearFar"), cam.zNear, cam.zFar);
            glUniform1f(glGetUniformLocation(prog, "uTime"), time);
            setVec3(prog, "uCamPos", cam.position);
        };
        if (!smoke_.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
            glBufferData(GL_ARRAY_BUFFER, smoke_.size() * sizeof(FxInstance), smoke_.data(), GL_STREAM_DRAW);
            glUseProgram(fxProgram_);
            fxUniforms(fxProgram_);
            setMat4(fxProgram_, "uViewProj", cam.proj * cam.view);
            setVec3(fxProgram_, "uCamRight", cam.right);
            setVec3(fxProgram_, "uCamUp", cam.up);
            glBindVertexArray(fxVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(smoke_.size()));
        }
        if (!volumes_.empty()) {
            // Se dibujan las caras traseras de cada caja: así se cubre bien tanto si la
            // cámara está fuera como dentro del volumen (el shader calcula la entrada).
            glUseProgram(volumeProgram_);
            fxUniforms(volumeProgram_);
            setMat4(volumeProgram_, "uViewProj", cam.proj * cam.view);
            setVec3(volumeProgram_, "uCamForward", cam.forward);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_3D, noiseTex_);
            glUniform1i(glGetUniformLocation(volumeProgram_, "uNoise"), 2);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glBindBuffer(GL_ARRAY_BUFFER, volumeVbo_);
            glBufferData(GL_ARRAY_BUFFER, volumes_.size() * sizeof(VolumeInstance), volumes_.data(), GL_STREAM_DRAW);
            glBindVertexArray(volumeVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, GLsizei(volumes_.size()));
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
        }
        glActiveTexture(GL_TEXTURE0);

        // Composición sobre la imagen de la ventana.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
        glUseProgram(compositeProgram_);
        glBindTexture(GL_TEXTURE_2D, fxTex_);
        glUniform1i(glGetUniformLocation(compositeProgram_, "uFx"), 0);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    glBindVertexArray(0);
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) b->items.clear();
    skinDraws_.clear();
    smoke_.clear();
    volumes_.clear();
    lights_.clear();
    scorches_.clear();
}
