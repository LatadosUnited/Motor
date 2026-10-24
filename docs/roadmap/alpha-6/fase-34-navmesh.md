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
Recast/Detour ([decisão](../../decisoes/2026-10-23-recast-detour.md)) numa camada nova, `ai/`, acima de `scene`;
nesta fase o navmesh é gerado no começo do Play, sem asset salvo, para ter algo andando antes do editor.

## Tarefas

- [x] Tarefa 1: Recast/Detour no CMake (estática, fora dos símbolos exportados) — cc6b5ef
- [x] Tarefa 2: Camada `ai/` com `NavMesh` (gerar, caminho, ponto mais perto, raio) — 7370316
- [x] Tarefa 3: Triângulos dos Colliders estáticos da cena (`CollectNavGeometry`) — 7370316
- [x] Tarefa 4: Componentes `NavMeshSurface` e `NavMeshAgent` — 7370316
- [x] Tarefa 5: `NavSystem` no `SceneRuntime`: gera no Play, move os agentes, segue um alvo — 7370316
- [x] Tarefa 6: `test_navigation` — 7370316
- [ ] Tarefa 7: Salvar o navmesh como asset `.mnav` (com GUID) e gerar no editor
- [ ] Tarefa 8: Desenhar o navmesh e os caminhos no viewport
- [ ] Tarefa 9: Ferramentas MCP `nav_bake`, `nav_path` e `nav_agents`
- [ ] Tarefa 10: Seção de navegação no guia de scripts, com exemplo compilado

## Pronto quando

O `test_navigation` passa (navmesh de chão com parede, rampa suave e íngreme, ilhas separadas, agente que contorna
a parede e chega, agente que segue um alvo), uma cena de teste aberta no editor mostra o navmesh no viewport, e um
agente posto nela anda até o destino no Play, conferido pelo MCP.

## Limitações

- O navmesh é gerado a cada Play (custo no começo do jogo numa cena grande) e não aparece no editor até a Tarefa 8.
- Um bloco só: navmesh com mais de 65535 vértices falha (blocos vêm com o TileCache, Fase 36).
- O agente mexe no Transform direto: não usa a física nem desvia de outros agentes (Fase 35).
- A cápsula entra no navmesh como o cilindro que a contém.
