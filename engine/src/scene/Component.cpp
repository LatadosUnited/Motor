#include "motor/scene/Component.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/Components.h"

namespace motor {

ComponentRegistry& ComponentRegistry::Instance() {
    static ComponentRegistry registry;
    static const bool builtInsRegistered = (RegisterBuiltInComponents(registry), true);
    (void)builtInsRegistered;
    return registry;
}

const ComponentType* ComponentRegistry::Find(std::string_view name) const {
    for (const auto& type : m_types) {
        if (type->name == name) return type.get();
    }
    return nullptr;
}

bool ComponentsEqual(const ComponentType& type, const void* a, const void* b) {
    for (const FieldInfo& f : type.fields) {
        bool same = true;
        switch (f.type) {
            case FieldType::Bool: same = f.As<bool>(a) == f.As<bool>(b); break;
            case FieldType::Int: same = f.As<int>(a) == f.As<int>(b); break;
            case FieldType::Float: same = f.As<float>(a) == f.As<float>(b); break;
            case FieldType::Vec2: same = f.As<glm::vec2>(a) == f.As<glm::vec2>(b); break;
            case FieldType::Vec3: same = f.As<glm::vec3>(a) == f.As<glm::vec3>(b); break;
            case FieldType::Vec4: same = f.As<glm::vec4>(a) == f.As<glm::vec4>(b); break;
            case FieldType::String: same = f.As<std::string>(a) == f.As<std::string>(b); break;
            case FieldType::Mesh: same = f.As<std::shared_ptr<Mesh>>(a) == f.As<std::shared_ptr<Mesh>>(b); break;
            case FieldType::Texture:
                same = f.As<std::shared_ptr<Texture2D>>(a) == f.As<std::shared_ptr<Texture2D>>(b);
                break;
        }
        if (!same) return false;
    }
    return true;
}

void RegisterBuiltInComponents(ComponentRegistry& registry) {
    // A ordem dos campos é a do Inspector e a do arquivo.
    registry.Register<MeshRenderer>("MeshRenderer", "Mesh Renderer")
        .Field("mesh", "Malha", &MeshRenderer::mesh)
        .FieldRef("texture", "Textura", [](MeshRenderer& m) -> auto& { return m.material.albedo; })
        .FieldRef("color", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; }, {.color = true})
        .FieldRef("specular", "Especular", [](MeshRenderer& m) -> auto& { return m.material.specular; },
                  {.slider = true, .min = 0.0f, .max = 2.0f})
        .FieldRef("shininess", "Brilho", [](MeshRenderer& m) -> auto& { return m.material.shininess; },
                  {.slider = true, .logarithmic = true, .min = 1.0f, .max = 256.0f, .format = "%.0f"})
        .OnAdd([](MeshRenderer& m, AssetLibrary& assets) { m.mesh = assets.FindMesh("Cubo"); });
}

} // namespace motor
