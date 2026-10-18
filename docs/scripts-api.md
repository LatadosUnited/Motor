---
tipo: guia
atualizado: 2026-10-17
---

# Referência da API dos scripts

Tudo que um script do Motor pode usar, conferido com os headers da engine. Para aprender passo a passo, leia
primeiro o [guia](scripts.md). Fontes, se quiser ir além: `engine/include/motor/scene/Script.h`,
`ScriptModule.h`, `Scene.h`, `Component.h`, `Animator.h`, `engine/include/motor/physics/PhysicsSystem.h`,
`engine/include/motor/ui/UiSystem.h` e `engine/include/motor/audio/AudioSystem.h`.

O teste `test_script_docs` confere que esta página cita todos os métodos públicos de `motor::Script`, de
`motor::PhysicsSystem`, de `motor::UiSystem` e de `motor::AudioSystem`, todas as opções de campo e os campos de
`GameTime`, `Collision`, `UiPointer`, `AudioSettings` e `AudioVoiceInfo`. Mudou um desses
headers? Atualize aqui.

Versão da interface entre o editor e a DLL de scripts: `kScriptApiVersion` = 10 (`ScriptModule.h`). Ela sobe
quando a interface muda de forma incompatível; o editor recusa uma DLL compilada com outra versão (basta
recompilar os scripts).

## Includes

| Include | Traz |
|---|---|
| `<motor/scene/ScriptModule.h>` | o necessário para qualquer script: `motor::Script`, `MOTOR_SCRIPT`, `Scene`, `Entity`, `Transform`, os componentes da engine, `Key`, GLM (vetores, matrizes, quaternions) |
| `<motor/physics/PhysicsSystem.h>` | `motor::PhysicsSystem` (forças, personagem, raios) |
| `<motor/ui/UiSystem.h>` | `motor::UiSystem` (texto, mostrar/esconder, botões) e os componentes de UI |
| `<motor/audio/AudioSystem.h>` | `motor::AudioSystem` (tocar, parar e volume dos sons) |
| `<motor/scene/Animator.h>` | `motor::Animator` (já vem com o `ScriptModule.h`; inclua para deixar claro) |
| `<glm/geometric.hpp>` | `glm::length`, `glm::normalize`, `glm::dot`, `glm::cross` (inclua para não depender de includes indiretos) |
| `<glm/gtc/quaternion.hpp>` | `glm::angleAxis`, `glm::quatLookAt` |

## Registro: `MOTOR_SCRIPT`

```cpp
MOTOR_SCRIPT(NomeUnico) {
    registry.Register<MinhaStruct>("Identificador", "Nome no editor")
        .Field("chave", "Rótulo", &MinhaStruct::membro, {/* FieldOptions, opcional */});
}
```

- `MOTOR_SCRIPT(Nome)`: bloco que roda quando a DLL é carregada (de novo a cada recompilação). `Nome` só
  precisa ser único dentro do arquivo; use o nome do script. `registry` é o `motor::ComponentRegistry`.
- `Register<T>(identificador, rótulo)`: registra a struct `T` (que herda de `motor::Script`, tem construtor
  padrão e é copiável). O identificador é gravado na cena: precisa ser único entre todos os scripts e os
  componentes da engine.
- `.Field(chave, rótulo, &T::membro, opções)`: campo editável, salvo e comparado (desfazer).
- `.FieldRef(chave, rótulo, [](T& t) -> auto& { return t.algo.membro; }, opções)`: campo que está dentro de
  outra struct.
- `.OnAdd([](T& t, motor::AssetLibrary& assets) { ... })`: valores iniciais quando o componente é
  adicionado pelo Inspector (ex.: escolher uma malha com `assets.FindMesh("Cubo")`).

Um `Register` sem `.Field` é válido: script sem campos.

### Tipos de campo

