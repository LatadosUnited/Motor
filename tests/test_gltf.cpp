// Fase 11: modelos glTF e animação esquelética, sem janela nem OpenGL.
// - Dados da Mesh na CPU (malhas embutidas e de arquivo) e raycast exato do editor nos triângulos.
// - Collider de malha da física com os triângulos de verdade (inclusive malhas embutidas).
// - Leitura de glTF gerado aqui mesmo (.gltf com data URIs, .glb, .gltf com .bin e imagem externos numa pasta
//   com acento): nós, malhas, materiais, imagens, pele e clipes.
// - Asset no projeto (GUID, malhas e clipes como "#nome"), importação como prefab com materiais e texturas.
// - Amostragem dos clipes (linear, degrau, cúbica), skinning na CPU, Animator (tocar, misturar, trocar) e o
//   sistema de animação do SceneRuntime.
// - O modelo de exemplo da engine (Robo.glb) tem os clipes do personagem.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/assets/Model.h>
#include <motor/core/Paths.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/renderer/Mesh.h>
#include <motor/renderer/Texture.h>
#include <motor/scene/Components.h>
#include <motor/scene/ModelImport.h>
#include <motor/scene/Prefab.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/Script.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace motor;
namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

// PNG 1x1 vermelho.
constexpr const char* kRedPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC";

std::vector<uint8_t> Base64Decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::vector<uint8_t> out;
    int bits = 0, buffer = 0;
    for (char c : text) {
        const int v = value(c);
        if (v < 0) continue;
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = bytes[i], b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0, b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? table[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? table[n & 63] : '=';
    }
    return out;
}

void WriteFile(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

void WriteFile(const fs::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Monta um glTF pequeno: buffer binário + JSON.
struct GltfBuilder {
    json doc = {{"asset", {{"version", "2.0"}}}};
    std::vector<uint8_t> bin;

    int View(const void* data, size_t bytes) {
        while (bin.size() % 4) bin.push_back(0);
        const size_t offset = bin.size();
        bin.insert(bin.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes}});
        return static_cast<int>(doc["bufferViews"].size()) - 1;
    }
    int Accessor(int view, int componentType, size_t count, const char* type) {
        doc["accessors"].push_back({{"bufferView", view}, {"componentType", componentType}, {"count", count}, {"type", type}});
        return static_cast<int>(doc["accessors"].size()) - 1;
    }
    int Floats(const std::vector<float>& v, const char* type, int components) {
        return Accessor(View(v.data(), v.size() * sizeof(float)), 5126, v.size() / static_cast<size_t>(components), type);
    }
    int Positions(const std::vector<float>& v) {
        const int a = Floats(v, "VEC3", 3);
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (size_t i = 0; i + 2 < v.size(); i += 3) {
            lo = glm::min(lo, glm::vec3(v[i], v[i + 1], v[i + 2]));
            hi = glm::max(hi, glm::vec3(v[i], v[i + 1], v[i + 2]));
        }
        doc["accessors"][a]["min"] = {lo.x, lo.y, lo.z};
        doc["accessors"][a]["max"] = {hi.x, hi.y, hi.z};
        return a;
    }
    int Indices(const std::vector<uint16_t>& v) {
        return Accessor(View(v.data(), v.size() * 2), 5123, v.size(), "SCALAR");
    }
    int Bytes4(const std::vector<uint8_t>& v) {
        return Accessor(View(v.data(), v.size()), 5121, v.size() / 4, "VEC4");
    }

    // .gltf com o buffer num data URI.
    std::string Gltf() const {
        json d = doc;
        d["buffers"] = json::array({{{"byteLength", bin.size()}, {"uri", "data:application/octet-stream;base64," + Base64Encode(bin)}}});
        return d.dump(2);
    }
    // .gltf com o buffer num arquivo ao lado.
    std::string GltfExternal(const std::string& binName) const {
        json d = doc;
        d["buffers"] = json::array({{{"byteLength", bin.size()}, {"uri", binName}}});
        return d.dump(2);
    }
    // .glb: cabeçalho + JSON + BIN.
    std::vector<uint8_t> Glb() const {
        json d = doc;
        d["buffers"] = json::array({{{"byteLength", bin.size()}}});
        std::string text = d.dump();
        while (text.size() % 4) text += ' ';
        std::vector<uint8_t> data = bin;
        while (data.size() % 4) data.push_back(0);
        std::vector<uint8_t> out;
        auto u32 = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        };
        u32(0x46546C67);
        u32(2);
        u32(static_cast<uint32_t>(12 + 8 + text.size() + 8 + data.size()));
        u32(static_cast<uint32_t>(text.size()));
        u32(0x4E4F534A);
        out.insert(out.end(), text.begin(), text.end());
        u32(static_cast<uint32_t>(data.size()));
        u32(0x004E4942);
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }
};

