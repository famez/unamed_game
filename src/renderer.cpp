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
const vec3 kSunColor = vec3(1.0, 0.93, 0.82) * 2.0;
const vec3 kFillColor = vec3(0.55, 0.62, 0.75) * 0.35;  // relleno frío opuesto al sol
const vec3 kZenith   = vec3(0.12, 0.26, 0.58);
const vec3 kHorizon  = vec3(0.58, 0.67, 0.78);
const vec3 kBounce   = vec3(0.22, 0.19, 0.15);
const float kFogDensity = 0.011;

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
layout(location = 7) in vec3 aParams; // x = material, y = destello
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;  // > 0: pasada de silueta, la caja crece estos metros por lado
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec2 vParams;
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
    vParams = aParams.xy;
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
flat in vec2 vParams;
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

    if (material == 3) {  // suelo: losas de 10 m alternas + líneas cada 1 m y 10 m
        vec2 w = vWorldPos.xz;
        float checker = mod(floor(w.x / 10.0) + floor(w.y / 10.0), 2.0);
        base *= mix(0.9, 1.06, checker);
        base *= 1.0 - 0.25 * gridLine(w);
        base *= 1.0 - 0.45 * gridLine(w / 10.0);
    }

    // Chamuscado: oscurece suelo y plataformas (caras de arriba) bajo los fuegos, con
    // borde irregular gracias al ruido.
    if ((material == 1 || material == 3) && n.y > 0.5) {
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
    if (material != 3) {
        vec3 d = (0.5 - abs(vLocal)) * vSize;
        float mid = d.x + d.y + d.z - min(d.x, min(d.y, d.z)) - max(d.x, max(d.y, d.z));
        float width = material == 1 ? 0.06 : 0.012;
        edge = mix(0.5, 1.0, smoothstep(0.0, width, mid));
    }

    vec3 V = normalize(uCameraPos - vWorldPos);
    float ndl = max(dot(n, uSunDir), 0.0);
    float shadow = shadowFactor(ndl);

    // Ambiente hemisférico (cielo arriba, rebote cálido del suelo abajo) + relleno suave
    // desde el lado contrario al sol, para que las caras en sombra no queden planas.
    vec3 skyAmbient = mix(kHorizon, kZenith, 0.35) * 1.3;
    vec3 ambient = mix(kBounce * 1.6, skyAmbient, n.y * 0.5 + 0.5) * 0.75;
    vec3 fillDir = normalize(vec3(-uSunDir.x, 0.3, -uSunDir.z));
    ambient += kFillColor * max(dot(n, fillDir), 0.0);
    float specPower = material == 2 ? 80.0 : 20.0;
    float specAmount = material == 2 ? 1.2 : (material == 3 ? 0.04 : 0.15);
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
        color += rim * kHorizon * 0.5;
    }
    color *= edge;
    color = mix(color, vec3(1.6), vParams.y);  // destello de impacto

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-pow(dist * kFogDensity, 2.0));
    color = mix(color, kHorizon, fog);
    fragColor = vec4(finish(color), 1.0);
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
out vec4 fragColor;
void main() {
    vec3 dir = normalize(uCamForward + vNdc.x * uTanHalf.x * uCamRight + vNdc.y * uTanHalf.y * uCamUp);
    vec3 color = mix(kHorizon, kZenith, smoothstep(0.0, 0.5, dir.y));
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
out vec4 fragColor;
void main() {
    // Mancha redonda con borde de ruido que se desplaza lentamente.
    float seed = vParams.x;
    float n = fbm(vUv * 1.8 + vec2(seed * 3.1, seed * 1.7 - uTime * 0.35));
    float a = (1.0 - smoothstep(0.25, 1.0, length(vUv) + (n - 0.5) * 0.7)) * vParams.y * (1.0 - vFog);
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
uniform vec3 uCamPos;
uniform float uTime;
out vec4 fragColor;

// Ruido de valor 3D (hash barato sin senos) y fbm de 3 octavas.
float hash3(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise3(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x),
                   mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x),
                   mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm3(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) { v += a * noise3(p); p = p * 2.02 + vec3(3.1, 1.7, 5.3); a *= 0.5; }
    return v;
}

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
        // fbm de 3 octavas da ~0,44 ± 0,12: se normaliza a 0..1 con más contraste.
        float n = clamp((fbm3(q) / 0.875 - 0.5) * 1.9 + 0.5, 0.0, 1.0);
        float dens = clamp((n - 0.28 - h * 0.75 - d * d * 0.25) * 4.0, 0.0, 1.0);
        dens *= smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.85, 1.0, d));
        return vec2(dens, clamp(dens * (1.0 - h * 0.8) * 0.9, 0.0, 1.0));
    }
    // Esfera (explosión): bola turbulenta que sube.
    float d = length(p) / R;
    if (d > 1.0) return vec2(0.0);
    float n = fbm3(p / R * 2.0 + vec3(0.0, -uTime * 1.6, 0.0) + seed);
    float dens = clamp((n * 1.3 - d * 1.1 + 0.3) * 4.0, 0.0, 1.0);
    return vec2(dens, clamp(dens * (1.25 - d * 0.6), 0.0, 1.0));
}

