# Motor — instruções para o Claude

Engine 3D em C++20 + OpenGL 4.5 feita do zero, com editor (Dear ImGui). Visão geral e uso: `README.md`.
Plano por fases: `ROADMAP.md`. O usuário fala português: responda, comente o código e escreva textos de
interface e logs em **português**.

## Compilar e testar (Windows, MSYS2 UCRT64)

```bash
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug   # só na primeira vez
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

- O build deve sair **sem avisos** do nosso código (`-Wall -Wextra -Wpedantic` na engine). Avisos de
  depreciação do CMake vindos de `build/_deps` (GLM) são esperados.
- Dependências via FetchContent em `CMakeLists.txt`, sempre fixadas (tag, commit ou URL com SHA256).
  Filosofia de **libs mínimas**: pergunte antes de adicionar uma nova.
- `build/` é gerado: pode ser apagado e recriado. Nunca rode scripts que reescrevem arquivos em massa sem
  excluir `build/`.

## Estrutura

- `engine/` — biblioteca `Motor`, dividida em camadas: **core → renderer → project → assets → scene → app**.
  Cada camada tem `include/motor/<camada>/`, `src/<camada>/` e o seu `CMakeLists.txt` (alvo `motor_<camada>`).
  Uma camada só inclui headers dela e das camadas de baixo; o `test_layers` falha se isso for quebrado.
  Arquivo novo entra no `CMakeLists.txt` da sua camada. Camada nova (física, áudio...): pasta própria e
  entrada em `MOTOR_LAYERS` (`engine/CMakeLists.txt`). Precisa subir uma dependência? Mova o código para
  baixo ou inverta a dependência (callback, constante na camada de baixo), nunca inclua para cima.
  - `assets/`: `AssetLibrary` (malhas/texturas embutidas e de arquivo), `AssetDatabase` (GUIDs em `.meta`),
    `AssetLoaders` (OBJ próprio, imagens via stb_image).
  - `scene/`: `Scene` (entidades, hierarquia, cópia profunda e `operator==`), `SceneSerializer` (`.mscene`),
    `SceneRuntime` (loop de jogo: scripts e sistemas registrados, `RuntimeSystem.h`).
  - `project/`: `Project` (pasta + `.mproject`). `core/Paths.h`: conversões UTF-8.
  - `app/`: `Application` (loop principal). O `Renderer` carrega as funções do OpenGL; a `Window` só cria o contexto.
  - `renderer/GL.h`: loader OpenGL próprio (X-macro). Função GL nova = adicionar na lista de lá.
  - `ui/` (acima de scene): HUD do jogo. Componentes `Ui*`, `UiLayout` (âncoras), `Font` (atlas SDF com o
    `imstb_truetype.h` do ImGui), `UiRenderer` (sobre o `renderer/Renderer2D`), `UiSystem` (cliques).
  - `audio/` (acima de scene): som do jogo com a miniaudio. `AudioSystem` (sistema do `SceneRuntime`);
    componentes `AudioSource` e `AudioListener` ficam em `scene/`.
- `editor/` — `MotorEditor`. `EditorApp` só monta as peças (loop, layout, barra de menus, atalhos,
  desfazer); `SceneViewport` (câmera livre, gizmo, seleção, arquivos soltos), `PlayController` (menu Jogo,
  botões e pedidos de Play), `SceneDocument` (cena/projeto abertos, menu Arquivo, popups, título),
  `ScriptController` (menu e painel Scripts, compilação e recarga da DLL de scripts do projeto).
  Um arquivo por painel, `EditorContext` (estado compartilhado), `UndoHistory`.
  `PlayMode` (cópia/restauração da cena no Play; o loop de jogo é `motor::SceneRuntime`).
  Recurso novo do editor vai na peça da sua área, não no `EditorApp`.
  `mcp/`: servidor MCP do editor (`McpProtocol` JSON-RPC, `McpServer` HTTP com cpp-httplib, `McpSceneTools`
  ferramentas de cena testáveis sem janela, `McpController` peça do editor com as demais ferramentas).
- Scripts de jogo ficam na pasta `Scripts/` de cada projeto (`motor::Script` + `MOTOR_SCRIPT`, ver
  `motor/scene/ScriptModule.h`). O editor compila uma DLL por projeto (`ScriptProject`, `ScriptBuilder`) e
  recarrega sem fechar (`ScriptHost`). A engine é a `libMotor.dll`, compartilhada pelo editor e pela DLL de
  scripts; modelos de script em `editor/templates/Scripts/`.
- `player/` — `Player.exe`, o executável do jogo exportado (roda um projeto sem o editor). A exportação fica em
  `editor/src/GameExport` (assets referenciados e pasta de saída) e `ExportController` (Arquivo > Exportar jogo).
- `sandbox/` — demo mínima da engine. `tests/` — testes `ctest` (ver abaixo).

## Convenções

- **Caminhos e texto são UTF-8.** Nunca use `path::string()` nem `char*` de APIs ANSI do Windows: use
  `motor::ToUtf8` / `PathFromUtf8` e as versões wide (`_wgetenv`, `...W`). Acentos em nomes de pasta são
  o caso de teste padrão ("Jogo Ação").
- JSON com `nlohmann::ordered_json`; formatos versionados (`"format"`, `"version"`) e leitura tolerante
  (campo ausente = padrão). Mudou o formato? Suba a versão e continue lendo as antigas.
- Gravação de arquivos importantes: escreve `.tmp` e renomeia (ver `SceneSerializer::Save`).
- Assets de arquivo são referenciados por **GUID** (`.meta` ao lado). Quem move/renomeia/exclui arquivo
  no editor leva o `.meta` junto (`ProjectPanel`). Exclusão vai sempre para a **Lixeira**, nunca definitiva.
- **Recurso novo de entidade = componente registrado** (`motor/scene/Component.h`, exemplos no README e em
  `engine/src/scene/MeshRenderer.cpp`). Não adicione campos soltos em `Entity`: Inspector, serialização,
  cópia e desfazer saem de graça dos campos registrados. Componente da engine = header + `.cpp` próprios com
  `MOTOR_COMPONENT_MODULE` (sem lista central); o header entra em `Components.h`.
- **Tipo de campo novo** (enum, referência a asset...): especialize `motor::FieldTraits<T>` e registre o
  desenhador do Inspector com `MOTOR_FIELD_DRAWER` (`editor/src/FieldDrawers.h`), cada um no seu `.cpp`.
  Não acrescente valores ao enum `FieldType` (isso exigiria mexer em `ComponentsEqual`, `SceneSerializer`
  e `DrawField` ao mesmo tempo). Exemplo completo em `tests/test_component_extension.cpp`.
- Desfazer é automático: `UndoHistory::Track` compara a cena a cada frame (`Scene::operator==`). Dado de
  cena fora dos componentes (luz, ambiente) precisa entrar na cópia e no `operator==` de `Scene`.
- Estilo: o do código vizinho (4 espaços, `m_` em membros, comentários curtos explicando o porquê).

## Testes

- Cada `tests/test_*.cpp` usa `TestUtil.h` (`CHECK`, `SECTION`, `TestDir`, `TestResult`) e é registrado em
  `tests/CMakeLists.txt` com `motor_add_test`. Testes não criam janela nem contexto OpenGL.
- Funcionalidade nova com lógica testável sem GPU ganha teste aqui.

## Verificar a interface — regras importantes

- **Prefira o servidor MCP do editor** para ver e testar: abra `MotorEditor.exe "<projeto>" --mcp` (porta
  47800) e chame as ferramentas por HTTP (`POST http://127.0.0.1:47800/mcp`, JSON-RPC `tools/call`), ou pelo
  Claude Code se o servidor `motor` estiver configurado. `viewport_capture` substitui o PrintWindow; `play`,
  `game_key`, `ui_click` e `log_read` testam o jogo sem entrada sintética global. Recurso novo do editor que
  um agente precise usar ganha ferramenta em `McpController` (ou `McpSceneTools`, se for só cena).
