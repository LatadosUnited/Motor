// Importação de FBX (ufbx): o mesmo robô de exemplo em FBX (tests/data/Robo.fbx, exportado pelo Blender a partir
// do engine/assets/models/Robo.glb) tem de virar o mesmo modelo que o glTF: clipes, esqueleto, pele, materiais e,
// principalmente, a mesma malha deformada nas mesmas poses (escala, eixos e pele certos).
#include "TestUtil.h"

#include <motor/assets/Model.h>
#include <motor/renderer/Mesh.h>
#include <motor/scene/Animator.h>

#include <glm/common.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {
bool Near(float a, float b, float eps) {
    return std::abs(a - b) <= eps;
}

const AnimationClip* FindClip(const ModelData& data, const std::string& name) {
    for (const auto& c : data.clips) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

struct Bounds {
    glm::vec3 lo{1e9f};
    glm::vec3 hi{-1e9f};
};

// Caixa da malha deformada (como o shader faz) no espaço do modelo, no instante `time` do clipe.
Bounds SkinnedBounds(const std::shared_ptr<Model>& model, const ModelData& data, const char* clip, float time) {
    Animator anim;
    anim.model = model;
    CHECK(anim.Play(clip, true));
    anim.Update(time);
    const auto globals = anim.GlobalPose();
    Bounds b;
    std::vector<glm::mat4> joints;
    for (size_t n = 0; n < data.nodes.size(); ++n) {
        for (int p : data.nodes[n].primitives) {
            const ModelPrimitive& prim = data.primitives[static_cast<size_t>(p)];
            const auto& mesh = model->meshes[static_cast<size_t>(p)].mesh;
            if (!mesh->IsSkinned() || prim.weights.empty()) continue;
            mesh->GetSkin()->JointMatrices(globals, joints);
            const glm::mat4 entity = globals[n];  // a entidade da malha, como no Scene::SubmitTo
            for (size_t v = 0; v < prim.mesh.vertices.size(); ++v) {
                const glm::vec3 local = SkinPosition(prim.mesh.vertices[v].position, prim.weights[v], joints);
                const glm::vec3 world = glm::vec3(entity * glm::vec4(local, 1.0f));
                b.lo = glm::min(b.lo, world);
                b.hi = glm::max(b.hi, world);
            }
        }
    }
    return b;
}

bool SameBounds(const Bounds& a, const Bounds& b, float eps) {
    for (int i = 0; i < 3; ++i) {
        if (!Near(a.lo[i], b.lo[i], eps) || !Near(a.hi[i], b.hi[i], eps)) return false;
    }
    return true;
}
} // namespace

