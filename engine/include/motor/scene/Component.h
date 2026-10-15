#pragma once

// Sistema de componentes: cada tipo de componente é uma struct comum, registrada uma vez com a lista dos
// seus campos. A partir dessa descrição a engine e o editor fazem sozinhos o resto: copiar, comparar
// (desfazer), salvar/carregar na cena e desenhar no Inspector.
//
//   struct Girar { float velocidade = 90.0f; };
//   registry.Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace motor {

class AssetLibrary;
class Mesh;
class Script;
class Texture2D;

enum class FieldType { Bool, Int, Float, Vec2, Vec3, Vec4, String, Mesh, Texture };

template <class F> struct FieldTypeOf;  // tipo C++ -> FieldType (só os suportados compilam)
template <> struct FieldTypeOf<bool> { static constexpr FieldType value = FieldType::Bool; };
template <> struct FieldTypeOf<int> { static constexpr FieldType value = FieldType::Int; };
template <> struct FieldTypeOf<float> { static constexpr FieldType value = FieldType::Float; };
template <> struct FieldTypeOf<glm::vec2> { static constexpr FieldType value = FieldType::Vec2; };
template <> struct FieldTypeOf<glm::vec3> { static constexpr FieldType value = FieldType::Vec3; };
template <> struct FieldTypeOf<glm::vec4> { static constexpr FieldType value = FieldType::Vec4; };
template <> struct FieldTypeOf<std::string> { static constexpr FieldType value = FieldType::String; };
template <> struct FieldTypeOf<std::shared_ptr<Mesh>> { static constexpr FieldType value = FieldType::Mesh; };
template <> struct FieldTypeOf<std::shared_ptr<Texture2D>> { static constexpr FieldType value = FieldType::Texture; };

// Como o campo aparece no Inspector. Use com inicializadores designados, na ordem declarada:
//   {.slider = true, .min = 0.0f, .max = 2.0f}
struct FieldOptions {
    bool color = false;        // vec3/vec4 como seletor de cor
    bool slider = false;       // float/int como slider entre min e max (senão, arrastar)
    bool logarithmic = false;  // slider logarítmico
    float speed = 0.05f;       // sensibilidade ao arrastar
    float min = 0.0f;          // min == max: sem limite
    float max = 0.0f;
    const char* format = nullptr;  // printf; nulo = padrão do tipo
    const char* tooltip = nullptr;
};

struct FieldInfo {
    std::string key;    // nome no arquivo da cena
    std::string label;  // nome no Inspector
    FieldType type = FieldType::Float;
    FieldOptions options;
    std::function<void*(void*)> access;  // componente -> endereço do valor

    void* Get(void* component) const { return access(component); }
    const void* Get(const void* component) const { return access(const_cast<void*>(component)); }
    template <class F> F& As(void* component) const { return *static_cast<F*>(Get(component)); }
    template <class F> const F& As(const void* component) const { return *static_cast<const F*>(Get(component)); }
};

struct ComponentType {
    std::string name;   // identificador estável, gravado na cena ("MeshRenderer")
    std::string label;  // nome no editor ("Mesh Renderer")
    std::vector<FieldInfo> fields;

    std::function<void*()> create;
    std::function<void*(const void*)> clone;
    std::function<void(void*)> destroy;
    std::function<void(void*, AssetLibrary&)> onAdd;  // opcional: valores iniciais ao adicionar pelo editor
    std::function<Script*(void*)> asScript;           // só para tipos que herdam de Script (Script.h)

    bool IsScript() const { return static_cast<bool>(asScript); }
};

// Mesmos valores em todos os campos registrados (estado não registrado não entra na comparação).
bool ComponentsEqual(const ComponentType& type, const void* a, const void* b);

