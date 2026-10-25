---
tipo: fase
alpha: 6
fase: 37
titulo: Percepção
status: pronta
prioridade: 3
areas: [ai, scene]
branch: fase-37-percepcao
inicio: 2026-10-25
fim: 2026-10-25
---

# Fase 37 — Percepção

## Objetivo

Os agentes das Fases [34](fase-34-navmesh.md) e [35](fase-35-multidao.md) andam bem, mas não percebem nada: um
script que quer saber se o guarda vê o jogador mede a distância e, sem raio de linha de visada, "vê" através das
paredes. Esta fase dá aos agentes sentidos, como o AI Perception da Unreal: **visão** (cone, distância e linha de
visada pelo `PhysicsSystem::Raycast`), **audição** (ruídos que alcançam quem está perto, inclusive os sons do
`audio/`) e **memória** (a última posição vista ou ouvida de cada alvo, esquecida depois de um tempo). A Fase 36
(mundo que muda) não é pré-requisito, e esta começa antes dela a pedido do usuário.

A engine dá só os sentidos e as consultas. O que o agente faz com o que percebe (fugir, perseguir, investigar) é do
script do jogo; os comportamentos prontos são da [Fase 38](../alpha-6.md), e os exemplos ficam em `docs/examples/`.

Quem tem o componente `Perception` percebe; quem tem o `PerceptionTarget` pode ser percebido (o jogador, a ovelha,
uma isca). Os raios de linha de visada são o caro: o `PerceptionSystem` roda antes dos scripts, corta pelo cone e
pela distância primeiro e reparte os raios entre os passos quando há muitos pares.

## Tarefas

- [x] Tarefa 1: Componentes `Perception` (visão, audição, memória) e `PerceptionTarget` — fe719d7
- [x] Tarefa 2: `PerceptionSystem` no `SceneRuntime`: visão por cone, distância e linha de visada — 6dc34ab
- [x] Tarefa 3: Audição: `MakeNoise` dos scripts e os sons do `AudioSource` de quem é alvo — 5ba8d0a
- [x] Tarefa 4: Memória do agente (última posição, há quanto tempo, esquecer) e consultas dos scripts — e0964ff
- [x] Tarefa 5: Raios de visada repartidos entre os passos e grade de alvos para muitos agentes — cdb107d
- [x] Tarefa 6: Ponto aleatório no navmesh e ponto de fuga no `NavSystem` — bd7f315
- [x] Tarefa 7: Cone de visão e audição no viewport, ferramenta MCP `ai_perception` — a65f711
- [x] Tarefa 8: Guia de scripts e exemplo compilado — 8c2c5cd

## Pronto quando

O `test_perception` passa sem janela: o agente vê o alvo na frente e não o vê atrás de uma parede, fora do cone ou
além da distância; ouve um ruído dentro do alcance e não fora dele; lembra a última posição e esquece depois do tempo
da memória. 100 agentes olhando 20 alvos custam menos de 1 ms por passo em Release. No exemplo do guia, um veado que
pasta foge quando vê ou ouve o jogador, e não foge dele atrás de uma parede. No editor, pelo MCP, o `ai_perception`
mostra quem cada agente vê numa cena de teste.

## Feito

- Tarefa 1: componentes `Perception` (alcance e ângulo da visão, percebe em volta, olhos, audição, memória, grupos
  que percebe) e `PerceptionTarget` (grupo, ponto olhado, visibilidade, alcance dos sons). Na Tarefa 3, o liga e
  desliga dos sons do alvo virou o alcance (`soundRange`, vezes o volume da fonte): a distância máxima da Fonte de
  áudio (30 m) fazia um passo ser ouvido longe demais.
- Tarefa 2: `PerceptionSystem` (camada `ai`, ordem `kSystemOrderPerception` = -50: depois da física, antes dos
  scripts). Vê pelo alcance, pelo cone e pela linha de visada (`PhysicsSystem::Raycast`); Colliders do alvo, de quem
  olha e dos filhos de cada um não tapam. O raio da física só ignora uma entidade: quando bate num filho de quem olha
  (um escudo), um segundo raio vai do alvo para os olhos.
- Tarefa 3: audição. `MakeNoise` dos scripts (ouvido no passo seguinte, dura um passo) e os sons que tocam na Fonte
  de áudio dos alvos. A camada `ai` passa a depender de `audio`.
- Tarefa 4: memória: última posição (a origem do alvo visto; onde fez o ruído ouvido), idade, esquecer depois da
  memória; `GetMemory`, `Recall` e `Forget`.
- Tarefa 5: grade de alvos (células de 16 m) e orçamento de raios por passo (128, `SetRaycastBudget`; os pares
  olhados há mais tempo primeiro). 100 agentes olhando 20 alvos que andam: 7,0 ms por passo em Debug antes, 0,8 ms
  depois e 0,27 ms em Release (meta: 1 ms); 376 pares olhados por passo em vez de 2000. O custo não estava nos raios
  (42 por passo), mas em achar entidade e matriz de novo para cada par.
- Tarefa 6: `RandomPoint` (sorteio do Detour, com a semente do Play) e `FleePoint` (para o lado oposto, desviando de
  22,5° em 22,5° até 135°) no `NavMesh` e no `NavSystem`. `kScriptApiVersion` 18 (o `NavSystem` guarda o
  `SceneRuntime`).
- Tarefa 7: no viewport (botão Navmesh), o cone de cada Percepção, o alcance dos sons dos alvos e, no Play, linhas
  até quem vê e o que ouviu e um X onde lembra; ferramenta MCP `ai_perception`. Conferido no editor pelo MCP
  (PrintWindow): o guarda vê o jogador a 4,08 m, não vê o escondido atrás da parede (o raio dele bate) e, com o
  jogador passado para trás da parede, lembra onde o viu 1,3 s depois.
- Tarefa 8: guia de scripts, seção 15, "Ver, ouvir e lembrar", com `docs/examples/Veado.cpp`: o veado pasta (andou
  11,9 m em 8 s sem sair de 3 m de casa), não vê o jogador atrás da parede, foge ao ouvir os passos dele e, ao vê-lo
  a 2,5 m, está a 10,9 m dois segundos depois.

## Limitações

- O som atravessa paredes e chega em linha reta; não contorna pelo navmesh nem abafa atrás de uma parede.
- A visão olha um ponto só do alvo (o ponto olhado): um alvo com só a cabeça para fora de uma mureta pode não ser
  visto. O cone é medido em 3D e desenhado no chão.
- Com o orçamento de raios estourado, a visão atrasa alguns passos (quem entra no cone demora a ser visto, quem passa
  para trás de uma parede demora a sumir). Sair do alcance ou do cone vale na hora.
- Ruídos sem dono (`MakeNoise` sem fonte) são ouvidos, mas não ficam na memória.
- `FleePoint` olha em linha reta pelo navmesh: num beco sem saída escolhe o lado que mais afasta, sem planejar a
  rota de fuga inteira. `RandomPoint` em polígonos muito grandes às vezes cai no plano B (andar reto pela superfície
  até o ponto sorteado), que se acumula perto das paredes.
- Sem botão próprio no viewport: a percepção aparece junto do navmesh (botão Navmesh).
