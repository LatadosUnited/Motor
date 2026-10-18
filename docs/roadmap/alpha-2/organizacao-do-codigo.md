---
tipo: fase
alpha: 2
fase: 17.5
titulo: Organização do código
status: pronta
prioridade: 3
areas: [editor, cena, física, assets, hub, docs]
branch: refactor/organizacao
inicio: 2026-10-18
fim: 2026-10-18
---

# Organização do código (entre as Fases 17 e 18)

## Objetivo

Deixar o código mais fácil de continuar antes das fases que mexem em muitas partes (18 e 20). O código está
saudável (cerca de 38 mil linhas, sem `TODO`), mas o peso se concentra em poucos lugares: 7 arquivos com 600 a
1200 linhas, umas 6 funções com mais de 120 linhas e os 53 arquivos do editor numa pasta só. Tudo aqui é
**sem mudar comportamento**: um commit por passo, build sem avisos e `ctest` inteiro a cada um.

## Tarefas

### 1. Pastas do editor

- [x] **`editor/src/` em subpastas por área**, com os includes a partir de `editor/src`: `document/`
  (`SceneDocument`), `viewport/` (`SceneViewport`, `UiEditor`), `play/` (`PlayController`, `PlayMode`),
  `panels/` (Hierarquia, Inspector, Cena, Projeto), `drawers/` (`FieldDrawers` e os `*Drawer.cpp`),
  `scripts/` (`Script*`), `export/` (`GameExport`, `ExportController`); `mcp/` já existe. Na raiz ficam as
  peças comuns (`EditorApp`, `EditorContext`, `EditorSettings`, `UndoHistory`, `FileDialog`, `main.cpp`).
  *(origem: análise do código de 18/10/2026)* (d378c4a)

### 2. Arquivos grandes

- [x] `ProjectPanel.cpp` (~1220 linhas): estado e varredura / desenho da lista e da grade / operações de arquivo
  (renomear, criar, copiar, mover) / ícones e miniaturas. *(origem: Fase 20, "antes de tudo")* (3612322)
- [x] `SceneDocument.cpp` (~780): cena (abrir, salvar, recuperação) / projeto (abrir, criar, mover) / menus e
  popups. *(origem: Fase 20, "antes de tudo")* (413d2ef)
- [x] `SceneViewport.cpp` (~580): câmera, seleção e arquivos soltos / desenho por cima (ícones, colliders,
  gizmo, barra). *(origem: Fase 20, "antes de tudo")* (413d2ef)
- [x] `McpController.cpp` (~710): um arquivo por grupo de ferramentas (editor, arquivos, Play, vista).
  *(origem: alpha 3, item 5)* (c5611ce)
- [x] `HubApp.cpp` (~1040): lógica / aba Projetos / aba Versões. *(origem: análise do código de 18/10/2026)* (e106302)
- [x] `PhysicsSystem.cpp` (~950): o `World` (560 linhas dentro de uma struct) em arquivo próprio.
  *(origem: alpha 3, item 5)* (2bb8b83)
- [x] `SceneSerializer.cpp` (~850): campos e componentes / cena / recuperação. *(origem: alpha 3, item 5)* (93fc025)
- [x] `AssetLibrary.cpp` (~600): materiais em arquivo próprio. *(origem: alpha 3, item 5)* (edcbf9a)

### 3. Funções longas

- [x] Quebrar em partes com nome as funções com mais de ~120 linhas: `AddSceneTools` (282, `McpSceneTools`),
  `AddPlayTools` (219) e `AddFileTools` (162, `McpController`), `ScriptBuilder::Start` (214), `ParseGltf` (162),
  `ImportModel` (126), `SceneDocument::DrawPopups` (125), `FieldValueFromJson` (111).
  *(origem: análise do código de 18/10/2026)* (c5611ce, a86a547)

### 4. Documentação

- [x] `CLAUDE.md` (estrutura e áreas com os caminhos novos), a Fase 20 e a alpha 3 sem as divisões feitas aqui.
  *(origem: análise do código de 18/10/2026)* (89205b3)
