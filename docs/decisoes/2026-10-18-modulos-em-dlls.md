---
tipo: decisao
data: 2026-10-18
status: proposta
fases: [24, 25]
---

# Módulos da engine em DLLs separadas

Continua o estudo de [modularizar a engine](2026-10-18-modularizar-a-engine.md). Lá, dividir a
`libMotor.dll` ficou como alternativa para depois. O usuário gostou da ideia (18/10/2026) e pediu o
catálogo de todos os módulos em que o sistema pode ser dividido.

## Contexto

Hoje cada camada é uma biblioteca de objetos (`motor_<camada>`), mas todas são ligadas numa DLL só, a
`libMotor.dll`. Junto com elas vão a Jolt, a miniaudio, o ImGui e o GLFW, e todos os símbolos são exportados
(`--export-all-symbols`). O editor, o `Player.exe`, a DLL de scripts de cada projeto e os testes ligam nessa
DLL.

Os pontos de extensão de que a divisão precisa **já funcionam entre DLLs**, porque a DLL de scripts usa todos
eles:

- `MOTOR_COMPONENT_MODULE`: um módulo que aparece depois do registro inicial (de uma DLL carregada mais
  tarde) é registrado na hora (`Component.h`, `Component.cpp`). Para descarregar um módulo opcional com o
  editor aberto, `ComponentModuleRegistrar` precisará ganhar destrutor que chame `Unregister` (hoje só o
  `ScriptHost` desregistra os scripts antes do `FreeLibrary`).
- `MOTOR_RUNTIME_SYSTEM`: o registro guarda o `type_info`, e o `GetSystem<T>()` compara pelo nome do tipo,
  justamente para funcionar entre DLLs. Descarregar uma DLL tira os seus sistemas do registro.
- O editor e o Player já carregam DLLs com `LoadLibraryW` (`ScriptHost.cpp`, `player/src/main.cpp`).
- Nenhum header público expõe o `nlohmann::json`. Ele pode ficar privado em cada DLL, sem uma cópia
  atrapalhar a outra.

## Decisão

Proposta: dividir a engine nos **12 módulos** abaixo. Cada módulo vira uma DLL, e as que são pequenas demais
para ficar sozinhas entram na DLL de outro módulo. A divisão só começa **depois** dos passos 1 a 5 do
[estudo anterior](2026-10-18-modularizar-a-engine.md), que tiram as dependências erradas. Antes disso, várias
DLLs teriam dependências circulares.

### Mapa

```
                         MotorCore
              ┌──────────────┼────────────────┐
        MotorPlatform    MotorAssets      (MotorProject → dentro de MotorAssets)
         │      │            │
   MotorImGui  MotorRender   MotorScene ───────────────┬───────────────┬──────────────┐
                  │          │                         │               │              │
                  └──── MotorGraphics (+ App)    MotorPhysics    MotorAudio    MotorAnimation
                             │
                          MotorUi
```

Seta = "depende de". Obrigatórios para um jogo com janela: Core, Platform, Render, Assets, Scene e Graphics.
Opcionais: ImGui, Physics, Audio, Animation e Ui.

### Catálogo

#### 1. MotorCore (obrigatório)

- **Conteúdo:** `Log`, `Paths` (UTF-8), `EngineAssets`, `Version.h` (gerado).
- **Depende de:** só da API do Windows.
- **Bibliotecas:** nenhuma.
- **Dono de:** o log do processo (única cópia).
- **Antes de dividir:** `Window`, `Input` e `ImGuiLayer` saem daqui (vão para os módulos 2 e 3).
- **Quem usa sozinho:** ferramentas sem janela (conversor de assets, testes, um servidor dedicado no
  futuro).

#### 2. MotorPlatform (obrigatório para ter janela)

- **Conteúdo:** `Window`, `Input`.
- **Depende de:** Core.
- **Bibliotecas:** GLFW (estática e escondida aqui; **a única cópia no processo**).
- **Dono de:** o estado do `Input` (o mesmo para editor, Player e scripts).
- **Antes de dividir:** o renderer pega as funções do OpenGL com o `glfwGetProcAddress`. Isso vira um
  callback que a `Window` entrega ao renderer, para o Render não depender do GLFW. O
  `FlyCameraController` (hoje em `scene/`) usa `Window` e `Input`: vai para Graphics ou para o editor.

