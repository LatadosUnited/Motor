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
- [ ] **Identidade:** ícone e nome do `.exe` nas configurações do projeto; versão do jogo.
- [ ] **Exportação sempre Release:** Player e `libMotor.dll` de Release da versão instalada, e a DLL de scripts
  recompilada em Release ao exportar. *(origem: Fase 10, limitação)*
- [ ] **Salvar progresso:** API de dados persistentes para scripts (chave/valor e arquivos em
  `%APPDATA%\<Jogo>\`), com exemplo no guia.
- [ ] **Controle (gamepad)** pela GLFW, com mapeamento de ações ("Pular" = Espaço ou botão A) configurável no
  projeto. *(origem: análise do código, item 9: "WASD ou analógico")*
- [ ] **Tela de carregamento** simples ao trocar de cena.

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

## Limitações

- A troca para tela cheia e de resolução não foi testada automaticamente (mudaria a tela do usuário): fica no
  roteiro de teste à mão (seção 12). A janela Configurações do jogo também não foi vista (o MCP não abre menus).
- No editor, tela cheia, resolução e vsync pedidos pelo jogo só ficam guardados (o editor não muda a própria
  janela), e as opções não são gravadas: cada Play volta às do projeto.
