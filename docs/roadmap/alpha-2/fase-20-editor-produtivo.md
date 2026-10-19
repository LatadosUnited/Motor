---
tipo: fase
alpha: 2
fase: 20
titulo: Editor produtivo
status: planejada
prioridade: 5
areas: [editor, mcp, assets]
branch: 
inicio: 
fim: 
---

# Fase 20 — Editor produtivo

## Objetivo

Montar cenas grandes só com o editor, sem cansar: selecionar vários, copiar e colar, alinhar na grade, achar
na hierarquia, e prefabs e materiais que se comportam como o resto.

## Tarefas

- [ ] ~~**Antes de tudo, dividir os arquivos que esta fase vai mexer**, um por commit, sem mudar comportamento:
  `ProjectPanel.cpp` (~1200 linhas), `SceneViewport.cpp` e `SceneDocument.cpp` (~600).~~ Feito na
  [Organização do código](organizacao-do-codigo.md). *(origem: revisão do código de 17/10/2026)*
- [ ] **Seleção múltipla** na hierarquia e no viewport (Ctrl/Shift, retângulo), com gizmo e Inspector para vários.
- [ ] **Copiar, recortar, colar e duplicar** várias entidades (com filhos), também entre cenas.
- [ ] **Snapping** na grade, em ângulos e em passos de escala; grade visível configurável.
- [ ] **Busca na hierarquia** (nome e tipo de componente).
- [ ] **Contorno verde do collider só no objeto selecionado**, como na Unity. Hoje o viewport desenha o de todos
  os objetos com collider o tempo todo (`SceneViewport::DrawColliders`, `editor/src/viewport/SceneViewportOverlay.cpp`),
  e desde a Fase 18 toda forma criada já vem com Collider: a cena enche de linhas e o custo na CPU cresce com o
  número de objetos (12 a ~100 linhas cada, refeitas a cada quadro). O botão **Colliders** da barra vira
  **Selecionado / Todos / Nenhum** (padrão Selecionado), lembrado entre aberturas. *(origem: teste à mão da
  Fase 19, 19/10/2026, gravidade atrapalha)*
- [ ] **Janelas Cena e Jogo lado a lado** no Play. *(origem: Fase 2, limitação)*
- [ ] **Prefab dentro de prefab.** *(origem: Fase 6, limitação)*
- [ ] **Edições de material e de prefab no desfazer**, e editadas no Play voltam ao parar. *(origem: Fase 6,
  limitação)*
- [ ] **Modelos:** malhas de modelo no combo "Malha"; pré-visualizar animação fora do Play. *(origem: Fase 11,
  limitação)*
- [ ] **MCP acompanha:** seleção múltipla, copiar/colar, mover a câmera do editor para um ponto, eventos para o
  agente. *(origem: Fase 13, pendências)*

## Pronto quando

Montar uma cena com 50 objetos alinhados na grade, duplicados em grupo e reorganizados por busca é rápido só
com o editor, e o desfazer cobre tudo, inclusive materiais e prefabs.

## Feito

## Limitações
