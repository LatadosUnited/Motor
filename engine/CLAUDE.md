# Engine — instruções para o Claude

Módulo Engine: `engine/` (a `libMotor.dll`), `player/` (`Player.exe`) e `sandbox/`. Estado e roteiro em
`docs/engine/`; uso em `engine/README.md`. As regras comuns estão no `CLAUDE.md` da raiz.

## Camadas

`engine/` é a biblioteca `Motor`, dividida em camadas: **core → renderer → project → assets → scene → app**, e
por cima `physics`, `ui` e `audio`. Cada camada tem `include/motor/<camada>/`, `src/<camada>/` e o seu
`CMakeLists.txt` (alvo `motor_<camada>`).

- Uma camada só inclui headers dela e das camadas de baixo; o `test_layers` falha se isso for quebrado.
- Arquivo novo entra no `CMakeLists.txt` da sua camada. Header usado só dentro da camada fica em `src/<camada>/`,
  não em `include/` (ex.: `physics/PhysicsWorld.h`, `scene/SceneJson.h`, `assets/AssetLibraryUtil.h`).
- Camada nova: pasta própria e entrada em `MOTOR_LAYERS` (`engine/CMakeLists.txt`). Precisa subir uma
  dependência? Mova o código para baixo ou inverta a dependência (callback, constante na camada de baixo), nunca
  inclua para cima.
- O que tem em cada uma:
  - `core/`: `Window`, `Input`, `Log`, `Paths.h` (UTF-8), `FrameStats` (contadores por quadro), `Profile.h` (Tracy).
  - `renderer/`: `Renderer` (carrega as funções do OpenGL), `GL.h` (loader próprio em X-macro: função GL nova =
    adicionar na lista de lá), `Shader`, `Mesh`, `Texture2D`, `Framebuffer`, sombras, céu, pós-processamento.
  - `project/`: `Project` (pasta + `.mproject`), opções do jogador, ações de entrada, saves.
  - `assets/`: `AssetLibrary` (malhas/texturas embutidas e de arquivo), `AssetDatabase` (GUIDs em `.meta`),
    `AssetLoaders` (OBJ próprio, imagens via stb_image), glTF (cgltf) e FBX (ufbx).
  - `scene/`: `Scene` (entidades, hierarquia, cópia profunda, `operator==`, listas por tipo, matriz de mundo com
    cache), `SceneSerializer` (`.mscene`), `SceneRuntime` (loop de jogo: scripts e sistemas), prefabs, componentes.
  - `app/`: `Application` (loop principal com passo fixo).
  - `physics/` (Jolt), `ui/` (HUD: componentes `Ui*`, `UiLayout`, `Font` SDF, `UiRenderer`, `UiSystem`), `audio/`
    (miniaudio: `AudioSystem`; os componentes `AudioSource` e `AudioListener` ficam em `scene/`).
- A engine é uma DLL só, a `libMotor.dll` (com ImGui e GLFW dentro, `--export-all-symbols`), compartilhada pelo
  editor, pelo Player e pela DLL de scripts dos projetos.

## Componentes

- **Recurso novo de entidade = componente registrado** (`motor/scene/Component.h`, exemplos em `engine/README.md`
  e em `engine/src/scene/MeshRenderer.cpp`). Não adicione campos soltos em `Entity`: Inspector, serialização,
  cópia e desfazer saem de graça dos campos registrados. Componente da engine = header + `.cpp` próprios com
  `MOTOR_COMPONENT_MODULE` (sem lista central); o header entra em `Components.h`.
- **Tipo de campo novo** (enum, referência a asset...): especialize `motor::FieldTraits<T>` e registre o
  desenhador do Inspector com `MOTOR_FIELD_DRAWER` (`editor/src/drawers/`). Não acrescente valores ao enum
  `FieldType`. Exemplo completo em `tests/test_component_extension.cpp`.
- Desfazer é automático: o editor compara a cena a cada quadro (`Scene::operator==`). Dado de cena fora dos
  componentes (ambiente) precisa entrar na cópia e no `operator==` de `Scene`.
- Quem mexe direto em `Entity::components` (em vez de `Entity::Add`/`Remove`) chama `MarkSceneStructureChanged()`:
  as listas por tipo (`Scene::EntitiesWith`) e a lista de scripts dependem disso.

## Sistemas do `SceneRuntime`

Camada de cima (física, áudio, HUD) que precisa rodar a cada passo do jogo registra um `motor::RuntimeSystem` num
`.cpp` próprio com `MOTOR_RUNTIME_SYSTEM(Nome, Tipo, ordem)` (`motor/scene/RuntimeSystem.h`, que documenta a ordem).
Não mexa no `SceneRuntime` para isso. Use as ordens de lá (`kSystemOrderPhysics`, `kSystemOrderAudio`,
`kSystemOrderUi`; ordem nova = constante nova no fim da lista); `order < 0` roda antes dos scripts. Estado do
sistema fica na instância (uma por runtime, criada no `Start`); scripts acham com `GetSystem<T>()`. Exemplo
testado: `tests/test_runtime_systems.cpp`.

## Scripts dos jogos

- Scripts ficam na pasta `Scripts/` de cada projeto (`motor::Script` + `MOTOR_SCRIPT`, ver
  `motor/scene/ScriptModule.h`). Guia e referência em `docs/scripts.md` e `docs/scripts-api.md`, conferidos pelo
  `test_script_docs` (os exemplos de `docs/examples/` compilam numa DLL de scripts).
- **Interface com a DLL de scripts mudou** (`Component.h`, `Script.h`, `ScriptModule.h`, ou o layout de `Entity`
  e `Scene`)? Suba `kScriptApiVersion` (em `ScriptModule.h`, com a linha do histórico) e o número em
  `docs/scripts-api.md`.

## Desempenho

- Medir antes de otimizar: Tracy (`-DMOTOR_TRACY=ON`), contadores por quadro (`FrameStats`), `Player --perf`.
- `test_desempenho` (rótulo `desempenho`) só mede em Release: `ctest --test-dir build-release -L desempenho`. No
  Debug aparece como pulado. Mudou o desempenho de propósito (melhora medida)? Regrave a base com
  `MOTOR_DESEMPENHO_GRAVAR=1` (só os números que mudaram de verdade) e diga no commit. Detalhes em `bench/README.md`.
- Comparação com a Unity e cena de referência: `bench/`.
