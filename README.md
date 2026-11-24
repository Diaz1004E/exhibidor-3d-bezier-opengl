# Exhibidor 3D Interactivo con Curvas de Bézier e Iluminación Phong

Proyecto desarrollado en C++ y OpenGL moderno (Core Profile 3.3) como entrega de parcial de Computación Gráfica.

## Datos del Estudiante
* **Nombre:** [EDWIN FELIPE DIAZ SANTANA]
* **Código:** [6014383]
* **Video Explicativo (YouTube):** [Pega aquí el enlace de tu video]

---

## Arquitectura del Software

El proyecto sigue una arquitectura modular orientada a gráficos en tiempo real, separando la gestión de ventana/entrada, la carga de activos 3D, la lógica matemática de animación y la interfaz de usuario:

```text
opengl/
├── main.cpp              # Bucle principal, escena, cámara, animación Bézier e interfaz ImGui
├── Origin.h              # Renderizado de los ejes coordenados de referencia (X, Y, Z)
├── shaders/
│   ├── vertex.vert       # Transformación de vértices (MVP) y cálculo de normales en espacio mundo
│   └── fragment.frag     # Modelo de iluminación Phong (Ambiental, Difusa y Especular) + texturas
├── modelos/
│   └── scene.gltf        # Modelo 3D del vehículo importado con materiales y texturas
├── texturas/
│   ├── verde.jpg         # Textura del mostrador base
│   └── asfalto.jpg       # Textura de la plataforma giratoria
└── libraries/
    ├── stb_image.h       # Decodificación de imágenes y texturas 2D
    ├── tiny_gltf_v3.h    # Parser y cargador de escenas/mallas en formato glTF 2.0
    └── imgui/            # Librería Dear ImGui + backends (GLFW y OpenGL 3)