---
tipo: alpha
alpha: 6
status: planejada
foco: Inteligência artificial
---

# Alpha 6 — Inteligência artificial (rascunho)

> **Rascunho.** Planejada em 23/10/2026 a pedido do usuário, com a Recast/Detour aprovada
> ([decisão](../decisoes/2026-10-23-recast-detour.md)). A Fase 34 começou antes das alphas 3 a 5 terminarem, também
> a pedido do usuário; as outras esperam a ordem do roteiro.

Foco: inimigos e NPCs que andam pelo cenário sozinhos, percebem o jogador e decidem o que fazer, como o NavMesh e
as ferramentas de IA da Unity. Tudo numa camada nova, `ai/`, acima de `scene`; os componentes ficam em `scene/`.

**A alpha 6 está pronta quando:** o jogo de exemplo do guia tem guardas que patrulham, ouvem passos, perseguem o
jogador pelo navmesh (desviando uns dos outros e das portas fechadas) e desistem quando o perdem, e tudo isso é
testado no `ctest` sem janela.

## Fases

| Fase | Assunto | Status |
|---|---|---|
| 34 | [Navmesh e agentes](alpha-6/fase-34-navmesh.md) | pronta (24/10) |
| 35 | [Multidão e física](alpha-6/fase-35-multidao.md) | pronta (24/10) |
| 36 | Mundo que muda | planejada |
| 37 | [Percepção](alpha-6/fase-37-percepcao.md) | pronta (25/10) |
| 38 | Comportamento | planejada |

## Fase 35 — Multidão e física

- **Multidão:** DetourCrowd, vários agentes desviando uns dos outros, com fila de buscas por quadro.
- **Agente com corpo físico:** o agente anda como personagem da Jolt (empurra caixas, não atravessa o que não está
  no navmesh).
- **Pulos e áreas com custo:** off-mesh links (pulos, escadas de mão), áreas como água e lama que custam mais,
  filtros por tipo de agente.
- **Animação segue o agente:** a velocidade escolhe entre parado, andar e correr no Animator.

## Fase 36 — Mundo que muda

- **Navmesh em blocos** (TileCache): uma porta fechada ou uma caixa empurrada refazem só o bloco onde estão;
  componente `NavMeshObstacle`.
- **Geração em segundo plano** no sistema de tarefas da Fase 26.
- **Navmesh do mundo aberto:** blocos do terreno (alpha 4) carregados e descartados conforme o jogador anda.

## Fase 37 — Percepção

- Cone de visão com raio de linha de visada (`PhysicsSystem::Raycast`), sons que alcançam quem está perto
  (ligado ao `audio/`) e a última posição vista do alvo na memória do agente.

## Fase 38 — Comportamento

- **Máquina de estados** primeiro (patrulhar, perseguir, atacar, fugir).
- **Árvore de comportamento** com blackboard, salva como asset `.mbt`, e o editor visual dela no ImGui.
- **Decisão por pontuação** (utility AI), opcional.
- **Inimigos no jogo de exemplo** do guia.
