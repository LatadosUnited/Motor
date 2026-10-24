---
tipo: fase
alpha: 6
fase: 35
titulo: Multidão e física
status: em andamento
prioridade: 2
areas: [ai, scene]
branch: fase-35-multidao
inicio: 2026-10-24
fim: 
---

# Fase 35 — Multidão e física

## Objetivo

Os agentes da [Fase 34](fase-34-navmesh.md) andam cada um por si: um atravessa o outro, todos mexem no Transform
direto (sem física) e o navmesh só conhece chão e parede. Esta fase faz deles uma multidão. Com o DetourCrowd, que
vem na mesma Recast/Detour já aprovada, eles desviam uns dos outros e as buscas de caminho entram numa fila
dividida entre os quadros. Também ganham um corpo físico (o `CharacterController` da Jolt), pulos e escadas
(off-mesh links), áreas que custam mais (água, lama) e a animação andando junto com a velocidade.

`SetDestination` continua respondendo na hora, como na Fase 34: o caminho é buscado ali e entregue pronto à
multidão. A fila fica para o que a multidão refaz sozinha e para quem segue um alvo.

## Tarefas

- [x] Tarefa 1: Multidão com o DetourCrowd (desvio entre agentes, fila de buscas por quadro) — d2b716f
- [x] Tarefa 2: Raio, altura e desvio ligável no `NavMeshAgent` — d2b716f
- [x] Tarefa 3: Agente com `CharacterController` (anda pela física, sem sair do navmesh) — 3695388
- [ ] Tarefa 4: Off-mesh links (pulos e escadas de mão) como componente
- [ ] Tarefa 5: Áreas com custo e filtro por tipo de agente
- [ ] Tarefa 6: Animação segue a velocidade do agente (parado, andar, correr)
- [ ] Tarefa 7: Raio dos agentes no viewport e vizinhos no `nav_agents`
- [ ] Tarefa 8: Guia de scripts e exemplo compilado

## Pronto quando

O `test_navigation` passa com agentes que se cruzam num corredor sem se atravessar (distância entre eles nunca
menor que a soma dos raios menos uma folga), 50 agentes indo ao mesmo ponto sem travar o passo, um agente com
`CharacterController` que empurra uma caixa e um que pula por um off-mesh link. No editor, pelo MCP, dois grupos
de agentes trocam de lado numa cena de teste.

## Feito

(Preencher ao terminar.)

## Limitações

(Preencher ao terminar.)
