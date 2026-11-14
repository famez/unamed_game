// Composición final: escena + bloom, un poco más de saturación y viñeta suave.
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
out vec4 fragColor;
void main() {
    vec3 c = texture(uScene, vUv).rgb + texture(uBloom, vUv).rgb * uBloomStrength;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.12);
    vec2 d = vUv - 0.5;
    c *= 1.0 - 0.28 * dot(d, d);
    fragColor = vec4(c, 1.0);
}
