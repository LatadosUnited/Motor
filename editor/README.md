# Editor — uso

O `MotorEditor.exe` (`editor/`). Estado e roteiro: `docs/editor/estado.md` e `docs/editor/roteiro.md`. Os formatos
de arquivo (cenas, materiais, prefabs) e os componentes estão em `engine/README.md`.

## Editor

| Ação | Como |
|---|---|
| Voar pela cena | segurar botão direito no viewport + WASD/QE (Shift acelera) |
| Selecionar | clique no objeto no viewport ou na hierarquia (Esc limpa) |
| Selecionar vários | Ctrl+clique alterna (viewport e hierarquia); Shift+clique na hierarquia pega o intervalo; arrastar no vazio do viewport faz um retângulo (com Ctrl/Shift, soma); Ctrl+A seleciona tudo. Gizmo, Del, Ctrl+D e arrastar na hierarquia agem em todos; o Inspector mostra o último escolhido e repete nos outros o que mudar nele |
| Gizmo | Q nenhum / W mover / E rotacionar / R escalar; X alterna eixos Local/Global |
| Snap | botão "Snap" na barra do viewport, ou segurar Ctrl ao arrastar (padrão 0.5 m / 15° / 0.1). Mover com encaixe põe a posição na grade. A seta ao lado do Snap abre os passos (mover, girar, escalar) e a grade (visível, altura) |
| Grade | botão "Grade" (começa desligada): linhas no chão a cada 10 passos do mover (as finas, de cada passo, ligam em "Linhas finas" na seta do Snap), eixos X (vermelho) e Z (azul); objetos na frente tapam a grade. Encaixe e grade ficam salvos no editor |
| Reparentar | arrastar na hierarquia (soltar no vazio = raiz) |
| Buscar na hierarquia | Ctrl+F ou a caixa ao lado do "+ Criar": palavras do nome e `t:componente` (`t:luz`, `t:collider`, `t:Girar`), sem diferença de maiúsculas e acentos; os resultados aparecem numa lista com o caminho do pai, e "Selecionar todas" pega todos. Esc ou "x" limpa |
| Criar | botão "+ Criar", menu Entidade ou clique direito na hierarquia; as formas (Cubo, Esfera, Plano, Cilindro, Cápsula, Cone) já vêm com o Collider que bate com elas |
| Focar / Excluir / Duplicar | F / Del / Ctrl+D |
| Copiar / Recortar / Colar | Ctrl+C / Ctrl+X / Ctrl+V (também no menu Editar e no clique direito da hierarquia, com "Colar como filho"): os selecionados com os filhos, colados no mesmo lugar do mundo; vale colar em outra cena do projeto |
| Desfazer / Refazer | Ctrl+Z / Ctrl+Y (ou Ctrl+Shift+Z), também no menu Editar; segurar repete. Cobre também materiais (`.mmat`) e prefabs editados no Inspector (o arquivo volta junto). No Play, o que se muda em materiais e prefabs volta ao parar |
| Restaurar layout | Ver > Restaurar layout (o layout fica salvo em `imgui.ini`) |
| Novo / Abrir projeto | Arquivo > Novo projeto... / Abrir projeto... / Projetos recentes |
| Nova / Abrir cena | Arquivo > Nova cena (Ctrl+N) / Abrir cena... (Ctrl+O) |
| Salvar cena | Arquivo > Salvar cena (Ctrl+S) / Salvar cena como... (Ctrl+Shift+S) |
| Play / Parar | botão ▶ / ⏹ no topo do viewport, Ctrl+P ou menu Jogo |
| Pausa / Avançar um quadro | botões ⏸ / ⏭ (ou Ctrl+Shift+P) |
| Câmera do jogo ↔ do editor no Play | botão "Câmera: jogo/editor" ou F8 |
| Cena e Jogo lado a lado | Jogo > "Cena e Jogo lado a lado": a janela **Jogo** aparece à direita do Viewport e mostra a câmera do jogo (também fora do Play); no Play o jogo recebe teclado e mouse com ela em foco (clique nela) e o Viewport fica na câmera do editor, para mexer na cena jogando. Fechar a janela desliga; a escolha fica salva |