// Boneco de teste:
//   Raiz (junta) -> Osso (junta, y = 1)        esqueleto com 2 juntas
//   Pele: triângulo com pele (v0 na Raiz, v1 no Osso, v2 meio a meio)
//   Base (x = 2, malha "Caixa" com 2 primitivas) -> Topo (y = 1, escala 2, malha "Topo")
//   Vazio: nó sem malha (não vira entidade)
// Materiais: "Pele" (cinza linear 0.5, com textura) e "Metal/Vermelho". Clipes: "Girar" (Osso gira 90° em Z
// em 1 s, linear), "Pular" (Raiz sobe 1 m no meio, degrau) e um sem nome (cúbica, Raiz anda 2 m em X em 2 s).
// imageUri vazio: imagem no buffer (bufferView); senão, o URI dado.
GltfBuilder MakeBoneco(const std::string& imageUri = {}) {
    GltfBuilder b;
    json& d = b.doc;

    // Pele.
    const int pelePos = b.Positions({0, 0, 0, 0, 2, 0, 1, 1, 0});
    const int peleUv = b.Floats({0, 0, 0, 1, 1, 0}, "VEC2", 2);
    const int peleJoints = b.Bytes4({0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0});
    const int peleWeights = b.Floats({1, 0, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0, 0}, "VEC4", 4);
    d["meshes"].push_back({{"name", "Corpo"},
                           {"primitives", json::array({{{"attributes", {{"POSITION", pelePos}, {"TEXCOORD_0", peleUv},
                                                                        {"JOINTS_0", peleJoints}, {"WEIGHTS_0", peleWeights}}},
                                                        {"material", 0}}})}});
    // Caixa: duas primitivas (uma sem índices).
    const int quadPos = b.Positions({-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f});
    const int quadIdx = b.Indices({0, 2, 1, 0, 3, 2});
    const int triPos = b.Positions({0, 0, 0, 1, 0, 0, 0, 1, 0});
    d["meshes"].push_back({{"name", "Caixa"},
                           {"primitives", json::array({{{"attributes", {{"POSITION", quadPos}}}, {"indices", quadIdx}, {"material", 1}},
                                                       {{"attributes", {{"POSITION", triPos}}}, {"material", 0}}})}});
    d["meshes"].push_back({{"name", "Topo"},
                           {"primitives", json::array({{{"attributes", {{"POSITION", triPos}}}, {"material", 1}}})}});

    d["nodes"] = json::array({
        {{"name", "Raiz"}, {"children", {1}}},
        {{"name", "Osso"}, {"translation", {0, 1, 0}}},
        {{"name", "Pele"}, {"mesh", 0}, {"skin", 0}},
        {{"name", "Base"}, {"mesh", 1}, {"translation", {2, 0, 0}}, {"children", {4}}},
        {{"name", "Topo"}, {"mesh", 2}, {"translation", {0, 1, 0}}, {"scale", {2, 2, 2}}},
        {{"name", "Vazio"}},
    });
    d["scenes"] = json::array({{{"nodes", {0, 2, 3, 5}}}});
    d["scene"] = 0;

    // Pele: a inversa do bind do Osso desfaz o y = 1.
    const std::vector<float> ibm = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                                    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    d["skins"] = json::array({{{"joints", {0, 1}}, {"inverseBindMatrices", b.Floats(ibm, "MAT4", 16)}}});

    // Material e imagem.
    if (imageUri.empty()) {
        const std::vector<uint8_t> png = Base64Decode(kRedPngBase64);
        d["images"] = json::array({{{"name", "Rosto"}, {"bufferView", b.View(png.data(), png.size())}, {"mimeType", "image/png"}}});
    } else {
        d["images"] = json::array({{{"uri", imageUri}}});
    }
    d["textures"] = json::array({{{"source", 0}}});
    d["materials"] = json::array({
        {{"name", "Pele"},
         {"pbrMetallicRoughness", {{"baseColorFactor", {0.5, 0.5, 0.5, 1.0}}, {"baseColorTexture", {{"index", 0}}}, {"roughnessFactor", 0.5}}}},
        {{"name", "Metal/Vermelho"}, {"pbrMetallicRoughness", {{"baseColorFactor", {1.0, 0.0, 0.0, 1.0}}, {"roughnessFactor", 0.0}}}},
    });

    // Clipes.
    const float s = std::sin(glm::quarter_pi<float>()), c = std::cos(glm::quarter_pi<float>());
    const int girarT = b.Floats({0, 1}, "SCALAR", 1);
    const int girarV = b.Floats({0, 0, 0, 1, 0, 0, s, c}, "VEC4", 4);
    const int pularT = b.Floats({0, 0.5f}, "SCALAR", 1);
    const int pularV = b.Floats({0, 0, 0, 0, 1, 0}, "VEC3", 3);
    const int andarT = b.Floats({0, 2}, "SCALAR", 1);
    const int andarV = b.Floats({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0}, "VEC3", 3);
    d["animations"] = json::array({
        {{"name", "Girar"},
         {"samplers", json::array({{{"input", girarT}, {"output", girarV}}})},
         {"channels", json::array({{{"sampler", 0}, {"target", {{"node", 1}, {"path", "rotation"}}}}})}},
        {{"name", "Pular"},
         {"samplers", json::array({{{"input", pularT}, {"output", pularV}, {"interpolation", "STEP"}}})},
         {"channels", json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}})}},
        {{"samplers", json::array({{{"input", andarT}, {"output", andarV}, {"interpolation", "CUBICSPLINE"}}})},
         {"channels", json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}})}},
    });
    return b;
}

