// Skinning: cada vértice se mueve con la mezcla de las matrices de sus dos huesos. Las
// salidas son las mismas que las del shader de instancias, así se reutiliza su fragment
// shader (iluminación, sombras, contorno).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in float aMaterial;
uniform mat4 uBones[20];
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;
uniform float uFlash;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec3 vParams;
void main() {
    mat4 skin = uBones[int(aJoints.x)] * aWeights.x + uBones[int(aJoints.y)] * aWeights.y;
    vec3 n = normalize(mat3(skin) * aNormal);
    vec4 world = skin * vec4(aPos, 1.0);
    world.xyz += n * uInflate;
    vWorldPos = world.xyz;
    vNormal = n;
    vLocal = vec3(0.0);
    vSize = vec3(1.0);
    vColor = aColor;
    vParams = vec3(aMaterial, uFlash, 1.0);  // forma "redondeada": sin bisel
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