**Desfazer** vale para tudo que muda a cena: transform (gizmo ou Inspector), criar, excluir, duplicar,
reparentar, renomear, trocar malha/textura/cor, luz e ambiente. Cada interação vira **um** passo: um
arraste inteiro do gizmo ou de um campo, ou uma edição de nome do clique até o Enter. A seleção volta
junto. Guarda até 100 passos por cena; abrir ou criar outra cena limpa o histórico. Operações de arquivo
do painel Projeto (renomear, mover, excluir arquivos) não entram no histórico. Dentro de um campo de
texto, Ctrl+Z desfaz o texto digitado.

O título da janela mostra a cena aberta e um `*` quando há alterações não salvas. Novo, Abrir e
fechar a janela perguntam antes de descartar alterações (Salvar / Não salvar / Cancelar).

### Modo Play

- **Play** guarda uma cópia da cena e liga o loop de jogo: os scripts recebem `OnStart`, `OnUpdate`
  (todo frame) e `OnFixedUpdate` (passo fixo de 1/60 s). Fora do Play nenhum script roda.
- O viewport passa a mostrar a cena pela **câmera principal** (componente Câmera; ver abaixo), sem gizmo
  nem contorno de seleção. Sem câmera na cena, mostra pela câmera do editor com um aviso. F8 (ou o botão
  "Câmera") alterna para a câmera livre do editor sem parar o jogo.
- O jogo só recebe teclado e mouse com o **viewport em foco** (o Play já dá o foco a ele) e sem campo de
  texto ativo. Nesse estado as teclas de atalho do editor (W/E/R, Del...) ficam com o jogo.
- Indicação: borda azul no viewport e "JOGANDO" na barra de menus (laranja/"EM PAUSA" na pausa).
- **Pausa** congela o jogo; "Avançar um quadro" roda um passo de cada vez.
- **Parar** chama `OnDestroy` e devolve a cena exatamente como estava: tudo que mudou no Play (pelos
  scripts ou pelo Inspector) é descartado. Desfazer fica desligado durante o Play, salvar pede para parar
  antes, e abrir/criar cena ou projeto e fechar o editor param o Play primeiro.

## Projetos

Um projeto é uma pasta:

```
MeuJogo/
  MeuJogo.mproject     { "format": "motor-project", "version": 2, "name": "MeuJogo",
                         "engineVersion": "0.2.0", "startupScene": "Assets/Scenes/Principal.mscene" }
  Assets/
    Scenes/
      Principal.mscene
  Scripts/             scripts do jogo em C++ (ver `engine/README.md`, "Scripts")
    Girar.cpp
  .motor/              gerado pelo editor (compilação dos scripts); pode apagar, fica fora do git
  .gitignore
```

- **Novo projeto** cria essa estrutura (em Documentos, por padrão) com uma cena inicial: chão, um cubo que
  gira com o script de exemplo `Scripts/Girar.cpp` e a câmera do jogo.
- O editor lembra o último projeto e a última cena aberta em `%APPDATA%/Motor/editor.json`
  e os reabre ao iniciar. Sem projeto, abre a cena de exemplo.
- A primeira cena salva num projeto sem cena inicial vira a `startupScene`.
- `engineVersion` é a versão do Motor que salvou o projeto por último (o Hub abre cada projeto com a sua).
  Projetos do formato 1 (sem esse campo) abrem como "versão desconhecida"; abrir um projeto de outra versão
  no editor passa o projeto para a versão do editor (aviso no log). A versão do editor aparece no título, em
  **Ajuda > Sobre** e em `MotorEditor --version`.

### Linha de comando do editor

```
MotorEditor [pasta-do-projeto | arquivo.mproject] [opções]
  --project <pasta>             abre o projeto (o mesmo que passar a pasta sozinha)
  --create <pasta-pai> <nome>   cria o projeto (como Arquivo > Novo projeto) e abre
  --settings <arquivo>          usa outro arquivo de configuração no lugar de %APPDATA%/Motor/editor.json
  --no-settings                 não lê nem grava configuração nenhuma (agentes e testes automáticos)
  --mcp[=porta]                 liga o servidor MCP
  --version                     mostra a versão (e a da API de scripts) e sai
```

