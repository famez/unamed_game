// El mar: transparente donde hay poco fondo (se ve la arena y lo que hay en él) y cada vez
// más opaco y oscuro con la profundidad; refleja el cielo según el ángulo (Fresnel) y el sol
// en destellos; espuma en la orilla y en las crestas. La refracción se finge torciendo el
// color del fondo con la normal de las ondas pequeñas.
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightPos;
in float vCrest;
uniform vec3 uSunDir;
uniform vec3 uCameraPos;
uniform float uTime;
uniform float uWorldColor;
uniform float uSeaLevel;
uniform float uWaves;
uniform sampler2D uFloor;
uniform vec4 uFloorRect;
uniform sampler2DShadow uShadowMap;
uniform float uDarkness;
uniform int uLitCount;
uniform vec4 uLit[24];
out vec4 fragColor;

float floorAt(vec2 xz) {
    vec2 uv = (xz - uFloorRect.xy) / uFloorRect.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return -40.0;
    return texture(uFloor, uv).r;
}

float sunShadow() {
    vec3 p = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) return 1.0;
    return texture(uShadowMap, vec3(p.xy, p.z - 0.002));
}

// Ondas pequeñas: la pendiente de dos capas de ruido que se mueven en sentidos distintos.
vec2 ripple(vec2 p) {
    float e = 0.15;
    vec2 a = p * 0.9 + vec2(uTime * 0.35, uTime * 0.2), b = p * 1.7 - vec2(uTime * 0.25, -uTime * 0.4);
    float h0 = noise(a) + 0.5 * noise(b);
    float hx = noise(a + vec2(e, 0.0)) + 0.5 * noise(b + vec2(e, 0.0));
    float hz = noise(a + vec2(0.0, e)) + 0.5 * noise(b + vec2(0.0, e));
    return vec2(hx - h0, hz - h0) / e;
}

void main() {
    float bed = floorAt(vWorldPos.xz);
    float depth = vWorldPos.y - bed;
    if (depth < -0.05) discard;  // bajo tierra (la rejilla pasa por debajo de la playa)
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec2 r = ripple(vWorldPos.xz) * 0.12;
    vec3 n = normalize(vNormal + vec3(r.x, 0.0, r.y));
    float dist = length(uCameraPos - vWorldPos);
    n = normalize(mix(n, vec3(0.0, 1.0, 0.0), smoothstep(60.0, 260.0, dist) * 0.7));  // lejos, más liso
    float sat = mix(0.12, 1.0, uWorldColor);
    // Color del agua según lo que haya debajo: turquesa en lo poco hondo, azul oscuro en lo hondo.
    vec3 shallow = mix(vec3(0.20, 0.30, 0.31), vec3(0.10, 0.52, 0.52), sat);
    vec3 deep = mix(vec3(0.05, 0.07, 0.09), vec3(0.015, 0.09, 0.18), sat);
    float k = 1.0 - exp(-max(depth, 0.0) * 0.45);
    vec3 water = mix(shallow, deep, k);
    float sh = mix(0.45, 1.0, sunShadow());
    // Luz del sol que entra y se esparce dentro (las crestas, al trasluz, verdean).
    float sss = pow(max(dot(V, -uSunDir), 0.0), 3.0) * vCrest * 0.6;
    water = water * (0.55 + 0.6 * max(dot(n, uSunDir), 0.0) * sh) + mix(vec3(0.1), vec3(0.05, 0.35, 0.3), sat) * sss;
    // Reflejo: el cielo hacia donde rebota la vista (Fresnel de Schlick) y el sol en destellos.
    vec3 R = reflect(-V, n);
    vec3 sky = mix(kHorizon, kZenith, pow(clamp(R.y, 0.0, 1.0), 0.6)) * 1.25;
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 color = mix(water, sky, clamp(fres, 0.0, 1.0));
    float glint = pow(max(dot(R, uSunDir), 0.0), 220.0) * 6.0 + pow(max(dot(R, uSunDir), 0.0), 24.0) * 0.25;
    color += kSunColor * glint * sh;
    // Espuma: en la orilla (con un borde que va y viene) y en las crestas más altas.
    float shoreFoam = 1.0 - smoothstep(0.0, 0.35 + 0.2 * sin(uTime * 1.3 + vWorldPos.x * 0.15), depth);
    float foamNoise = noise(vWorldPos.xz * 2.2 + vec2(uTime * 0.4, 0.0)) * noise(vWorldPos.xz * 5.0 - uTime * 0.3);
    float foam = clamp(shoreFoam * smoothstep(0.08, 0.3, foamNoise + shoreFoam * 0.3) + smoothstep(0.62, 0.9, vCrest) * foamNoise * 1.4, 0.0, 1.0);
    color = mix(color, mix(vec3(0.6), vec3(0.85, 0.9, 0.92), sat) * (0.6 + 0.4 * sh), foam * 0.85);
    // Transparencia: se ve el fondo donde es poco hondo y se mira desde arriba.
    float alpha = clamp(mix(0.25, 1.0, k) + fres * 0.5 + foam, 0.0, 1.0);
    alpha *= smoothstep(-0.05, 0.08, depth);
    // Oscuridad (en modo sombra): como el resto del escenario.
    if (uDarkness > 0.0) {
        float lit = 0.0;
        for (int i = 0; i < uLitCount; ++i) {
            float dy = uLit[i].y - vWorldPos.y;
            if (dy < -1.0 || dy > 9.0) continue;
            lit = max(lit, 1.0 - smoothstep(uLit[i].w * 0.6, uLit[i].w, length(vWorldPos.xz - uLit[i].xz)));
        }
        color = mix(color, color * vec3(0.10, 0.08, 0.17), (1.0 - lit) * uDarkness);
    }
    float fog = 1.0 - exp(-pow(dist * kWorldFog, 2.0));
    vec3 fogColor = mix(vec3(dot(kHorizon, vec3(0.299, 0.587, 0.114))), kHorizon, mix(0.15, 1.0, uWorldColor));
    color = mix(color, fogColor, fog);
    alpha = mix(alpha, 1.0, fog);
    fragColor = vec4(finish(color), alpha);
}
