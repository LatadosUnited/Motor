#pragma once

// Sistema de componentes: cada tipo de componente é uma struct comum, registrada uma vez com a lista dos
// seus campos. A partir dessa descrição a engine e o editor fazem sozinhos o resto: copiar, comparar
// (desfazer), salvar/carregar na cena e desenhar no Inspector.
//
//   struct Girar { float velocidade = 90.0f; };
//   registry.Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);
//
// Extensões sem mexer em arquivos centrais:
// - Componentes da engine: cada módulo registra os seus num .cpp próprio com MOTOR_COMPONENT_MODULE (abaixo).
// - Tipo de campo novo (enum, referência a outro asset...): especialize FieldTraits<T> (abaixo). Comparação,
//   cena e cópia saem de graça; o Inspector usa o desenhador registrado pelo editor para esse tipo.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace motor {

class AssetLibrary;
class Mesh;
class Script;
class Texture2D;

// Custom: tipo descrito por FieldTraits<T> (CustomFieldType em FieldInfo::custom).
enum class FieldType { Bool, Int, Float, Vec2, Vec3, Vec4, String, Mesh, Texture, Custom };

// Tipo de campo definido fora da engine. As funções recebem o endereço do valor (um T).
struct CustomFieldType {
    std::string name;  // identificador do tipo ("LightKind"); o editor acha o desenhador por ele
    std::function<bool(const void* a, const void* b)> equal;
    // Valor como texto JSON (qualquer valor JSON: número, texto, objeto...). Referências a assets usam `assets`.
    std::function<std::string(const void* value, const AssetLibrary& assets)> toJson;
    // Lê o valor do texto JSON gravado por toJson. JSON inesperado: deixe o valor como está, preencha `error`
    // (se não for nulo) com o formato esperado e devolva false.
    std::function<bool(std::string_view json, void* value, AssetLibrary& assets, std::string* error)> fromJson;
};

// Para usar um tipo T novo em campos de componente, especialize no namespace motor:
//
//   template <> struct FieldTraits<LightKind> {
//       static CustomFieldType Describe() {
//           return {"LightKind",
//                   [](const void* a, const void* b) { return *(const LightKind*)a == *(const LightKind*)b; },
//                   [](const void* v, const AssetLibrary&) { return std::to_string(int(*(const LightKind*)v)); },
//                   [](std::string_view json, void* v, AssetLibrary&, std::string* error) { ... }};
//       }
//   };
template <class F> struct FieldTraits;

template <class F> struct FieldTypeOf;  // tipo C++ -> FieldType dos tipos embutidos
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
    // Nome antigo do campo: ao ler uma cena (ou recarregar os scripts) sem o nome atual, usa o valor gravado
    // com este. Renomeie o campo sem perder os valores já ajustados: .Field("forca", "Força", &X::forca,
    // {.formerKey = "velocidade"}).
    const char* formerKey = nullptr;
};

struct FieldInfo {
    std::string key;    // nome no arquivo da cena
    std::string label;  // nome no Inspector
    FieldType type = FieldType::Float;
    const CustomFieldType* custom = nullptr;  // só com type == Custom
    FieldOptions options;
    size_t offset = 0;                   // usado quando `access` é vazio (campos diretos)
    std::function<void*(void*)> access;  // componente -> endereço do valor (FieldRef)

    void* Get(void* component) const {
        return access ? access(component) : static_cast<void*>(static_cast<char*>(component) + offset);
    }
    const void* Get(const void* component) const {
        return access ? access(const_cast<void*>(component))
                      : static_cast<const void*>(static_cast<const char*>(component) + offset);
    }
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
    const std::type_info* cppType = nullptr;          // tipo C++ registrado (achar o tipo de T entre DLLs)

    bool IsScript() const { return static_cast<bool>(asScript); }
};

// Componente cujo tipo não está registrado: script removido ou renomeado, ou DLL de scripts ainda não
// carregada. Guarda o JSON original do componente para gravá-lo de volta igual e para religá-lo quando o
// tipo reaparecer (SceneSerializer::ResolveMissingComponents). Tipo: ComponentRegistry::MissingType().
struct MissingComponent {
    std::string type;  // nome do tipo que falta ("Girar")
    std::string json;  // objeto JSON completo do componente, como no arquivo da cena
};

// Mesmos valores em todos os campos registrados (estado não registrado não entra na comparação).
bool ComponentsEqual(const ComponentType& type, const void* a, const void* b);
[[noreturn]] void ThrowUnregisteredComponent();