void CheckBonecoData(const ModelData& m, bool embeddedImage) {
    CHECK(m.nodes.size() == 6);
    CHECK(m.nodes.size() == 6 && m.nodes[0].name == "Raiz" && m.nodes[1].name == "Osso" && m.nodes[1].parent == 0);
    CHECK(m.nodes.size() == 6 && m.nodes[2].name == "Pele" && m.nodes[2].skin == 0);
    CHECK(m.nodes.size() == 6 && m.nodes[4].name == "Topo" && m.nodes[4].parent == 3 && Near(m.nodes[4].local.scale, glm::vec3(2.0f)));
    CHECK(m.primitives.size() == 4);
    if (m.primitives.size() == 4) {
        CHECK(m.primitives[0].name == "Corpo" && m.primitives[0].mesh.vertices.size() == 3 && m.primitives[0].weights.size() == 3);
        CHECK(m.primitives[1].name == "Caixa.0" && m.primitives[1].mesh.indices.size() == 6 && m.primitives[1].material == 1);
        CHECK(m.primitives[2].name == "Caixa.1" && m.primitives[2].mesh.indices.size() == 3 && m.primitives[2].material == 0);
        CHECK(m.primitives[3].name == "Topo");
        // UV do glTF (origem em cima) virada para a nossa (origem embaixo); normais calculadas (+Z).
        CHECK(Near(m.primitives[0].mesh.vertices[1].uv.y, 0.0f) && Near(m.primitives[0].mesh.vertices[0].uv.y, 1.0f));
        CHECK(Near(m.primitives[1].mesh.vertices[0].normal, glm::vec3(0, 1, 0)));
        CHECK(Near(m.primitives[0].weights[2].weights.x, 0.5f) && Near(m.primitives[0].weights[2].joints.y, 1.0f));
    }
    CHECK(m.materials.size() == 2);
    if (m.materials.size() == 2) {
        CHECK(m.materials[0].name == "Pele" && m.materials[0].image == 0);
        CHECK(Near(m.materials[0].color.r, std::pow(0.5f, 1.0f / 2.2f), 1e-3f));  // linear -> sRGB
        CHECK(m.materials[1].image == -1 && m.materials[1].shininess > m.materials[0].shininess);
    }
    CHECK(m.images.size() == 1);
    if (m.images.size() == 1) {
        if (embeddedImage) {
            CHECK(m.images[0].bytes == Base64Decode(kRedPngBase64) && m.images[0].mimeType == "image/png");
        } else {
            CHECK(m.images[0].bytes.empty() && m.images[0].file.filename() == "rosto.png");
        }
    }
    CHECK(m.skins.size() == 1 && m.skins[0].joints == std::vector<int>({0, 1}));
    CHECK(m.skins.size() == 1 && Near(m.skins[0].inverseBind[1][3].y, -1.0f));
    CHECK(m.IsJoint(1) && !m.IsJoint(2));
    CHECK(m.clips.size() == 3);
    if (m.clips.size() == 3) {
        CHECK(m.clips[0].name == "Girar" && Near(m.clips[0].duration, 1.0f) && m.clips[0].channels.size() == 1);
        CHECK(m.clips[0].channels[0].joint == "Osso" && m.clips[0].channels[0].jointIndex == 1);
        CHECK(m.clips[1].channels[0].interpolation == AnimationInterpolation::Step);
        CHECK(m.clips[2].name == "animacao2" && m.clips[2].channels[0].interpolation == AnimationInterpolation::CubicSpline);
        CHECK(m.clips[2].channels[0].values.size() == 6);
    }
}

// Script de teste: troca o clipe do Animator do filho pelo GetAnimator.
struct TrocaClipe : Script {
    int frames = 0;
    void OnUpdate(float) override {
        if (++frames == 3) {
            if (Animator* a = GetAnimator()) a->CrossFade("Pular", 0.5f);
        }
    }
};