`bool`, `int`, `float`, `glm::vec2`, `glm::vec3`, `glm::vec4`, `std::string`, `std::shared_ptr<motor::Mesh>`,
`std::shared_ptr<motor::Texture2D>`, e os tipos da engine com editor: `motor::ColliderShape`,
`motor::LightType`, `motor::UiAnchor`, `motor::UiTextAlign` e `motor::UiFont`. Outro tipo não compila (`static assertion failed: tipo de campo não suportado`).

### Opções de campo: `motor::FieldOptions`

Inicializadores designados, na ordem abaixo: `{.slider = true, .min = 0.0f, .max = 2.0f}`.

| Opção | Tipo | Padrão | Efeito |
|---|---|---|---|
| `color` | `bool` | `false` | `vec3`/`vec4` como seletor de cor |
| `slider` | `bool` | `false` | `int`/`float` como slider entre `min` e `max` (senão, arrastar) |
| `logarithmic` | `bool` | `false` | slider logarítmico |
| `speed` | `float` | `0.05` | sensibilidade ao arrastar |
| `min` | `float` | `0` | limite inferior (`min == max`: sem limite) |
| `max` | `float` | `0` | limite superior |
| `format` | `const char*` | `nullptr` | formato `printf` (`"%.1f m/s"`); nulo = padrão do tipo |
| `tooltip` | `const char*` | `nullptr` | texto ao passar o mouse |

## `motor::Script`

Base de todo script. Os métodos virtuais são opcionais: sobrescreva com `override`.

### Ciclo de vida

| Método | Quando é chamado |
|---|---|
| `virtual void OnStart()` | uma vez, antes do primeiro `OnUpdate` (ou quando a entidade fica ativa; entidade criada no meio de um passo começa no seguinte) |
| `virtual void OnUpdate(float dt)` | todo frame; `dt` em segundos |
| `virtual void OnFixedUpdate(float dt)` | a cada passo fixo (1/60 s), depois da física |
| `virtual void OnDestroy()` | ao parar o Play ou quando a entidade é destruída (só se já houve `OnStart`) |
| `virtual void OnCollisionEnter(const Collision& collision)` | começou um contato físico com outra entidade |
| `virtual void OnCollisionExit(EntityId other)` | acabou o contato (não sobrou nenhum ponto de contato entre as duas) |
| `virtual void OnTriggerEnter(EntityId other)` | `other` entrou num trigger (este ou o outro lado é o trigger) |
| `virtual void OnTriggerExit(EntityId other)` | `other` saiu do trigger |
| `bool HasStarted() const` | se o `OnStart` já rodou nesta cópia do script |

Os eventos de física chegam depois do passo da física e antes do `OnFixedUpdate`, nos dois lados do par.

### A própria entidade

| Método | Devolve |
|---|---|
| `Entity& GetEntity()` | a entidade dona do script |
| `EntityId GetId() const` | o id dela |
| `Transform& GetTransform()` | o Transform dela (local, relativo ao pai) |
| `Scene& GetScene()` | a cena que está rodando |

### Cena

| Método | Efeito |
|---|---|
| `Entity* FindEntity(std::string_view name)` | a primeira entidade com esse nome, ou `nullptr` |
| `Entity& CreateEntity(std::string name, EntityId parent = 0)` | cria uma entidade vazia (na raiz, ou filha de `parent`); os scripts dela começam no próximo passo |
| `void Destroy(EntityId id)` | destrói a entidade e os descendentes no fim do passo (`OnDestroy` antes) |
| `void DestroySelf()` | `Destroy(GetId())` |
| `void LoadScene(std::string_view scene)` | troca de cena no fim do passo: a atual para (`OnDestroy` de todos) e a nova começa do zero. `scene` é o nome (`"Fase1"`) ou o caminho relativo ao projeto (`"Assets/Cenas/Fase1.mscene"`, a extensão é opcional). Cena não encontrada: erro no console e o jogo continua. No editor, Parar volta à cena de antes do Play |

### Tempo e entrada

| Método | Devolve |
|---|---|
| `const GameTime& Time() const` | o tempo do jogo (abaixo) |
| `bool IsKeyDown(Key key) const` | tecla apertada |
| `bool WasKeyPressed(Key key) const` | tecla desceu neste frame (use no `OnUpdate`) |
| `bool IsMouseDown(MouseButton button) const` | botão do mouse apertado: `MouseButton::Left`, `Right`, `Middle` |
| `glm::vec2 MouseDelta() const` | movimento do mouse neste frame, em pixels |