### Painel Projeto

Navegador da pasta `Assets`: árvore de pastas à esquerda, conteúdo à direita, em **lista** (com tipo e
tamanho) ou em **grade** (ícones grandes e miniaturas das imagens) — botão "Ver".

| Ação | Como |
|---|---|
| Entrar na pasta / abrir cena | duplo clique ou Enter (outros arquivos abrem no programa padrão do Windows) |
| Voltar uma pasta | botão ▲ ou Backspace (também dá para clicar na trilha `Assets > ...`) |
| Selecionar vários | Ctrl+clique alterna, Shift+clique pega o intervalo, Ctrl+A seleciona tudo |
| Nova pasta / nova cena | botão "+ Criar" ou clique direito no espaço vazio |
| Copiar / recortar / colar | Ctrl+C / Ctrl+X / Ctrl+V (cola na pasta aberta; nome repetido vira "nome 2") |
| Duplicar | Ctrl+D (cópia na mesma pasta) |
| Renomear | F2 ou clique direito (a extensão é mantida) |
| Mover | arrastar os itens para uma pasta (na lista, na árvore ou na trilha) |
| Excluir | Del ou clique direito: **vai para a Lixeira**, com confirmação |
| Buscar | Ctrl+F ou o campo "Buscar": procura pelo nome em todo o `Assets` (Esc limpa) |
| Filtrar | caixa ao lado da busca: Todos / Cenas / Texturas / Malhas |
| Tamanho dos ícones | Ver > Tamanho, ou Ctrl + roda do mouse no modo grade |
| Mostrar no Explorer | clique direito |

**Importar arquivos:** arraste arquivos ou pastas do Explorer para a janela do editor. Eles são
**copiados** para a pasta sob o mouse no painel (ou para a pasta aberta). Também funciona copiar no
Explorer (Ctrl+C) e colar no painel (Ctrl+V), e o contrário: o Ctrl+C do painel cola no Explorer.

Renomear, mover ou excluir a cena aberta (ou a pasta dela) atualiza o editor: o título e a
cena inicial do projeto acompanham. Se a cena aberta for excluída, ela continua na memória como
"Sem título" com alterações pendentes. Mudanças feitas fora do editor aparecem em até 1 s.

## Velocidade da compilação dos scripts

- **Cabeçalho pré-compilado** (`build/bin/sdk/MotorScriptPch.h.gch`, alvo `motor_sdk_pch`), refeito quando um
  header da engine muda e incluído em todo script.
- **Headers leves**: `Script.h` não puxa `<format>` (o `Log("x = {:.2f}", x)` formata dentro da `libMotor.dll`),
  `Scene.h` não puxa o renderer, e registrar campos não instancia `std::function` por campo.
- **Configura uma vez só**: o CMakeLists gerado lista os `.cpp` (sem `GLOB ... CONFIGURE_DEPENDS`) e só muda
  quando entra ou sai arquivo; mudar a versão do Motor ou o toolchain apaga o cache e reconfigura limpo.
- **Ninja e lld**: se `ninja.exe` e `ld.lld.exe` estiverem ao lado do compilador (MSYS2 UCRT64:
  `pacman -S mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-lld`), são usados; senão, make e `ld`.
- O painel **Scripts** e a ferramenta MCP `scripts_status` mostram o tempo de cada etapa e de cada arquivo.
- **Vigia imediata**: o editor recebe do Windows o aviso de arquivo salvo na pasta `Scripts/` (antes conferia a
  cada 0,75 s).
- **Editor mais leve enquanto compila**: fora do Play, o editor desenha a 10 FPS em segundo plano e a 20 FPS
  durante a compilação. A 60 FPS ele ocupava ~75% de um núcleo e, com o vídeo integrado dividindo a energia,
  a compilação de um script levava o triplo (1,6 s em vez de 0,54 s).