// Dono de um componente de tipo conhecido só em tempo de execução. Copiar duplica o conteúdo.
class ComponentPtr {
public:
    explicit ComponentPtr(const ComponentType& type) : m_type(&type), m_data(type.create()) {}
    ComponentPtr(const ComponentPtr& other) : m_type(other.m_type), m_data(other.m_type->clone(other.m_data)) {}
    ComponentPtr(ComponentPtr&& other) noexcept : m_type(other.m_type), m_data(std::exchange(other.m_data, nullptr)) {}
    ComponentPtr& operator=(const ComponentPtr& other) {
        if (this != &other) *this = ComponentPtr(other);
        return *this;
    }
    ComponentPtr& operator=(ComponentPtr&& other) noexcept {
        std::swap(m_type, other.m_type);
        std::swap(m_data, other.m_data);
        return *this;
    }
    ~ComponentPtr() {
        if (m_data) m_type->destroy(m_data);
    }

    const ComponentType& Type() const { return *m_type; }
    void* Data() { return m_data; }
    const void* Data() const { return m_data; }

private:
    const ComponentType* m_type;
    void* m_data;
};

template <class T>
class ComponentBuilder {
public:
    explicit ComponentBuilder(ComponentType& type) : m_type(type) {}

    // Campo direto: .Field("velocidade", "Velocidade", &Girar::velocidade)
    template <class F>
    ComponentBuilder& Field(const char* key, const char* label, F T::*member, FieldOptions options = {}) {
        return Add(key, label, FieldTypeOf<F>::value, options,
                   [member](void* c) -> void* { return &(static_cast<T*>(c)->*member); });
    }

    // Campo dentro de outra struct: .FieldRef("cor", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; })
    template <class Getter>
    ComponentBuilder& FieldRef(const char* key, const char* label, Getter getter, FieldOptions options = {}) {
        using F = std::remove_cvref_t<std::invoke_result_t<Getter, T&>>;
        return Add(key, label, FieldTypeOf<F>::value, options,
                   [getter](void* c) -> void* { return &getter(*static_cast<T*>(c)); });
    }

    ComponentBuilder& OnAdd(std::function<void(T&, AssetLibrary&)> fn) {
        m_type.onAdd = [fn = std::move(fn)](void* c, AssetLibrary& assets) { fn(*static_cast<T*>(c), assets); };
        return *this;
    }

private:
    ComponentBuilder& Add(const char* key, const char* label, FieldType type, FieldOptions options,
                          std::function<void*(void*)> access) {
        m_type.fields.push_back({key, label, type, options, std::move(access)});
        return *this;
    }

    ComponentType& m_type;
};

class ComponentRegistry {
public:
    // Registro global. Na primeira chamada registra os componentes da engine (MeshRenderer...).
    static ComponentRegistry& Instance();

    template <class T>
    ComponentBuilder<T> Register(std::string name, std::string label) {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>,
                      "componentes precisam de construtor padrão e de cópia");
        auto type = std::make_unique<ComponentType>();
        type->name = std::move(name);
        type->label = std::move(label);
        type->create = [] { return static_cast<void*>(new T()); };
        type->clone = [](const void* p) { return static_cast<void*>(new T(*static_cast<const T*>(p))); };
        type->destroy = [](void* p) { delete static_cast<T*>(p); };
        // Script só precisa estar completo aqui se T herdar dele (e aí, por herança, já está).
        if constexpr (std::is_base_of_v<Script, T>) {
            type->asScript = [](void* p) -> Script* { return static_cast<T*>(p); };
        }
        ComponentType& ref = *type;
        m_types.push_back(std::move(type));
        Slot<T>() = &ref;
        return ComponentBuilder<T>(ref);
    }

    const ComponentType* Find(std::string_view name) const;
    const std::vector<std::unique_ptr<ComponentType>>& Types() const { return m_types; }

    // Tipo registrado para T (nullptr se T nunca foi registrado).
    template <class T>
    static const ComponentType* TypeOf() {
        Instance();
        return Slot<T>();
    }

private:
    ComponentRegistry() = default;

    template <class T>
    static const ComponentType*& Slot() {
        static const ComponentType* slot = nullptr;
        return slot;
    }

    std::vector<std::unique_ptr<ComponentType>> m_types;
};

// Registra os componentes que vêm com a engine (Components.cpp).
void RegisterBuiltInComponents(ComponentRegistry& registry);

} // namespace motor
