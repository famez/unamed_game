in vec2 vUv;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 fragColor;
void main() {
    vec3 c = vec3(0.0);
    c += texture(uSrc, vUv + vec2(-1.0, -1.0) * uTexel).rgb;
    c += texture(uSrc, vUv + vec2( 0.0, -1.0) * uTexel).rgb * 2.0;
    c += texture(uSrc, vUv + vec2( 1.0, -1.0) * uTexel).rgb;
    c += texture(uSrc, vUv + vec2(-1.0,  0.0) * uTexel).rgb * 2.0;
    c += texture(uSrc, vUv).rgb * 4.0;
    c += texture(uSrc, vUv + vec2( 1.0,  0.0) * uTexel).rgb * 2.0;
    c += texture(uSrc, vUv + vec2(-1.0,  1.0) * uTexel).rgb;
    c += texture(uSrc, vUv + vec2( 0.0,  1.0) * uTexel).rgb * 2.0;
    c += texture(uSrc, vUv + vec2( 1.0,  1.0) * uTexel).rgb;
    fragColor = vec4(c / 16.0, 1.0);
}