Medido com `test_script_project --bench` (Debug, GCC 15, i5-6400, scripts do modelo "Novo script"; o
"antes" é o início da Fase 17):

| Caso | Antes | Depois |
|---|---|---|
| 1 script, do zero | 8,42 s | 2,63 s |
| 10 scripts, do zero | 22,10 s | 4,30 s |
| 10 scripts, 1 arquivo editado (compilar → recarregado) | 9,19 s | 1,00 s |
| 50 scripts, do zero | 90,72 s | 11,47 s |

Etapas do caso "1 arquivo editado": compilar 0,59 s, ligar 0,23 s, recarregar 0,02 s. Dentro do editor aberto
(do salvar o arquivo até a recarga, medido pelo MCP com 10 scripts): 1,1 a 1,5 s; antes destes ajustes, ~2,7 s
mais a espera da vigia.

Limites atuais: os scripts precisam do Motor compilado na mesma máquina (o SDK aponta para a pasta de
build); um erro grave dentro de um script (ponteiro inválido) derruba o editor; sem projeto aberto não há
scripts.

## Exportar o jogo

**Arquivo > Exportar jogo...** monta uma pasta que roda o jogo sem o editor (e sem o MSYS2): escolha a cena
inicial (a do projeto vem marcada; a caixa "Gravar como cena inicial" grava a escolha no `.mproject`, no
campo `startupScene` que já existia) e a pasta de saída (padrão: `<projeto>/Exportado`). Depois de exportar,
os botões **Abrir pasta** e **Rodar o jogo** levam ao resultado.

```
Exportado/
  Jogo Ação.exe          o Player com o nome do projeto
  libMotor.dll           a engine (e libstdc++-6, libgcc_s_seh-1, libwinpthread-1: runtime do GCC)
  engine/                shaders e fontes da engine
  Dados/                 o projeto, só com o que o jogo usa
    Jogo Ação.mproject   com a cena inicial
    GameScripts.dll      os scripts compilados
    Assets/...           assets usados, com os .meta (os mesmos GUIDs do projeto)
  motor-export.json      marca de exportação
  Jogo Ação.log          criado ao rodar: o log do jogo
```

- **O que entra de `Assets/`:** todas as cenas (a troca de cena dos scripts é por nome, e o nome pode ser
  montado no código); tudo o que elas citam, seguindo as referências (malhas, texturas, materiais, prefabs,
  modelos glTF com os `.bin` e as imagens deles, fontes da UI, céus HDR, e qualquer componente novo que guarde
  um `{guid, path}`); a pasta **`Assets/Recursos/`** inteira (para arquivos que o jogo abre por conta própria);
  e caminhos `"Assets/..."` escritos entre aspas nos fontes de `Scripts/`. O resto fica de fora. Referências
  quebradas (GUID sem arquivo) aparecem como aviso e o jogo é exportado mesmo assim.
- **Sempre em Release** (Fase 21): o jogo leva o `Player.exe` e a `libMotor.dll` de Release e a DLL de
  scripts é **recompilada em Release** ao exportar (em `.motor/scripts/build-release`, separada da do editor; a
  janela mostra "Compilando os scripts em Release..."). O Motor em Release vem da pasta do editor (versão
  instalada de Release), de `release/` ao lado dela (o `motor_package` de uma versão de Debug copia um para lá)
  ou de `build-release/bin` no código-fonte (`MOTOR_RELEASE_BIN_DIR` no CMake). Sem nenhum, o jogo sai com o
  Motor do editor e um aviso explica como ter o de Release:

  ```bash
  cmake -S . -B build-release -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DMOTOR_BUILD_TESTS=OFF
  cmake --build build-release -j8 --target Player
  ```
- **Scripts:** um erro de compilação em Release aparece na janela (e no log) e cancela a exportação. Sem Motor
  em Release, a DLL do editor precisa estar compilada (a exportação recusa durante a compilação ou com erros).
  Os fontes não vão, só a `GameScripts.dll`.
- **Exportar de novo** na mesma pasta substitui a exportação anterior (só o que ela tinha criado, listado na
  marca); uma pasta que não esteja vazia nem seja uma exportação é recusada.
