#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

// ============================================================
// DEAR IMGUI (Ajusta la ruta si los tienes en otra carpeta)
// ============================================================
// Cambia estas dos líneas en tu main.cpp:
#include "libraries/imgui/imgui.h"
#include "libraries/imgui/backends/imgui_impl_glfw.h"
#include "libraries/imgui/backends/imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "libraries/stb_image.h"

#define TINYGLTF3_ENABLE_FS
#include "libraries/tiny_gltf_v3.h"

#include "Origin.h"

// ============================================================
// GEOMETRÍA BÁSICA
// ============================================================

struct Vertex {
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 uv;
};

struct Mesh {
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;
    GLuint textureID = 0;
    GLenum drawMode = GL_TRIANGLES;

    void setup(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, GLenum mode = GL_TRIANGLES) {
        drawMode = mode;
        indexCount = static_cast<GLsizei>(indices.size());
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
        for (int i = 0; i < 3; ++i) glEnableVertexAttribArray(i);
        glBindVertexArray(0);
    }

    void draw(GLint mvpLoc, GLint useTexLoc, GLint modelLoc, const glm::mat4& pv, const glm::mat4& model) const {
        bool useTex = textureID != 0;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(pv * model));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(useTexLoc, useTex ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glBindVertexArray(VAO);
        glDrawElements(drawMode, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void destroy() {
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (VBO) glDeleteBuffers(1, &VBO);
        if (EBO) glDeleteBuffers(1, &EBO);
        if (textureID) glDeleteTextures(1, &textureID);
    }
};

// ============================================================
// CURVA DE BÉZIER CÚBICA + DERIVADA (TANGENTE)
// ============================================================

glm::vec3 evaluateBezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    glm::vec3 p = uuu * p0;         // (1 - t)^3 * P0
    p += 3.0f * uu * t * p1;        // 3(1 - t)^2 * t * P1
    p += 3.0f * u * tt * p2;        // 3(1 - t) * t^2 * P2
    p += ttt * p3;                  // t^3 * P3
    return p;
}

// Derivada primera B'(t) para calcular hacia dónde apunta el carro en cada punto
glm::vec3 evaluateBezierTangent(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float u = 1.0f - t;
    glm::vec3 tangent = 3.0f * u * u * (p1 - p0)
        + 6.0f * u * t * (p2 - p1)
        + 3.0f * t * t * (p3 - p2);
    if (glm::length(tangent) > 0.0001f) {
        return glm::normalize(tangent);
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

Mesh createBezierPathMesh(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, int segments) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;
    verts.reserve(segments + 1);
    indices.reserve(segments + 1);

    // Degradado de color a lo largo de la curva para que resalte cada tramo
    glm::vec4 colorStart(1.0f, 0.2f, 0.2f, 1.0f); // Rojo cálido al inicio
    glm::vec4 colorEnd(0.1f, 0.95f, 1.0f, 1.0f);  // Cian brillante al final

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        glm::vec3 pos = evaluateBezier(p0, p1, p2, p3, t);
        glm::vec4 gradColor = glm::mix(colorStart, colorEnd, t);
        verts.push_back({ pos, gradColor, {0.0f, 0.0f} });
        indices.push_back(static_cast<unsigned int>(i));
    }

    Mesh pathMesh;
    pathMesh.setup(verts, indices, GL_LINE_STRIP);
    return pathMesh;
}

// ============================================================
// CÁMARA (MATRIZ DE VISTA / TRASLACIÓN EN 3 EJES)
// ============================================================

struct Camera {
    glm::vec3 pos = glm::vec3(0.0f, 1.8f, 5.2f);
    float speed = 3.0f;

    void update(GLFWwindow* window, float dt) {
        float vel = speed * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) pos.z -= vel;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) pos.z += vel;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) pos.x -= vel;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) pos.x += vel;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) pos.y += vel;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) pos.y -= vel;
    }

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(pos, glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

// ============================================================
// TEXTURAS Y SHADERS
// ============================================================

GLuint uploadTexture(const unsigned char* data, int w, int h, GLenum fmt) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    return id;
}

