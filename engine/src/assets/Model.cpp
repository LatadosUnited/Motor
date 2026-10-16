#include "motor/assets/Model.h"

#include "motor/core/Log.h"
#include "motor/renderer/Mesh.h"

#include <algorithm>

namespace motor {

bool ModelData::IsJoint(int node) const {
    return std::any_of(skins.begin(), skins.end(), [&](const ModelSkin& s) {
        return std::find(s.joints.begin(), s.joints.end(), node) != s.joints.end();
    });
}

std::shared_ptr<Mesh> Model::FindMesh(std::string_view name) const {
    for (const NamedMesh& m : meshes) {
        if (m.name == name) return m.mesh;
    }
    return nullptr;
}

std::shared_ptr<AnimationClip> Model::FindClip(std::string_view name) const {
    for (const auto& clip : clips) {
        if (clip->name == name) return clip;
    }
    return nullptr;
}

std::shared_ptr<Model> BuildModel(ModelData data) {
    auto model = std::make_shared<Model>();

    // Esqueleto: todos os nós, na mesma ordem (pai antes do filho, garantido pelo leitor).
    auto skeleton = std::make_shared<Skeleton>();
    for (const ModelNode& n : data.nodes) skeleton->joints.push_back({n.name, n.parent, n.local});
    if (!skeleton->Finalize()) {
        log::Warn("Modelo: hierarquia de nós inválida; esqueleto ignorado");
        skeleton->joints.clear();
        skeleton->Finalize();
    }
    model->skeleton = skeleton;

    std::vector<std::shared_ptr<const Skin>> skins;
    for (const ModelSkin& s : data.skins) {
        auto skin = std::make_shared<Skin>();
        skin->skeleton = skeleton;
        skin->joints = s.joints;
        skin->inverseBind = s.inverseBind;
        skins.push_back(std::move(skin));
    }

    // Pele de cada primitiva: a do (primeiro) nó que usa a malha.
    std::vector<int> skinOfPrimitive(data.primitives.size(), -1);
    for (const ModelNode& n : data.nodes) {
        if (n.skin < 0) continue;
        for (int p : n.primitives) {
            if (skinOfPrimitive[static_cast<size_t>(p)] < 0) skinOfPrimitive[static_cast<size_t>(p)] = n.skin;
        }
    }

    for (size_t i = 0; i < data.primitives.size(); ++i) {
        const ModelPrimitive& p = data.primitives[i];
        const int skin = skinOfPrimitive[i];
        std::shared_ptr<Mesh> mesh;
        if (skin >= 0 && !p.weights.empty()) {
            mesh = std::make_shared<Mesh>(p.mesh.vertices, p.mesh.indices, p.weights);
            mesh->SetSkin(skins[static_cast<size_t>(skin)]);
        } else {
            mesh = std::make_shared<Mesh>(p.mesh.vertices, p.mesh.indices);
        }
        model->meshes.push_back({p.name, std::move(mesh)});
    }

    for (const AnimationClip& c : data.clips) {
        auto clip = std::make_shared<AnimationClip>(c);
        clip->skeleton = skeleton;
        model->clips.push_back(std::move(clip));
    }
    // Os vértices já estão nas malhas: a descrição guarda o resto (nós, materiais, imagens) para o importador.
    for (ModelPrimitive& p : data.primitives) {
        p.mesh = {};
        p.weights = {};
    }
    data.clips.clear();
    model->data = std::move(data);
    return model;
}

} // namespace motor