- **Player** (`build/bin/Player.exe`, pasta `player/`): o executável do jogo, compilado junto com o editor.
  Abre `Dados/` ao lado dele, carrega a DLL de scripts e roda a cena inicial como o Play do editor (mesmo
  `SceneRuntime`, física, HUD e câmera principal). Para testar um projeto sem exportar:
  `Player.exe "C:/caminho/MeuJogo"`. `--quadros N` fecha sozinho depois de N quadros (testes automáticos);
  `--janela` abre em janela mesmo com tela cheia nas opções; `--opcoes "<arquivo>"` usa outro arquivo de opções.
  O título da janela é só o nome do jogo (sem FPS); `--stats` acrescenta FPS, ms e draws (medição, `bench/`), e
  `--perf "<arquivo.csv>"` grava os contadores de cada quadro (ver `engine/README.md`, "Contadores por quadro"); `--segundos S` fecha
  sozinho depois de S segundos (e grava o `--perf`).
- **Identidade do jogo** (Fase 21, **Arquivo > Configurações do jogo** ou MCP `game_settings`): nome do `.exe`
  (vazio = o do projeto), versão (`1.0.0`, vai para as propriedades do `.exe` e para `Script::GameVersion`) e
  ícone (`.ico` com vários tamanhos ou `.png` de até 256x256, de `Assets/`). A exportação grava o ícone e a versão
  como recursos do Windows no `.exe` copiado; o ícone aparece no Explorer, na janela e na barra de tarefas (a
  GLFW usa o recurso `GLFW_ICON`). Ícone que não serve faz a exportação ser recusada antes de mexer na pasta.
- **Tela de carregamento** (Fase 21): o jogo exportado desenha uma cena parada enquanto abre e a cada
  `LoadScene` (o carregamento trava o jogo até terminar). Sem configurar, fundo escuro e "Carregando..."; com
  **Configurações do jogo > Tela de carregamento** (MCP `game_settings`, `loading_scene`), uma cena do projeto
  feita no editor com UI (imagem, texto) e, se quiser, câmera e objetos. Ela é só desenhada: scripts e física
  não rodam nela. No Play do editor a troca de cena não mostra a tela.
- **Controle e ações** (Fase 21): os scripts perguntam por ações (`GetAxis("Horizontal")`,
  `WasActionPressed("Pular")`) do mapa do projeto (**Arquivo > Controles**, `.mproject` em `"input"`, MCP
  `input_actions`), que liga teclas, mouse e o controle (gamepad, padrão Xbox, pela GLFW). O padrão traz
  Horizontal, Vertical, Olhar X/Y, Pular, Correr, Interagir e Pausa. Controle direto: `IsGamepadButtonDown`,
  `GamepadAxisValue`. Os modelos Jogador e MoverWASD andam com o teclado ou o analógico. O MCP `game_gamepad`
  simula um controle no Play.
- **Progresso salvo** (Fase 21): os scripts guardam chave/valor em "slots" com `SaveSlot()` (`motor::SaveData`:
  `GetInt`/`SetInt`, `GetString`, `GetVec3`... e `Save()`), `HasSave`, `DeleteSave` e `SaveFolder`. No jogo
  exportado ficam em `%APPDATA%\<Jogo>\saves\<slot>.json`; no Play do editor, em `<projeto>/.motor/saves/`
  (**Jogo > Apagar saves do Play**, MCP `game_saves`). O que não foi gravado é gravado ao fechar o jogo e ao
  parar o Play; arquivo estragado vira `.corrompido` e o jogo segue sem save.
- **Opções do jogador** (Fase 21): tela cheia, resolução, vsync, MSAA e sombras. As iniciais ficam no projeto
  (**Arquivo > Configurações do jogo**, `.mproject` formato 4, ou a ferramenta MCP `game_settings`); depois valem
  as do jogador, gravadas em `%APPDATA%\<Jogo>\opcoes.json`. Os scripts montam o menu com `GetOptions`,
  `ApplyOptions`, `Resolutions` e `QuitGame` (guia, seção "Interface do jogo"). No Play do editor só o MSAA e as
  sombras valem, e nada é gravado.
  Os shaders são procurados em `engine/` ao lado do executável e, se não houver, na pasta do código-fonte.
