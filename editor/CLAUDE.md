# Editor — instruções para o Claude

Módulo Editor: `editor/` (`MotorEditor.exe`). Estado e roteiro em `docs/editor/`; uso em `editor/README.md`. As
regras comuns estão no `CLAUDE.md` da raiz; as da engine, em `engine/CLAUDE.md`.

## Estrutura

`EditorApp` só monta as peças (loop, layout, barra de menus, atalhos, desfazer). `editor/src/` tem uma subpasta
por área e os includes partem de `editor/src` (`#include "scripts/ScriptHost.h"`):

- `document/` (`SceneDocument`: cena e projeto abertos, menu Arquivo, popups, título);
- `viewport/` (`SceneViewport`: câmera livre, gizmo, seleção, arquivos soltos; `UiEditor`; painel de estatísticas);
- `play/` (`PlayController`: menu Jogo, botões e pedidos de Play; `PlayMode`: cópia e restauração da cena; o loop
  de jogo é `motor::SceneRuntime`);
- `panels/` (Hierarquia, Inspector, Cena, Projeto), `drawers/` (`FieldDrawers` e os `*Drawer.cpp`);
- `scripts/` (`ScriptController`, `ScriptProject`, `ScriptBuilder`, `ScriptHost`: compilar e recarregar a DLL de
  scripts do projeto); modelos de script em `editor/templates/Scripts/`;
- `export/` (`GameExport`: assets referenciados e pasta de saída; `ExportController`: Arquivo > Exportar jogo);
- `mcp/` (servidor MCP: `McpProtocol` JSON-RPC, `McpServer` HTTP com cpp-httplib, `McpSceneTools` ferramentas de
  cena testáveis sem janela, `McpController` com as demais, divididas em `McpFileTools.cpp` e `McpPlayTools.cpp`);
- na raiz, as peças comuns (`EditorApp`, `EditorContext`, `EditorSettings`, `UndoHistory`, `FileDialog`).

Recurso novo do editor vai na peça da sua área, não no `EditorApp`. Peça grande se divide em `<Peça><Parte>.cpp`
(ex.: `ProjectPanelDraw.cpp`), com o que as partes dividem num header interno (`ProjectPanelUtil.h`).

## Convenções do editor

- Assets de arquivo são referenciados por **GUID** (`.meta` ao lado). Quem move, renomeia ou exclui arquivo no
  editor leva o `.meta` junto (`ProjectPanel`). Exclusão vai sempre para a **Lixeira**.
- Recurso novo que um agente precise usar ganha ferramenta MCP em `McpController` (ou `McpSceneTools`, se for só
  cena).

## Verificar a interface — regras importantes

- **Prefira o servidor MCP do editor** para ver e testar: abra `MotorEditor.exe "<projeto>" --mcp` (porta 47800)
  e chame as ferramentas por HTTP (`POST http://127.0.0.1:47800/mcp`, JSON-RPC `tools/call`), ou pelo Claude Code
  se o servidor `motor` estiver configurado. `viewport_capture` substitui o PrintWindow; `play`, `game_key`,
  `ui_click` e `log_read` testam o jogo sem entrada sintética global.
- **Nunca simule teclado ou mouse globais** (SendKeys, keybd_event, SetCursorPos...): o foco pode ir para outra
  janela e o texto vai parar em outro lugar (já aconteceu). Peça confirmação explícita antes de qualquer entrada
  sintética.
- Sem MCP: abra o editor com um projeto de teste na pasta temporária e capture a janela com `PrintWindow`
  (flag `PW_RENDERFULLCONTENT`, funciona sem foco), depois encerre o processo.
- Abrir o editor grava `%APPDATA%\Motor\editor.json` (último projeto, recentes). **Use `--no-settings`** (ou
  `--settings <arquivo>`) em testes automáticos e, mesmo assim, faça cópia antes e restaure depois: é a
  configuração real do usuário. Projeto de teste novo: `--create <pasta-pai> <nome>` ou a ferramenta MCP
  `project_create`.
- O que o MCP não cobre (menus, arrastar, atalhos, gizmo) fica como "não testado" no relatório, com passos para
  o usuário conferir (e, se for recorrente, no `docs/teste-manual.md`).
