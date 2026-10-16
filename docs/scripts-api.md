# Referência da API dos scripts

Tudo que um script do Motor pode usar, conferido com os headers da engine. Para aprender passo a passo, leia
primeiro o [guia](scripts.md). Fontes, se quiser ir além: `engine/include/motor/scene/Script.h`,
`ScriptModule.h`, `Scene.h`, `Component.h` e `engine/include/motor/physics/PhysicsSystem.h`.

O teste `test_script_docs` confere que esta página cita todos os métodos públicos de `motor::Script` e de
`motor::PhysicsSystem`, todas as opções de campo e os campos de `GameTime` e `Collision`. Mudou um desses
headers? Atualize aqui.

Versão da interface entre o editor e a DLL de scripts: `kScriptApiVersion` = 5 (`ScriptModule.h`). Ela sobe
quando a interface muda de forma incompatível; o editor recusa uma DLL compilada com outra versão (basta
recompilar os scripts).

## Includes

| Include | Traz |
|---|---|
| `<motor/scene/ScriptModule.h>` | o necessário para qualquer script: `motor::Script`, `MOTOR_SCRIPT`, `Scene`, `Entity`, `Transform`, os componentes da engine, `Key`, GLM (vetores, matrizes, quaternions) |
| `<motor/physics/PhysicsSystem.h>` | `motor::PhysicsSystem` (forças, personagem, raios) |
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
`std::shared_ptr<motor::Texture2D>`, e os enums da engine com editor: `motor::ColliderShape`,
`motor::LightType`. Outro tipo não compila (`static assertion failed: tipo de campo não suportado`).

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
| `motor::MeshRenderer` | Mesh Renderer | `std::shared_ptr<Mesh> mesh`; `Material material` com `glm::vec4 color`, `std::shared_ptr<Texture2D> albedo`, `float specular`, `float shininess` |
| `motor::CameraComponent` | Câmera | `fov` (graus, vertical), `nearPlane`, `farPlane`, `bool main` |
| `motor::LightComponent` | Luz | `LightType type` (`Directional`, `Point`, `Spot`), `color`, `intensity`, `range`, `innerAngle`, `outerAngle` |
| `motor::ColliderComponent` | Collider | `ColliderShape shape` (`Box`, `Sphere`, `Capsule`, `Mesh`), `size`, `radius`, `height`, `center`, `isTrigger`, `friction`, `restitution` |
| `motor::RigidbodyComponent` | Rigidbody | `mass`, `useGravity`, `isKinematic`, `linearDamping`, `angularDamping` |
| `motor::CharacterControllerComponent` | Controlador de personagem | `height`, `radius`, `maxSlope` (graus), `stepHeight`, `mass` |

Mudar um campo de Collider, Rigidbody ou Controlador de personagem durante o Play refaz o corpo no próximo
passo da física.

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
