layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform mat4 uBones[20];
uniform mat4 uLightViewProj;
void main() {
    mat4 skin = uBones[int(aJoints.x)] * aWeights.x + uBones[int(aJoints.y)] * aWeights.y;
    gl_Position = uLightViewProj * skin * vec4(aPos, 1.0);
}