int main() {
    const fs::path fbxFile = fs::path(MOTOR_TEST_DATA_DIR) / "Robo.fbx";
    const fs::path glbFile = fs::path(MOTOR_ENGINE_ASSETS_DIR) / "models" / "Robo.glb";

    SECTION("Robo.fbx: clipes, pele, materiais e textura embutida");
    ModelData fbx;
    ModelData glb;
    {
        std::string error;
        CHECK(LoadFbx(fbxFile, fbx, &error));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        CHECK(LoadGltf(glbFile, glb, &error));
        // Nomes dos clipes sem o "Armature|" que o Blender põe.
        for (const char* name : {"Parado", "Andando", "Pulando"}) {
            const AnimationClip* a = FindClip(fbx, name);
            const AnimationClip* b = FindClip(glb, name);
            CHECK(a && b && !a->channels.empty());
            if (a && b) {
                std::printf("    %s: FBX %.3f s, glTF %.3f s\n", name, a->duration, b->duration);
                CHECK(Near(a->duration, b->duration, 0.05f));
            }
        }
        CHECK(fbx.skins.size() == 1 && !fbx.primitives.empty() && !fbx.primitives[0].weights.empty());
        CHECK(fbx.materials.size() >= 2);
        CHECK(fbx.images.size() == 1 && !fbx.images[0].bytes.empty());  // textura embutida no FBX
        // Só as malhas com pele: o Blender exporta também a forma de exibição dos ossos (uma esfera solta).
        auto sceneTriangles = [](const ModelData& d) {
            size_t total = 0;
            for (const ModelNode& n : d.nodes) {
                if (n.skin < 0) continue;
                for (int p : n.primitives) total += d.primitives[static_cast<size_t>(p)].mesh.indices.size() / 3;
            }
            return total;
        };
        const size_t triangles = sceneTriangles(fbx), glbTriangles = sceneTriangles(glb);
        std::printf("    triângulos: FBX %zu, glTF %zu; nós: FBX %zu, glTF %zu\n", triangles, glbTriangles,
                    fbx.nodes.size(), glb.nodes.size());
        CHECK(triangles == glbTriangles);
        // Pesos de cada vértice somam 1.
        bool normalized = true;
        for (const auto& p : fbx.primitives) {
            for (const SkinWeights& w : p.weights) normalized &= Near(w.weights.x + w.weights.y + w.weights.z + w.weights.w, 1.0f, 1e-3f);
        }
        CHECK(normalized);
    }

    SECTION("Mesma malha deformada que o glTF: repouso e poses dos clipes (metros, Y para cima)");
    {
        const ModelData fbxData = fbx, glbData = glb;  // BuildModel leva os vértices para as malhas
        const auto a = BuildModel(std::move(fbx));
        const auto b = BuildModel(std::move(glb));
        CHECK(a->skeleton && a->skeleton->joints.size() <= 128);
        for (const auto& [clip, time] : {std::pair{"Parado", 0.0f}, {"Andando", 0.35f}, {"Pulando", 0.4f}}) {
            const Bounds fa = SkinnedBounds(a, fbxData, clip, time);
            const Bounds fb = SkinnedBounds(b, glbData, clip, time);
            std::printf("    %s %.2f s: FBX (%.2f %.2f %.2f)-(%.2f %.2f %.2f)  glTF (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", clip,
                        time, fa.lo.x, fa.lo.y, fa.lo.z, fa.hi.x, fa.hi.y, fa.hi.z, fb.lo.x, fb.lo.y, fb.lo.z, fb.hi.x,
                        fb.hi.y, fb.hi.z);
            CHECK(SameBounds(fa, fb, 0.02f));
        }
    }

    SECTION("Nome do clipe pelo arquivo (Mixamo: \"X Bot@Idle.fbx\" com a pilha \"mixamo.com\")");
    {
        // Um FBX ASCII mínimo: um nó animado numa pilha de nome genérico.
        const std::string ascii = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Model: 1000, "Model::Osso", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,100,0
		}
	}
	AnimationStack: 2000, "AnimStack::mixamo.com", "" {
		Properties70:  {
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 3000, "AnimLayer::BaseLayer", "" {
	}
	AnimationCurveNode: 4000, "AnimCurveNode::T", "" {
		Properties70:  {
			P: "d|X", "Number", "", "A",0
			P: "d|Y", "Number", "", "A",100
			P: "d|Z", "Number", "", "A",0
		}
	}
	AnimationCurve: 5000, "AnimCurve::", "" {
		Default: 0
		KeyVer: 4009
		KeyTime: *2 {
			a: 0,46186158000
		}
		KeyValueFloat: *2 {
			a: 100,200
		}
		KeyAttrFlags: *1 {
			a: 24840
		}
		KeyAttrDataFloat: *4 {
			a: 0,0,0,0
		}
		KeyAttrRefCount: *1 {
			a: 2
		}
	}
}
Connections:  {
	C: "OO",1000,0
	C: "OO",2000,0
	C: "OO",3000,2000
	C: "OO",4000,3000
	C: "OP",4000,1000, "Lcl Translation"
	C: "OP",5000,4000, "d|Y"
}
)";
        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(ascii.data()), ascii.size());
        ModelData data;
        std::string error;
        CHECK(ParseFbx(bytes, ".", data, &error, "X Bot@Idle"));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        CHECK(data.clips.size() == 1 && data.clips[0].name == "Idle");
        CHECK(!data.clips.empty() && Near(data.clips[0].duration, 1.0f, 0.05f));
        // Centímetros viram metros: o osso a 100 cm fica a 1 m.
        if (!data.nodes.empty()) std::printf("    nós %zu, osso y = %.3f\n", data.nodes.size(), data.nodes[0].local.translation.y);
        CHECK(data.nodes.size() == 1 && Near(data.nodes[0].local.translation.y, 1.0f, 1e-3f));
        ModelData other;
        CHECK(ParseFbx(bytes, ".", other, &error, "Andar"));  // sem "@": o nome do arquivo inteiro
        CHECK(!other.clips.empty() && other.clips[0].name == "Andar");
        CHECK(!ParseFbx(std::span<const uint8_t>(), ".", data, &error) && !error.empty());  // vazio: erro, sem queda
    }

    return TestResult();
}
