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
// Ou, sem rótulos próprios: MOTOR_FIELDS(Girar, velocidade) (abaixo).
//
// O nome passado para Register é o que vai no arquivo da cena: renomear a struct não perde dados, renomear
// o nome registrado sim (o componente antigo fica guardado como "desconhecido" até o nome voltar).

#include "motor/scene/Component.h"
#include "motor/scene/EntityRef.h"
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
// 12: Fase 17: EntityRef, Script::OnMessage/Send/Broadcast, temporizadores, aleatórios, GetComponent,
//     FieldOptions::formerKey.
// 13: Fase 18: camadas de colisão (campo layer no Collider e no CharacterController, máscara no Raycast),
//     consultas de área, SetCharacterVelocity, useGravity; PlayOneShot e grupo de volume no AudioSource.
// 14: Fase 21: opções do jogador (GetOptions/ApplyOptions/Resolutions), QuitGame, GameVersion e saves
//     (SaveSlot/HasSave/DeleteSave/SaveFolder, motor::SaveData), ações de entrada (IsActionDown, GetAxis...) e
//     controle (GamepadButton/GamepadAxis).
// 15: campo shadows (ShadowCasting: com sombra, sem sombra, só sombra) no MeshRenderer.
// 16: Fase 25: Entity::cache (matriz de mundo guardada pela Scene) muda o tamanho de Entity.
// 17: Fase 35: NavMeshAgent ganhou raio, altura e desvio (multidão); o NavSystem guarda a multidão.
// 18: Fase 37: o NavSystem guarda o SceneRuntime (RandomPoint); componentes e sistema de percepção novos.
// 19: Fase 25: EntityCache::owner (índices da cena acompanham Add e Remove) muda o tamanho de Entity.
// 20: Fase 26: a lista de objetos do SubmitTo guardada na Scene muda o tamanho de Scene.
// 21: entrada v2: Script::GetVector e TextInput; InputAction e InputBinding com campos novos (tipo, part, with);
//     vários controles (pad e device nas funções de entrada, GamepadCount, LastInputDevice...); contextos
//     (SetInputContext...), IsPointerOverUi; segurar, toque, toque duplo e buffer (WasActionHeld...).
// 22: Fase 28: MeshRenderer::isStatic e os estáticos guardados na Scene (MarkRenderDirty) mudam os tamanhos; a Mesh
//     guarda onde está na GPU (Mesh::Gpu) no lugar do VAO próprio; componentes e entidades em blocos por tipo
//     (ComponentRegistry::Register constrói na memória do BlockPool).
// 23: Fase 28: componente Terrain; os estáticos guardados na Scene ganham a origem de cada objeto (malha e matriz, para
//     o Raycast) e a versão dos terrenos, mudando o tamanho de Scene (MarkRenderDirty é inline).
inline constexpr int kScriptApiVersion = 23;

// Nomes das funções exportadas pela DLL de scripts (o ponto de entrada é gerado pelo editor).
inline constexpr const char* kScriptApiVersionSymbol = "MotorScriptApiVersion";
inline constexpr const char* kRegisterScriptsSymbol = "MotorRegisterScripts";
using ScriptApiVersionFn = int (*)();
using RegisterScriptsFn = void (*)(ComponentRegistry&);

template <class T>
T* Script::GetComponent() {
    return GetEntity().template Get<T>();
}

template <class T>
T* Script::GetComponent(EntityId id) {
    Entity* e = GetEntity(id);
    return e ? e->template Get<T>() : nullptr;
}

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

// Registro curto: o script com o nome da struct e os campos listados, com o rótulo tirado do nome do campo
// ("velocidadeMaxima" aparece como "Velocidade maxima"). Para rótulo próprio, slider, cor ou tooltip, use o
// bloco MOTOR_SCRIPT com .Field(...) completo.
//
//   MOTOR_FIELDS(Jogador, velocidade, pulo, vidas)
#define MOTOR_FIELDS(Type, ...)                                                                       \
    MOTOR_SCRIPT(Type) {                                                                              \
        registry.Register<Type>(#Type, #Type) MOTOR_DETAIL_FOR_EACH(MOTOR_DETAIL_FIELD, Type, __VA_ARGS__); \
    }

#define MOTOR_DETAIL_FIELD(Type, member) .Field(#member, &Type::member)
// Repete `macro(Type, x)` para cada x da lista (C++20 __VA_OPT__; até 256 itens).
#define MOTOR_DETAIL_PARENS ()
#define MOTOR_DETAIL_EXPAND(...) MOTOR_DETAIL_EXPAND3(MOTOR_DETAIL_EXPAND3(MOTOR_DETAIL_EXPAND3(MOTOR_DETAIL_EXPAND3(__VA_ARGS__))))
#define MOTOR_DETAIL_EXPAND3(...) MOTOR_DETAIL_EXPAND2(MOTOR_DETAIL_EXPAND2(MOTOR_DETAIL_EXPAND2(MOTOR_DETAIL_EXPAND2(__VA_ARGS__))))
#define MOTOR_DETAIL_EXPAND2(...) MOTOR_DETAIL_EXPAND1(MOTOR_DETAIL_EXPAND1(MOTOR_DETAIL_EXPAND1(MOTOR_DETAIL_EXPAND1(__VA_ARGS__))))
#define MOTOR_DETAIL_EXPAND1(...) __VA_ARGS__
#define MOTOR_DETAIL_FOR_EACH(macro, Type, ...) \
    __VA_OPT__(MOTOR_DETAIL_EXPAND(MOTOR_DETAIL_FOR_EACH_HELPER(macro, Type, __VA_ARGS__)))
#define MOTOR_DETAIL_FOR_EACH_HELPER(macro, Type, first, ...) \
    macro(Type, first) __VA_OPT__(MOTOR_DETAIL_FOR_EACH_AGAIN MOTOR_DETAIL_PARENS(macro, Type, __VA_ARGS__))
#define MOTOR_DETAIL_FOR_EACH_AGAIN() MOTOR_DETAIL_FOR_EACH_HELPER

#endif // MOTOR_SCENE_SCRIPT_MODULE_H