- [ ] Grafo atualizado (`/graphify . --update`) depois do merge no `main` (`CLAUDE.md`, "Regras de cada
  tarefa", 5). *(origem: análise do código de 18/10/2026)*

## Pronto quando

Nenhum `.cpp` de `engine/src`, `editor/src` ou `hub/src` passa de ~600 linhas e nenhuma função passa de ~120;
build sem avisos e `ctest` inteiro passando; o editor abre um projeto e entra no Play pelo MCP como antes.

## Feito

Sem mudança de comportamento: os testes e o editor (pelo MCP) fazem o mesmo de antes; o que mudou é onde está o
código e o tamanho das partes.

- **Editor em subpastas** (`document/`, `viewport/`, `play/`, `panels/`, `drawers/`, `scripts/`, `export/`, `mcp/`;
  peças comuns na raiz). Includes a partir de `editor/src` (`target_include_directories` no `MotorEditor`; os
  testes já tinham).
- **Arquivos divididos**, com o que as partes dividem num header interno:
  - `ProjectPanel` → `ProjectPanel.cpp` (estado, varredura, seleção), `ProjectPanelDraw.cpp`,
    `ProjectPanelFiles.cpp`, `ProjectPanelUtil.h/.cpp` (ícones, caminhos, miniaturas).
  - `SceneDocument` → `SceneDocument.cpp` (ações e cenas), `SceneDocumentProject.cpp`, `SceneDocumentUi.cpp`.
  - `SceneViewport` → `SceneViewport.cpp` e `SceneViewportOverlay.cpp` (ícones, colliders, gizmo, barra).
  - `McpController` → mais `McpFileTools.cpp` (cenas/projeto, assets) e `McpPlayTools.cpp` (jogo, scripts).
  - `HubApp` → `HubApp.cpp`, `HubProjectsTab.cpp`, `HubVersionsTab.cpp`, `HubWidgets.h/.cpp`.
  - `PhysicsSystem` → `PhysicsJolt.h` (camadas, conversões, contatos), `PhysicsWorld.h` (o `World`, com os
    métodos fora da struct), `PhysicsWorld.cpp` (formas e corpos), `PhysicsWorldEvents.cpp` (personagens,
    Transforms, eventos).
  - `SceneSerializer` → `SceneSerializer.cpp` (cena), `SceneSerializerFields.cpp`, `SceneRecovery.cpp`,
    `SceneJson.h` (detalhes comuns).
  - `AssetLibrary` → mais `AssetLibraryMaterials.cpp` e `AssetLibraryUtil.h`.
- **Funções longas em partes com nome:** `ScriptBuilder::Start` (os tempos da compilação viraram a classe
  `BuildTimer`), `ParseGltf` (materiais e imagens, malhas, nós, peles, clipes), `ImportModel` (materiais,
  entidades do prefab), `AddSceneTools` (entidades, edição, componentes, ambiente), os grupos do MCP,
  `SceneDocument::DrawPopups` (um método por popup), `FieldValueFromJson` (malha e textura num só modelo).
- **Números:** o maior `.cpp` caiu de 1222 para 631 linhas (`ScriptProject.cpp`, não mexido); a maior função, de
  282 para 110 linhas. Build sem avisos e `ctest` 28/28 a cada commit.
- **Verificado pelo MCP** (porta 47801, `--no-settings`): projeto aberto, Play e Parar; projeto novo criado pelo
  `project_create`, scripts compilados com tempos por arquivo (configurar 2,00 s, compilar 0,59 s, ligar 0,26 s)
  e Play com o script carregado.

## Limitações

- `ScriptProject.cpp` (631), `SceneRuntime.cpp` (612) e `Prefab.cpp` (606) ficaram perto do limite de ~600; não
  foram divididos agora.
- Os headers internos novos (`*Util.h`, `PhysicsJolt.h`, `SceneJson.h`) repetem um pouco do que existia (ex.:
  `SetError` em mais de uma camada); juntar isso num lugar só fica para quando incomodar.
- Dois defeitos achados na verificação, sem relação com a divisão, foram para a
  [caixa de entrada](../entrada.md): projeto compilado antes da Fase 17 não compila mais os scripts, e ferramenta
  MCP sem argumento obrigatório derruba o editor em Debug.
