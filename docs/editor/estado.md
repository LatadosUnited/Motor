---
tipo: estado
modulo: editor
versao: 0.2.650
atualizado: 2026-10-21
---

# Editor — estado

O `MotorEditor.exe`, sobre a Engine. Código em `editor/src/` (uma subpasta por área: `document/`, `viewport/`,
`play/`, `panels/`, `drawers/`, `scripts/`, `export/`, `mcp/`) e modelos em `editor/templates/`. Como usar:
`editor/README.md`. [Roteiro](roteiro.md).

## O que faz hoje

- **Edição da cena:** painéis em docking (Hierarquia, Inspector, Cena, Projeto, Viewport), gizmo de mover,
  girar e escalar com snap e grade, seleção múltipla e edição em várias entidades, busca na hierarquia
  (`t:componente`), copiar e colar entre cenas, desfazer automático (inclui materiais e prefabs).
- **Assets:** painel Projeto com miniaturas, arrastar para a cena, renomear e mover levando o `.meta`,
  exclusão para a Lixeira; edição de materiais (`.mmat`) e prefabs (`.mprefab`) no Inspector; importação de
  glTF e FBX como prefab.
- **Play:** cópia da cena, câmera do jogo ou do editor (F8), pausa e avanço por quadro, Cena e Jogo lado a lado,
  painel de estatísticas por quadro.
- **Scripts:** compila a DLL de scripts do projeto (CMake + GCC do MSYS2, cabeçalho pré-compilado) e recarrega
  sem fechar; painel Scripts com erros e tempos; criar script pelo painel Projeto.
- **Exportar o jogo:** pasta autossuficiente com o Player, só os assets usados e os scripts em Release.
- **Projetos:** novo, abrir, recentes, cena inicial; linha de comando para o Hub e para agentes
  (`--create`, `--no-settings`, `--mcp`).
- **Servidor MCP** (porta 47800): um agente vê e mexe no editor pelas mesmas operações da interface (cena,
  arquivos, Play, entrada do jogo, captura do viewport, scripts, log).

## Números

- Editor Release, cena pequena: ImGui gasta 6 a 7 ms de CPU por quadro, mais que a cena (Render ~2 ms).
- Cena de referência (~5000 entidades): gravar a cena em JSON leva ~337 ms e ler ~314 ms
  (`test_desempenho`); a checagem de "não salvo" faz isso a cada 0,5 s.
- Recarga de um script simples: 1,5 a 1,9 s do salvar; com `<format>`, 5 a 6,5 s (projeto Island, Fase 17).

## Limitações conhecidas

- **Precisa do MSYS2 (UCRT64) e do CMake** instalados para compilar os scripts (caminhos no `sdk/MotorSDK.json`).
- **Um erro grave num script** (ponteiro inválido) fecha o editor.
- **Desfazer** compara a cena inteira a cada quadro e guarda até 100 cópias completas; "não salvo" serializa a
  cena inteira em JSON. Os dois pesam em cenas grandes.
- Com o viewport estreito (~830 px), a linha de FPS fica por baixo dos botões de Play (`docs/entrada.md`).
- O que o MCP não cobre (menus, arrastar, atalhos, gizmo) só se testa à mão ([roteiro de teste](../teste-manual.md)).

## Histórico

No [arquivo](../arquivo/leiame.md): [Fase 20 — Editor produtivo](../arquivo/roadmap/alpha-2/fase-20-editor-produtivo.md),
[Fase 17 — Scripts](../arquivo/roadmap/alpha-2/fase-17-scripts.md), [Fase 21 — Jogo entregue](../arquivo/roadmap/alpha-2/fase-21-jogo-entregue.md),
Fase 13 (servidor MCP) na [alpha 1](../arquivo/roadmap/alpha-1.md).
