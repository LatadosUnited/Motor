#include "motor/scene/Component.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/Components.h"

#include <algorithm>
#include <cstring>

namespace motor {

namespace {
struct ComponentModule {
    const char* name;
    ComponentModuleFn fn;
};

// Estáticas locais: os registradores rodam na inicialização estática, em qualquer ordem entre os .cpp.
std::vector<ComponentModule>& Modules() {
    static std::vector<ComponentModule> modules;
    return modules;
}

bool& ModulesRegistered() {
    static bool registered = false;
    return registered;
}
} // namespace

ComponentModuleRegistrar::ComponentModuleRegistrar(const char* name, ComponentModuleFn fn) {
    if (ModulesRegistered()) {
        fn(ComponentRegistry::Instance());  // módulo de uma DLL carregada depois: entra na hora
    } else {
        Modules().push_back({name, fn});
    }
}

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

const ComponentType* ComponentRegistry::FindCppType(const std::type_info& info) const {
    // type_info::operator== compara pelo nome do tipo, então funciona entre a engine e as DLLs de scripts.
    for (const auto& type : m_types) {
        if (type->cppType && *type->cppType == info) return type.get();
    }
    if (info == typeid(MissingComponent)) return &MissingType();
    return nullptr;
}

void ComponentRegistry::Unregister(const ComponentType* type) {
    std::erase_if(m_types, [&](const auto& t) { return t.get() == type; });
    ++m_generation;
}

const ComponentType& ComponentRegistry::MissingType() {
    static const ComponentType type = [] {
        ComponentType t;
        Describe<MissingComponent>(t, "MissingComponent", "Componente desconhecido");
        // Campos só para cópia e comparação (desfazer); o serializador grava o JSON guardado, não estes.
        ComponentBuilder<MissingComponent>(t)
            .Field("type", "Tipo", &MissingComponent::type)
            .Field("json", "Dados", &MissingComponent::json);
        return t;
    }();
    return type;
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
            case FieldType::Custom: same = f.custom->equal(f.Get(a), f.Get(b)); break;
        }
        if (!same) return false;
    }
    return true;
}

void RegisterBuiltInComponents(ComponentRegistry& registry) {
    // Ordem alfabética do nome do módulo: estável, não depende da ordem de ligação dos .cpp.
    std::vector<ComponentModule> modules = Modules();
    std::sort(modules.begin(), modules.end(),
              [](const ComponentModule& a, const ComponentModule& b) { return std::strcmp(a.name, b.name) < 0; });
    for (const ComponentModule& module : modules) module.fn(registry);
    ModulesRegistered() = true;
}

} // namespace motor