vec3 fireRamp(float t) {  // brasa → naranja → amarillo → núcleo casi blanco
    vec3 c = mix(vec3(0.5, 0.05, 0.0), vec3(1.0, 0.33, 0.02), smoothstep(0.05, 0.35, t));
    c = mix(c, vec3(1.0, 0.7, 0.18), smoothstep(0.5, 0.85, t));
    return mix(c, vec3(1.0, 0.95, 0.75), smoothstep(0.93, 1.0, t));
}

void main() {
    vec3 ro = uCamPos, rd = normalize(vWorld - uCamPos);
    // Entrada y salida del rayo en la caja (método de los slabs).
    vec3 inv = 1.0 / rd;
    vec3 t0 = (vBoxMin.xyz - ro) * inv, t1 = (vBoxMax.xyz - ro) * inv;
    vec3 tMin = min(t0, t1), tMax = max(t0, t1);
    float tNear = max(max(max(tMin.x, tMin.y), tMin.z), 0.0);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);
    if (tFar <= tNear) discard;

    const int kSteps = 22;
    float dt = (tFar - tNear) / float(kSteps);
    // Desfase aleatorio por píxel: cambia el bandeado de los pasos por un grano fino.
    float t = tNear + dt * hash3(vec3(gl_FragCoord.xy, fract(uTime) * 61.0));
    float k = dt / vShape.w * 2.2;  // densidad óptica relativa al tamaño del fuego
    vec3 color = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < kSteps; ++i) {
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

Mat4 scale(Vec3 s) {
    Mat4 r = Mat4::identity();
    r.m[0] = s.x;
    r.m[5] = s.y;
    r.m[10] = s.z;
    return r;
}

}  // namespace

bool Renderer::init() {
    meshProgram_ = link(kMeshVs, kMeshFs, true);
    shadowProgram_ = link(kShadowVs, kShadowFs, false);
    skyProgram_ = link(kSkyVs, kSkyFs, true);
    fxProgram_ = link(kSmokeVs, kSmokeFs, true);
    volumeProgram_ = link(kVolumeVs, kVolumeFs, true);
    if (!meshProgram_ || !shadowProgram_ || !skyProgram_ || !fxProgram_ || !volumeProgram_) return false;

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

    glGenVertexArrays(1, &vao_);
    glGenVertexArrays(1, &highlightVao_);
    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &cubeVbo_);
    glGenBuffers(1, &instanceVbo_);
    glGenBuffers(1, &highlightVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(CubeVertex), verts.data(), GL_STATIC_DRAW);
    setupMeshVao(vao_, instanceVbo_);
    setupMeshVao(highlightVao_, highlightVbo_);

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
    instances_.reserve(512);
    return true;
}