namespace detail {
// Rótulo do Inspector a partir do nome do campo: "velocidadeMaxima" e "velocidade_maxima" -> "Velocidade maxima".
inline std::string LabelFromKey(std::string_view key) {
    std::string label;
    auto isUpper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto isLowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    for (size_t i = 0; i < key.size(); ++i) {
        const char c = key[i];
        if (c == '_') {
            if (!label.empty() && label.back() != ' ') label += ' ';
        } else if (isUpper(c) && i > 0 && isLowerOrDigit(key[i - 1])) {
            // "velocidadeMaxima": nova palavra, em minúscula; "corRGB": a sigla fica como está.
            label += ' ';
            const bool acronym = i + 1 < key.size() && isUpper(key[i + 1]);
            label += acronym ? c : static_cast<char>(c - 'A' + 'a');
        } else {
            label += c;
        }
    }
    while (!label.empty() && label.back() == ' ') label.pop_back();
    if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') label[0] = static_cast<char>(label[0] - 'a' + 'A');
    return label;
}

void AddComponentField(ComponentType& type, const char* key, const char* label, FieldType fieldType,
                       const CustomFieldType* custom, const FieldOptions& options,
                       std::function<void*(void*)> access);
void AddComponentFieldOffset(ComponentType& type, const char* key, const char* label, FieldType fieldType,
                             const CustomFieldType* custom, const FieldOptions& options, size_t offset);
} // namespace detail

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

    // Campo com o rótulo tirado do nome: .Field("velocidadeMaxima", &Girar::velocidadeMaxima) aparece como
    // "Velocidade maxima" no Inspector.
    template <class F>
    ComponentBuilder& Field(const char* key, F T::*member, FieldOptions options = {}) {
        return Field(key, detail::LabelFromKey(key).c_str(), member, options);
    }

    // Campo direto: .Field("velocidade", "Velocidade", &Girar::velocidade)
    template <class F>
    ComponentBuilder& Field(const char* key, const char* label, F T::*member, FieldOptions options = {}) {
        alignas(T) unsigned char storage[sizeof(T)];
        const T* sample = reinterpret_cast<const T*>(storage);
        const size_t offset = static_cast<size_t>(
            reinterpret_cast<const unsigned char*>(&(sample->*member)) - storage);
        if constexpr (requires { FieldTypeOf<F>::value; }) {
            detail::AddComponentFieldOffset(m_type, key, label, FieldTypeOf<F>::value, nullptr, options, offset);
        } else {
            static_assert(requires { FieldTraits<F>::Describe(); },
                          "tipo de campo não suportado: use um tipo embutido ou especialize motor::FieldTraits<T>");
            static const CustomFieldType custom = FieldTraits<F>::Describe();
            detail::AddComponentFieldOffset(m_type, key, label, FieldType::Custom, &custom, options, offset);
        }
        return *this;
    }

    // Campo dentro de outra struct: .FieldRef("cor", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; })
    template <class Getter>
    ComponentBuilder& FieldRef(const char* key, const char* label, Getter getter, FieldOptions options = {}) {
        using F = std::remove_cvref_t<std::invoke_result_t<Getter, T&>>;
        return Add<F>(key, label, options, [getter](void* c) -> void* { return &getter(*static_cast<T*>(c)); });
    }

    ComponentBuilder& OnAdd(std::function<void(T&, AssetLibrary&)> fn) {
        m_type.onAdd = [fn = std::move(fn)](void* c, AssetLibrary& assets) { fn(*static_cast<T*>(c), assets); };
        return *this;
    }

private:
    template <class F>
    ComponentBuilder& Add(const char* key, const char* label, FieldOptions options, std::function<void*(void*)> access) {
        if constexpr (requires { FieldTypeOf<F>::value; }) {
            detail::AddComponentField(m_type, key, label, FieldTypeOf<F>::value, nullptr, options, std::move(access));
        } else {
            static_assert(requires { FieldTraits<F>::Describe(); },
                          "tipo de campo não suportado: use um tipo embutido ou especialize motor::FieldTraits<T>");
            // Uma descrição por tipo (e por módulo: cada DLL tem a sua cópia, válida enquanto ela estiver carregada).
            static const CustomFieldType custom = FieldTraits<F>::Describe();
            detail::AddComponentField(m_type, key, label, FieldType::Custom, &custom, options, std::move(access));
        }
        return *this;
    }

    ComponentType& m_type;
};

