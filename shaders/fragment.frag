#version 330 core

out vec4 FragColor;

in vec4 ourColor;
in vec2 TexCoord;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform bool uUseTexture;

// Dirección HACIA la luz (arriba, a la derecha y al frente). Cámbiala para mover la luz.
const vec3  LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.8));
const float AMBIENT   = 0.0;   // luz mínima en las zonas en sombra
const float DIFFUSE   = 1;   // fuerza de la luz direccional

void main() {
    vec4 base = uUseTexture ? texture(uTexture, TexCoord) * ourColor : ourColor;

    // Normal de cada cara calculada con derivadas (no hace falta que el modelo traiga normales).
    // Siempre apunta hacia la cámara, así que funciona por ambos lados.
    vec3 n = cross(dFdx(vWorldPos), dFdy(vWorldPos));

    float light = 1.0;  // si no hay superficie (p. ej. líneas de los ejes), no se aplica luz
    if (dot(n, n) > 1e-12) {
        light = AMBIENT + DIFFUSE * max(dot(normalize(n), LIGHT_DIR), 0.0);
    }

    FragColor = vec4(base.rgb * light, base.a);
}