alignas(16) unsigned char g_fakeTexture[64] = {};

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_gltf_tmp");

    SECTION("Mesh: cópia das posições e dos índices na CPU (sem OpenGL)");
    {
        const auto cube = Mesh::CreateCube();
        CHECK(cube->Positions().size() == 24 && cube->Indices().size() == 36 && cube->IndexCount() == 36);
        CHECK(Near(cube->BoundsMin(), glm::vec3(-0.5f)) && Near(cube->BoundsMax(), glm::vec3(0.5f)));
        const auto plane = Mesh::CreatePlane();
        CHECK(plane->Positions().size() == 4 && plane->Indices().size() == 6);
        const Vertex tri[] = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        const uint32_t idx[] = {0, 1, 2};
        Mesh mesh(tri, idx);
        CHECK(mesh.Positions().size() == 3 && Near(mesh.Positions()[1], glm::vec3(1, 0, 0)) && mesh.Indices()[2] == 2);
        CHECK(!mesh.IsSkinned() && mesh.Weights().empty());
        const SkinWeights w[] = {{}, {}, {}};
        Mesh skinned(tri, idx, w);
        CHECK(skinned.Weights().size() == 3 && !skinned.IsSkinned());  // pesos sem pele: desenhada parada
        skinned.SetSkin(std::make_shared<Skin>());
        CHECK(skinned.IsSkinned());
    }

    SECTION("Raycast do editor: triângulos, não a caixa");
    {
        Scene scene;
        Entity& ball = scene.CreateEntity("Bola");
        ball.Add<MeshRenderer>().mesh = Mesh::CreateSphere();
        // Pela quina da caixa (x = z = 0.45): a caixa seria atingida, a esfera (raio 0.5) não.
        CHECK(!scene.Raycast({0.45f, 5.0f, 0.45f}, {0, -1, 0}));
        // Perto do polo (bem no vértice do polo o raio passaria entre triângulos degenerados).
        const auto hit = scene.Raycast({0.01f, 5.0f, 0.013f}, {0, -1, 0});
        CHECK(hit && hit->entity == ball.id && Near(hit->distance, 4.5f, 2e-3f));
        // Escala e rotação da entidade valem.
        ball.transform.scale = glm::vec3(4.0f);
        const auto big = scene.Raycast({0.01f, 5.0f, 0.013f}, {0, -1, 0});
        CHECK(big && Near(big->distance, 3.0f, 1e-2f));
        // Plano visto de baixo também conta (raio dos dois lados).
        Entity& floor = scene.CreateEntity("Chão");
        floor.Add<MeshRenderer>().mesh = Mesh::CreatePlane();
        floor.transform.position = {10, 0, 0};
        const auto under = scene.Raycast({10.2f, -1.0f, 0.1f}, {0, 1, 0});
        CHECK(under && under->entity == floor.id && Near(under->distance, 1.0f));
    }

    SECTION("Collider de malha exato (malhas embutidas e criadas em código)");
    {
        Scene scene;
        Entity& ball = scene.CreateEntity("Esfera");
        ball.Add<MeshRenderer>().mesh = Mesh::CreateSphere();
        ball.Add<ColliderComponent>().shape = ColliderShape::Mesh;
        ball.transform.scale = glm::vec3(2.0f);
        // Rampa de dois triângulos subindo 2 m em +X (como a do test_physics, mas uma Mesh de verdade).
        const Vertex ramp[] = {{{-5, 0, -5}, {}, {}}, {{5, 2, -5}, {}, {}}, {{5, 2, 5}, {}, {}}, {{-5, 0, 5}, {}, {}}};
        const uint32_t rampIdx[] = {0, 2, 1, 0, 3, 2};
        Entity& rampa = scene.CreateEntity("Rampa");
        rampa.transform.position = {20, 0, 0};
        rampa.Add<MeshRenderer>().mesh = std::make_shared<Mesh>(ramp, rampIdx);
        rampa.Add<ColliderComponent>().shape = ColliderShape::Mesh;

        PhysicsSettings& settings = PhysicsSettings::Global();
        const PhysicsSettings saved = settings;
        settings.meshData = {};
        settings.workerThreads = 0;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.FixedUpdate(1.0f / 60.0f);
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica != nullptr);
        if (fisica) {
            const auto top = fisica->Raycast({0, 5, 0}, {0, -1, 0});
            CHECK(top && top->entity == ball.id && Near(top->point.y, 1.0f, 0.01f));
            // Quina da caixa (0.9, 0.9): a caixa dos limites seria atingida em y = 1; a esfera não.
            CHECK(!fisica->Raycast({0.9f, 5.0f, 0.9f}, {0, -1, 0}));
            const auto onRamp = fisica->Raycast({20, 10, 2}, {0, -1, 0});
            CHECK(onRamp && onRamp->entity == rampa.id && Near(onRamp->point.y, 1.0f, 1e-3f) && onRamp->normal.x < -0.1f);
        }
        runtime.Stop();
        settings = saved;
    }

    SECTION("glTF: .gltf com data URIs");
    const GltfBuilder boneco = MakeBoneco();
    {
        ModelData data;
        std::string error;
        const std::string text = boneco.Gltf();
        CHECK(ParseGltf({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, dir, data, &error));
        CheckBonecoData(data, true);
    }

    SECTION("glTF: .glb");
    {
        const fs::path file = dir / "Boneco.glb";
        WriteFile(file, boneco.Glb());
        ModelData data;
        std::string error;
        CHECK(LoadGltf(file, data, &error));
        CheckBonecoData(data, true);
        // Erros viram mensagem, não queda.
        WriteFile(dir / "quebrado.glb", std::string("glTF lixo"));
        CHECK(!LoadGltf(dir / "quebrado.glb", data, &error) && !error.empty());
        CHECK(!LoadGltf(dir / "não existe.gltf", data, &error) && !error.empty());
    }

    SECTION("glTF: .bin e imagem externos, pasta com acento");
    {
        const fs::path folder = dir / PathFromUtf8("Modelos Ação");
        fs::create_directories(folder);
        const GltfBuilder external = MakeBoneco("rosto.png");
        WriteFile(folder / PathFromUtf8("Bonéco.gltf"), external.GltfExternal("Bon%C3%A9co.bin"));
        WriteFile(folder / PathFromUtf8("Bonéco.bin"), external.bin);
        WriteFile(folder / "rosto.png", Base64Decode(kRedPngBase64));
        ModelData data;
        std::string error;
        CHECK(LoadGltf(folder / PathFromUtf8("Bonéco.gltf"), data, &error));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        CheckBonecoData(data, false);
        CHECK(data.images.size() == 1 && data.images[0].file == (folder / "rosto.png").lexically_normal());
    }

    // Projeto com o modelo nos Assets.
    const fs::path root = dir / PathFromUtf8("Jogo Ação");
    const fs::path models = root / "Assets" / "Modelos";
    fs::create_directories(models);
    WriteFile(models / "Boneco.glb", boneco.Glb());
    AssetLibrary assets;
    assets.AddMesh("Cubo", Mesh::CreateCube());
    assets.SetRoot(root);
    assets.SetTextureFactory([](const ImageData& image) {
        CHECK(image.width == 1 && image.height == 1 && image.pixels[0] == 255 && image.pixels[1] == 0);
        return std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(g_fakeTexture), [](Texture2D*) {});
    });

    SECTION("Modelo como asset: GUID, malhas e clipes como partes do arquivo");
    std::shared_ptr<Model> model;
    std::string modelGuid;
    {
        std::string error;
        CHECK(AssetTypeFromPath("x/Robo.GLB") == AssetType::Model && AssetTypeFromPath("a.gltf") == AssetType::Model);
        model = assets.GetModel("Assets/Modelos/Boneco.glb", &error);
        CHECK(model != nullptr);
        modelGuid = assets.Database().FindGuid(models / "Boneco.glb");
        CHECK(AssetDatabase::IsValidGuid(modelGuid) && fs::exists(models / "Boneco.glb.meta"));
        if (model) {
            CHECK(model->meshes.size() == 4 && model->skeleton->joints.size() == 6 && model->clips.size() == 3);
            const auto corpo = model->FindMesh("Corpo");
            CHECK(corpo && corpo->IsSkinned() && corpo->GetSkin()->skeleton == model->skeleton);
            CHECK(corpo && corpo->Positions().size() == 3 && corpo->Indices().size() == 3);
            CHECK(model->FindMesh("Topo") && !model->FindMesh("Topo")->IsSkinned());
            CHECK(assets.GetMesh("Assets/Modelos/Boneco.glb#Corpo") == corpo);
            CHECK(assets.GetMeshByGuid(modelGuid + "#Corpo") == corpo);
            const auto ref = assets.RefOf(corpo.get());
            CHECK(ref && ref->guid == modelGuid + "#Corpo" && ref->name == "Assets/Modelos/Boneco.glb#Corpo");
            const auto clip = assets.GetClip("Assets/Modelos/Boneco.glb#Girar", &error);
            CHECK(clip && clip == model->FindClip("Girar") && clip->skeleton == model->skeleton);
            CHECK(assets.GetClipByGuid(modelGuid + "#Pular") == model->FindClip("Pular"));
            const auto clipRef = assets.RefOf(clip.get());
            CHECK(clipRef && clipRef->guid == modelGuid + "#Girar");
            CHECK(!assets.GetMesh("Assets/Modelos/Boneco.glb#NaoTem", &error) && !error.empty());
            CHECK(assets.GetModel("Assets/Modelos/Boneco.glb") == model);  // em cache
        }
    }

    SECTION("Amostragem dos clipes");
    if (model) {
        const Skeleton& sk = *model->skeleton;
        const auto girar = model->FindClip("Girar");
        auto pose = sk.RestPose();
        const auto binding = BindClip(*girar, sk);
        CHECK(binding.size() == 1 && binding[0] == 1);
        SampleClip(*girar, binding, 0.5f, pose);
        // Metade do caminho até 90° em Z: 45°.
        const glm::vec3 x = pose[1].rotation * glm::vec3(1, 0, 0);
        CHECK(Near(x, glm::vec3(std::cos(glm::quarter_pi<float>()), std::sin(glm::quarter_pi<float>()), 0), 1e-3f));
        CHECK(Near(pose[1].translation, glm::vec3(0, 1, 0)));  // sem canal de posição: fica a de repouso

        pose = sk.RestPose();
        const auto pular = model->FindClip("Pular");
        SampleClip(*pular, BindClip(*pular, sk), 0.49f, pose);
        CHECK(Near(pose[0].translation.y, 0.0f));  // degrau: só muda na chave
        SampleClip(*pular, BindClip(*pular, sk), 0.5f, pose);
        CHECK(Near(pose[0].translation.y, 1.0f));

        const auto andar = model->FindClip("animacao2");
        SampleClip(*andar, BindClip(*andar, sk), 1.0f, pose);
        CHECK(Near(pose[0].translation.x, 1.0f));  // Hermite com tangentes zero: meio do caminho no meio do tempo
        SampleClip(*andar, BindClip(*andar, sk), 0.5f, pose);
        CHECK(pose[0].translation.x > 0.1f && pose[0].translation.x < 0.5f);  // começa devagar

        CHECK(Near(ClipTime(2.5f, 1.0f, true), 0.5f) && Near(ClipTime(-0.25f, 1.0f, true), 0.75f));
        CHECK(Near(ClipTime(2.5f, 1.0f, false), 1.0f) && Near(ClipTime(0.3f, 0.0f, true), 0.0f));

        // Clipe de outro esqueleto acha as juntas pelo nome.
        Skeleton other;
        other.joints = {{"Outro", -1, {}}, {"Osso", 0, {}}};
        other.Finalize();
        CHECK(BindClip(*girar, other) == std::vector<int>({1}));
        Skeleton none;
        none.joints = {{"X", -1, {}}};
        none.Finalize();
        CHECK(BindClip(*girar, none) == std::vector<int>({-1}));
    }

    SECTION("Skinning na CPU");
    if (model) {
        const Skeleton& sk = *model->skeleton;
        const auto& skin = *model->FindMesh("Corpo")->GetSkin();
        std::vector<glm::mat4> joints;
        // Pose de repouso: as matrizes das juntas são a identidade (global * inversa do bind).
        skin.JointMatrices(sk.restGlobal, joints);
        CHECK(joints.size() == 2 && Near(glm::vec3(joints[1][3]), glm::vec3(0.0f)) && Near(joints[1][0][0], 1.0f));
        // Osso girado 90° em Z: o vértice (0, 2, 0) preso nele vai para (-1, 1, 0).
        auto pose = sk.RestPose();
        SampleClip(*model->FindClip("Girar"), BindClip(*model->FindClip("Girar"), sk), 1.0f, pose);
        std::vector<glm::mat4> globals(sk.joints.size());
        sk.ComputeGlobals(pose, globals);
        skin.JointMatrices(globals, joints);
        const auto& weights = model->FindMesh("Corpo")->Weights();
        const auto& positions = model->FindMesh("Corpo")->Positions();
        CHECK(Near(SkinPosition(positions[0], weights[0], joints), glm::vec3(0, 0, 0)));
        CHECK(Near(SkinPosition(positions[1], weights[1], joints), glm::vec3(-1, 1, 0), 1e-4f));
        // Meio a meio: média das duas juntas. (1,1,0) pela Raiz fica; pelo Osso vai para (0, 2, 0).
        CHECK(Near(SkinPosition(positions[2], weights[2], joints), glm::vec3(0.5f, 1.5f, 0), 1e-4f));
        // Mistura de poses.
        const JointTransform a{}, b{{2, 0, 0}, glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0)), glm::vec3(3.0f)};
        const JointTransform half = Blend(a, b, 0.5f);
        CHECK(Near(half.translation, glm::vec3(1, 0, 0)) && Near(half.scale, glm::vec3(2.0f)));
        CHECK(Near(glm::angle(half.rotation), glm::quarter_pi<float>(), 1e-4f));
    }

    SECTION("Animator: tocar, misturar e trocar de clipe");
    if (model) {
        Animator a;
        a.model = model;
        CHECK(a.ClipNames() == std::vector<std::string>({"Girar", "Pular", "animacao2"}));
        CHECK(!a.Play("NaoTem") && !a.IsPlaying() && a.CurrentClip().empty());
        a.Update(0.1f);
        CHECK(a.GlobalPose().size() == 6 && a.PoseSkeleton() == model->skeleton.get());  // parado: repouso
        CHECK(Near(glm::vec3(a.GlobalPose()[1][3]), glm::vec3(0, 1, 0)));

        CHECK(a.Play("Girar") && a.IsPlaying() && a.CurrentClip() == "Girar");
        a.Update(0.25f);
        a.Update(0.25f);
        CHECK(Near(a.ClipTime(), 0.5f) && Near(a.ClipLength(), 1.0f));
        CHECK(Near(glm::angle(glm::quat_cast(glm::mat3(a.GlobalPose()[1]))), glm::quarter_pi<float>(), 1e-3f));
        CHECK(a.Play("Girar") && Near(a.ClipTime(), 0.5f));  // já tocando: continua
        a.Update(0.75f);
        CHECK(Near(a.ClipTime(), 0.25f) && !a.IsFinished());  // loop

        // Troca misturando com Pular (Raiz sobe 1 m em 0,5 s, degrau, sem loop).
        a.speed = 0.0f;
        CHECK(a.CrossFade("Pular", 0.4f, false));
        a.speed = 1.0f;
        a.Update(0.2f);  // Pular em 0,2 s (degrau ainda em 0); peso 0,5
        CHECK(a.CurrentClip() == "Pular" && Near(glm::vec3(a.GlobalPose()[0][3]).y, 0.0f));
        a.Update(0.4f);  // Pular chegou ao fim (0,5 s): Raiz em y = 1, mistura acabou
        CHECK(Near(glm::vec3(a.GlobalPose()[0][3]).y, 1.0f) && Near(glm::vec3(a.GlobalPose()[1][3]).y, 2.0f));
        CHECK(a.IsFinished() && Near(a.ClipTime(), 0.5f));
        CHECK(a.Play("Pular") && Near(a.ClipTime(), 0.0f));  // terminou: tocar de novo recomeça

        // Mistura pela metade: Girar saindo contra Pular (que não mexe no Osso, em repouso).
        a.Stop();
        CHECK(!a.IsPlaying());
        a.Play("Girar");
        a.Update(0.999f);  // ~90°
        a.CrossFade("Pular", 1.0f);
        a.Update(0.5f);  // Girar em 1,499 s -> 0,499 s no loop (~45°), peso 0,5: ~22,5° (0,39 rad)
        const float angle = glm::angle(glm::quat_cast(glm::mat3(a.GlobalPose()[1])));
        CHECK(angle > 0.35f && angle < 0.43f);

        // Clipe de outro arquivo/esqueleto (pelo nome das juntas).
        a.Stop();
        CHECK(a.Play(std::shared_ptr<const AnimationClip>(model->FindClip("Girar"))));
        a.model = nullptr;
        a.Update(0.1f);
        CHECK(a.GlobalPose().empty());
    }

    SECTION("Importar: prefab, materiais e texturas");
    ModelImportResult imported;
    if (model) {
        std::string error;
        const fs::path file = models / "Boneco.glb";
        CHECK(ModelNeedsImport(file) && ModelPrefabPath(file) == models / "Boneco.mprefab");
        CHECK(ImportModel(file, assets, &imported, &error));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        CHECK(!ModelNeedsImport(file));
        CHECK(fs::exists(imported.prefab) && fs::exists(AssetDatabase::MetaPathFor(imported.prefab)));
        CHECK(imported.folder == models / "Boneco_materiais" && imported.materials == 2 && imported.textures == 1);
        CHECK(fs::exists(imported.folder / "Pele.mmat") && fs::exists(imported.folder / "Metal_Vermelho.mmat"));
        CHECK(fs::exists(imported.folder / "Rosto.png") && fs::exists(imported.folder / "Rosto.png.meta"));
        const std::string pele = ReadText(imported.folder / "Pele.mmat");
        const std::string rostoGuid = assets.Database().FindGuid(imported.folder / "Rosto.png");
        CHECK(!rostoGuid.empty() && pele.find(rostoGuid) != std::string::npos);
        CHECK(pele.find("Assets/Modelos/Boneco_materiais/Rosto.png") != std::string::npos);
        // Raiz, Pele (com pele, filha da raiz), Base (2 partes) com Topo. Ossos e Vazio não viram entidade.
        CHECK(imported.entities == 6);

        PrefabLibrary library;
        Prefab* prefab = library.GetFile(imported.prefab, assets, &error);
        CHECK(prefab != nullptr);
        if (prefab) {
            const Entity* rootE = prefab->scene.Find(prefab->root);
            CHECK(rootE && rootE->name == "Boneco");
            const Animator* animator = rootE ? rootE->Get<Animator>() : nullptr;
            CHECK(animator && animator->model == model && animator->clip == "Girar" && animator->playOnStart);
            Scene scene;
            const EntityId inst = Prefabs::Instantiate(*prefab, scene);
            const Entity* skinE = nullptr;
            const Entity* baseE = nullptr;
            const Entity* topoE = nullptr;
            for (const auto& e : scene.Entities()) {
                if (e->name == "Pele") skinE = e.get();
                if (e->name == "Base") baseE = e.get();
                if (e->name == "Topo") topoE = e.get();
            }
            CHECK(skinE && skinE->parent == inst && skinE->Get<MeshRenderer>()->mesh == model->FindMesh("Corpo"));
            CHECK(skinE && skinE->Get<MeshRenderer>()->sharedMaterial && skinE->Get<MeshRenderer>()->sharedMaterial->albedo);
            CHECK(baseE && Near(baseE->transform.position, glm::vec3(2, 0, 0)) && scene.Children(baseE->id).size() == 3);
            CHECK(topoE && topoE->parent == baseE->id && Near(topoE->transform.scale, glm::vec3(2.0f)));
            CHECK(topoE && topoE->Get<MeshRenderer>()->sharedMaterial &&
                  Near(topoE->Get<MeshRenderer>()->sharedMaterial->color.r, 1.0f));
            CHECK(!scene.FindByName("Osso") && !scene.FindByName("Vazio"));
            CHECK(FindAnimator(scene, inst) == scene.Find(inst)->Get<Animator>());

            // Cena salva e aberta de novo: as malhas e o modelo voltam pelo GUID "#nome".
            const fs::path sceneFile = root / "Assets" / "Teste.mscene";
            CHECK(SceneSerializer::Save(sceneFile, scene, assets, &error));
            const std::string text = ReadText(sceneFile);
            CHECK(text.find(modelGuid + "#Corpo") != std::string::npos && text.find("Boneco.glb#Corpo") != std::string::npos);
            AssetLibrary fresh;
            fresh.SetRoot(root);
            fresh.SetTextureFactory([](const ImageData&) {
                return std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(g_fakeTexture), [](Texture2D*) {});
            });
            Scene loaded;
            CHECK(SceneSerializer::Load(sceneFile, loaded, fresh, &error));
            const Entity* skin2 = loaded.FindByName("Pele");
            const auto freshModel = fresh.GetModel("Assets/Modelos/Boneco.glb");
            CHECK(skin2 && freshModel && skin2->Get<MeshRenderer>()->mesh == freshModel->FindMesh("Corpo"));
            const Entity* root2 = loaded.FindByName("Boneco");
            CHECK(root2 && root2->Get<Animator>() && root2->Get<Animator>()->model == freshModel);
        }

        // Importar de novo (modelo exportado outra vez) mantém os GUIDs do prefab e dos materiais.
        const std::string prefabGuid = assets.Database().FindGuid(imported.prefab);
        const std::string materialGuid = assets.Database().FindGuid(imported.folder / "Pele.mmat");
        CHECK(ImportModel(file, assets, &imported, &error));
        CHECK(assets.Database().FindGuid(imported.prefab) == prefabGuid);
        CHECK(assets.Database().FindGuid(imported.folder / "Pele.mmat") == materialGuid);
        CHECK(ImportModelsInProject(assets) == 0);  // tudo em dia
    }

    SECTION("Sistema de animação no Play (SceneRuntime) e GetAnimator dos scripts");
    if (model) {
        ComponentRegistry::Instance().Register<TrocaClipe>("TrocaClipe", "Troca clipe");
        Scene scene;
        Entity& player = scene.CreateEntity("Jogador");
        const EntityId playerId = player.id;
        player.Add<TrocaClipe>();
        Entity& body = scene.CreateEntity("Modelo", playerId);
        Animator& anim = body.Add<Animator>();
        anim.model = model;
        anim.clip = "Girar";
        const EntityId bodyId = body.id;
        CHECK(FindAnimator(scene, playerId) == &anim && FindAnimator(scene, bodyId) == &anim);

        SceneRuntime runtime(scene);
        runtime.Start();
        const Animator* a = scene.Find(bodyId)->Get<Animator>();
        CHECK(a->IsPlaying() && a->CurrentClip() == "Girar" && a->GlobalPose().size() == 6);
        runtime.Update(0.1f);
        CHECK(Near(a->ClipTime(), 0.1f));
        runtime.Update(0.1f);
        runtime.Update(0.1f);  // 3º frame: o script pede "Pular"
        CHECK(a->CurrentClip() == "Pular");
        scene.Find(bodyId)->active = false;  // inativo: não avança
        const float t = a->ClipTime();
        runtime.Update(0.1f);
        CHECK(Near(a->ClipTime(), t));
        runtime.Stop();

        // Fora do Play nada avança; sem clipe inicial, nada toca.
        Scene idle;
        Animator& still = idle.CreateEntity("Parado").Add<Animator>();
        still.model = model;
        SceneRuntime r2(idle);
        r2.Start();
        CHECK(!idle.FindByName("Parado")->Get<Animator>()->IsPlaying());
        r2.Stop();
    }

    SECTION("Modelo de exemplo da engine: Robo.glb");
    {
        ModelData robo;
        std::string error;
        CHECK(LoadGltf(fs::path(MOTOR_ENGINE_ASSETS_DIR) / "models" / "Robo.glb", robo, &error));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        auto hasClip = [&](const char* name) {
            for (const auto& c : robo.clips) {
                if (c.name == name && c.duration > 0.1f && !c.channels.empty()) return true;
            }
            return false;
        };
        CHECK(hasClip("Parado") && hasClip("Andando") && hasClip("Pulando"));
        CHECK(robo.skins.size() == 1 && !robo.primitives.empty() && !robo.primitives[0].weights.empty());
        CHECK(robo.materials.size() >= 2 && robo.images.size() == 1);
        const auto built = BuildModel(std::move(robo));
        bool skinned = false;
        for (const auto& m : built->meshes) skinned = skinned || m.mesh->IsSkinned();
        CHECK(skinned && built->skeleton->joints.size() <= 128);
        // Pés no chão na pose de repouso (y mínimo ~0) e altura de gente (~1,8 m).
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const auto& m : built->meshes) {
            lo = glm::min(lo, m.mesh->BoundsMin());
            hi = glm::max(hi, m.mesh->BoundsMax());
        }
        CHECK(Near(lo.y, 0.0f, 0.05f) && hi.y > 1.5f && hi.y < 2.1f);
    }

    return TestResult();
}
