in vec2 vNdc;
uniform vec3 uCamForward, uCamRight, uCamUp;
uniform vec2 uTanHalf;  // tan(fov/2) * aspect, tan(fov/2)
uniform vec3 uSunDir;
uniform float uTime;
uniform float uWorldColor;
out vec4 fragColor;
void main() {
    vec3 dir = normalize(uCamForward + vNdc.x * uTanHalf.x * uCamRight + vNdc.y * uTanHalf.y * uCamUp);
    vec3 color = mix(kHorizon, kZenith, smoothstep(0.0, 0.5, dir.y));
    color = mix(vec3(dot(color, vec3(0.299, 0.587, 0.114))), color, mix(0.1, 1.0, uWorldColor)) * mix(0.8, 1.0, uWorldColor);
    // Nubes: ruido sobre un plano alto, más densas hacia el horizonte.
    if (dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.15) * 1.6;
        // Sin croma, cielo cubierto: más nubes, grises y bajas.
        float c = smoothstep(mix(0.3, 0.5, uWorldColor), 0.75, fbm(uv + vec2(uTime * 0.01, 0.0)));
        vec3 cloud = mix(vec3(0.30, 0.31, 0.34), vec3(1.0, 0.98, 0.96) * 1.25, uWorldColor);
        color = mix(color, cloud, c * smoothstep(0.0, 0.15, dir.y) * 0.9);
    }
    if (uNight > 0.0 && dir.y > 0.0) {  // estrellas: puntos de una rejilla en la esfera, que titilan
        vec2 cell = floor(dir.xz / (dir.y + 0.3) * 90.0);
        float h = hash(cell);
        float tw = 0.6 + 0.4 * sin(uTime * 3.0 + h * 40.0);
        color += vec3(0.9, 0.92, 1.0) * step(0.985, h) * tw * smoothstep(0.05, 0.3, dir.y) * uNight * 1.5;
    }
    float sun = max(dot(dir, uSunDir), 0.0);
    // Disco y halo (de noche, la luna: más pequeña y fría).
    color += kSunColor * (pow(sun, mix(600.0, 2000.0, uNight)) * mix(4.0, 30.0, uNight) + pow(sun, 12.0) * 0.08) *
             mix(0.05, 1.0, max(uWorldColor, uNight));
    fragColor = vec4(finish(color), clamp(pow(sun, 300.0) * 2.0, 0.0, 1.0));  // el sol brilla
}
