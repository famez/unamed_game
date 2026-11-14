// Humo: quads mirando a cámara generados con gl_VertexID; los datos van por instancia.
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
