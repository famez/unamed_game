layout(location = 0) in vec3 aPos;
layout(location = 6) in mat4 aModel;
uniform mat4 uLightViewProj;
void main() { gl_Position = uLightViewProj * aModel * vec4(aPos, 1.0); }