GLuint loadTexture(const char* filepath) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filepath, &w, &h, &ch, 0);
    if (!data) { std::cerr << "Error al cargar textura: " << filepath << std::endl; return 0; }
    GLuint id = uploadTexture(data, w, h, (ch == 4) ? GL_RGBA : GL_RGB);
    stbi_image_free(data);
    return id;
}

GLuint createShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    auto readFile = [](const std::string& p) {
        std::ifstream f(p);
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
        };
    std::string vCode = readFile(vertPath), fCode = readFile(fragPath);
    const char* vSrc = vCode.c_str(); const char* fSrc = fCode.c_str();

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);   glShaderSource(vs, 1, &vSrc, nullptr); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fSrc, nullptr); glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

// ============================================================
// GEOMETRÍA PROCEDURAL (MOSTRADOR, PLATAFORMA Y MARCADORES)
// ============================================================

Mesh createBox(float w, float h, float d, const glm::vec4& color, GLuint texID = 0) {
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    std::vector<Vertex> verts = {
        {{-hw, -hh,  hd}, color, {0, 0}}, {{ hw, -hh,  hd}, color, {1, 0}}, {{ hw,  hh,  hd}, color, {1, 1}}, {{-hw,  hh,  hd}, color, {0, 1}},
        {{ hw, -hh, -hd}, color, {0, 0}}, {{-hw, -hh, -hd}, color, {1, 0}}, {{-hw,  hh, -hd}, color, {1, 1}}, {{ hw,  hh, -hd}, color, {0, 1}},
        {{-hw,  hh,  hd}, color, {0, 0}}, {{ hw,  hh,  hd}, color, {1, 0}}, {{ hw,  hh, -hd}, color, {1, 1}}, {{-hw,  hh, -hd}, color, {0, 1}},
        {{-hw, -hh, -hd}, color, {0, 0}}, {{ hw, -hh, -hd}, color, {1, 0}}, {{ hw, -hh,  hd}, color, {1, 1}}, {{-hw, -hh,  hd}, color, {0, 1}},
        {{ hw, -hh,  hd}, color, {0, 0}}, {{ hw, -hh, -hd}, color, {1, 0}}, {{ hw,  hh, -hd}, color, {1, 1}}, {{ hw,  hh,  hd}, color, {0, 1}},
        {{-hw, -hh, -hd}, color, {0, 0}}, {{-hw, -hh,  hd}, color, {1, 0}}, {{-hw,  hh,  hd}, color, {1, 1}}, {{-hw,  hh, -hd}, color, {0, 1}}
    };
    std::vector<unsigned int> indices;
    for (unsigned int i = 0; i < 6; ++i) {
        unsigned int o = i * 4;
        indices.insert(indices.end(), { o, o + 1, o + 2, o, o + 2, o + 3 });
    }
    Mesh mesh; mesh.setup(verts, indices); mesh.textureID = texID;
    return mesh;
}

