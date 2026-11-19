// El mar: una rejilla alrededor de la cámara (más densa cerca) que el vértice levanta con olas
// de Gerstner (cada punto da una vuelta pequeña: crestas afiladas y valles anchos). Las olas
// vienen de mar adentro hacia la orilla y se calman donde el fondo está cerca (uFloor: el
// relieve como textura de alturas).
layout(location = 0) in vec2 aGrid;  // -1..1
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform vec3 uCameraPos;
uniform float uTime;
uniform float uSeaLevel;
uniform float uWaves;      // altura de las olas (m)
uniform float uReach;      // hasta dónde llega la rejilla desde la cámara (m)
uniform sampler2D uFloor;  // altura del relieve
uniform vec4 uFloorRect;   // x0, z0, ancho, fondo
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightPos;
out float vCrest;

float floorAt(vec2 xz) {
    vec2 uv = (xz - uFloorRect.xy) / uFloorRect.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return -40.0;
    return texture(uFloor, uv).r;
}

// Cuatro trenes de olas: dirección, longitud de onda, altura relativa y empinamiento.
const vec4 kWave[4] = vec4[4](vec4(0.12, 1.0, 14.0, 0.55), vec4(-0.35, 0.94, 9.0, 0.30), vec4(0.55, 0.83, 6.0, 0.18),
                               vec4(-0.1, 0.99, 3.6, 0.08));

void gerstner(vec2 p, float amp, out vec3 offset, out vec3 normal, out float crest) {
    offset = vec3(0.0);
    vec3 dx = vec3(1.0, 0.0, 0.0), dz = vec3(0.0, 0.0, 1.0);
    crest = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 d = normalize(kWave[i].xy);
        float k = 6.2831 / kWave[i].z;
        float c = sqrt(9.8 / k);
        float a = amp * kWave[i].w;
        float q = 0.7 / (k * a * 4.0 + 1e-4);  // empinamiento (sin que las crestas se crucen)
        float f = k * (dot(d, p) - c * uTime);
        float s = sin(f), co = cos(f);
        offset += vec3(q * a * d.x * co, a * s, q * a * d.y * co);
        dx += vec3(-q * d.x * d.x * k * a * s, d.x * k * a * co, -q * d.x * d.y * k * a * s);
        dz += vec3(-q * d.x * d.y * k * a * s, d.y * k * a * co, -q * d.y * d.y * k * a * s);
        crest += max(s, 0.0) * kWave[i].w;
    }
    normal = normalize(cross(dz, dx));
}

void main() {
    vec2 g = aGrid * (0.12 + 0.88 * abs(aGrid));  // más vértices cerca de la cámara
    vec2 xz = floor(uCameraPos.xz / 4.0) * 4.0 + g * uReach;
    float depth = uSeaLevel - floorAt(xz);
    // Lejos de la orilla, olas enteras; donde el fondo se acerca, más bajas (rompen en la arena).
    float amp = uWaves * clamp(0.25 + depth * 0.3, 0.0, 1.0) * (depth > -0.5 ? 1.0 : 0.0);
    vec3 off, nrm;
    float crest;
    gerstner(xz, amp, off, nrm, crest);
    vec3 world = vec3(xz.x, uSeaLevel, xz.y) + off;
    vWorldPos = world;
    vNormal = nrm;
    vCrest = crest * clamp(amp / max(uWaves, 1e-3), 0.0, 1.0);
    vLightPos = uLightViewProj * vec4(world, 1.0);
    gl_Position = uViewProj * vec4(world, 1.0);
}