#### 3. MotorImGui (opcional no jogo, obrigatório no editor)

- **Conteúdo:** `ImGuiLayer` e os objetos do ImGui (com os backends GLFW e OpenGL 3).
- **Depende de:** Platform.
- **Bibliotecas:** ImGui (**um contexto só** no processo; editor e DLL de scripts usam esta cópia).
- **Por que separar:** um jogo exportado que não usa ImGui não precisa levá-lo. O ImGuizmo continua no
  editor, ligado nesta DLL.

#### 4. MotorRender (obrigatório para desenhar)

- **Conteúdo:** loader OpenGL (`GL.h`), `Shader`, `Mesh` (GPU), `Texture`, `Framebuffer`, `Camera`,
  `Renderer`, `Renderer2D`, `Shadows`, `Sky`, `PostProcess`, `Pbr`, `ShadowMath`, `DrawList`.
- **Depende de:** Core.
- **Bibliotecas:** nenhuma (o loader do GL é nosso).
- **Antes de dividir:** os tipos que são só dados (`Skeleton`, `Light`, `Environment`, `Material` como
  descrição) saem para Assets ou Scene, para os módulos de cima não precisarem do Render só por causa
  deles.
- **Futuro:** é o ponto onde entraria outra API gráfica (Vulkan, por exemplo) como outra DLL com a mesma
  interface.

#### 5. MotorAssets (obrigatório), com o Project dentro

- **Conteúdo:** `Project` (`.mproject`), `AssetDatabase` (GUIDs, `.meta`), `AssetLoaders` (OBJ, imagens),
  `GltfLoader`, `Model`, `Animation`, `AssetLibrary`, `BuiltinAssets`.
- **Depende de:** Core.
- **Bibliotecas:** stb_image, cgltf (as duas são compiladas só aqui).
- **Por que o Project vai junto:** tem um arquivo só (`Project.cpp`) e é sempre usado com os assets. Uma DLL
  própria não valeria o custo.
- **Antes de dividir:** os assets passam a guardar só os dados na CPU, e o envio para a GPU vai para
  Graphics (passo 5 do estudo anterior).
- **Futuro:** os importadores (glTF, OBJ) podem virar um módulo `MotorImport` só do editor, se a exportação
  do jogo passar a converter os modelos para um formato próprio.

#### 6. MotorScene (obrigatório)

- **Conteúdo:** `Component` (registro), `Scene`, `Transform`, `SceneSerializer`, `SceneRuntime`,
  `RuntimeSystem`, `Script`, `ScriptModule`, `Prefab`, `PrefabInstance`, `ModelImport`.
- **Depende de:** Core, Assets.
- **Bibliotecas:** nenhuma.
- **Dono de:** o registro de componentes, o de módulos de componentes e o de sistemas do jogo. Todos os
  outros módulos se registram aqui.
- **Antes de dividir:** a cena para de desenhar (`Render`, `SubmitTo` e `CollectLights` vão para
  Graphics). Os componentes de física, áudio, malha, luz e câmera vão para os seus módulos. O
  `ModelImport` (que hoje adiciona `MeshRenderer` e `Animator` por tipo C++) ou cria esses componentes pelo
  nome no `ComponentRegistry` ou vai para Graphics/Import. `struct Collision` e os callbacks virtuais
  `OnCollision*`/`OnTrigger*` continuam em `Script.h` (só usam `EntityId` e `glm::vec3`, sem depender da
  Jolt). O `Script.h` deixa de incluir `Input.h` direto, ou passa a depender de Platform (decidir na hora).

#### 7. MotorGraphics (obrigatório para desenhar a cena), com o App dentro

- **Conteúdo:** `SceneRenderer` (novo, com o que sai da `Scene`), componentes `MeshRenderer`,
  `LightComponent` e `CameraComponent`, cache de malhas e texturas na GPU, `Application` (loop principal),
  `FlyCameraController`.
- **Depende de:** Scene, Render, Platform.
- **Bibliotecas:** nenhuma.
- **Por que o App vai junto:** `Application` é um arquivo só e precisa de janela e renderer, as mesmas
  dependências deste módulo.