Mesh createCylinder(float radius, float height, int segments, const glm::vec4& color) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;
    float hh = height * 0.5f;

    unsigned int botCenter = 0; verts.push_back({ {0.0f, -hh, 0.0f}, color, {0.5f, 0.5f} });
    unsigned int topCenter = 1; verts.push_back({ {0.0f,  hh, 0.0f}, color, {0.5f, 0.5f} });

    for (int i = 0; i <= segments; ++i) {
        float angle = glm::two_pi<float>() * i / segments;
        float c = std::cos(angle), s = std::sin(angle);
        float u = (c + 1.0f) * 0.5f, v = (s + 1.0f) * 0.5f;
        verts.push_back({ {radius * c,  hh, radius * s}, color, {u, v} });
        verts.push_back({ {radius * c, -hh, radius * s}, color, {u, v} });
    }
    for (int i = 0; i < segments; ++i) {
        unsigned int topCurr = 2 + i * 2, topNext = topCurr + 2;
        indices.insert(indices.end(), { topCenter, topNext, topCurr });
        indices.insert(indices.end(), { botCenter, topCurr + 1, topNext + 1 });
    }

    unsigned int sideStart = static_cast<unsigned int>(verts.size());
    for (int i = 0; i <= segments; ++i) {
        float angle = glm::two_pi<float>() * i / segments;
        float x = radius * std::cos(angle), z = radius * std::sin(angle);
        float u = (float)i / segments;
        verts.push_back({ {x,  hh, z}, color, {u, 1.0f} });
        verts.push_back({ {x, -hh, z}, color, {u, 0.0f} });
    }
    for (int i = 0; i < segments; ++i) {
        unsigned int curr = sideStart + i * 2, next = curr + 2;
        indices.insert(indices.end(), { curr, curr + 1, next, next, curr + 1, next + 1 });
    }

    Mesh mesh; mesh.setup(verts, indices);
    return mesh;
}

// ============================================================
// CARGADOR MÍNIMO DE GLTF / GLB
// ============================================================

struct GLTFModel {
    std::vector<Mesh> meshes;
    glm::vec3 pivotOffset = glm::vec3(0.0f);
    float autoScale = 1.0f;

    bool load(const std::string& path) {
        stbi_set_flip_vertically_on_load(false);

        tg3_model model; tg3_error_stack errs; tg3_parse_options opts;
        tg3_error_stack_init(&errs); tg3_parse_options_init(&opts);
        opts.images_as_is = 0;
        opts.required_sections = TG3_REQUIRE_ALL;

        auto rc = tg3_parse_file(&model, &errs, path.c_str(), static_cast<uint32_t>(path.size()), &opts);
        if (rc != TG3_OK) {
            std::cerr << "[GLTF] tg3_parse_file fallo, codigo = " << (int)rc << "\n";
            tg3_error_stack_free(&errs);
            return false;
        }

        std::string baseDir;
        size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) baseDir = path.substr(0, slash + 1);

