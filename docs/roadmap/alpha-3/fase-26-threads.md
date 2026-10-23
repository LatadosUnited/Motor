---
tipo: fase
alpha: 3
fase: 26
titulo: Várias threads
status: em andamento
prioridade: 4
areas: [core, physics, renderer]
branch: fase-26-threads
inicio: 2026-10-23
---

# Fase 26 — Várias threads

## Objetivo

Usar os 4 núcleos da máquina de referência. Na medição de 21/10 só a física saía da thread principal (pool próprio
da Jolt); render, scripts, UI, áudio e o passo do jogo rodavam numa thread só (92% de um núcleo com 10 000 cubos).
Um sistema de tarefas único, dividido com a Jolt, deixa a montagem do quadro e a física usarem as mesmas threads
sem disputar núcleos entre dois pools.

## Tarefas

- [x] Tarefa 1: Criar o sistema de tarefas (job system), dividido com a Jolt — 2f398d5
- [ ] Tarefa 2: Paralelizar a montagem do quadro (matrizes, recorte, lotes)
- [ ] Tarefa 3: Paralelizar a sincronização da física e a animação
- [ ] Tarefa 4: Estudar uma thread só para o render
- [ ] Tarefa 5: Carregar texturas, malhas e sons em segundo plano
- [ ] Tarefa 6: Documentar que os scripts continuam numa thread só
- [ ] Tarefa 7: Mostrar as threads e os scripts no Tracy

## Pronto quando

Em `cubos10000` e na referência, a thread principal abaixo de 50% de um núcleo e o quadro mais rápido; `ctest`
repetido sem corrida de dados.

## Andamento

Feito até agora (commits 2f398d5, d46d931, d4c5f52):

- `motor::JobSystem` (`core/JobSystem.h`): núcleos − 1 threads de trabalho ("Tarefas N" no Tracy), `ParallelFor`
  em que quem pede também trabalha, `Schedule`. `MOTOR_THREADS` muda o número (0 = tudo na thread que pede).
- A Jolt roda nele (`physics/JoltJobs`, sobre o `JobSystemWithBarrier` dela) quando `workerThreads < 0` (padrão);
  um número pedido (testes, `test_physics_bench`) ainda cria um pool só da Jolt.
- Renderer: matriz da normal, caixa no mundo, chaves de ordem e recorte da câmera saíram do `Submit` para um passo
  paralelo no `EndScene` (`PrepareItems`); as sombras recortam todas as passadas (cascatas e faces de cubo) de uma
  vez, em paralelo, e montam os lotes de cada passada em paralelo antes de desenhar.
- Animação: cada `Animator` calcula a pose em paralelo.

### Medição (23/10/2026)

Player Release, 1280×720 em janela, MSAA 4x, vsync desligado, máquina de referência; mediana do quadro do segundo 4
em diante e CPU de cada thread entre os segundos 6 e 12, em % de um núcleo. `main` = cf070ac (depois da Fase 23).
Sem os scripts compilados (o `Bobber` da referência fica parado).

| Cena | Build | Quadro | GPU | CPU Render | FixedUpdate | Thread principal | Threads de trabalho |
|---|---|---|---|---|---|---|---|
| `cubos10000` | `main` | 17,5–18,0 ms | 14,1–15,5 | 11,7–12,7 | — | 66–70% | — |
| `cubos10000` | Fase 26 | 18,0–18,7 ms | 14,6–15,7 | 11,4–11,9 | — | 55–64% | 5–8% cada |
| referência | `main` | 48,3–48,7 ms | 46,4–46,8 | 8,6–8,9 | 14,1–15,5 | 44–50% | 12–15% (pool da Jolt) |
| referência | Fase 26 | 48,2–48,7 ms | 46,3–46,8 | 7,9–8,0 | 14,6–15,7 | 44–47% | 12–18% |

Cronômetro temporário dentro do renderer (`cubos10000`, média de 100 quadros): preparar os itens (matrizes, caixas,
chaves, recorte e ordem) caiu de ~3,0 ms (`MOTOR_THREADS=0`) para ~1,8 ms; o `Submit` da cena ficou em ~6,5 ms nos
dois casos.

Leitura:

- As duas cenas estão **limitadas pela GPU** (HD 530): tirar CPU da thread principal não muda o quadro aqui.
- A física no pool da engine rende o mesmo que no pool próprio da Jolt.
- O maior custo serial que sobrou é o `Scene::SubmitTo` (percorrer a cena, `CachedWorld`, `Resolved`, ~6,5 ms com
  10 000 objetos): é ele que segura a thread principal acima de 50% em `cubos10000`. Paralelizar exige o cache da
  matriz de mundo seguro entre threads (área da Fase 25) — próximo passo da Tarefa 2.
- Muito ruído entre execuções iguais quando a máquina está ocupada (outras compilações): medir com ela parada.