#### 8. MotorPhysics (opcional)

- **Conteúdo:** `PhysicsSystem`, `ColliderGizmo` (linhas dos colliders) e os componentes
  `ColliderComponent`, `RigidbodyComponent` e `CharacterControllerComponent`.
- **Depende de:** Scene, Assets (collider de malha).
- **Bibliotecas:** Jolt (estática, com os símbolos escondidos, como já é hoje).
- **Ganho:** é o maior módulo opcional. Um jogo sem física deixa de levar a Jolt.

#### 9. MotorAudio (opcional)

- **Conteúdo:** `AudioSystem` e os componentes `AudioSource` e `AudioListener`.
- **Depende de:** Scene, Assets.
- **Bibliotecas:** miniaudio (compilada só aqui).

#### 10. MotorAnimation (opcional)

- **Conteúdo:** componente e sistema `Animator`, `Animation` e `Skeleton` (o esqueleto como dado; o
  skinning na GPU continua no Render).
- **Depende de:** Scene, Assets.
- **Bibliotecas:** nenhuma.
- **Antes de dividir:** o `Script::GetAnimator()` (em `Script.h`) liga a API dos scripts a este módulo. Ou
  vira `GetComponent<Animator>()`, ou fica como função do próprio módulo.

#### 11. MotorUi (opcional)

- **Conteúdo:** `UiComponents`, `UiLayout`, `Font` (SDF), `UiRenderer`, `UiSystem`.
- **Depende de:** Graphics (o `Renderer2D`), Scene, Assets.
- **Bibliotecas:** `imstb_truetype.h` (copiado do ImGui; não depende do MotorImGui).
- **Futuro:** dá para separar a lógica (layout, cliques, sem GPU) do desenho, se um servidor dedicado
  precisar rodar a UI sem janela.

#### 12. Módulos do editor (fase seguinte, opcional)

O editor continua sendo um executável. Cada módulo opcional da engine ganharia uma **extensão do editor**,
carregada só quando o módulo está presente:

| Extensão | Conteúdo hoje | Acoplamento que precisa sair |
|---|---|---|
| `EditorPhysics` | `ColliderShapeDrawer.cpp`, linhas dos colliders no `SceneViewport` | `SceneViewport` chama o `ColliderGizmo` direto |
| `EditorAudio` | `AudioClipDrawer.cpp`, ferramenta MCP `audio_status` | `PlayMode.cpp` e `McpController.cpp` incluem `AudioSystem.h` para pausar o som |
| `EditorUi` | `UiEditor.*`, `UiFieldDrawers.cpp`, a UI desenhada no viewport | `SceneViewport` cria um `UiRenderer`; `McpController.cpp` inclui `UiSystem.h` |
| `EditorMcp` | `editor/src/mcp/` e o cpp-httplib | já é uma peça própria; separar só se valer a pena |

Para isso o editor precisa de um ponto de extensão (algo como um `MOTOR_EDITOR_EXTENSION`, com eventos
"Play pausado", "desenhar no viewport" e "registrar ferramentas MCP"), no mesmo estilo dos registros que já
existem. O `MOTOR_FIELD_DRAWER` já funciona assim.

### Fica de fora da divisão

- Hub, `distrib/`, `server/` e `installer/`: já não ligam na engine.
- A DLL de scripts de cada projeto: continua igual, mas liga nos módulos que usa.
- `nlohmann_json`: só headers, privado em cada DLL.

### Regras que a divisão exige

1. **Cada biblioteca externa tem um dono só** (tabela abaixo). Duas DLLs com o próprio GLFW ou o próprio
   ImGui quebram a janela e o contexto.
2. **Os registros moram em MotorScene, e o log e o Input em Core e Platform.** Nenhum módulo de cima
   guarda uma cópia.
3. **Exportação explícita:** `--export-all-symbols` em cada DLL reexportaria o que ela liga estaticamente.
   Cada módulo ganha um `MOTOR_<MODULO>_API` (dllexport/dllimport) ou um arquivo `.def`, e a Jolt e a
   miniaudio continuam escondidas.
4. **Carregamento:** os obrigatórios são ligados normalmente (o Windows os carrega antes do `main`). Os
   opcionais são carregados com `LoadLibraryW` a partir da lista de módulos do projeto, **antes** de abrir
   a primeira cena, para os componentes já estarem registrados.
