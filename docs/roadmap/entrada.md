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