        auto uriDecode = [](const std::string& s) {
            std::string out;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
                    out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                }
                else out += s[i];
            }
            return out;
            };

        std::vector<GLuint> glTextures(model.images_count, 0);
        for (uint32_t i = 0; i < model.images_count; ++i) {
            const auto& img = model.images[i];
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = nullptr;

            if (img.uri.data != nullptr && img.uri.len > 0) {
                std::string uri(img.uri.data, img.uri.len);
                if (uri.rfind("data:", 0) != 0) {
                    std::string full = baseDir + uriDecode(uri);
                    pixels = stbi_load(full.c_str(), &w, &h, &ch, 4);
                    if (!pixels) std::cerr << "[IMG] no se pudo cargar: " << full << "\n";
                }
            }
            else if (img.buffer_view >= 0 && static_cast<uint32_t>(img.buffer_view) < model.buffer_views_count) {
                const auto& bv = model.buffer_views[img.buffer_view];
                const uint8_t* data = model.buffers[bv.buffer].data.data + bv.byte_offset;
                pixels = stbi_load_from_memory(data, static_cast<int>(bv.byte_length), &w, &h, &ch, 4);
            }
            if (pixels) {
                glTextures[i] = uploadTexture(pixels, w, h, GL_RGBA);
                stbi_image_free(pixels);
            }
        }

        glm::vec3 minP(1e9f), maxP(-1e9f);

        auto nodeLocal = [](const tg3_node& n) -> glm::mat4 {
            glm::mat4 M(1.0f);
            bool isIdentity = true;
            for (int i = 0; i < 16; ++i) {
                M[i / 4][i % 4] = static_cast<float>(n.matrix[i]);
                if (n.matrix[i] != ((i % 5 == 0) ? 1.0 : 0.0)) isIdentity = false;
            }
            if (!isIdentity) return M;

            glm::vec3 t((float)n.translation[0], (float)n.translation[1], (float)n.translation[2]);
            glm::quat q((float)n.rotation[3], (float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2]);
            glm::vec3 s((float)n.scale[0], (float)n.scale[1], (float)n.scale[2]);
            return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), s);
            };

        struct Src { const tg3_accessor* acc; const uint8_t* data; int stride; };
        auto source = [&](int idx, int minStride) {
            const auto& a = model.accessors[idx];
            const auto& v = model.buffer_views[a.buffer_view];
            return Src{ &a, model.buffers[v.buffer].data.data + v.byte_offset + a.byte_offset,
                        std::max(minStride, (int)tg3_accessor_byte_stride(&a, &v)) };
            };

        auto processMesh = [&](uint32_t m, const glm::mat4& world) {
            const tg3_mesh* mesh = &model.meshes[m];
            for (uint32_t p = 0; p < mesh->primitives_count; ++p) {
                const tg3_primitive* prim = &mesh->primitives[p];
                if (!prim || (prim->mode != -1 && prim->mode != TG3_MODE_TRIANGLES)) continue;

                int posIdx = -1, uvIdx = -1;
                for (uint32_t a = 0; a < prim->attributes_count; ++a) {
                    std::string key(prim->attributes[a].key.data, prim->attributes[a].key.len);
                    if (key == "POSITION") posIdx = prim->attributes[a].value;
                    else if (key == "TEXCOORD_0") uvIdx = prim->attributes[a].value;
                }
                if (posIdx < 0) continue;

                glm::vec4 baseColor(1.0f);
                GLuint texID = 0;
                if (prim->material >= 0 && static_cast<uint32_t>(prim->material) < model.materials_count) {
                    const auto& pbr = model.materials[prim->material].pbr_metallic_roughness;
                    const auto& f = pbr.base_color_factor;
                    baseColor = glm::vec4((float)f[0], (float)f[1], (float)f[2], (float)f[3]);
                    int texIndex = pbr.base_color_texture.index;
                    if (texIndex >= 0 && static_cast<uint32_t>(texIndex) < model.textures_count) {
                        int img = model.textures[texIndex].source;
                        if (img >= 0 && static_cast<uint32_t>(img) < glTextures.size()) texID = glTextures[img];
                    }
                }

                Src pos = source(posIdx, 12);
                std::vector<Vertex> verts(pos.acc->count);
                for (uint32_t i = 0; i < pos.acc->count; ++i) {
                    const float* ptr = reinterpret_cast<const float*>(pos.data + i * pos.stride);
                    verts[i].pos = glm::vec3(world * glm::vec4(ptr[0], ptr[1], ptr[2], 1.0f));
                    verts[i].color = baseColor;
                    verts[i].uv = glm::vec2(0.0f);
                    minP = glm::min(minP, verts[i].pos);
                    maxP = glm::max(maxP, verts[i].pos);
                }

                if (uvIdx >= 0) {
                    Src uv = source(uvIdx, 8);
                    for (uint32_t i = 0; i < std::min(pos.acc->count, uv.acc->count); ++i) {
                        const float* ptr = reinterpret_cast<const float*>(uv.data + i * uv.stride);
                        verts[i].uv = glm::vec2(ptr[0], ptr[1]);
                    }
                }

                std::vector<unsigned int> indices;
                if (prim->indices >= 0) {
                    int compSize = 1;
                    {
                        auto ct = model.accessors[prim->indices].component_type;
                        compSize = (ct == TG3_COMPONENT_TYPE_UNSIGNED_INT) ? 4 : (ct == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) ? 2 : 1;
                    }
                    Src idx = source(prim->indices, compSize);
                    for (uint32_t i = 0; i < idx.acc->count; ++i) {
                        const uint8_t* ptr = idx.data + i * idx.stride;
                        if (compSize == 2) indices.push_back(*reinterpret_cast<const uint16_t*>(ptr));
                        else if (compSize == 4) indices.push_back(*reinterpret_cast<const uint32_t*>(ptr));
                        else indices.push_back(*ptr);
                    }
                }
                else {
                    for (uint32_t i = 0; i < pos.acc->count; ++i) indices.push_back(i);
                }

                Mesh mObj;
                mObj.setup(verts, indices);
                mObj.textureID = texID;
                meshes.push_back(mObj);
            }
            };

        std::function<void(int, const glm::mat4&)> visitNode = [&](int nodeIdx, const glm::mat4& parent) {
            if (nodeIdx < 0 || static_cast<uint32_t>(nodeIdx) >= model.nodes_count) return;
            const tg3_node& n = model.nodes[nodeIdx];
            glm::mat4 world = parent * nodeLocal(n);
            if (n.mesh >= 0) processMesh(static_cast<uint32_t>(n.mesh), world);
            for (uint32_t c = 0; c < n.children_count; ++c) visitNode(n.children[c], world);
            };

        if (model.scenes_count > 0) {
            const tg3_scene& sc = model.scenes[(model.default_scene >= 0) ? model.default_scene : 0];
            for (uint32_t i = 0; i < sc.nodes_count; ++i) visitNode(sc.nodes[i], glm::mat4(1.0f));
        }

        if (minP.x < maxP.x) {
            glm::vec3 extent = maxP - minP;
            float maxDim = std::max({ extent.x, extent.y, extent.z });
            autoScale = (maxDim > 0.0f) ? 1.5f / maxDim : 1.0f; // Escala ajustada para lucir mejor sobre la curva
            pivotOffset = glm::vec3(-0.5f * (minP.x + maxP.x), -minP.y, -0.5f * (minP.z + maxP.z));
        }

        tg3_model_free(&model); tg3_error_stack_free(&errs);
        return !meshes.empty();
    }

    void draw(GLint mvpLoc, GLint useTexLoc, GLint modelLoc, const glm::mat4& pv, const glm::mat4& parentMatrix) const {
        glm::mat4 modelMat = glm::translate(glm::scale(parentMatrix, glm::vec3(autoScale)), pivotOffset);
        for (const auto& mesh : meshes) mesh.draw(mvpLoc, useTexLoc, modelLoc, pv, modelMat);
    }

    void destroy() {
        for (auto& m : meshes) m.destroy();
        meshes.clear();
    }
};

