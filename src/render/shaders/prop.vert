// Piezas del escenario (.glb de assets/props): una malla fija dibujada muchas veces de una
// vez (instancias), cada una con su matriz. Mismas salidas que el skinning: se reutiliza el
// fragment shader de la escena.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 5) in float aMaterial;
layout(location = 6) in mat4 aModel;  // ocupa 6, 7, 8 y 9
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform float uInflate;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
out vec3 vSize;
out vec4 vColor;
out vec4 vLightPos;
flat out vec3 vParams;
void main() {
    vec3 n = normalize(mat3(aModel) * aNormal);
    vec4 world = aModel * vec4(aPos, 1.0);
    world.xyz += n * uInflate;
    vWorldPos = world.xyz;
    vNormal = n;
    vLocal = vec3(0.0);
    vSize = vec3(1.0);
    vColor = aColor;
    vParams = vec3(aMaterial, 0.0, 1.0);  // forma "redondeada": sin bisel
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
