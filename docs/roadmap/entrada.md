---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](../padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

Triagem de 21/10/2026 (replanejamento): o ImGui caro e a linha de FPS por baixo dos botões de Play foram para a
Fase 27; o jogo de exemplo da Fase 18, para a Fase 33.

## Para triar

- 2026-10-19 — Claude (gravação segura, Fase 16) — a falha intermitente do `Project::Save` não se repetiu em
  8 rodadas para capturar a mensagem; a correção (troca que tenta de novo, cf18356) cobre o caso do arquivo
  preso, que reproduz "Acesso negado". Se `test_export` ou `test_distribuicao` voltarem a falhar em paralelo,
  guardar a mensagem.
- 2026-10-21 — Claude (Fase 23, Tarefa 2) — `Scene::SubmitTo` percorre todas as entidades procurando `MeshRenderer`
  a cada quadro, em vez da lista por tipo `EntitiesWith<MeshRenderer>()` da Fase 25; é parte dos ~10 ms de "montar
  a lista" em `cubos10000`. Área da cena — `engine/src/scene/Scene.cpp` (`SubmitTo`)
- 2026-10-21 — Claude (Fase 23, Tarefa 3) — `test_script_project` falhou uma vez no `ctest -j4` (Release) e passou
  sozinho e na rodada seguinte; a saída da falha não foi guardada. Se repetir, rodar com `--output-on-failure` —
  `tests/test_script_project.cpp`
- 2026-10-21 — Claude (Fase 23) — no `ctest -j4` (Release), testes pesados falham às vezes por disputa de CPU:
  `test_desempenho` (`cena_comparar` +33%, limite 30%) e `test_script_project`; os dois passam sozinhos. Rodar o de
  desempenho só com `-L desempenho`, ou tirá-lo do paralelo (`RUN_SERIAL`) — `tests/CMakeLists.txt`
- 2026-10-21 — Claude (fecho da Fase 23) — causa da falha intermitente do `test_script_project` no `ctest -j4`
  (Debug e Release): a checagem de tempo `vazioSeconds < 0.6f * withoutPch` (compilar com o PCH abaixo de 60% do
  tempo sem ele) falha quando outros testes disputam a CPU; passa sozinho. Tirar do paralelo (`RUN_SERIAL`) ou
  afrouxar a checagem — `tests/test_script_project.cpp:489`
- 2026-10-23 — Claude (estudo da engine) — não há CI: nenhum workflow roda o build e o `ctest` a cada push. O servidor
  git (Gitea/Forgejo) tem Actions; um build Release com `ctest -L` por rótulo já pegaria regressões — raiz do
  repositório (não há `.gitea/` nem `.github/`)
- 2026-10-23 — Claude (estudo da engine) — tirar do paralelo os testes que falham por disputa de CPU no
  `ctest -j4` (`test_desempenho`, `test_script_project`), com `RUN_SERIAL`, como as linhas de 21/10 acima
  sugerem — `tests/CMakeLists.txt`
- 2026-10-23 — Claude (estudo da engine) — sem build com sanitizers (ASan/UBSan) nem `clang-tidy`; ficam mais fáceis
  com o Clang da Fase 31 — `CMakeLists.txt`
- 2026-10-23 — Claude (estudo da engine) — texturas sobem para a GPU descomprimidas (PNG/JPG via stb_image, sem
  BC1/BC5/BC7 nem KTX2). Comprimir na importação deve dar o maior ganho de VRAM e de banda na HD 530, que divide
  a memória com a CPU — `engine/src/renderer/Texture.cpp`, `engine/src/assets/AssetLoaders.cpp`
- 2026-10-23 — Claude (estudo da engine) — o handle do OpenGL vaza na API pública (`Texture::NativeHandle()`,
  `Shader::Id()`, 6 usos, o único fora do renderer no painel Projeto) e o backend OpenGL do ImGui fica no `core/`.
  Fechar isso é o primeiro passo de uma camada de RHI (e de um backend Vulkan ou D3D12 depois) —
  `engine/include/motor/renderer/Texture.h:28`, `engine/include/motor/renderer/Shader.h:53`,
  `engine/src/core/ImGuiLayer.cpp:8`, `editor/src/panels/ProjectPanelDraw.cpp`
- 2026-10-23 — Claude (estudo da engine) — backend Vulkan: o OpenGL já está confinado em `engine/src/renderer/`
  (3,3 mil linhas, 19 shaders), então é viável, mas só compensa depois da Fase 26 (gravar comandos em várias
  threads). Antes: a camada de RHI e os shaders compilados para SPIR-V (glslang/shaderc), com o GLSL como fonte.
  Alternativa a escrever o RHI do zero: SDL3 GPU, NVRHI, Diligent ou bgfx (dependência nova, precisa de decisão)
  — `engine/src/renderer/`
- 2026-10-23 — Claude (estudo da engine) — teto fixo de 32 luzes, com cada pixel percorrendo todas; mundo aberto
  e fogueiras (alpha 4) vão pedir iluminação clustered (forward+) — `engine/assets/shaders/lit.frag:17`
- 2026-10-23 — Claude (estudo da engine) — sem recarga de shader no editor: salvar o `.frag` e ver na hora —
  `engine/src/renderer/Shader.cpp`
- 2026-10-23 — Claude (estudo da engine) — o jogo exportado leva os arquivos de origem (PNG, glTF, JSON); preparar
  os assets na exportação (texturas comprimidas, malhas binárias) encurtaria o carregamento — `editor/src/export/`
- 2026-10-23 — Claude (estudo da engine) — só Windows: o `windows.h` aparece em 11 arquivos fora das camadas de
  plataforma (log, assets, scripts, MCP, diálogos, Player). A GLFW e o GCC já são multiplataforma; isolar essas
  chamadas abriria Linux e Steam Deck — `engine/src/core/Log.cpp`, `engine/src/core/EngineAssets.cpp`,
  `editor/src/scripts/`, `editor/src/mcp/McpServer.cpp`, `editor/src/FileDialog.cpp`, `player/src/main.cpp`
- 2026-10-23 — Claude (estudo da engine) — sem navegação (navmesh e pathfinding, ex.: Recast/Detour); inimigos
  que perseguem pedem isso. O roteiro só a cita como "depois da alpha 4" — sem arquivo
- 2026-10-23 — Claude (estudo da engine) — o Animator só toca e mistura clipes (Play, CrossFade): faltam máquina de
  estados, blend trees (andar e correr pela velocidade), IK de pés e eventos de animação (som do passo) —
  `engine/src/scene/Animator.cpp`
- 2026-10-23 — Claude (estudo da engine) — áudio sem efeitos (reverb, filtros) nem abafamento atrás de paredes;
  a miniaudio já tem nós de efeito — `engine/src/audio/`
- 2026-10-23 — Claude (estudo da engine) — sem localização: os textos do HUD não têm tabela por idioma —
  `engine/src/ui/`
- 2026-10-23 — Claude (estudo da engine) — editor: miniaturas e prévia de materiais e modelos no painel Projeto,
  um gráfico dos contadores por quadro dentro do editor (hoje só no Tracy, à parte) e um console de variáveis
  (ex.: `r.shadows 0`) mudáveis em tempo real, também pelo MCP — `editor/src/panels/`, `editor/src/viewport/`
- 2026-10-26 — Claude (Hub residente) — Hub: um editor aberto direto pelo `MotorEditor.exe` não é visto pelo Hub, e
  uma ferramenta do `/mcpengine/<Projeto>` abre outro editor no mesmo projeto; o editor poderia avisar o Hub ao abrir
  sozinho (ou o Hub achar o editor pela pasta do projeto) — `hub/src/HubEngineProxy.cpp`, `mcp/include/motor/mcp/HubLink.h`
- 2026-10-27 — Claude (Fase 28, otimização da GPU) — a DLL de scripts não é recompilada quando um header da engine
  muda (ex.: `kScriptApiVersion` 26 → 27): os headers entram pelo PCH (`-include MotorScriptPch.h`), que o ninja não
  lista nas dependências dos `.obj`, então "Recompilar" só relinka e o editor e o Player seguem recusando a DLL
  ("API 26, esperado 27"). Contorno: tocar os `.cpp` de `Scripts/`. Correção: pôr o `.gch` (ou a versão da API)
  como dependência dos objetos, ou recompilar tudo quando o SDK mudar — `editor/src/scripts/`, `cmake/MotorPackage.cmake`
- 2026-10-27 — Claude (Fase 28, otimização da GPU) — o Player com `--perf` que recusa a DLL de scripts ("A DLL de
  scripts não é desta versão do Motor") escreve "Encerrando." e o erro no log mas não sai: o processo fica aberto
  até ser encerrado pelo PID — `player/src/`
- 2026-10-28 — Claude (Interface em documentos) — a exportação do jogo não segue as referências de dentro dos
  `.mui` e `.mstyle` (folhas, fontes e imagens citadas pelo documento não vão para a pasta exportada) —
  `editor/src/export/GameExport.cpp` (`WalkRefs`)
- 2026-10-28 — Claude (Interface em documentos) — a DLL de scripts não recompila o `MotorScriptEntry.cpp` quando o
  `kScriptApiVersion` sobe (os headers da engine entram com `-isystem` e não contam como dependência), então a
  DLL segue dizendo a versão antiga até o arquivo ser tocado — `editor/src/scripts/ScriptBuilder`
