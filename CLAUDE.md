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
  - `scene/`: `Scene` (entidades, hierarquia, cópia profunda e `operator==`), `SceneSerializer` (`.mscene`).
  - `project/`: `Project` (pasta + `.mproject`). `core/Paths.h`: conversões UTF-8.
  - `app/`: `Application` (loop principal). O `Renderer` carrega as funções do OpenGL; a `Window` só cria o contexto.
  - `renderer/GL.h`: loader OpenGL próprio (X-macro). Função GL nova = adicionar na lista de lá.
- `editor/` — `MotorEditor`. `EditorApp` só monta as peças (loop, layout, barra de menus, atalhos,
  desfazer); `SceneViewport` (câmera livre, gizmo, seleção, arquivos soltos), `PlayController` (menu Jogo,
  botões e pedidos de Play), `SceneDocument` (cena/projeto abertos, menu Arquivo, popups, título),
  `ScriptController` (menu e painel Scripts, compilação e recarga da DLL de scripts do projeto).
  Um arquivo por painel, `EditorContext` (estado compartilhado), `UndoHistory`.
  `PlayMode` (cópia/restauração da cena no Play; o loop de jogo é `motor::SceneRuntime`).
  Recurso novo do editor vai na peça da sua área, não no `EditorApp`.
- Scripts de jogo ficam na pasta `Scripts/` de cada projeto (`motor::Script` + `MOTOR_SCRIPT`, ver
  `motor/scene/ScriptModule.h`). O editor compila uma DLL por projeto (`ScriptProject`, `ScriptBuilder`) e
  recarrega sem fechar (`ScriptHost`). A engine é a `libMotor.dll`, compartilhada pelo editor e pela DLL de
  scripts; modelos de script em `editor/templates/Scripts/`.
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

- **Nunca simule teclado ou mouse globais** (SendKeys, keybd_event, SetCursorPos...) para testar o editor:
  o foco pode ir para outra janela e o texto vai parar em outro lugar (já aconteceu). Peça confirmação
  explícita antes de qualquer entrada sintética.
- Para ver o editor: abra-o com um projeto de teste na pasta temporária e capture a janela com
  `PrintWindow` (flag `PW_RENDERFULLCONTENT`, funciona sem foco), depois encerre o processo.
  Ex.: `MotorEditor.exe "C:\...\scratchpad\Projeto Teste"`.
- Abrir o editor grava `%APPDATA%\Motor\editor.json` (último projeto, recentes). **Faça cópia antes e
  restaure depois** — é a configuração real do usuário.
- O que depende de cliques (menus, arrastar, atalhos) fica como "não testado" no relatório, com passos
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