class ComponentRegistry {
public:
    // Registro global. Na primeira chamada registra os componentes da engine (MeshRenderer...).
    static ComponentRegistry& Instance();

    template <class T>
    ComponentBuilder<T> Register(std::string_view name, std::string_view label) {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>,
                      "componentes precisam de construtor padrão e de cópia");
        Script* (*asScriptFn)(void*) = nullptr;
        if constexpr (std::is_base_of_v<Script, T>) {
            asScriptFn = [](void* p) -> Script* { return static_cast<T*>(p); };
        }
        ComponentType& ref = AddType(
            name, label, &typeid(T),
            []() -> void* { return static_cast<void*>(new T()); },
            [](const void* p) -> void* { return static_cast<void*>(new T(*static_cast<const T*>(p))); },
            [](void* p) { delete static_cast<T*>(p); },
            asScriptFn);
        return ComponentBuilder<T>(ref);
    }

    // Remove um tipo (scripts de uma DLL que vai ser descarregada). Não pode haver componentes dele vivos:
    // antes, troque-os por MissingComponent (SceneSerializer::DetachComponents).
    void Unregister(const ComponentType* type);

    const ComponentType* Find(std::string_view name) const;
    const std::vector<std::unique_ptr<ComponentType>>& Types() const { return m_types; }

    // Tipo dos componentes desconhecidos (MissingComponent). Não aparece em Types() nem em Find().
    static const ComponentType& MissingType();

    // Tipo registrado para T (nullptr se T não está registrado). Procura pelo typeid, e não por uma variável
    // estática por tipo, porque cada DLL teria a sua cópia dela: assim um script numa DLL acha o
    // MeshRenderer registrado pela engine. O resultado fica em cache neste módulo até o registro mudar.
    template <class T>
    static const ComponentType* TypeOf() {
        const ComponentRegistry& registry = Instance();
        static const ComponentType* cached = nullptr;
        static uint64_t cachedGeneration = 0;
        if (cachedGeneration != registry.m_generation) {
            cached = registry.FindCppType(typeid(T));
            cachedGeneration = registry.m_generation;
        }
        return cached;
    }

private:
    ComponentRegistry() = default;

    ComponentType& AddType(std::string_view name, std::string_view label, const std::type_info* cppType,
                           void* (*create)(), void* (*clone)(const void*), void (*destroy)(void*),
                           Script* (*asScript)(void*));

    const ComponentType* FindCppType(const std::type_info& info) const;

    std::vector<std::unique_ptr<ComponentType>> m_types;
    uint64_t m_generation = 1;  // muda a cada Register/Unregister (invalida os caches de TypeOf)
};

// ---------------------------------------------------------------------------
// Módulos de componentes da engine. Cada módulo (MeshRenderer, Câmera e, nas próximas fases, luzes, física,
// áudio...) registra os seus tipos num .cpp próprio, sem lista central para editar:
//
//   MOTOR_COMPONENT_MODULE(Fisica) {
//       registry.Register<Rigidbody>("Rigidbody", "Rigidbody").Field("massa", "Massa", &Rigidbody::massa);
//   }
//
// Os módulos rodam na primeira chamada de ComponentRegistry::Instance(), em ordem alfabética do nome do
// módulo (ordem estável: é a do menu "Adicionar componente"). Um módulo que só aparece depois disso (de
// uma DLL carregada mais tarde) é registrado na hora. Scripts de projeto usam MOTOR_SCRIPT (ScriptModule.h).
// ---------------------------------------------------------------------------
using ComponentModuleFn = void (*)(ComponentRegistry& registry);

struct ComponentModuleRegistrar {
    ComponentModuleRegistrar(const char* name, ComponentModuleFn fn);
};

#define MOTOR_COMPONENT_MODULE(Name)                                                                        \
    static void MotorComponentModule_##Name(::motor::ComponentRegistry& registry);                         \
    static const ::motor::ComponentModuleRegistrar g_motorComponentModule_##Name(#Name,                   \
                                                                                  &MotorComponentModule_##Name); \
    static void MotorComponentModule_##Name([[maybe_unused]] ::motor::ComponentRegistry& registry)

// Registra os módulos de componentes da engine (chamado por Instance()).
void RegisterBuiltInComponents(ComponentRegistry& registry);

} // namespace motor
