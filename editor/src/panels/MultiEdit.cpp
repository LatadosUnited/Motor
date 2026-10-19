#include "panels/MultiEdit.h"

#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>

namespace editor {

namespace {
using motor::FieldInfo;
using motor::FieldType;

bool FieldEqual(const FieldInfo& f, const void* a, const void* b) {
    switch (f.type) {
        case FieldType::Bool: return f.As<bool>(a) == f.As<bool>(b);
        case FieldType::Int: return f.As<int>(a) == f.As<int>(b);
        case FieldType::Float: return f.As<float>(a) == f.As<float>(b);
        case FieldType::Vec2: return f.As<glm::vec2>(a) == f.As<glm::vec2>(b);
        case FieldType::Vec3: return f.As<glm::vec3>(a) == f.As<glm::vec3>(b);
        case FieldType::Vec4: return f.As<glm::vec4>(a) == f.As<glm::vec4>(b);
        case FieldType::String: return f.As<std::string>(a) == f.As<std::string>(b);
        case FieldType::Mesh: return f.As<std::shared_ptr<motor::Mesh>>(a) == f.As<std::shared_ptr<motor::Mesh>>(b);
        case FieldType::Texture:
            return f.As<std::shared_ptr<motor::Texture2D>>(a) == f.As<std::shared_ptr<motor::Texture2D>>(b);
        case FieldType::Custom: return f.custom->equal(f.Get(a), f.Get(b));
    }
    return true;
}

void CopyField(const FieldInfo& f, const void* from, void* to, motor::AssetLibrary& assets) {
    switch (f.type) {
        case FieldType::Bool: f.As<bool>(to) = f.As<bool>(from); break;
        case FieldType::Int: f.As<int>(to) = f.As<int>(from); break;
        case FieldType::Float: f.As<float>(to) = f.As<float>(from); break;
        case FieldType::Vec2: f.As<glm::vec2>(to) = f.As<glm::vec2>(from); break;
        case FieldType::Vec3: f.As<glm::vec3>(to) = f.As<glm::vec3>(from); break;
        case FieldType::Vec4: f.As<glm::vec4>(to) = f.As<glm::vec4>(from); break;
        case FieldType::String: f.As<std::string>(to) = f.As<std::string>(from); break;
        case FieldType::Mesh:
            f.As<std::shared_ptr<motor::Mesh>>(to) = f.As<std::shared_ptr<motor::Mesh>>(from);
            break;
        case FieldType::Texture:
            f.As<std::shared_ptr<motor::Texture2D>>(to) = f.As<std::shared_ptr<motor::Texture2D>>(from);
            break;
        case FieldType::Custom:
            // Tipos de campo novos só sabem ler e gravar JSON: a cópia passa por ele (referências por GUID).
            f.custom->fromJson(f.custom->toJson(f.Get(from), assets), f.Get(to), assets, nullptr);
            break;
    }
}

// Eixos de `b` diferentes de `a` passam para `target`.
void CopyChangedAxes(const glm::vec3& a, const glm::vec3& b, glm::vec3& target) {
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) target[i] = b[i];
    }
}

bool Skipped(const motor::ComponentType& type) {
    return &type == motor::ComponentRegistry::TypeOf<motor::PrefabInstance>() ||
           &type == motor::ComponentRegistry::TypeOf<motor::PrefabNested>() ||
           &type == &motor::ComponentRegistry::MissingType();
}
} // namespace

void PropagateEdits(const motor::Entity& before, const motor::Entity& after, motor::Entity& target,
                    motor::AssetLibrary& assets) {
    if (before.active != after.active) target.active = after.active;

    motor::Transform& t = target.transform;
    CopyChangedAxes(before.transform.position, after.transform.position, t.position);
    CopyChangedAxes(before.transform.scale, after.transform.scale, t.scale);
    if (before.transform.rotation != after.transform.rotation) {
        const glm::vec3 a = glm::degrees(glm::eulerAngles(before.transform.rotation));
        const glm::vec3 b = glm::degrees(glm::eulerAngles(after.transform.rotation));
        glm::vec3 degrees = glm::degrees(glm::eulerAngles(t.rotation));
        // Tolerância: a ida e volta quat -> euler muda os outros eixos em frações de grau.
        for (int i = 0; i < 3; ++i) {
            if (std::abs(a[i] - b[i]) > 1e-3f) degrees[i] = b[i];
        }
        t.SetEulerDegrees(degrees);
    }

    for (const motor::ComponentPtr& component : after.components) {
        const motor::ComponentType& type = component.Type();
        if (Skipped(type)) continue;
        const void* old = before.Get(type);
        void* dst = target.Get(type);
        if (!old) {
            if (!dst) target.components.push_back(component);  // adicionado na principal
            continue;
        }
        if (!dst) continue;
        for (const FieldInfo& f : type.fields) {
            if (!FieldEqual(f, old, component.Data())) CopyField(f, component.Data(), dst, assets);
        }
    }
    for (const motor::ComponentPtr& component : before.components) {
        const motor::ComponentType& type = component.Type();
        if (!Skipped(type) && !after.Get(type)) target.Remove(type);  // removido na principal
    }
}

} // namespace editor
