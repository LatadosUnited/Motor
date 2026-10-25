---
tipo: fase
alpha: 6
fase: 37
titulo: Percepção
status: em andamento
prioridade: 3
areas: [ai, scene]
branch: fase-37-percepcao
inicio: 2026-10-25
fim: 
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
- [ ] Tarefa 4: Memória do agente (última posição, há quanto tempo, esquecer) e consultas dos scripts
- [ ] Tarefa 5: Raios de visada repartidos entre os passos e grade de alvos para muitos agentes
- [ ] Tarefa 6: Ponto aleatório no navmesh e ponto de fuga no `NavSystem`
- [ ] Tarefa 7: Cone de visão e audição no viewport, ferramenta MCP `ai_perception`
- [ ] Tarefa 8: Guia de scripts e exemplo compilado

## Pronto quando

O `test_perception` passa sem janela: o agente vê o alvo na frente e não o vê atrás de uma parede, fora do cone ou
além da distância; ouve um ruído dentro do alcance e não fora dele; lembra a última posição e esquece depois do tempo
da memória. 100 agentes olhando 20 alvos custam menos de 1 ms por passo em Release. No exemplo do guia, um veado que
pasta foge quando vê ou ouve o jogador, e não foge dele atrás de uma parede. No editor, pelo MCP, o `ai_perception`
mostra quem cada agente vê numa cena de teste.

## Feito

(Preencher ao terminar.)

## Limitações

(Preencher ao terminar.)
