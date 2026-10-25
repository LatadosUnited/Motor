---
tipo: fase
alpha: 6
fase: 35
titulo: Multidão e física
status: pronta
prioridade: 2
areas: [ai, scene]
branch: fase-35-multidao
inicio: 2026-10-24
fim: 2026-10-24
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
- [x] Tarefa 4: Off-mesh links (pulos e escadas de mão) como componente — 9c10922
- [x] Tarefa 5: Áreas com custo e filtro por tipo de agente — 4c9cfec
- [x] Tarefa 6: Animação segue a velocidade do agente (parado, andar, correr) — 868925b
- [x] Tarefa 7: Raio dos agentes no viewport e vizinhos no `nav_agents` — 3888cd4
- [x] Tarefa 8: Guia de scripts e exemplo compilado — 7f15a24

## Pronto quando

O `test_navigation` passa com agentes que se cruzam num corredor sem se atravessar (distância entre eles nunca
menor que a soma dos raios menos uma folga), 50 agentes indo ao mesmo ponto sem travar o passo, um agente com
`CharacterController` que empurra uma caixa e um que pula por um off-mesh link. No editor, pelo MCP, dois grupos
de agentes trocam de lado numa cena de teste.

## Feito

- Tarefas 1 e 2: `NavCrowd` (interno da camada `ai`) põe os agentes num `dtCrowd`. `SetDestination` continua
  respondendo na hora (o caminho é buscado ali e entregue pronto); quem segue um alvo usa a fila da multidão. No
  teste de 110 agentes a colisão do DetourCrowd deixava uns dentro dos outros (ela olha só os primeiros 32 da grade
  e não separa dois parados no mesmo ponto): uma separação própria depois de cada passo resolve, e o par mais perto
  fica a 0,64 m (raios de 0,5 m), com o passo mais lento perto de 10 ms em Debug. Muitos indo ao mesmo ponto chegam
  por bloqueio (parados perto do destino, ou encostados em quem já parou). Dois agentes trocando de lugar nunca
  ficam a menos de 1,42 m; o canto da Fase 34 sai em 4,6 s (antes 5,1 s), com a multidão cortando as curvas.
- Tarefa 3: com `CharacterController`, quem anda é a física (`MoveCharacter`); a multidão volta a cada passo para
  onde o corpo chegou, presa ao navmesh. O agente empurra a caixa até (3,5; 6,0), chega em 4,0 s em pé, nunca sai
  do navmesh e volta para ele depois de posto além da borda. A camada `ai` passa a depender de `physics`.
- Tarefa 4: `NavMeshLink` vira off-mesh connection do Detour (e vai no `.mnav`). A travessia é do `NavSystem`, em
  arco ou reta e na velocidade do agente: a do DetourCrowd não tem arco, é instantânea numa escada e, se o agente
  saísse da multidão, a animação guardada no lugar dele o levava de volta à ponta de saída. O agente pula um vão de
  2 m para uma plataforma 1 m mais alta (passa a y = 1,56); com física cai em pé.
- Tarefa 5: `NavMeshArea` marca o navmesh (áreas 1 a 15) e o custo fica no filtro, então muda na hora; o agente tem
  a máscara "Áreas por onde anda" (um filtro do DetourCrowd por máscara). Lama de custo 5 faz o caminho dar a
  volta, 1,5 atravessa. O exemplo do pastor achou mais dois defeitos, corrigidos em 98bccaa: o atalho de visão do
  DetourCrowd ignorava o custo e desfazia a volta, e um custo mudado por script só valia no passo seguinte.
- Tarefa 6: `NavMeshAgentAnimation` troca os clipes do Animator por `CrossFade` (parado, andando, correndo, pulando)
  e acompanha a velocidade (3,5 / 4 na corrida, 2 / 1,5 andando).
- Tarefa 7: no viewport, o raio de cada agente, as ligações em arco, as caixas das áreas com nome e custo e, no Play,
  as linhas até os vizinhos; `nav_agents` com `radius`, `on_link`, `neighbors` e `links`. Conferido no editor pelo
  MCP (PrintWindow): dez agentes em dois grupos trocando de lado, o par mais perto a 1,01 m.
- Tarefa 8: guia de scripts, seção 15, "Multidão, pulos e lama", com `docs/examples/Pastor.cpp`: sem chuva as seis
  ovelhas atravessam a lama, com chuva dão a volta, e todas chegam ao curral a mais de 1 m uma da outra.
- `kScriptApiVersion` 17 (`NavMeshAgent` e `NavSystem` mudaram de tamanho).

## Limitações

- Até 16 áreas (0 a 15: a marca dos polígonos do Detour tem 16 bits) e até 16 máscaras de agente diferentes (os
  filtros do DetourCrowd); passando disso, os agentes a mais andam por todas as áreas, com aviso no log.
- O custo de cada área é o da primeira Área de navegação com aquele número; duas caixas com o mesmo número e custos
  diferentes valem o da primeira.
- Agente com corpo físico atravessa a ligação pela velocidade pedida à física: numa escada encostada numa parede, a
  cápsula pode bater na borda do alto. Rigidbody num agente não é suportado.
- Durante a travessia de uma ligação o agente fica parado na multidão, na ponta de chegada: os outros desviam dele
  lá, não no meio do pulo.
- Muitos agentes no mesmo destino param em volta dele por bloqueio, não em formação; para formar, mande cada um a um
  lugar (como o pastor).
- O combo "Áreas por onde anda" e o uso de uma ligação não foram vistos no editor (só no `ctest`); os nomes dos
  clipes da Animação do agente são texto livre (sem lista dos clipes do modelo).
- Sem obstáculos que se mexem nem navmesh em blocos: Fase 36.
