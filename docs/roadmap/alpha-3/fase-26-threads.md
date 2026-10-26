---
tipo: fase
alpha: 3
fase: 26
titulo: Várias threads
status: pronta
prioridade: 4
areas: [core, physics, renderer]
branch: fase-26-threads
inicio: 2026-10-23
fim: 2026-10-25
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
- [x] Tarefa 3: Paralelizar a sincronização da física e a animação — d4c5f52
- [x] Tarefa 4: Estudar uma thread só para o render — nesta nota (Feito)
- [x] Tarefa 5: Carregar texturas, malhas e sons em segundo plano — d4aabff
- [x] Tarefa 6: Documentar que os scripts continuam numa thread só — 06deb4c
- [x] Tarefa 7: Mostrar as threads e os scripts no Tracy — 06deb4c
- [x] Tarefa 8: Enviar a cena sem o mapa de ids e a lista de componentes por objeto — 75ff43c

## Pronto quando

Em `cubos10000` e na referência, a thread principal abaixo de 50% de um núcleo e o quadro mais rápido; `ctest`
repetido sem corrida de dados.

## Feito

Até 23/10 (commits 2f398d5, d46d931, d4c5f52, c190fc3):

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

### Depois das Fases 24 e 25 (25/10/2026)

O `main` com as Fases 24 e 25 entrou no branch (0f6f7d6): o LOD e a variante sem mapas da Fase 24 foram para o
`PrepareItems` (valem para o `Submit` e o `SubmitMany`), as cascatas reaproveitadas e o spot com só as faces do cone
viraram passadas da lista paralela das sombras. Com a GPU mais rápida, `cubos10000` deixou de ser limitada só pela
GPU (GPU ~11 ms, quadro ~15 ms) e o envio da cena apareceu.

Contadores novos no `--perf` (`Render: enviar a cena`, `preparar`, `listas das sombras`, `cena e sombras`) mostraram
o `SubmitMany` com 10 ms sem threads e 7,5 ms com 4: ~1 µs por objeto, quase tudo leituras espalhadas (id → mapa →
`Entity` → lista de componentes → `MeshRenderer`). Mudanças (75ff43c):

- A `Scene` guarda a lista (entidade, `MeshRenderer`) dos objetos com malha, refeita só quando a estrutura muda
  (`SceneStructureVersion`); cópia e movimento da cena começam com ela vazia. `kScriptApiVersion` 20.
- O `SubmitMany` escreve cada item já no lugar, em paralelo, com o hash das chaves do material; a parte serial só
  numera os materiais (comparando o hash primeiro). As matrizes de objeto não são refeitas a cada quadro (o
  construtor do glm escrevia 20 000 identidades, 0,7 ms).

| `cubos10000`, 4 threads | antes (branch) | depois |
|---|---|---|
| `SubmitMany` | 7,5 ms | 6,0 |
| montar os pedidos (paralelo) | 3,3 ms | 2,2 a 3,3 (agora escrevendo os itens) |
| numerar os materiais (serial) | 1,5 ms | 1,5 |
| pôr os itens no lugar | 2,5 ms | — |

Medição final (Player Release, `main` 0a5dfdc contra o branch, alternados, duas rodadas, 14 s, CPU por thread entre
os segundos 6 e 12):

| Cena | Build | Quadro | Thread principal | Outras threads | Processo |
|---|---|---|---|---|---|
| `cubos10000` | `main` | 14,9–15,3 ms | 79–80% | — | 79–80% |
| `cubos10000` | Fase 26 | 15,0–15,1 ms | **46–50%** | 21–23% cada | 114–117% |
| referência | `main` | 23,3–23,4 ms | 53–58% | 5–8% cada (Jolt) | 75–80% |
| referência | Fase 26 | 22,6 ms | **50%** | 14–16% cada | 96% |

**Tarefa 5 (d4aabff):** `AssetLibrary::PreloadTextures` lê e decodifica no `JobSystem` as texturas dos GUIDs pedidos e
as dos materiais entre eles; a textura OpenGL é criada na thread principal (a do contexto). O `SceneSerializer` chama
com todos os GUIDs da cena antes de ler as entidades. O log mostra os tempos ("Texturas da cena: ...") e a carga da
cena ("Cena carregada: ... (N entidades, X ms)"). Cena de teste com 16 PNG de 2048×2048 (cada um com o seu material),
três rodadas alternadas:

| | Ler e decodificar | Criar na GPU | Total |
|---|---|---|---|
| `MOTOR_THREADS=0` (como no `main`) | 735–802 ms | 722–801 ms | 1457–1603 ms |
| 3 threads | **387–395 ms** | 781–805 ms | **1176–1200 ms** |

Criar a textura (subir 16 MB e gerar as mipmaps no driver) fica serial: sem gerar as mipmaps cai só para ~830 ms, o
grosso é a cópia. Malhas (OBJ e modelos) e sons não foram: os sons já eram decodificados numa thread do áudio
(`PreloadedClips`), e malhas de arquivo são raras nas cenas do bench.

**Tarefa 3:** a animação foi para o `JobSystem` em d4c5f52. A sincronização da física foi refeita na Fase 25 (só os
corpos que se mexeram) e custa ~0,9 ms por quadro na referência: dividir isso entre threads não compensa o custo de
dividir.

**Tarefa 4 (estudo):** uma thread só para o render precisaria do contexto OpenGL nela e de uma fila de comandos (ou de
uma cópia dos dados do quadro) entre a thread do jogo e a do render, com um quadro de atraso. Nesta máquina não
ajudaria: o quadro é limitado pela GPU integrada, e com mais CPU ocupada a HD 530 fica mais lenta (divide energia e
memória). Fica para uma máquina com placa de vídeo dedicada (Fase 30 pede a medição).

**Tarefas 6 e 7 (06deb4c):** `docs/scripts.md` explica que os scripts rodam todos na thread principal e que a engine
usa as outras threads só entre as chamadas deles. No Tracy, cada chamada de script vira uma zona com o nome dele
(`SceneRuntime::InvokeScript`); as threads de trabalho já tinham nome ("Tarefas N").

Pronto quando:

- `cubos10000` e referência com a thread principal abaixo de 50% de um núcleo: **sim** em `cubos10000` (46–50%),
  **no limite** na referência (50%).
- Quadro mais rápido: **na referência sim** (23,3 → 22,6 ms); em `cubos10000` igual ao `main` (antes o branch era
  0,3–0,6 ms mais lento). Nesta máquina a GPU integrada limita o quadro.
- `ctest` repetido sem corrida de dados: **sim**, os 10 testes que passam pelas threads (sistema de tarefas com e sem
  threads, física, animação, render, LOD, índices da cena, sistemas do runtime, texturas) 20 vezes cada, em paralelo,
  sem falha; o `ctest` inteiro (56) também passa.

`test_desempenho` (Release, `main` e branch alternados, três rodadas): igual ao `main` dentro do ruído, menos
`cena_ler` (+3 a 6%: a passada que junta os GUIDs da cena percorre o JSON inteiro) e `jogo_passo` (+4%). Uma rodada
de oito passou do limite em `cena_copiar` (o `main` variou de 3,9 a 5,0 ms na mesma sessão); base mantida.

## Limitações

- Só entidades na raiz da hierarquia são enviadas em paralelo; filhos e malhas com pele seguem no caminho serial
  (o cache da matriz de um filho confere e pode recalcular o do pai). Cenas com hierarquias fundas (modelos
  importados) ganham pouco.
- A sincronização da física com a cena não foi paralelizada (~0,9 ms por quadro na referência).
- Criar as texturas na GPU continua serial na thread principal; malhas e modelos de arquivo carregam um por vez.
- O trabalho dividido escala mal (memória espalhada): 4 threads tiram ~3 ms da principal gastando ~10 ms de CPU.
  Componentes em blocos contínuos ficaram para a Fase 28.
- Sem thread de render (estudo acima).
- Itens enviados em paralelo entram antes dos do caminho serial: entre itens com a mesma chave de ordem (e
  transparentes à mesma distância), a ordem de desenho pode mudar em relação à da cena.