// ============================================================
// MAIN
// ============================================================

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1000, 700, "Exhibidor 3D - Figura Coleccionable", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL()) { glfwTerminate(); return -1; }

    glViewport(0, 0, 1000, 700);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.18f, 0.22f, 0.30f, 1.0f);

    // --------------------------------------------------------
    // INICIALIZACIÓN DE DEAR IMGUI
    // --------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Shaders
    GLuint shaderProgram = createShaderProgram("shaders/vertex.vert", "shaders/fragment.frag");
    GLint mvpLoc = glGetUniformLocation(shaderProgram, "uMVP");
    GLint useTexLoc = glGetUniformLocation(shaderProgram, "uUseTexture");
    GLint texLoc = glGetUniformLocation(shaderProgram, "uTexture");
    GLint modelLoc = glGetUniformLocation(shaderProgram, "uModel");

    Origin origin;
    Camera camera;

    // Exhibidor
    GLuint Tex1 = loadTexture("texturas/verde.jpg");
    GLuint Tex2 = loadTexture("texturas/asfalto.jpg");

    Mesh counter = createBox(3.2f, 0.28f, 1.8f, glm::vec4(1.0f), Tex1);
    Mesh platform = createCylinder(1.4f, 0.15f, 48, glm::vec4(1.0f));
    Mesh support = createCylinder(0.85f, 0.75f, 32, glm::vec4(1.0f));
    platform.textureID = Tex2;
    support.textureID = Tex2;

    // --------------------------------------------------------
    // PUNTOS DE CONTROL BÉZIER + MARCADORES DE COLORES
    // --------------------------------------------------------
    const float surfaceY = -0.3055f;
    glm::vec3 bezierPoints[4] = {
        glm::vec3(-0.85f, surfaceY,  0.45f), // P0: Inicio (Rojo)
        glm::vec3(-0.45f, surfaceY, -0.95f), // P1: Control 1 (Verde)
        glm::vec3(0.55f, surfaceY, -0.85f), // P2: Control 2 (Azul)
        glm::vec3(0.85f, surfaceY,  0.45f)  // P3: Final (Amarillo)
    };

    // Marcadores 3D brillantes para cada punto de control de Bézier
    Mesh pointMarkers[4] = {
        createBox(0.07f, 0.07f, 0.07f, glm::vec4(1.0f, 0.15f, 0.15f, 1.0f)), // P0 Rojo
        createBox(0.07f, 0.07f, 0.07f, glm::vec4(0.15f, 1.0f, 0.25f, 1.0f)), // P1 Verde
        createBox(0.07f, 0.07f, 0.07f, glm::vec4(0.20f, 0.55f, 1.0f, 1.0f)), // P2 Azul
        createBox(0.07f, 0.07f, 0.07f, glm::vec4(1.0f, 0.90f, 0.10f, 1.0f))  // P3 Amarillo
    };

    // Línea visual de la trayectoria
    Mesh bezierPath = createBezierPathMesh(
        bezierPoints[0] + glm::vec3(0.0f, 0.006f, 0.0f),
        bezierPoints[1] + glm::vec3(0.0f, 0.006f, 0.0f),
        bezierPoints[2] + glm::vec3(0.0f, 0.006f, 0.0f),
        bezierPoints[3] + glm::vec3(0.0f, 0.006f, 0.0f),
        80
    );

    // Figura 3D (Ferrari GLTF)
    GLTFModel collectibleFigure;
    bool figureLoaded = collectibleFigure.load("modelos/scene.gltf");
    std::cout << "GLTF cargado: " << (figureLoaded ? "EXITO" : "FALLO") << "\n";

    // Variables de estado de animación e interfaz
    double lastTime = glfwGetTime();
    float animPhase = 0.0f;           // Acumulador de fase para no saltar al cambiar velocidad
    float bezierSpeed = 0.90f;        // Velocidad controlable desde ImGui
    bool isPaused = false;            // Botón de pausa
    float manualT = 0.0f;             // Posición t [0, 1]
    int rotationMode = 0;             // 0 = Seguir dirección de la curva | 1 = Giro continuo 360°
    float modelForwardOffset = 0.0f;  // Ajuste de orientación inicial del modelo GLTF (en grados)

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        // Actualizar cámara (si ImGui no está capturando el teclado)
        if (!io.WantCaptureKeyboard) {
            camera.update(window, dt);
        }

        // Actualizar título de la ventana con las coordenadas en vivo de la cámara
        char windowTitle[160];
        std::snprintf(windowTitle, sizeof(windowTitle),
            "Exhibidor 3D | Camara -> X: %.2f  Y: %.2f  Z: %.2f",
            camera.pos.x, camera.pos.y, camera.pos.z);
        glfwSetWindowTitle(window, windowTitle);

        // Iniciar nuevo frame de Dear ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Ventana de control Dear ImGui
        ImGui::Begin("Control de Animacion y Escena");
        ImGui::Text("Camara (WASD / QE): X=%.2f, Y=%.2f, Z=%.2f", camera.pos.x, camera.pos.y, camera.pos.z);
        ImGui::Separator();

        if (ImGui::Button(isPaused ? "Reanudar Recorrido" : "Detener en Punto Actual", ImVec2(-1, 30))) {
            isPaused = !isPaused;
        }

        ImGui::SliderFloat("Velocidad Bezier", &bezierSpeed, 0.1f, 3.5f, "%.2f");

        if (ImGui::SliderFloat("Posicion en Curva (t)", &manualT, 0.0f, 1.0f, "%.3f")) {
            isPaused = true; // Si mueves el slider manual, se detiene en ese punto exacto
        }

        ImGui::Separator();
        ImGui::Text("Rotacion del Vehiculo:");
        ImGui::RadioButton("Seguir direccion de la curva (Tangente)", &rotationMode, 0);
        ImGui::RadioButton("Giro continuo 360 grados", &rotationMode, 1);
        ImGui::SliderFloat("Ajuste frente GLTF (deg)", &modelForwardOffset, -180.0f, 180.0f, "%.0f");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "P0: Rojo (Inicio)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "P1: Verde");
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "P2: Azul");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "P3: Amarillo (Fin)");
        ImGui::End();

        // Renderizado 3D
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProgram);
        if (texLoc >= 0) glUniform1i(texLoc, 0);

        int w, h; glfwGetFramebufferSize(window, &w, &h);
        float aspect = (h > 0) ? (float)w / (float)h : 1.333f;
        glm::mat4 projection = glm::perspective(glm::radians(52.0f), aspect, 0.1f, 100.0f);
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 pv = projection * view;

        // Mostrador, soporte y plataforma
        counter.draw(mvpLoc, useTexLoc, modelLoc, pv, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.82f, 0.0f)));
        support.draw(mvpLoc, useTexLoc, modelLoc, pv, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.10f, 0.0f)));
        platform.draw(mvpLoc, useTexLoc, modelLoc, pv, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.38f, 0.0f)));

        // Dibujar línea guía de Bézier y los 4 puntos de control resaltados
        glLineWidth(3.0f);
        bezierPath.draw(mvpLoc, useTexLoc, modelLoc, pv, glm::mat4(1.0f));
        for (int i = 0; i < 4; ++i) {
            glm::mat4 markerMat = glm::translate(glm::mat4(1.0f), bezierPoints[i] + glm::vec3(0.0f, 0.035f, 0.0f));
            pointMarkers[i].draw(mvpLoc, useTexLoc, modelLoc, pv, markerMat);
        }

        // Calcular avance en la curva
        if (!isPaused) {
            animPhase += dt * bezierSpeed;
            manualT = 0.5f * (1.0f - std::cos(animPhase));
        }

        glm::vec3 currentPos = evaluateBezier(bezierPoints[0], bezierPoints[1], bezierPoints[2], bezierPoints[3], manualT);
        glm::vec3 tangent = evaluateBezierTangent(bezierPoints[0], bezierPoints[1], bezierPoints[2], bezierPoints[3], manualT);

        // Invertir dirección de la tangente cuando viene de regreso (sin(animPhase) < 0)
        float directionSign = (!isPaused && std::sin(animPhase) < 0.0f) ? -1.0f : 1.0f;
        glm::vec3 travelDir = tangent * directionSign;

        float yawAngle = 0.0f;
        if (rotationMode == 0) {
            // Rotación alineada a la curva de Bézier
            yawAngle = std::atan2(travelDir.x, travelDir.z) + glm::radians(modelForwardOffset);
        }
        else {
            // Rotación continua de 360 grados sobre su eje vertical Y
            yawAngle = static_cast<float>(currentTime) * 2.0f;
        }

        if (figureLoaded) {
            glm::mat4 figureTransform = glm::translate(glm::mat4(1.0f), currentPos);
            figureTransform = glm::rotate(figureTransform, yawAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            collectibleFigure.draw(mvpLoc, useTexLoc, modelLoc, pv, figureTransform);
        }

        origin.draw();

        // Renderizar la interfaz Dear ImGui encima de la escena 3D
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Limpieza de ImGui y recursos OpenGL
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    counter.destroy();
    platform.destroy();
    support.destroy();
    bezierPath.destroy();
    for (int i = 0; i < 4; ++i) pointMarkers[i].destroy();
    collectibleFigure.destroy();
    glDeleteProgram(shaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}