void Renderer::shutdown() {
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTex_);
    glDeleteBuffers(1, &cubeVbo_);
    glDeleteBuffers(1, &instanceVbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteProgram(meshProgram_);
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(fxProgram_);
    glDeleteBuffers(1, &fxVbo_);
    glDeleteVertexArrays(1, &fxVao_);
    glDeleteProgram(volumeProgram_);
    glDeleteBuffers(1, &volumeVbo_);
    glDeleteVertexArrays(1, &volumeVao_);
    glDeleteBuffers(1, &highlightVbo_);
    glDeleteVertexArrays(1, &highlightVao_);
}

void Renderer::setupMeshVao(GLuint vao, GLuint instanceVbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, n));
    // Atributos por instancia (divisor 1): matriz (4 columnas), color y parámetros.
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
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

void Renderer::box(const Mat4& transform, Vec3 center, Vec3 half, uint32_t color, Material m, float flash) {
    Mat4 model = transform * translate(center) * scale(half * 2.0f);
    Instance inst;
    for (int i = 0; i < 16; ++i) inst.model[i] = model.m[i];
    inst.color = color;
    inst.material = float(int(m));
    inst.flash = flash;
    inst.pad = 0;
    (highlighting_ ? highlighted_ : instances_).push_back(inst);
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
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    if (instances_.size() > instanceCapacity_) instanceCapacity_ = instances_.size() * 2;
    glBufferData(GL_ARRAY_BUFFER, instanceCapacity_ * sizeof(Instance), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instances_.size() * sizeof(Instance), instances_.data());
    GLsizei count = GLsizei(instances_.size());
    GLsizei highlightCount = GLsizei(highlighted_.size());
    if (highlightCount > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, highlightVbo_);
        glBufferData(GL_ARRAY_BUFFER, highlighted_.size() * sizeof(Instance), highlighted_.data(), GL_STREAM_DRAW);
    }

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
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
    if (highlightCount > 0) {
        glBindVertexArray(highlightVao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, highlightCount);
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
    float t = std::tan(cam.fovY * 0.5f);
    glUniform2f(glGetUniformLocation(skyProgram_, "uTanHalf"), t * cam.aspect, t);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 3) Escena.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(meshProgram_);
    setMat4(meshProgram_, "uViewProj", cam.proj * cam.view);
    setMat4(meshProgram_, "uLightViewProj", lightViewProj);
    setVec3(meshProgram_, "uSunDir", kSunDir);
    setVec3(meshProgram_, "uCameraPos", cam.position);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glUniform1i(glGetUniformLocation(meshProgram_, "uShadowMap"), 0);
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
    glUniform1i(glGetUniformLocation(meshProgram_, "uLightCount"), lightCount);
    if (lightCount > 0) {
        glUniform4fv(glGetUniformLocation(meshProgram_, "uLightPos"), lightCount, lightPos);
        glUniform3fv(glGetUniformLocation(meshProgram_, "uLightColor"), lightCount, lightColor);
    }
    int scorchCount = int(scorches_.size() / 5);
    float scorchPos[kMaxScorches * 4], scorchAmount[kMaxScorches];
    for (int i = 0; i < scorchCount; ++i) {
        for (int k = 0; k < 4; ++k) scorchPos[i * 4 + k] = scorches_[size_t(i) * 5 + size_t(k)];
        scorchAmount[i] = scorches_[size_t(i) * 5 + 4];
    }
    glUniform1i(glGetUniformLocation(meshProgram_, "uScorchCount"), scorchCount);
    if (scorchCount > 0) {
        glUniform4fv(glGetUniformLocation(meshProgram_, "uScorch"), scorchCount, scorchPos);
        glUniform1fv(glGetUniformLocation(meshProgram_, "uScorchAmount"), scorchCount, scorchAmount);
    }
    glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.0f);
    glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), 0, 0, 0, 0);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);

    // 4) Objetivo resaltado con el stencil buffer:
    //    a) se dibuja normal, escribiendo 1 en el stencil donde queda visible;
    //    b) se vuelve a dibujar inflado, con color plano, solo donde el stencil NO es 1:
    //       lo único que sobrevive es un borde alrededor de la silueta completa.
    if (highlightCount > 0) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glBindVertexArray(highlightVao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, highlightCount);

        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glDepthMask(GL_FALSE);
        float pulse = 0.5f + 0.5f * std::sin(time * 9.0f);  // la silueta "late"
        float r = float(highlightColor_ & 0xFF) / 255.0f, g = float((highlightColor_ >> 8) & 0xFF) / 255.0f,
              b = float((highlightColor_ >> 16) & 0xFF) / 255.0f;
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.03f + 0.015f * pulse);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), lerpf(r, 1.0f, 0.35f * pulse),
                    lerpf(g, 1.0f, 0.35f * pulse), lerpf(b, 1.0f, 0.35f * pulse), 1.0f);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, highlightCount);
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.0f);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), 0, 0, 0, 0);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glDisable(GL_STENCIL_TEST);
    }
    glDisable(GL_CULL_FACE);

    // 5) Efectos con alfa premultiplicado (se prueba la profundidad pero no se escribe):
    //    primero el humo (oscurece) y después el fuego volumétrico (suma luz).
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (!smoke_.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
        glBufferData(GL_ARRAY_BUFFER, smoke_.size() * sizeof(FxInstance), smoke_.data(), GL_STREAM_DRAW);
        glUseProgram(fxProgram_);
        setMat4(fxProgram_, "uViewProj", cam.proj * cam.view);
        setVec3(fxProgram_, "uCamRight", cam.right);
        setVec3(fxProgram_, "uCamUp", cam.up);
        setVec3(fxProgram_, "uCamPos", cam.position);
        glUniform1f(glGetUniformLocation(fxProgram_, "uTime"), time);
        glBindVertexArray(fxVao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(smoke_.size()));
    }
    if (!volumes_.empty()) {
        // Si la cámara está dentro de un volumen no se verían sus caras delanteras: esos
        // se dibujan por las traseras.
        std::vector<VolumeInstance> outside, inside;
        for (const VolumeInstance& v : volumes_) {
            const float m = 0.1f;
            bool in = cam.position.x > v.boxMin[0] - m && cam.position.x < v.boxMax[0] + m &&
                      cam.position.y > v.boxMin[1] - m && cam.position.y < v.boxMax[1] + m &&
                      cam.position.z > v.boxMin[2] - m && cam.position.z < v.boxMax[2] + m;
            (in ? inside : outside).push_back(v);
        }
        glUseProgram(volumeProgram_);
        setMat4(volumeProgram_, "uViewProj", cam.proj * cam.view);
        setVec3(volumeProgram_, "uCamPos", cam.position);
        glUniform1f(glGetUniformLocation(volumeProgram_, "uTime"), time);
        glEnable(GL_CULL_FACE);
        drawVolumes(outside, false);
        drawVolumes(inside, true);
        glDisable(GL_CULL_FACE);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    glBindVertexArray(0);
    instances_.clear();
    highlighted_.clear();
    smoke_.clear();
    volumes_.clear();
    lights_.clear();
    scorches_.clear();
}

void Renderer::drawVolumes(const std::vector<VolumeInstance>& list, bool cameraInside) {
    if (list.empty()) return;
    glCullFace(cameraInside ? GL_FRONT : GL_BACK);
    glBindBuffer(GL_ARRAY_BUFFER, volumeVbo_);
    glBufferData(GL_ARRAY_BUFFER, list.size() * sizeof(VolumeInstance), list.data(), GL_STREAM_DRAW);
    glBindVertexArray(volumeVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, GLsizei(list.size()));
}
