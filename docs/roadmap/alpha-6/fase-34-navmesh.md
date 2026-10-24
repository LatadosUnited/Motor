---
tipo: fase
alpha: 6
fase: 34
titulo: Navmesh e agentes
status: em andamento
prioridade: 1
areas: [ai, scene]
branch: fase-34-navmesh
inicio: 2026-10-24
---

# Fase 34 — Navmesh e agentes

## Objetivo

Dar ao Motor o equivalente ao NavMesh da Unity: um navmesh gerado a partir do cenário, busca de caminho e um
agente que anda até um destino contornando paredes. Base de todas as outras fases da [alpha 6](../alpha-6.md). Usa a
Recast/Detour ([decisão](../../decisoes/2026-10-23-recast-detour.md)) numa camada nova, `ai/`, acima de `scene`. O
navmesh é gerado no editor e salvo num `.mnav` do projeto; sem arquivo, é gerado no começo do Play.

## Tarefas

- [x] Tarefa 1: Recast/Detour no CMake (estática, fora dos símbolos exportados) — cc6b5ef
- [x] Tarefa 2: Camada `ai/` com `NavMesh` (gerar, caminho, ponto mais perto, raio) — 7370316
- [x] Tarefa 3: Triângulos dos Colliders estáticos da cena (`CollectNavGeometry`) — 7370316
- [x] Tarefa 4: Componentes `NavMeshSurface` e `NavMeshAgent` — 7370316
- [x] Tarefa 5: `NavSystem` no `SceneRuntime`: gera no Play, move os agentes, segue um alvo — 7370316
- [x] Tarefa 6: `test_navigation` — 7370316
- [x] Tarefa 7: Salvar o navmesh como asset `.mnav` (com GUID) e gerar no editor — 29ddb1e
- [x] Tarefa 8: Desenhar o navmesh e os caminhos no viewport — 18ed748
- [x] Tarefa 9: Ferramentas MCP `nav_bake`, `nav_path` e `nav_agents` — 865df81
- [ ] Tarefa 10: Seção de navegação no guia de scripts, com exemplo compilado

## Pronto quando

O `test_navigation` passa (navmesh de chão com parede, rampa suave e íngreme, ilhas separadas, agente que contorna
a parede e chega, agente que segue um alvo), uma cena de teste aberta no editor mostra o navmesh no viewport, e um
agente posto nela anda até o destino no Play, conferido pelo MCP.

## Feito

- Tarefa 7: no Play, a cena de teste do editor (chão de 40 m, parede de 12 m, agente seguindo uma esfera) gera o
  navmesh em ~20 ms (14 polígonos). Ao conferir no editor, o agente passava do canto da parede embalado e voltava
  ~70 cm até ele (chegava em 6,8 s); com a direção seguindo o caminho na hora, chega em 5,1 s sem recuar
  (`test_navigation`, seção "Cantos").
- Tarefa 8: conferido no editor de teste pelo MCP: fora do Play, o `.mnav` ligado à Superfície aparece em azul com
  as arestas, escondido atrás da parede (`viewport_capture`); no Play, com Cena e Jogo lado a lado, a linha amarela
  sai do agente, contorna a ponta da parede e termina no alvo (captura da janela).
- Tarefa 9: no editor de teste, `nav_bake` gerou o navmesh do chão de 40 m em 51 ms (14 polígonos), gravou
  `Assets/Navegacao/Navmesh.mnav` e o ligou à Superfície; o viewport passou a mostrá-lo; `nav_path` deu o mesmo
  caminho (18,2 m, cinco cantos) gerado na hora e lido do arquivo; no Play, `nav_agents` mostrou o agente com os
  cantos que faltavam e o navmesh vindo do arquivo, não desatualizado. É a mesma função do botão Gerar.

## Limitações

- Gerar roda na thread do editor (trava numa cena grande; segundo
  plano é da Fase 36). O botão Gerar não foi clicado no teste do editor; a função que ele chama foi usada pelo
  `nav_bake`.
- Navmesh salvo desatualizado (cenário ou Superfície mudaram depois de gerar) continua sendo usado, com aviso no log.
- Fora do Play, sem `.mnav` (navmesh gerado só no Play), o viewport não mostra nada: é preciso clicar em Gerar.
- Os caminhos só aparecem no Viewport com a câmera do editor (F8 ou Cena e Jogo lado a lado), como os colliders;
  a captura do MCP (`viewport_capture`) mostra o navmesh, mas não as linhas, que são desenhadas por cima no ImGui.
- Um bloco só: navmesh com mais de 65535 vértices falha (blocos vêm com o TileCache, Fase 36).
- O agente mexe no Transform direto: não usa a física nem desvia de outros agentes (Fase 35).
- A cápsula entra no navmesh como o cilindro que a contém.
