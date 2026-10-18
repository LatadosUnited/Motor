#pragma once
#ifndef MOTOR_SCENE_SCRIPT_MODULE_H
#define MOTOR_SCENE_SCRIPT_MODULE_H

// Registro dos scripts de um projeto. Os .cpp da pasta Scripts/ do projeto são compilados pelo editor numa
// DLL; cada arquivo registra os seus scripts com MOTOR_SCRIPT, e o editor chama todos esses blocos ao
// carregar a DLL (de novo a cada recompilação):
//
//   #include <motor/scene/ScriptModule.h>
//
//   struct Girar : motor::Script {
//       float velocidade = 90.0f;
//       void OnUpdate(float dt) override { ... }
//   };
//
//   MOTOR_SCRIPT(Girar) {
//       registry.Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);
//   }
//
// O nome passado para Register é o que vai no arquivo da cena: renomear a struct não perde dados, renomear
// o nome registrado sim (o componente antigo fica guardado como "desconhecido" até o nome voltar).

#include "motor/scene/Component.h"
#include "motor/scene/Scene.h"
#include "motor/scene/Script.h"
#include "motor/scene/Transform.h"

namespace motor {

// Sobe quando a interface entre o editor e a DLL de scripts muda de forma incompatível.
// 2: FieldInfo ganhou `custom` (FieldTraits). 3: Script::GetSystem (sistemas do SceneRuntime).
// 4: Scene perdeu `light` (luzes viraram componentes). 5: eventos de colisão e trigger no Script (física).
// 6: MeshRenderer ganhou `sharedMaterial` (asset de material) e há o componente PrefabInstance (prefabs).
// 7: Script::LoadScene (troca de cena) e a UI do jogo (componentes Ui*, UiSystem).
// 8: componente Animator e Script::GetAnimator (animação esquelética); Mesh guarda os dados na CPU.
// 9: Material PBR (metal, rugosidade, mapas; sem `shininess`), sombra na Luz e o ambiente novo (Fase 7).
// 10: áudio (Fase 8): componentes AudioSource e AudioListener, AudioSystem.
// 11: Fase 16: PrefabRef e Script::Instantiate; roll na câmera.
inline constexpr int kScriptApiVersion = 11;

// Nomes das funções exportadas pela DLL de scripts (o ponto de entrada é gerado pelo editor).
inline constexpr const char* kScriptApiVersionSymbol = "MotorScriptApiVersion";
inline constexpr const char* kRegisterScriptsSymbol = "MotorRegisterScripts";
using ScriptApiVersionFn = int (*)();
using RegisterScriptsFn = void (*)(ComponentRegistry&);

namespace detail {
// Lista encadeada dos blocos MOTOR_SCRIPT do módulo (inline: cada DLL tem a sua própria lista, sem alocar heap).
struct ScriptRegistrar {
    RegisterScriptsFn fn;
    ScriptRegistrar* next = nullptr;
    static inline ScriptRegistrar* head = nullptr;
    static inline ScriptRegistrar* tail = nullptr;

    explicit ScriptRegistrar(RegisterScriptsFn f) noexcept : fn(f) {
        if (tail) tail->next = this;
        else head = this;
        tail = this;
    }
};

inline void RegisterModuleScripts(ComponentRegistry& registry) {
    for (const ScriptRegistrar* r = ScriptRegistrar::head; r; r = r->next) r->fn(registry);
}
} // namespace detail

} // namespace motor

#define MOTOR_SCRIPT(Name)                                                                           \
    static void MotorRegisterScript_##Name(::motor::ComponentRegistry& registry);                   \
    static ::motor::detail::ScriptRegistrar g_motorScriptRegistrar_##Name{&MotorRegisterScript_##Name}; \
    static void MotorRegisterScript_##Name([[maybe_unused]] ::motor::ComponentRegistry& registry)

#endif // MOTOR_SCENE_SCRIPT_MODULE_H