- **Nunca simule teclado ou mouse globais** (SendKeys, keybd_event, SetCursorPos...) para testar o editor:
  o foco pode ir para outra janela e o texto vai parar em outro lugar (já aconteceu). Peça confirmação
  explícita antes de qualquer entrada sintética.
- Para ver o editor: abra-o com um projeto de teste na pasta temporária e capture a janela com
  `PrintWindow` (flag `PW_RENDERFULLCONTENT`, funciona sem foco), depois encerre o processo.
  Ex.: `MotorEditor.exe "C:\...\scratchpad\Projeto Teste"`.
- Abrir o editor grava `%APPDATA%\Motor\editor.json` (último projeto, recentes). **Faça cópia antes e
  restaure depois** — é a configuração real do usuário.
- O que o MCP não cobre (menus, arrastar, atalhos, gizmo) fica como "não testado" no relatório, com passos
  para o usuário conferir.

## Grafo de conhecimento (graphify)

- `graphify-out/` (fora do git) tem o grafo do código: `GRAPH_REPORT.md` (resumo), `graph.json`, `graph.html`.
- Para perguntas de arquitetura ("quem chama X?", "como Y chega em Z?"), consulte antes de varrer arquivos:
  `graphify query "<pergunta>"`, `graphify path "A" "B"`, `graphify explain "X"`.
