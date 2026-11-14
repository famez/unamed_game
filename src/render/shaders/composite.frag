in vec2 vUv;
uniform sampler2D uFx;
out vec4 fragColor;
void main() { fragColor = texture(uFx, vUv); }
