---
tipo: fase
alpha: 2
fase: 21
titulo: Jogo entregue
status: em andamento
prioridade: 6
areas: [player, exportacao, core, scripts]
branch: fase-21-jogo-entregue
inicio: 2026-10-19
fim: 
---

# Fase 21 — Jogo entregue

## Objetivo

O jogo exportado deixa de ser uma "janela de teste": opções para o jogador, identidade própria, progresso
salvo e controle.

## Tarefas

- [x] **Opções do jogador:** tela cheia, resolução, vsync e qualidade (sombras, MSAA) num arquivo de
  configurações do jogo, com API para os scripts montarem um menu de opções. O MSAA hoje é sempre 4x: o
  `Renderer::SetSamples` existe, mas o projeto não o expõe. Título da janela sem o FPS. *(origem: Fase 10,
  limitações; análise do código, item 3)* — `a31aaae`
- [x] **Identidade:** ícone e nome do `.exe` nas configurações do projeto; versão do jogo. — `ca7c989`
- [x] **Exportação sempre Release:** Player e `libMotor.dll` de Release da versão instalada, e a DLL de scripts
  recompilada em Release ao exportar. *(origem: Fase 10, limitação)* — `7e2e64e`
- [x] **Salvar progresso:** API de dados persistentes para scripts (chave/valor e arquivos em
  `%APPDATA%\<Jogo>\`), com exemplo no guia. — `c6d8a39`
- [x] **Controle (gamepad)** pela GLFW, com mapeamento de ações ("Pular" = Espaço ou botão A) configurável no
  projeto. *(origem: análise do código, item 9: "WASD ou analógico")* — `d5e6b3f`
- [x] **Tela de carregamento** simples ao trocar de cena. — `24bc1c6`

## Pronto quando

O jogo da [Fase 19](fase-19-validacao.md) exportado abre em tela cheia com ícone próprio, tem menu de opções e
de continuar (save), e é jogável com controle.

## Feito

- **Opções do jogador** (`a31aaae`): `motor::GameOptions` (`project/GameOptions.h`: tela cheia, resolução,
  vsync, MSAA, sombras) com as padrão no projeto (`.mproject` formato 4, bloco `"player"`; **Arquivo >
  Configurações do jogo**; MCP `game_settings`) e as do jogador em `%APPDATA%\<Jogo>\opcoes.json`
  (`PlayerDataDir`, que serve também para os saves). Os scripts falam com quem roda o jogo pelo `GameHost`
  (`scene/GameHost.h`, `SceneRuntime::SetHost`): `GetOptions`, `ApplyOptions`, `Resolutions` e `QuitGame`
  (`kScriptApiVersion` 14). O Player aplica na hora (`Window::SetDisplayMode`, `SetVSync`,
  `Renderer::SetSamples` e o novo `Renderer::SetShadows`, que desliga ou limita a qualidade de cada luz) e
  grava; abre com as opções salvas e o título é só o nome do jogo (`ApplicationDesc::statsInTitle`). Novas
  opções `--janela` e `--opcoes`. No Play do editor (`PlayHost`) cada Play começa nas opções do projeto, o MSAA e
  as sombras valem no viewport e `QuitGame` para o Play. Exemplo `docs/examples/MenuOpcoes.cpp` no guia.
  Testes: `test_game_options`; conferido no editor (MCP) e no Player com um script que aplica e sai.

- **Identidade** (`ca7c989`): `GameIdentity` no projeto (nome do `.exe`, versão, ícone), na seção Identidade
  das Configurações do jogo e no MCP `game_settings`. A exportação lê o ícone antes de mexer na pasta (`.ico`
  com todas as imagens ou `.png` de até 256x256) e grava no `.exe` copiado, com `UpdateResource`
  (`export/ExeResources`), o grupo de ícones `GLFW_ICON` (a GLFW o usa sozinha na janela e na barra de tarefas)
  e o `VS_VERSIONINFO` (nome, versão, nome do arquivo). `Script::GameVersion` para o menu. Testes:
  `test_exe_identity` (grava numa cópia do próprio teste e lê com `GetFileVersionInfo`/`FindResource`);
  conferido exportando pelo MCP: propriedades do `.exe`, ícone no Explorer e na barra de título.

- **Exportação sempre Release** (`7e2e64e`): `export/ReleaseRuntime` acha um Motor em Release (Player, DLLs de
  runtime e `sdk/MotorSDK.json` com `buildType` Release) na pasta do editor, em `release/` ao lado dela ou em
  `MOTOR_RELEASE_BIN_DIR` (cache do CMake, padrão `build-release/bin`). O `ExportController` exporta em duas
  etapas (`Begin`/`Poll`): recompila a DLL de scripts em Release contra esse SDK (`.motor/scripts/cmake-release`
  e `build-release`, um `ScriptBuilder` próprio, em segundo plano na janela; o MCP `game_export` espera) e só
  então copia. Sem Motor em Release, exporta com o do editor e avisa como ter um. O `motor_package` de uma
  versão que não é de Release copia um build de Release para `release/`, com SDK de caminhos relativos (headers
  de `sdk/include`). Conferido: pelo editor de Debug do código e pelo editor do pacote, a pasta exportada caiu
  de ~117 MB para ~12 MB, a DLL de scripts de Release carrega e o jogo roda. Teste: `test_release_runtime`.

- **Salvar progresso** (`c6d8a39`): `motor::SaveData` (chave/valor com int, float, bool, texto e vec3; gravação
  atômica; arquivo estragado vira `.corrompido`) e `SaveStore` (slots `<nome>.json` numa pasta), em
  `project/SaveData.h`. Scripts: `SaveSlot`, `HasSave`, `DeleteSave` e `SaveFolder`, pelo `GameHost::Saves`. O
  Player grava em `%APPDATA%\<Jogo>\saves\`; o Play do editor em `<projeto>/.motor/saves` (relidos a cada
  Play; **Jogo > Apagar saves do Play**; MCP `game_saves`). O que não foi gravado é gravado ao fechar o jogo e
  ao parar o Play. Exemplo `docs/examples/MenuContinuar.cpp` (Continuar / Novo jogo e o jogador voltando para
  onde parou). Testes: `test_save_data`; conferido no Play do editor (MCP) e no jogo exportado em Release
  rodado duas vezes.

- **Controle e mapa de ações** (`d5e6b3f`): o `motor::Input` lê o primeiro controle que a GLFW reconhece como
  gamepad (padrão Xbox; analógicos com Y para cima positivo e zona morta de 0,2; gatilhos de 0 a 1), com nomes
  para teclas, botões e eixos e um modo simulado (testes e MCP). `motor::InputActions` (`project/`) é o mapa de
  ações do projeto, no `.mproject` (`"input"`): cada ação soma as entradas (`scale` -1 para o lado negativo),
  limitada a -1..1, e conta como apertada acima de 0,5. Padrão: Horizontal, Vertical, Olhar X/Y, Pular,
  Correr, Interagir e Pausa. Scripts: `GetAxis`, `IsActionDown`, `WasActionPressed`/`WasActionReleased` e o
  controle direto, todos com o filtro de foco. **Arquivo > Controles** edita o mapa (aperte para ligar, menu
  com todas as entradas, valor ao vivo de cada ação); MCP `input_actions` e `game_gamepad` (controle
  simulado). Os modelos Jogador e MoverWASD agora usam as ações. Testes: `test_input_actions`; conferido no
  Play pelo MCP com o controle simulado (analógico, teclado junto, botões A e Start).

- **Tela de carregamento** (`24bc1c6`): o Player desenha uma cena parada (sem scripts nem física) ao abrir e
  antes de cada `LoadScene`: a do projeto (`GameIdentity::loadingScene`, Configurações do jogo > Tela de
  carregamento, MCP `game_settings` com `loading_scene`) ou `DefaultLoadingScene` (`ui/LoadingScreen.h`, fundo
  escuro e "Carregando..." embaixo à direita). Com câmera, a cena 3D também é desenhada; sem, só a UI. A janela
  do jogo nasce escondida (`WindowDesc::visible`) e aparece já com a tela, sem o quadro branco de ~1 s enquanto o
  renderer inicia. Teste: `test_loading_screen`; conferido no jogo exportado (capturas da tela padrão e da
  janela só aparecendo pronta).

Todas as tarefas estão feitas. Falta o "Pronto quando": o jogo da Fase 19 exportado com tudo isso (tela cheia,
ícone, menu de opções e de continuar, controle), e o teste à mão do roteiro (seção 12).

## Limitações

- A troca para tela cheia e de resolução não foi testada automaticamente (mudaria a tela do usuário): fica no
  roteiro de teste à mão (seção 12). A janela Configurações do jogo também não foi vista (o MCP não abre menus).
- No editor, tela cheia, resolução e vsync pedidos pelo jogo só ficam guardados (o editor não muda a própria
  janela), e as opções não são gravadas: cada Play volta às do projeto.
- O `.png` vira uma imagem só no ícone: o Windows reduz para os tamanhos pequenos. Para ícones nítidos em 16 e
  32 pixels, use um `.ico` com vários tamanhos. Sem nome de empresa nem direitos autorais nas propriedades.
- Quem compila o Motor em Debug precisa compilar também o `build-release` (pelo menos o alvo `Player`) para
  exportar em Release; sem ele a exportação sai em Debug com um aviso. O build de Release não é refeito sozinho
  quando o código muda: depois de mudar a engine, recompile o `build-release` (alvo `Player`) antes de exportar,
  ou o jogo leva uma `libMotor.dll` velha (com API de scripts diferente é recusado; com a mesma, pode quebrar).
- Saves sem nuvem nem vários perfis de jogador; a versão do formato (`motor-save` 1) sobe se o formato mudar,
  mas os valores que o jogo grava são do jogo: mudar o significado de uma chave entre versões é com o script.
- Controle de verdade não testado (sem um aqui): a leitura é a da GLFW (`glfwGetGamepadState`), conferida só
  com o controle simulado. Um controle só (o primeiro); sem vibração e sem trocar teclas dentro do jogo
  exportado (o mapa é o do projeto). A janela Controles não foi vista na tela (o MCP não abre menus).
- A tela de carregamento é um quadro parado: sem animação nem barra de progresso (o carregamento trava o jogo).
  No Play do editor a troca de cena não a mostra. Só conferida ao abrir o jogo; a troca por `LoadScene` usa o
  mesmo código, mas não foi capturada.
