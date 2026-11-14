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