5. **Cena com componente de módulo ausente:** abre do mesmo jeito. O componente desconhecido é guardado e
   salvo de volta, como já acontece na recarga dos scripts (`test_hotreload`), e o Inspector avisa que falta
   o módulo.
6. **Versão:** os módulos são da mesma versão do Motor (todos vêm no mesmo pacote). O `kScriptApiVersion`
   continua valendo para a DLL de scripts.
7. **`test_layers`:** passa a conferir o grafo de módulos acima, e não a ordem de uma lista.

| Biblioteca | Dono |
|---|---|
| GLFW | MotorPlatform |
| ImGui (+ backends) | MotorImGui |
| ImGuizmo | editor |
| stb_image, cgltf | MotorAssets |
| Jolt | MotorPhysics |
| miniaudio | MotorAudio |
| `imstb_truetype.h` | MotorUi |
| cpp-httplib | editor (MCP) |
| nlohmann_json | cada DLL, privado |

### O que muda fora da engine

- **`.mproject`:** campo novo `"modules": ["physics", "audio", "ui", "animation"]`, que exige subir a versão do
  formato. Projeto antigo sem o campo = todos os módulos, para nada quebrar.
- **Editor:** carrega os módulos do projeto ao abrir e ganha uma tela em Projeto > Módulos para ligar e
  desligar cada um.
- **Exportação (`GameExport.cpp`):** a lista fixa `kRuntimeDlls` vira "obrigatórias + módulos do projeto".
- **SDK dos scripts (`MotorSDK.cmake`):** um alvo importado por módulo (`Motor::Scene`, `Motor::Physics`...),
  e o CMake gerado para o projeto liga só nos módulos ligados.
- **`motor_package` e instalador:** copiam todas as DLLs. O pacote fica do mesmo tamanho, mas o jogo
  exportado fica menor.

## Ordem sugerida

1. Passos 1 a 5 do [estudo anterior](2026-10-18-modularizar-a-engine.md): dependências certas, ainda numa
   DLL só.
2. Exportação explícita (`MOTOR_<MODULO>_API`) ainda numa DLL só. Dá para conferir que nada sumiu antes de
   dividir.
3. Primeiro corte: **MotorCore + MotorPlatform + MotorImGui** de um lado e o resto do outro. É o corte que
   testa as regras 1 e 2 (cópia única do GLFW, do ImGui e do log).
4. **MotorPhysics** como a primeira DLL opcional (a que mais pesa), com o carregamento pela lista do
   `.mproject`.
5. Audio, Animation e Ui, um por vez.
6. Render, Assets, Scene e Graphics separados entre si (o corte mais trabalhoso, por causa da API dos
   scripts).
7. Extensões do editor.

Cada passo é uma tarefa `refactor/<assunto>`, com build sem avisos, `ctest` inteiro e um jogo exportado sem
o módulo para testar.

## Alternativas

- **DLL só, com módulos ligados e desligados pelo CMake:** dá builds menores, mas a escolha é na hora de
  compilar a engine, não por projeto. Também não aceita plugins. É o passo 1 do estudo anterior e continua
  sendo o caminho até a divisão.
- **Uma DLL por camada atual, sem juntar nenhuma (14 DLLs):** `Project` e `App` têm um arquivo cada. Mais
  DLLs só aumentam o custo de carregar e de empacotar.
- **Poucas DLLs grandes** (Core, Runtime e Opcionais): é mais simples, mas um jogo com física e sem áudio
  ainda levaria a miniaudio.

## Consequências

- Mais fácil: jogo exportado só com o que usa; plugins de terceiros (um módulo novo é uma DLL que se
  registra sozinha); recompilar e ligar só o módulo mexido; mais tarde, um renderer novo como outra DLL.
- Mais difícil: exportação explícita em toda a API pública; mais DLLs no SDK, no pacote e na exportação;
  erros de "DLL não encontrada" ou de ordem de carregamento, que hoje não existem; testar o jogo com e sem
  cada módulo opcional.
- Revisar esta proposta depois dos passos 1 a 5 do estudo anterior, quando as dependências reais estiverem
  medidas pelo `test_layers` novo.