As quatro de entrada devolvem falso/zero quando o jogo não tem o foco. `motor::Input` (`motor/core/Input.h`)
tem mais funções (`WasKeyReleased`, `MousePosition`, `ScrollDelta`...), mas sem esse filtro de foco.

`motor::GameTime`:

| Campo | Tipo | Significado |
|---|---|---|
| `delta` | `float` | segundos desde o último `OnUpdate` |
| `fixedDelta` | `float` | passo do `OnFixedUpdate` |
| `elapsed` | `double` | segundos desde o Play (sem as pausas) |
| `frame` | `uint64_t` | quantos `OnUpdate` já rodaram |

### Sistemas e log

| Método | Efeito |
|---|---|
| `template <class T> T* GetSystem()` | o sistema do tipo `T` deste Play (ex.: `GetSystem<motor::PhysicsSystem>()`), ou `nullptr`. Já existe no `OnStart` e ainda no `OnDestroy` |
| `motor::Animator* GetAnimator()` | o Animator da entidade ou do primeiro descendente que tiver um (o modelo filho do jogador), ou `nullptr`. Ver [Animação](#animação-motoranimator) |
| `void Log(fmt, args...)` | escreve no console com o nome da entidade. Formato do `std::format`: `Log("vidas = {}", vidas)` |

Fora de um script (funções livres, outras structs), use `motor::log::Info(...)`, `Warn(...)` ou
`Error(...)` (`motor/core/Log.h`), com o mesmo formato.

### Cópia

Copiar um script (o Play copia a cena; Ctrl+D duplica a entidade) copia os membros da struct, mas a cópia
começa sem dono e "não iniciada". Só os campos registrados são salvos na cena.

## `motor::Collision`

Recebido no `OnCollisionEnter`.

| Campo | Tipo | Significado |
|---|---|---|
| `other` | `EntityId` | a outra entidade |
| `point` | `glm::vec3` | ponto de contato, no mundo |
| `normal` | `glm::vec3` | aponta do outro corpo para este (para onde este é empurrado) |
| `relativeSpeed` | `float` | velocidade de aproximação no contato (m/s); 0 se só encostaram |

## Entidades: `motor::Entity`

| Membro | Significado |
|---|---|
| `EntityId id` | número único na cena (`motor::kNullEntity` = 0 = nenhuma) |
| `std::string name` | nome (não precisa ser único) |
| `bool active` | desativada: os scripts dela (e dos filhos) não rodam, e ela não é desenhada nem tem física |
| `EntityId parent` | o pai (`kNullEntity` = raiz) |
| `Transform transform` | posição, rotação e escala relativas ao pai |
| `T* Get<T>()` | componente do tipo `T`, ou `nullptr` |
| `bool Has<T>()` | se tem o componente |
| `T& Add<T>()` | adiciona (ou devolve o existente) |
| `void Remove<T>()` | remove (não remova o próprio script durante o Play) |

`T` pode ser um componente da engine ou um script registrado. `T` sem registro lança a exceção
`componente não registrado no ComponentRegistry`.

## Cena: `motor::Scene`

O que é útil num script (pelo `GetScene()`):

| Método | Efeito |
|---|---|
| `Entity* Find(EntityId id)` | a entidade, ou `nullptr` se não existe (mais) |
| `Entity* FindByName(std::string_view name)` | a primeira com esse nome |
| `const auto& Entities()` | todas, na ordem da cena (`for (const auto& e : GetScene().Entities()) e->name...`) |
| `std::vector<EntityId> Children(EntityId parent)` | os filhos (`kNullEntity` = as raízes) |
| `glm::mat4 WorldMatrix(EntityId id)` | matriz no mundo; posição = `glm::vec3(m[3])`, frente = `-glm::normalize(glm::vec3(m[2]))` |
| `void SetWorldMatrix(EntityId id, const glm::mat4& world)` | põe a entidade nessa matriz do mundo |
| `bool SetParent(EntityId id, EntityId parent, bool keepWorld = true)` | muda o pai (falso se criar um ciclo) |
| `bool IsActiveInHierarchy(EntityId id)` | ativa e com todos os pais ativos |
| `EntityId FindMainCamera()` | a câmera do jogo |
| `Environment environment` | céu, neblina e luz ambiente da cena |

Para criar e destruir entidades durante o jogo, use os métodos do script (`CreateEntity`, `Destroy`), não
os da cena: eles avisam o loop do jogo.

## `motor::Transform`

| Membro | Significado |
|---|---|
| `glm::vec3 position` | posição (metros) |
| `glm::quat rotation` | rotação (quaternion; identidade = sem rotação) |
| `glm::vec3 scale` | escala |
| `void Rotate(const glm::vec3& axis, float radians)` | gira em torno de um eixo do mundo |
| `void SetEulerDegrees(const glm::vec3& degrees)` | define a rotação por ângulos (graus) |
| `glm::mat4 Matrix() const` | matriz local |

Convenções: Y para cima, metros, a "frente" de entidades, câmeras e luzes é o **-Z** local. Para virar para
uma direção no plano: `glm::angleAxis(std::atan2(-dir.x, -dir.z), glm::vec3(0, 1, 0))`.

## Componentes da engine

Todos em `ScriptModule.h` (via `motor/scene/Components.h`).

| Tipo | No editor | Campos |
|---|---|---|
| `motor::MeshRenderer` | Mesh Renderer | `std::shared_ptr<Mesh> mesh`; `std::shared_ptr<Material> sharedMaterial` (asset `.mmat`, compartilhado; nulo = sem asset); `Material material` (o próprio, usado só sem asset) com `glm::vec4 color`, `std::shared_ptr<Texture2D> albedo`, `float metallic`, `float roughness`, `float specular` (reflexo dos não metais, 0.5 = 4%), mapas `normalMap`, `metallicRoughnessMap`, `occlusionMap`, `emissiveMap` e `glm::vec3 emissive` com `emissiveIntensity`; `Resolved()` devolve o material que está valendo |
| `motor::PrefabInstance` | Prefab | `PrefabLink link` (`guid` do `.mprefab`, `source`, `overrides`); posto pelo editor em cada entidade de uma instância de prefab, não precisa mexer |
| `motor::CameraComponent` | Câmera | `fov` (graus, vertical), `nearPlane`, `farPlane`, `bool main` |
| `motor::LightComponent` | Luz | `LightType type` (`Directional`, `Point`, `Spot`), `color`, `intensity`, `range`, `innerAngle`, `outerAngle`, `bool castShadows`, `ShadowQuality shadowQuality` (`Low`, `Medium`, `High`) |
| `motor::ColliderComponent` | Collider | `ColliderShape shape` (`Box`, `Sphere`, `Capsule`, `Mesh`), `size`, `radius`, `height`, `center`, `isTrigger`, `friction`, `restitution` |
| `motor::RigidbodyComponent` | Rigidbody | `mass`, `useGravity`, `isKinematic`, `linearDamping`, `angularDamping` |
| `motor::CharacterControllerComponent` | Controlador de personagem | `height`, `radius`, `maxSlope` (graus), `stepHeight`, `mass` |
| `motor::AudioSource` | Fonte de áudio | `AudioClip clip` (`guid` e `path` do `.wav`/`.ogg`/`.mp3`; `file`, o arquivo no disco, resolvido ao abrir a cena; `IsEmpty()`), `volume`, `pitch`, `bool loop`, `bool playOnStart`, `bool spatial` (som 3D), `minDistance`, `maxDistance`; tocar e parar na seção [Som](#som-motoraudiosystem) |
| `motor::AudioListener` | Ouvinte de áudio | `volume` (volume geral); na câmera. Sem nenhum ativo, ouve a câmera principal |
| `motor::Animator` | Animator | `std::shared_ptr<Model> model` (glTF), `std::string clip` (clipe inicial), `bool playOnStart`, `bool loop`, `float speed`; controle na seção [Animação](#animação-motoranimator) |

Mudar um campo de Collider, Rigidbody ou Controlador de personagem durante o Play refaz o corpo no próximo
passo da física.

## Animação: `motor::Animator`

Componente na raiz de um modelo glTF importado (o prefab do modelo já vem com ele). Anima o esqueleto do
modelo; as malhas com pele abaixo dele seguem a pose. No Play, o clipe inicial começa sozinho e a animação
avança a cada frame, depois do `OnUpdate` dos scripts. Ache com `GetAnimator()` (ou
`GetEntity().Get<motor::Animator>()`).

| Método | Efeito |
|---|---|
| `bool Play(std::string_view clipName, bool loop = true)` | toca o clipe do modelo, trocando na hora. Se já estiver tocando, continua (se terminou, sem loop, recomeça). `false` se o modelo não tem o clipe |
| `bool CrossFade(std::string_view clipName, float duration = 0.2f, bool loop = true)` | troca misturando com o clipe atual durante `duration` segundos. Pedir o clipe atual não faz nada (dá para chamar a cada passo) |
| `Play(clip, loop)` / `CrossFade(clip, duration, loop)` | mesmos, com um `std::shared_ptr<const AnimationClip>` de outro modelo com as mesmas juntas (achadas pelo nome) |
| `void Stop()` | para e volta à pose de repouso |
| `bool IsPlaying() const` | há um clipe tocando (mesmo parado no fim, sem loop) |
| `bool IsFinished() const` | o clipe sem loop chegou ao fim |
| `const std::string& CurrentClip() const` | nome do clipe atual (`""` se parado) |
| `float ClipTime() const` / `float ClipLength() const` | segundos dentro do clipe atual / duração dele |
| `bool HasClip(std::string_view clipName) const` | o modelo tem o clipe |
| `std::vector<std::string> ClipNames() const` | clipes do modelo |
| `void Update(float dt)` | avança e calcula a pose (o sistema de animação chama; não precisa chamar no Play) |
| `GlobalPose()` / `PoseSkeleton()` | pose calculada (matriz de cada junta, espaço do modelo) e o esqueleto dela |

Campos: `model`, `clip` (clipe inicial), `playOnStart`, `loop` (o clipe inicial repete), `speed` (1 = normal,
0 = congela; vale para todos os clipes).

```cpp
if (auto* anim = GetAnimator()) {
    if (WasKeyPressed(motor::Key::E)) anim->CrossFade("Acenar", 0.2f, false);
    if (anim->CurrentClip() == "Acenar" && anim->IsFinished()) anim->CrossFade("Parado");
}
```

`motor::FindAnimator(scene, id)` faz a mesma busca do `GetAnimator()` para qualquer entidade.

## Física: `motor::PhysicsSystem`

`#include <motor/physics/PhysicsSystem.h>` e `auto* fisica = GetSystem<motor::PhysicsSystem>();`.
Os ids são os das entidades. Chamadas para uma entidade sem corpo (ainda não criado, que só nasce no próximo
passo fixo) devolvem `false`/zero.

| Método | Efeito |
|---|---|
| `std::optional<PhysicsRaycastHit> Raycast(origin, direction, maxDistance = 1000, ignore = kNullEntity) const` | primeiro corpo no raio (`direction` não precisa ser normalizada); `ignore` é atravessada; triggers não contam |
| `bool AddForce(EntityId id, const glm::vec3& force)` | Rigidbody: força em newtons, durante o próximo passo |
| `bool AddImpulse(EntityId id, const glm::vec3& impulse)` | Rigidbody: muda a velocidade na hora (kg·m/s) |
| `bool SetVelocity(EntityId id, const glm::vec3& velocity)` | Rigidbody: define a velocidade |
| `glm::vec3 GetVelocity(EntityId id) const` | Rigidbody: velocidade atual |
| `void MoveCharacter(EntityId id, const glm::vec3& velocity)` | personagem: velocidade de andar (m/s, a parte vertical é ignorada); vale até a próxima chamada |
| `bool Jump(EntityId id, float speed)` | personagem: pula com essa velocidade se estiver no chão; `true` se vai pular |
| `bool IsGrounded(EntityId id) const` | personagem no chão |
| `glm::vec3 GetCharacterVelocity(EntityId id) const` | velocidade do personagem |
| `bool HasBody(EntityId id) const` | a entidade já tem corpo ou personagem |
| `const PhysicsStats& Stats() const` | números do último passo (corpos, tempos em ms) |
| `OnStart`, `OnFixedUpdate`, `OnStop` | ganchos do sistema, chamados pela engine; não chame de um script |

`motor::PhysicsRaycastHit`: `entity`, `point`, `normal` (da superfície atingida) e `distance`.

Configuração global (`motor::PhysicsSettings::Global()`: `gravity`, `workerThreads`, `maxSubstep`) é lida a
cada Play; é para a aplicação, não para scripts.

## Interface do jogo: `motor::UiSystem`

`#include <motor/ui/UiSystem.h>` e `auto* ui = GetSystem<motor::UiSystem>();`. Os elementos são achados pelo
nome da entidade (a primeira com esse nome) ou pelo id. O sistema roda depois dos scripts (ordem
`kSystemOrderUi`): o clique detectado num frame é lido pelos scripts no `OnUpdate` seguinte.

| Método | Efeito |
|---|---|
| `bool SetText(entity, std::string_view text)` | muda o texto do UI: Texto; `false` se a entidade não existe ou não tem o componente. Texto igual não refaz nada |
| `std::string GetText(std::string_view entity) const` | o texto atual, ou `""` |
| `bool SetVisible(entity, bool visible)` | mostra ou esconde o elemento e os filhos (`UiRect::visible`; cria o UI: Retângulo se faltar) |
| `bool IsVisible(std::string_view entity) const` | visível de fato: entidade ativa e ela e os pais com `visible` |
| `bool WasClicked(entity) const` | o botão foi clicado (soltou o clique nele) no frame anterior; leia no `OnUpdate` |
| `EntityId HoveredButton() const` | o botão sob o mouse, ou 0 |
| `bool ScreenRect(EntityId entity, UiBox& out) const` | retângulo do elemento na tela (pixels de verdade, `out.min`/`out.max`) no último passo; `false` se não estava visível |
| `void SetPointer(const UiPointer& pointer)`, `const UiPointer& Pointer() const` | o mouse em relação à imagem do jogo; quem chama é o editor (ou o jogo exportado), não um script |
| `OnStart`, `OnUpdate`, `OnStop` | ganchos do sistema, chamados pela engine; não chame de um script |

`motor::UiPointer`: `screenSize` (pixels da imagem do jogo), `position` (mouse, a partir do canto superior
esquerdo), `inside` (o mouse está sobre o jogo) e `down` (botão esquerdo apertado).

Os componentes (`motor/ui/UiComponents.h`) também podem ser lidos e mudados direto, com `Get<T>()`:

| Componente | Campos |
|---|---|
| `motor::UiRect` | `anchor` (`motor::UiAnchor`: `TopLeft`, `Top`, `TopRight`, `Left`, `Center`, `Right`, `BottomLeft`, `Bottom`, `BottomRight`, `Stretch`), `position`, `size` (`glm::vec2`, pixels de uma tela de 1080 de altura), `order` (`int`), `visible` (`bool`) |
| `motor::UiText` | `text` (`std::string`, UTF-8, `\n` quebra a linha), `font` (`motor::UiFont`: `guid` e `path` do `.ttf`; vazio = padrão), `fontSize` (`float`), `color` (`glm::vec4`), `align` (`motor::UiTextAlign`: `Left`, `Center`, `Right`) |
| `motor::UiImage` | `texture` (`std::shared_ptr<motor::Texture2D>`, nula = cor sólida), `color` |
| `motor::UiButton` | `normalColor`, `hoverColor`, `pressedColor`, `interactable` (`bool`); estado do jogo: `hovered`, `pressed`, `clicked` |

Na âncora, o ponto da tela e o ponto do elemento coincidem (`TopRight` prende o canto superior direito do
elemento no canto superior direito do pai). Com `Stretch`, `position` são as margens esquerda e de cima e
`size` as margens direita e de baixo.

## Som: `motor::AudioSystem`

`#include <motor/audio/AudioSystem.h>` e `auto* audio = GetSystem<motor::AudioSystem>();`. As fontes são
entidades com `motor::AudioSource`, achadas pelo nome (a primeira com esse nome) ou pelo id. O sistema roda
depois dos scripts (ordem `kSystemOrderAudio`): a cada passo lê volume, tom, repetir e a posição das fontes
e do ouvinte. O dispositivo de som só abre quando o primeiro som toca; sem dispositivo, tudo funciona em
silêncio.

| Método | Efeito |
|---|---|
| `bool Play(entity)` | toca o clipe do AudioSource desde o começo (recomeça se já tocava); `false` sem a entidade, sem AudioSource, sem clipe ou se o arquivo não abriu (aviso no log) |
| `bool Stop(entity)` | para o som (o próximo `Play` começa do início); `false` sem a entidade ou sem AudioSource |
| `bool SetVolume(entity, float volume)` | muda `AudioSource::volume` (0 = mudo, 1 = normal); vale na hora |
| `bool IsPlaying(entity) const` | o som está tocando (em pausa conta como tocando) |
| `void StopAll()` | para todos os sons |
| `void SetPaused(bool paused)`, `bool IsPaused() const` | pausa todos os sons e continua de onde parou; quem chama é o editor (Pausa do Play) |
| `bool HasDevice() const` | tocando numa saída de som de verdade (`false`: sem dispositivo, ou ainda nenhum som tocou) |
| `std::vector<float> ReadMix(int frames)`, `int SampleRate() const` | sem dispositivo, mistura `frames` quadros estéreo e devolve as amostras (para testes) |
| `std::vector<AudioVoiceInfo> Voices() const` | as fontes que já tocaram neste Play (diagnóstico) |
| `glm::vec3 ListenerPosition() const` | onde está o ouvinte |
| `float OutputLevel(int channel) const` | nível (RMS) do som que saiu no último bloco, canal 0 (esquerdo) ou 1 (direito) |
| `OnStart`, `OnUpdate`, `OnStop` | ganchos do sistema, chamados pela engine; não chame de um script |

`motor::AudioVoiceInfo`: `entity`, `clip` (caminho no projeto), `playing`, `spatial`, `distance` (até o
ouvinte) e `gain` (volume da fonte vezes a queda com a distância). Configuração global
(`motor::AudioSettings::Global()`: `useDevice`, `masterVolume`) é lida a cada Play; é para a aplicação e os
testes, não para scripts.

Som 3D: o volume cai com a distância pelo modelo inverso (`minDistance / distância`, cheio até
`minDistance`, constante depois de `maxDistance`) e o lado vem da direção do ouvinte (a frente é o -Z dele).

## Teclas

`motor::Key` (`motor/core/Input.h`):

| Grupo | Valores |
|---|---|
| Letras | `A` ... `Z` |
| Números | `D0` ... `D9` |
| Funções | `F1` ... `F12` |
| Setas | `Up`, `Down`, `Left`, `Right` |
| Modificadores | `LeftShift`, `RightShift`, `LeftControl`, `RightControl`, `LeftAlt`, `RightAlt`, `LeftSuper`, `RightSuper` |
| Outras | `Space`, `Enter`, `Escape`, `Tab`, `Backspace`, `Insert`, `Delete`, `PageUp`, `PageDown`, `Home`, `End`, `Apostrophe`, `Comma`, `Minus`, `Period`, `Slash`, `Semicolon`, `Equal` |

No Play, as teclas de atalho do editor (W/E/R, Del...) ficam com o jogo enquanto o viewport tem o foco.
