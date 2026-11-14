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
