---
tipo: roteiro
modulo: engine
atualizado: 2026-10-21
---

# Engine — roteiro

[Estado](estado.md). Uma linha por tarefa; a origem entre parênteses. Os detalhes de cada item antigo estão na
nota indicada do [arquivo](../arquivo/leiame.md). Feito sai daqui e vai para o `CHANGELOG`.

## Agora

- [ ] Índice de nomes para `FindEntity`/`FindByName`. *(Fase 25)*
- [ ] `Scene::Children` sem percorrer a cena (duplicar e copiar sub-árvores grandes ainda é quadrático). *(Fase 25)*
- [ ] Física: sincronização cena–física a cada passo; alvo `fisica1000` acima de 85% da Unity. *(Fase 25)*
- [ ] Física: os picos isolados de até ~185 ms no `test_physics_bench`. *(Fase 5)*

## Próximo

- [ ] Medir numa máquina com GPU dedicada (`bench/medir.ps1`, `test_desempenho`, `--perf`) e repetir o A/B das
  consultas de GPU (`MOTOR_GPU_TIMERS=0`) com a máquina calma. *(Fase 22)*
- [ ] Refazer a comparação com a Unity com as mudanças de 21/10, e também com IL2CPP e o GPU Resident Drawer
  ligados do lado da Unity. *(conversa de 21/10/2026)*
- [ ] **Sombras** ([detalhes](../arquivo/roadmap/alpha-3.md)): profundidade em 16/24 bits e tamanhos menores;
  número de cascatas ajustável; cache das cascatas distantes; "projeta/recebe sombra" por objeto; spot com uma
  face só. *(Fase 23)*
- [ ] **Desenho em lote:** ordenar por shader/material/malha e instancing; uniforms sem busca por texto;
  `Material` sem cópia por item; LOD; recorte com BVH ou grade; depois, desenho guiado pela GPU (multi-draw
  indirect). Alvo: 10 000 objetos com pouca CPU. *(Fase 24)*
- [ ] Opções de qualidade para o custo fixo da GPU: bloom em resolução menor, MSAA desligado ou 2x com FXAA.
  *(linha de base da Fase 22)*

## Depois

- [ ] Dados por tipo em blocos contínuos (estilo ECS), mantendo a API dos scripts e o registro de componentes.
  *(Fase 25)*
- [ ] Sistema de tarefas (várias threads) para matrizes de mundo, recorte, lotes, animação e física; scripts
  numa thread. *(Fase 25)*
- [ ] Profiler nas threads (nomes no Tracy) e dentro dos scripts do jogo (passar `MOTOR_TRACY` pelo SDK).
  *(Fase 25)*
- [ ] **Ambiente:** terreno editável, água, vegetação com instancing, partículas. Pronto quando uma ilha com
  fogueira roda a 60 FPS no Player na máquina de referência. *(Fase 27)*
- [ ] **Renderização 2:** SSAO, probes de reflexo (depois SSR), exposição automática. *(Fase 28)*
- [ ] Jogo de exemplo do guia que junta câmera orbital, coleta por área, passos, música, cursor preso, vidro e
  cerca recortada, rodado pelo `test_script_docs`. *(Fase 18)*
- [ ] Versão própria da engine, separada da do editor (hoje as duas vêm do `project()` do CMake). Decidir junto
  com o [Editor](../editor/roteiro.md) e o formato do `motor-version.json` ([Instalador](../instalador/roteiro.md)).
  *(decisão de 21/10/2026)*

## Ideias (sem data)

Rede (multiplayer), navegação (pathfinding), máquina de estados de animação, glTF com morph targets e câmeras,
Lua, outras plataformas e APIs gráficas.
