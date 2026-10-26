---
tipo: fase
alpha: 3
fase: 
titulo: Organização do código 2
status: em andamento
prioridade: 
areas: [common, core, assets, project, scene, renderer, ai, editor, hub, distrib, docs]
branch: refactor/organizacao-2
inicio: 2026-10-25
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
| `ToUtf8` e `PathFromUtf8` | 2 | `motor::` em `engine/include/motor/core/Paths.h:11` e `:21` (engine, editor, Player); `hub::` em `hub/src/HubCore.cpp:84` (Hub, distribuição, instalador) |

O par de UTF-8 também confunde o grafo do graphify, que liga as chamadas pelo nome. Na conferência de 24/10, das 97
chamadas inferidas que ele pôs no `ToUtf8()` do Hub, só 20 eram do Hub de verdade: 62 vinham da engine e do editor,
que chamam o `motor::ToUtf8`, e 15 de headers que nem o chamam. Com uma definição só, as pontes falsas entre o Hub e
o editor somem do grafo no próximo `--update`. O `Paths.h` continua existindo para a engine e os scripts (incluindo o
header do `common/`), e o `HubCore.h` passa a usar o mesmo; o SDK dos scripts (`motor_package`) precisa levar o header
novo do `common/`.

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
  `Animator.cpp`, `physics/` e no renderer. Podem ir já, num branch só. A tarefa 10 vai junto: mexe no `Paths.h`
  (header público da área Núcleo, combinar antes) e no `HubCore` (área Hub).
- A tarefa 5 (`DrawScene`) **espera o merge do `fase-26-threads`**, que reescreve boa parte do `Renderer.cpp`
  (125 linhas mudadas). Também não deve rodar junto com a Fase 24, que mexe no renderer.
- A tarefa 7 é do editor. O `NavMeshBake` serve ao drawer da superfície e ao MCP, e a raiz de `editor/src` é só
  para as peças comuns.

## Tarefas

- [x] Tarefa 1: Criar `motor/common/Text.h` com o `SetError` e trocar as 14 cópias — 4653ffb
- [x] Tarefa 2: Criar `motor/common/FileRead.h` (texto e bytes) e trocar as 8 leituras — 4653ffb
- [ ] Tarefa 3: Pôr `Lower` e `LowerExtension` no `Text.h` e trocar as 6 cópias — 4653ffb (falta o `Input.cpp`)
- [x] Tarefa 4: Trocar a cópia do `ExecutableDir` no `ScriptProject` pela do `EngineAssets.h` — 4653ffb
- [ ] Tarefa 5: Dividir o `Renderer::DrawScene` nas etapas que ele já numera
- [ ] Tarefa 6: Dividir `NavMesh::Build`, `DrawControlsWindow`, `GameExport::Export` e `SceneViewport::Draw`
- [ ] Tarefa 7: Mover o `NavMeshBake` da raiz de `editor/src` para uma subpasta
- [ ] Tarefa 8: Tirar do `CLAUDE.md` as tabelas de fases das alphas 1 e 2
- [ ] Tarefa 9: Atualizar o `CLAUDE.md` (auxiliares do `common/`, pasta nova) e o grafo
- [x] Tarefa 10: Juntar os dois `ToUtf8` e `PathFromUtf8` (engine e Hub) num `motor/common/Utf8.h` — 4653ffb

## Pronto quando

- `grep` não acha mais `void SetError(std::string* error` fora do `common/`, nem as leituras de arquivo da tabela.
- Só há uma definição de `ToUtf8` e uma de `PathFromUtf8`, no `common/`, e no grafo as chamadas da engine e do
  editor caem no `ToUtf8` dela, não num do Hub.
- Nenhuma função de `engine/src` ou `editor/src` passa de 120 linhas, fora as `Add*Tools` do MCP.
- Build sem avisos, `ctest` inteiro passando (com `test_layers`) e o `test_desempenho` em Release igual à base, sem
  regravar.

## Feito

### Auxiliares no `common/` (tarefas 1 a 4 e 10, 25/10/2026)

Três headers novos em `common/include/motor/common/`, só com funções `inline`, no namespace `motor`:

- **`Utf8.h`:** `ToUtf8`, `ToGenericUtf8` e `PathFromUtf8`, que saíram do `motor/core/Paths.h` (ele inclui o novo,
  então nada muda para quem já o usava). O `HubCore.h` troca a cópia do hub por `using motor::ToUtf8` e
  `using motor::PathFromUtf8`: o `hub::ToUtf8` continua valendo para o hub e a distribuição. Como o `Paths.h` é
  público, o `common/include` entrou nos includes públicos da `libMotor.dll`, no `MOTOR_SDK_INCLUDE_DIRS` (scripts e
  o `.gch` deles, que agora também depende do `Utf8.h`) e na cópia do SDK do `motor_package` (só o `Utf8.h`).
- **`Text.h`:** `SetError`, `Lower` (minúsculas ASCII) e `LowerExtension`. As 15 cópias do `SetError` sumiram (as
  14 da tabela e uma que apareceu no `AssetLibraryUtil.h`), junto com a declaração do `SceneJson.h`. Fora do
  namespace `motor` (editor, hub, distribuição) fica um `using motor::SetError` no lugar da cópia. O
  `project_panel::Lower` virou `using motor::Lower` no `ProjectPanelUtil.h`.
- **`FileRead.h`:** `ReadTextFile` (o arquivo inteiro como texto, `std::optional`: `.value_or("")` onde arquivo
  ausente valia como vazio) e `ReadBinaryFile` (bytes, com a mensagem de erro do `AssetLoaders.cpp`). Além das 8 da
  tabela, saíram mais duas cópias: o `ReadText` dos materiais (`AssetLibraryMaterials.cpp`) e o do `Shader.cpp`,
  que agora só lança a exceção em cima do `ReadTextFile`.

O `ScriptProject::ExecutableDir` saiu; o `ScriptProject`, o `ReleaseRuntime` e o `ScriptController` usam o
`motor::ExecutableDir` (`motor/core/EngineAssets.h`), igual no Windows. O `kScriptApiVersion` não mudou: as funções
são `inline` e o SDK só ganhou um header.

As cinco tarefas foram num commit só: mexem nos mesmos arquivos (o `ScriptProject.cpp` tinha cópia de quatro
delas) e uma cópia velha visível junto da nova deixa a chamada ambígua, então não dá para trocar uma de cada vez
sem um estado intermediário que não compila.

## Limitações

- **`Lower` do `Input.cpp`** (recebe `std::string_view`) e **`DrawControlsWindow`** ficam para depois do branch
  `entrada-base`, que mexe nos dois arquivos.
- **Leituras no meio de outras funções:** ~14 lugares ainda abrem o arquivo e leem com `rdbuf()` direto (projeto,
  opções, save, prefab, cena, recuperação, `.meta`, configurações do editor, plano de instalação). Cada um tem a sua
  mensagem de erro e não era uma função copiada; dá para trocar pelo `ReadTextFile` aos poucos, quando mexer neles.