- Depois de mudanças grandes, atualize com `/graphify . --update`. O que não indexar fica em `.graphifyignore`.

## Armadilhas do ambiente

- No Bash deste ambiente, sequências como `\0` e `\n` dentro de comandos/heredocs chegam já convertidas
  em bytes reais. Para editar C++ com barras invertidas, use as ferramentas Edit/Write, não scripts.
- Python no Windows grava CRLF em modo texto: use `newline=''` ou `newline='\n'`. Os fontes usam LF.
- Constantes `CFSTR_*` do Win32 são ANSI sem `UNICODE`: use a string wide literal.

## Fluxo de trabalho

- Git: cada fase/assunto do `ROADMAP.md` em um branch próprio; commits pequenos com mensagem em português.
- Ao terminar uma tarefa: build sem avisos, `ctest` passando, README atualizado se mudou o uso.

## Trabalho em paralelo (vários agentes)

Cada tarefa tem uma **área**: as pastas que ela pode mudar à vontade. Fora da sua área, só os pontos de
coordenação abaixo, do jeito descrito. Dúvida se algo é da sua área: pergunte antes de editar.

### Áreas

| Área | Pastas e arquivos |
|---|---|
| Núcleo | `engine/{include/motor,src}/core/` |
| Renderer | `engine/{include/motor,src}/renderer/`, `engine/assets/shaders/` |
| Projeto e assets | `engine/{include/motor,src}/{project,assets}/` |
| Cena e componentes | `engine/{include/motor,src}/scene/` (núcleo: `Scene`, `Component`, `SceneSerializer`, `SceneRuntime`) |
| Aplicação | `engine/{include/motor,src}/app/`, `sandbox/` |
| Editor: viewport | `editor/src/SceneViewport.*` |
| Editor: Play | `editor/src/PlayController.*`, `editor/src/PlayMode.*` |
| Editor: arquivos | `editor/src/SceneDocument.*`, `FileDialog.*`, `EditorSettings.*` |
| Editor: scripts | `editor/src/Script*.*`, `editor/templates/` |
| Editor: painéis | `editor/src/{Hierarchy,Inspector,Scene,Project}Panel.*`, `FieldDrawers.*` |
| Editor: MCP | `editor/src/mcp/` |
| Testes | `tests/test_<sua área>.cpp` (arquivo novo por assunto; não reescreva testes de outra área) |

Componente novo da engine = `scene/<Nome>.h` + `scene/<Nome>.cpp` próprios (`MOTOR_COMPONENT_MODULE`), não
importa a fase: isso não conflita com ninguém. Tipo de campo novo = `FieldTraits<T>` + `MOTOR_FIELD_DRAWER`
em arquivos próprios. Camada nova (física, áudio, HUD) = pasta `engine/{include/motor,src}/<camada>/` própria.

### Pontos de coordenação (arquivos que várias tarefas tocam)

Só **acrescente** (uma linha ou um bloco no fim da lista), num commit pequeno separado, sem reordenar nem
reformatar o que existe. Precisa mudar mais que isso? Pare e pergunte ao usuário.

