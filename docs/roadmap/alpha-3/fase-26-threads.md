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
- [x] Tarefa 2: Paralelizar a montagem do quadro (matrizes, recorte, lotes) — c190fc3
- [ ] Tarefa 3: Paralelizar a sincronização da física e a animação
- [ ] Tarefa 4: Estudar uma thread só para o render
- [ ] Tarefa 5: Carregar texturas, malhas e sons em segundo plano
- [ ] Tarefa 6: Documentar que os scripts continuam numa thread só
- [ ] Tarefa 7: Mostrar as threads e os scripts no Tracy

## Pronto quando

Em `cubos10000` e na referência, a thread principal abaixo de 50% de um núcleo e o quadro mais rápido; `ctest`
repetido sem corrida de dados.

## Andamento

Feito até agora (commits 2f398d5, d46d931, d4c5f52, c190fc3):

- `motor::JobSystem` (`core/JobSystem.h`): núcleos − 1 threads de trabalho ("Tarefas N" no Tracy), `ParallelFor`
  em que quem pede também trabalha, `Schedule`. `MOTOR_THREADS` muda o número (0 = tudo na thread que pede).
- A Jolt roda nele (`physics/JoltJobs`, sobre o `JobSystemWithBarrier` dela) quando `workerThreads < 0` (padrão);
  um número pedido (testes, `test_physics_bench`) ainda cria um pool só da Jolt.
- Renderer: matriz da normal, caixa no mundo, chaves de ordem e recorte da câmera saíram do `Submit` para um passo
  paralelo no `EndScene` (`PrepareItems`); as sombras recortam todas as passadas (cascatas e faces de cubo) de uma
  vez, em paralelo, e montam os lotes de cada passada em paralelo antes de desenhar.
- Animação: cada `Animator` calcula a pose em paralelo.
- `Scene::SubmitTo` com `Renderer::SubmitMany`: pedidos e chaves de material montados em paralelo, só a numeração
  dos materiais (tabela única) fica serial, sobre um vetor contíguo. Entidades na raiz da hierarquia vão em paralelo
  (cada uma escreve só no próprio cache de matriz; o contador de `stamp` virou atômico); filhos e malhas com pele
  seguem no caminho serial. Conferido no Player: draws, triângulos e recortados iguais ao `main` em quatro cenas, e
  imagem igual pixel a pixel em `cubos1000sombra`.

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
- O maior custo serial que sobrou era o `Scene::SubmitTo` (~6,5 ms com 10 000 objetos). Um cronômetro dividiu:
  ~1,8 ms montando a lista, ~1,3 ms conferindo a matriz de mundo e ~5 ms no `Submit` do renderer (ler material e
  malha de cada objeto, espalhados na memória). Resolvido com o `SubmitMany` (abaixo).
- Muito ruído entre execuções iguais quando a máquina está ocupada (outras compilações): medir com ela parada.

### Medição com o `SubmitMany` (23/10/2026)

Mesmo método. `cubos10000`, o mesmo build da Fase 26 com 0, 1 e 3 threads de trabalho (`MOTOR_THREADS`):

| Threads de trabalho | Quadro | GPU | Cena HDR (GPU) | CPU Render | Thread principal | Outras | CPU do processo |
|---|---|---|---|---|---|---|---|
| 0 | 18,5 ms | 15,0–15,1 | 5,0 | 14,8 | 79–80% | — | 80% |
| 1 | 18,6–18,9 ms | 15,0–15,1 | 5,2 | 12,4–13,1 | 59–64% | 34–35% | 96–98% |
| 3 (padrão) | 18,9–19,1 ms | 15,3–15,6 | 5,4 | 12,0–12,1 | 50–52% | 21–25% cada | 113–124% |

Contra o `main` (66–70% e 61–63% em duas rodadas), a thread principal em `cubos10000` ficou entre 30% e 52%. A
referência não mudou (limitada pela GPU: quadro ~48 ms, thread principal 46–47%).

Leitura:

- **Nesta máquina o quadro não melhora, piora ~0,4 ms:** a CPU tira ~2,8 ms da thread principal, mas a HD 530
  divide energia e memória com a CPU e a passada da cena fica ~0,4 ms mais lenta com as 4 threads ocupadas.
  Numa máquina limitada pela CPU (placa de vídeo dedicada) os 2,8 ms devem aparecer no quadro: falta medir (Fase 24,
  Tarefa 1, já pede uma máquina assim).
- **O trabalho escala mal:** as threads de trabalho gastam juntas ~13 ms de CPU por quadro para tirar ~2,8 ms da
  principal. Um teste à parte mostrou que o `ParallelFor` em si é barato (~30 µs por chamada, e em conta pura o
  paralelo gasta a mesma CPU que o serial): o custo é a memória. Cada objeto lê entidade, componente, material e
  malha em lugares diferentes do heap, e 4 threads disputando a memória (com a GPU integrada) ficam cada uma mais
  lenta. Guardar os componentes em blocos contínuos (Fase 25, Tarefa 11) deve ajudar mais que mais threads.

## Limitações

- Só entidades na raiz da hierarquia são enviadas em paralelo; filhos e malhas com pele seguem no caminho serial
  (o cache da matriz de um filho confere e pode recalcular o do pai). Cenas com hierarquias fundas (modelos
  importados) ganham pouco.
- A sincronização da física com a cena (parte da Tarefa 3) não foi paralelizada: está sendo refeita na Fase 25
  (`fase-25-fisica-passos`, só os corpos que se mexeram).
- Itens enviados em paralelo entram antes dos do caminho serial: entre itens com a mesma chave de ordem (e
  transparentes à mesma distância), a ordem de desenho pode mudar em relação à da cena.
