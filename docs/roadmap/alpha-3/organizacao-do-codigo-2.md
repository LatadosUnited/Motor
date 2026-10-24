---
tipo: fase
alpha: 3
fase: 
titulo: Organização do código 2
status: planejada
prioridade: 
areas: [common, core, assets, project, scene, renderer, ai, editor, hub, distrib, docs]
branch: refactor/organizacao-2
inicio: 
fim: 
---

# Organização do código 2

## Objetivo

Limpar o que se acumulou desde a primeira Organização do código (alpha 2, nota no histórico do git, commit
`3661df6`) até a Fase 34. Na análise de 24/10/2026 a estrutura estava saudável: as camadas são respeitadas (o `test_layers`
confere, o grafo não mostra ciclo de includes) e nenhum arquivo de código passa de ~780 linhas. O que sobra são
três coisas. A primeira são **funções auxiliares copiadas**: o mesmo `SetError` em 14 arquivos e a leitura de
arquivo em 8. Depois, **uma função grande** no renderer e quatro médias. Por fim, **um arquivo fora do lugar**
no editor. Tudo **sem mudar comportamento**: um commit por tarefa, build sem avisos e `ctest` inteiro a cada uma.

As auxiliares vão para o `common/` (`motor_common`, que a engine, o hub e a distribuição já usam), em headers só
com funções `inline`, ao lado do `FileWrite.h`. Assim valem também para o hub e a distribuição, que não podem
incluir a engine. Elas também não entram na API dos scripts, então o `kScriptApiVersion` não muda.

### Onde está cada repetição (24/10/2026)

| Auxiliar | Cópias | Arquivos |
|---|---|---|
| `SetError(std::string*, std::string)` (`if (error) *error = ...`) | 14 iguais | `assets/AssetLoaders.cpp:43`, `FbxLoader.cpp:30`, `GltfLoader.cpp:43`; `project/GameOptions.cpp:24`, `Project.cpp:26`, `SaveData.cpp:25`; `scene/ModelImport.cpp:22`, `Prefab.cpp:20`, `SceneSerializerFields.cpp:247`; `editor/src/scripts/ScriptProject.cpp:27`; `hub/src/HubCore.cpp:20`; `distrib/src/Download.cpp:20`, `InstallPlan.cpp:19`, `Manifest.cpp:21` |
| Ler o arquivo inteiro (texto) | 5 | `editor/src/export/GameExport.cpp:41`, `scripts/ScriptIde.cpp:26`, `scripts/ScriptProject.cpp:31`; `hub/src/HubCore.cpp:24` e `distrib/src/Manifest.cpp:25` (com `std::optional`) |
| Ler o arquivo inteiro (bytes) | 3 | `assets/FbxLoader.cpp:34` e `GltfLoader.cpp:63` (iguais), `assets/AssetLoaders.cpp:53` (com mensagem de erro) |
| `Lower` (minúsculas ASCII) | 4 | `core/Input.cpp:50`, `editor/src/mcp/McpSceneTools.cpp:54`, `panels/ProjectPanelUtil.cpp:35`, `scripts/ScriptProject.cpp:77` |
| `LowerExtension` | 2 | `assets/AssetLoaders.cpp:47`, `editor/src/export/GameExport.cpp:35` |
| `ExecutableDir` | 2 | já é pública em `motor/core/EngineAssets.h:22`; `editor/src/scripts/ScriptProject.cpp:136` tem uma cópia |

O `SetError(Result&, ...)` do `GameExport.cpp:181` é outro (grava no resultado da exportação) e fica. Os dois
`CopyTree` (`Prefab.cpp`, `GameExport.cpp`) só têm o nome em comum.

### Funções longas

| Função | Linhas | Divisão |
|---|---|---|
| `Renderer::DrawScene` (`engine/src/renderer/Renderer.cpp:300`) | 186 | uma função por etapa que ela já numera: ordem, sombras, blocos do quadro, cena HDR em lotes, céu, transparentes |
| `NavMesh::Build` (`engine/src/ai/NavMesh.cpp:55`) | 143 | as etapas da Recast (voxels, regiões, contornos, polígonos) e a montagem do Detour |
| `SceneDocument::DrawControlsWindow` (`editor/src/document/SceneDocumentControls.cpp:89`) | 130 | uma função por grupo da janela |
| `GameExport::Export` (`editor/src/export/GameExport.cpp:291`) | 123 | coletar, copiar, gravar o manifesto |
| `SceneViewport::Draw` (`editor/src/viewport/SceneViewport.cpp:140`) | 116 | entrada, desenho, sobreposições |

As `Add*Tools` do MCP (até 223 linhas) ficam como estão: são listas de ferramentas registradas uma a uma, e
dividi-las não deixa nada mais fácil de ler. A diferença de estilo entre elas é de propósito. As de cena, testáveis
sem janela, são funções soltas (`McpSceneTools`, `AddNavTools`), e as que precisam do editor são métodos do
`McpController`.

### Ordem e conflitos

- As tarefas 1 a 4 mexem em arquivos de assets, projeto, cena, editor, hub e distribuição. Nenhum branch aberto em
  24/10 toca neles: o `fase-25-fisica-passos` mexe em `Scene.h` e `physics/`, o `fase-26-threads` em `Scene.cpp`,
  `Animator.cpp`, `physics/` e no renderer. Podem ir já, num branch só.
- A tarefa 5 (`DrawScene`) **espera o merge do `fase-26-threads`**, que reescreve boa parte do `Renderer.cpp`
  (125 linhas mudadas). Também não deve rodar junto com a Fase 24, que mexe no renderer.
- A tarefa 7 é do editor. O `NavMeshBake` serve ao drawer da superfície e ao MCP, e a raiz de `editor/src` é só
  para as peças comuns.

## Tarefas

- [ ] Tarefa 1: Criar `motor/common/Text.h` com o `SetError` e trocar as 14 cópias
- [ ] Tarefa 2: Criar `motor/common/FileRead.h` (texto e bytes) e trocar as 8 leituras
- [ ] Tarefa 3: Pôr `Lower` e `LowerExtension` no `Text.h` e trocar as 6 cópias
- [ ] Tarefa 4: Trocar a cópia do `ExecutableDir` no `ScriptProject` pela do `EngineAssets.h`
- [ ] Tarefa 5: Dividir o `Renderer::DrawScene` nas etapas que ele já numera
- [ ] Tarefa 6: Dividir `NavMesh::Build`, `DrawControlsWindow`, `GameExport::Export` e `SceneViewport::Draw`
- [ ] Tarefa 7: Mover o `NavMeshBake` da raiz de `editor/src` para uma subpasta
- [ ] Tarefa 8: Tirar do `CLAUDE.md` as tabelas de fases das alphas 1 e 2
- [ ] Tarefa 9: Atualizar o `CLAUDE.md` (auxiliares do `common/`, pasta nova) e o grafo

## Pronto quando

- `grep` não acha mais `void SetError(std::string* error` fora do `common/`, nem as leituras de arquivo da tabela.
- Nenhuma função de `engine/src` ou `editor/src` passa de 120 linhas, fora as `Add*Tools` do MCP.
- Build sem avisos, `ctest` inteiro passando (com `test_layers`) e o `test_desempenho` em Release igual à base, sem
  regravar.

## Feito

(Preencher ao terminar.)

## Limitações

(Preencher ao terminar.)