- `engine/CMakeLists.txt` (`MOTOR_LAYERS`) e `engine/src/<camada>/CMakeLists.txt` de outra área
- `engine/include/motor/Motor.h`, `engine/include/motor/scene/Components.h`
- `CMakeLists.txt` da raiz (`FetchContent`: biblioteca nova só com aprovação do usuário), `editor/CMakeLists.txt`
- `editor/src/EditorApp.*` (só para montar uma peça nova: membro, chamada no loop, item de menu) e `EditorContext.*`
- `README.md` e `ROADMAP.md` (só a seção da sua fase), `CLAUDE.md`
- Headers públicos de outra área (`engine/include/motor/...`): mudança de API é tarefa própria, combinada antes

### Fases do roteiro: onde cada uma mexe

| Fase | Área principal | Toca também (coordenação) | Não rodar junto com |
|---|---|---|---|
| 4 Luzes | componentes de luz em `scene/`, `renderer/` (N luzes), shaders | `Scene` e `SceneSerializer` (a luz da cena vira componente; cenas antigas convertidas), `SceneViewport` (ícones), painel Cena | 7 |
| 5 Física | camada nova `physics/` | **pré-requisito:** sistemas no `SceneRuntime` (abaixo); biblioteca nova | — |
| 6 Materiais/prefabs | `assets/`, `ProjectPanel` | `SceneSerializer` (prefab), Inspector | 11 |
| 7 Render avançado | `renderer/`, shaders | `Renderer` (mesmo arquivo da 4) | 4 |
| 8 Áudio | camada nova `audio/` | sistemas no `SceneRuntime`; biblioteca nova (miniaudio) | — |
| 9 HUD | camada nova `ui/` | `renderer/` (desenho 2D) | 7 |
| 11 Modelos 3D/animação | `assets/` (importador glTF), `renderer/` (`Mesh` na CPU, skinning) | prefabs da 6, collider de malha (`physics/`) | 6, 7 |
| 12 Documentação de scripts | `docs/`, `editor/templates/Scripts/` | `README.md` (seção Scripts), comentários de `Script.h` | — |
| 13 Servidor MCP | `editor/src/mcp/` | `EditorApp` (montar a peça), métodos sem diálogo em `SceneDocument`, `PlayController` (entrada externa), `ScriptController::GetStatus` | — |

Prioridade atual (ver `ROADMAP.md`, "Prioridades"): Alpha 1 fechada; 8 e 10 prontas; próximas a validação de uso real e "Ambiente".

**Sistemas do `SceneRuntime` (feito, pré-requisito das Fases 5 e 8):** camada de cima (física, áudio, HUD)
que precisa rodar a cada passo do jogo registra um `motor::RuntimeSystem` num `.cpp` próprio com
`MOTOR_RUNTIME_SYSTEM(Nome, Tipo, ordem)` (`motor/scene/RuntimeSystem.h`, que documenta a ordem exata). Não
mexa no `SceneRuntime` para isso. Use as ordens de lá (`kSystemOrderPhysics`, `kSystemOrderAudio`,
`kSystemOrderUi`; ordem nova = constante nova no fim da lista); `order < 0` roda antes dos scripts. Estado
do sistema fica na instância (uma por runtime, criada no `Start`); scripts acham com `GetSystem<T>()`.
Exemplo testado: `tests/test_runtime_systems.cpp`.

### Regras de cada tarefa

1. Um branch e uma worktree por tarefa (`fase-<n>-<assunto>` ou `refactor/<assunto>`), a partir do `main`
   atualizado. Nunca trabalhe direto no `main` nem na pasta de outra tarefa.
2. Antes de terminar: traga o `main` mais recente para o seu branch, resolva os conflitos, e confira
   build sem avisos + `ctest` inteiro (incluindo `test_layers`).
3. Não mude o comportamento de outra área "de passagem": anote no relatório como tarefa sugerida.
4. Formato de arquivo mudou (cena, projeto, `.meta`)? Suba a versão e continue lendo as antigas.
   Interface com a DLL de scripts mudou (`Component.h`, `Script.h`, `ScriptModule.h`)? Suba `kScriptApiVersion`.
5. Depois do merge no `main`, atualize o grafo: `/graphify . --update`.
