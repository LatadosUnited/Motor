---
tipo: decisao
data: 2026-10-23
status: aceita
fases: [34, 35, 36]
---

# Recast/Detour para a navegação

## Contexto

O Motor não tinha navegação: inimigos e NPCs só andavam em linha reta por script. A alpha 6 pede um navmesh gerado
a partir do cenário, busca de caminho, multidão e obstáculos que mudam, como o NavMesh da Unity. O `CLAUDE.md` pede
libs mínimas e aprovação do usuário para cada biblioteca nova.

## Decisão

Usar a Recast/Detour (zlib, C++), tag v1.6.0, aprovada pelo usuário em 23/10/2026. Recast gera o navmesh, Detour
busca os caminhos, DetourCrowd faz a multidão e DetourTileCache os obstáculos. Ela entra estática na `libMotor.dll`,
escondida atrás da camada `ai/` (nenhum header público inclui a Recast).

## Alternativas

- Navmesh próprio: meses de trabalho e pior nos casos difíceis (degraus, rampas, bordas, vãos baixos), que a Recast
  já resolve há anos em engines grandes (o NavMesh da Unity nasceu dela).
- Busca em grade (A* sobre células): simples, mas caminhos em zigue-zague, memória que cresce com a área e nada de
  andares sobrepostos.

## Consequências

- Mais uma dependência no `CMakeLists.txt` (tarball com SHA256), compilada com `-O2` também no Debug, como a Jolt.
- O navmesh fica num bloco só até a Fase 36 (TileCache); o limite é 65535 vértices.
- Rever se a Recast deixar de ser mantida ou se o mundo aberto da alpha 4 pedir algo que ela não cubra.
