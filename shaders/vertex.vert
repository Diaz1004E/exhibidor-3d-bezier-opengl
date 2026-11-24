#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoord;

out vec4 ourColor;
out vec2 TexCoord;
out vec3 vWorldPos;   // posición en el mundo, para calcular la luz

uniform mat4 uMVP;
uniform mat4 uModel;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    ourColor = aColor;
    TexCoord = aTexCoord;
}
