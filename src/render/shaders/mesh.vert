layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aModel;  // ocupa las posiciones 2..5
layout(location = 6) in vec4 aColor;
layout(location = 7) in vec3 aParams; // x = material, y = destello, z = forma (1 = elipsoide)
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;  // > 0: pasada de silueta, la caja crece estos metros por lado
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec3 vParams;
void main() {
    vec3 size = vec3(length(aModel[0].xyz), length(aModel[1].xyz), length(aModel[2].xyz));
    vec3 local = uInflate > 0.0 ? aPos * (size + 2.0 * uInflate) / size : aPos;
    vec4 world = aModel * vec4(local, 1.0);
    vWorldPos = world.xyz;
    // Matriz de normales para rotación * escala: R * S^-1 = M * S^-2.
    vNormal = mat3(aModel) * (aNormal / (size * size));
    vLocal = aPos;
    vSize = size;
    vColor = aColor;
    vParams = aParams;
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