- Pelo servidor MCP: ferramenta `game_export` (cena, pasta, `set_startup`). Testes: `tests/test_export.cpp`.

## Servidor MCP (agentes no editor)

O editor tem um servidor [MCP](https://modelcontextprotocol.io) (Model Context Protocol): um agente como o
Claude Code vê e mexe no editor pelas mesmas operações da interface, sem simular mouse nem teclado do sistema.

- **Ligar:** menu **Ver > Servidor MCP**. Fica ligado nas próximas vezes que o editor abrir. Outra opção é a
  linha de comando `MotorEditor <projeto> --mcp` (ou `--mcp=<porta>`). A porta padrão é **47800**, e enquanto
  o servidor estiver ligado aparece "MCP" na barra de menus.
- **Conectar o Claude Code** (uma vez):

  ```bash
  claude mcp add --transport http motor http://127.0.0.1:47800/mcp
  ```

- **Ferramentas:**

  | Grupo | Ferramentas |
  |---|---|
  | Situação | `editor_status`, `scene_tree`, `entity_get` (no Play, também o estado do jogo: clipe do Animator, no chão, velocidade), `component_types`, `log_read`, `events_read` (o que mudou no editor: seleção, cena, Play, scripts), `docs_read` (documentação de scripts desta versão; o `editor_status` mostra em `script_reference` as pastas da documentação e dos headers do SDK) |
  | Cena | `entity_create`, `entity_update`, `entity_delete`, `entity_duplicate`, `entity_copy`, `entity_cut`, `entity_paste`, `select`, `scene_search`, `component_add`, `component_remove`, `component_set`, `scene_environment`, `undo`, `redo` |
  | Arquivos | `project_create`, `scene_save`, `scene_open`, `scene_new`, `assets_list`, `prefab_instantiate`, `prefab_create`, `asset_apply` |
  | Jogo | `play` (play/pause/resume/step/stop; `wait` responde quando valeu), `game_key` (teclas só para o jogo), `ui_click` (botões do HUD), `audio_status` (sons tocando, ouvinte, nível da saída) |
  | Scripts | `scripts_build` (`wait` responde quando a compilação termina), `scripts_status` (erros com arquivo e linha) |
  | Exportar | `game_export` (pasta com o .exe do jogo, ver "Exportar o jogo") |
  | Ver | `viewport_capture` (PNG da cena e do HUD, desenhado na hora; vistas scene, game e editor), `view_focus`, `view_camera` (mover e virar a câmera do editor), `view_side_by_side` |

  As mudanças viram passos de **desfazer**, como as feitas na interface. Durante o Play, a cena é a do jogo:
  o que mudar é descartado ao parar. O servidor atende também com a janela do editor **minimizada** ou sem
  foco, e o `viewport_capture` desenha a cena na hora num framebuffer próprio, então a imagem é a do momento
  do pedido. `play` e `scripts_build` com `wait: true` só respondem quando o pedido valeu (o servidor confere a
  cada quadro, sem travar os outros pedidos), o que dispensa esperar com `sleep` em testes automáticos.
- **Segurança:** o servidor só escuta em `127.0.0.1` e recusa pedidos vindos de páginas da web (cabeçalhos
  `Origin`/`Host` de fora). Ele não tem senha: enquanto estiver ligado, qualquer programa deste computador pode
  usá-lo. Desligue quando não estiver usando.
- **Como funciona:** o protocolo JSON-RPC fica em `editor/src/mcp/McpProtocol`, o HTTP em `McpServer` e as
  ferramentas em `McpSceneTools` e `McpController`. Os pedidos chegam numa thread do HTTP e são atendidos na
  thread principal, uma vez por quadro. A captura sai em PNG com um compressor próprio (`ImageEncode`).
  Testes: `tests/test_mcp.cpp`.
