// Fuego volumétrico. La geometría es solo la caja que lo contiene; el fragment shader
// lanza un rayo desde la cámara, calcula dónde entra y sale de la caja y la recorre en
// pasos acumulando la luz que emite el fuego en cada punto (raymarching).
layout(location = 0) in vec3 aPos;      // cubo unidad
layout(location = 1) in vec4 aBoxMin;   // xyz, semilla
layout(location = 2) in vec4 aBoxMax;   // xyz, intensidad
layout(location = 3) in vec4 aShape;    // centro xyz, radio
layout(location = 4) in vec4 aShape2;   // altura, tipo
uniform mat4 uViewProj;
out vec3 vWorld;
flat out vec4 vBoxMin, vBoxMax, vShape, vShape2;
void main() {
    vWorld = mix(aBoxMin.xyz, aBoxMax.xyz, aPos + 0.5);
    vBoxMin = aBoxMin; vBoxMax = aBoxMax; vShape = aShape; vShape2 = aShape2;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
