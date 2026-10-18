---
tipo: fase
alpha: 2
fase: 17.5
titulo: Organização do código
status: em andamento
prioridade: 3
areas: [editor, cena, física, assets, hub, docs]
branch: refactor/organizacao
inicio: 2026-10-18
fim: 
---

# Organização do código (entre as Fases 17 e 18)

## Objetivo

Deixar o código mais fácil de continuar antes das fases que mexem em muitas partes (18 e 20). O código está
saudável (cerca de 38 mil linhas, sem `TODO`), mas o peso se concentra em poucos lugares: 7 arquivos com 600 a
1200 linhas, umas 6 funções com mais de 120 linhas e os 53 arquivos do editor numa pasta só. Tudo aqui é
**sem mudar comportamento**: um commit por passo, build sem avisos e `ctest` inteiro a cada um.

## Tarefas

### 1. Pastas do editor

- [ ] **`editor/src/` em subpastas por área**, com os includes a partir de `editor/src`: `document/`
  (`SceneDocument`), `viewport/` (`SceneViewport`, `UiEditor`), `play/` (`PlayController`, `PlayMode`),
  `panels/` (Hierarquia, Inspector, Cena, Projeto), `drawers/` (`FieldDrawers` e os `*Drawer.cpp`),
  `scripts/` (`Script*`), `export/` (`GameExport`, `ExportController`); `mcp/` já existe. Na raiz ficam as
  peças comuns (`EditorApp`, `EditorContext`, `EditorSettings`, `UndoHistory`, `FileDialog`, `main.cpp`).
  *(origem: análise do código de 18/10/2026)*

### 2. Arquivos grandes

- [ ] `ProjectPanel.cpp` (~1220 linhas): estado e varredura / desenho da lista e da grade / operações de arquivo
  (renomear, criar, copiar, mover) / ícones e miniaturas. *(origem: Fase 20, "antes de tudo")*
- [ ] `SceneDocument.cpp` (~780): cena (abrir, salvar, recuperação) / projeto (abrir, criar, mover) / menus e
  popups. *(origem: Fase 20, "antes de tudo")*
- [ ] `SceneViewport.cpp` (~580): câmera, seleção e arquivos soltos / desenho por cima (ícones, colliders,
  gizmo, barra). *(origem: Fase 20, "antes de tudo")*
- [ ] `McpController.cpp` (~710): um arquivo por grupo de ferramentas (editor, arquivos, Play, vista).
  *(origem: alpha 3, item 5)*
- [ ] `HubApp.cpp` (~1040): lógica / aba Projetos / aba Versões. *(origem: análise do código de 18/10/2026)*
- [ ] `PhysicsSystem.cpp` (~950): o `World` (560 linhas dentro de uma struct) em arquivo próprio.
  *(origem: alpha 3, item 5)*
- [ ] `SceneSerializer.cpp` (~850): campos e componentes / cena / recuperação. *(origem: alpha 3, item 5)*
- [ ] `AssetLibrary.cpp` (~600): materiais em arquivo próprio. *(origem: alpha 3, item 5)*

### 3. Funções longas

- [ ] Quebrar em partes com nome as funções com mais de ~120 linhas: `AddSceneTools` (282, `McpSceneTools`),
  `AddPlayTools` (219) e `AddFileTools` (162, `McpController`), `ScriptBuilder::Start` (214), `ParseGltf` (162),
  `ImportModel` (126), `SceneDocument::DrawPopups` (125), `FieldValueFromJson` (111).
  *(origem: análise do código de 18/10/2026)*

### 4. Documentação

- [ ] `CLAUDE.md` (estrutura e áreas com os caminhos novos), a Fase 20 e a alpha 3 sem as divisões feitas aqui,
  grafo atualizado. *(origem: análise do código de 18/10/2026)*

## Pronto quando

Nenhum `.cpp` de `engine/src`, `editor/src` ou `hub/src` passa de ~600 linhas e nenhuma função passa de ~120;
build sem avisos e `ctest` inteiro passando; o editor abre um projeto e entra no Play pelo MCP como antes.

## Feito

(Preencher ao terminar.)

## Limitações

(Preencher ao terminar.)
