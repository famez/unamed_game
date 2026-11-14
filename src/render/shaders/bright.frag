// Bloom: extracción de lo brillante (color × máscara alfa) a media resolución, cadena de
// reducciones con filtro de 5 muestras y vuelta hacia arriba sumando cada nivel con un
// filtro de tienda (el "dual filter" de los motores actuales: barato y sin escalones).
in vec2 vUv;
uniform sampler2D uSrc;
uniform vec2 uTexel;  // tamaño de un texel de la fuente
out vec4 fragColor;
void main() {
    vec3 c = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * uTexel;
        vec4 s = texture(uSrc, vUv + o);
        c += s.rgb * s.a;
    }
    fragColor = vec4(c * 0.25, 1.0);
}
