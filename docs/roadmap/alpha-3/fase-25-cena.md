---
tipo: fase
alpha: 3
fase: 25
titulo: Cena, dados e física
status: em andamento
prioridade: 3
areas: [scene, physics]
branch: fase-25-cena
inicio: 2026-10-21
---

# Fase 25 — Cena, dados e física

## Objetivo

Tirar da CPU o trabalho que cresce com o tamanho da cena e não com o que acontece nela, e baixar o custo da física
na cena de referência (11,6 ms por quadro na medição de 21/10, porque o quadro lento pede 3 a 4 passos fixos). No
replanejamento de 21/10 as threads saíram desta fase para a Fase 26 ([alpha-3.md](../alpha-3.md)); a organização
dos dados por tipo ficou aqui, porque é o que deixa a 26 dividir o trabalho sem travas.

## Tarefas

- [x] Tarefa 1: Listas por tipo de componente na `Scene` — bfc1f5f
- [x] Tarefa 2: Sistemas usando as listas por tipo — 10c2343
- [x] Tarefa 3: Destruir entidades em lote — 4cff640
- [x] Tarefa 4: Cache da matriz de mundo — b32086e
- [x] Tarefa 5: Limitar os passos fixos da física por quadro (e interpolar) — 653e39c
- [x] Tarefa 6: Sincronizar com a física só os corpos que se mexeram — c22171d
- [x] Tarefa 7: Investigar os picos de ~185 ms do `test_physics_bench` — c409eef
- [x] Tarefa 8: Índice de nomes para `FindEntity`/`FindByName` — a0fbbee
- [x] Tarefa 9: `Scene::Children` sem percorrer a cena — a0fbbee
- [x] Tarefa 10: Listas por tipo incrementais — a0fbbee
- [ ] ~~Tarefa 11: Guardar componentes em blocos contínuos por tipo (estilo ECS)~~ — foi para a Fase 28 (alpha 4)
- [x] Tarefa 12: Investigar por que metade das caixas da referência não dorme — 23f6a39

## Pronto quando

Cena de referência com física ≤ 3 ms por quadro (hoje 11,6), `fisica1000` acima de 85% da Unity, e
`test_desempenho` sem piora (`mundo_cena`, `jogo_passo`), medidos com o mesmo método de 21/10.

## Feito

Números do que já foi feito: histórico desta nota no git (commit 0220479) e `bench/README.md`.

**Tarefa 5 (22/10, branch `fase-25-fisica-passos`):** no máximo 4 passos fixos por quadro (`FixedStepClock`; acima
disso o jogo fica mais lento junto) e interpolação dos corpos dinâmicos e personagens no Update
(`PhysicsSettings::interpolate`, ligada). Contadores novos no `--perf`: `Fisica sincronizar`, `Fisica passo`,
`Fisica escrever`, `passos fixos` e `corpos ativos`.

Medição (Player Release, referência, 14 s, medianas por quadro do segundo 4 em diante, mesma sessão):

| | Quadro | Física | Passos por quadro | Sincronizar | Passo do Jolt | Escrever | GPU |
|---|---|---|---|---|---|---|---|
| antes (`main`) | 51,5 ms | — | — | — | — | — | — |
| depois | 51,4 ms | 11,8 ms | 3,1 (3 em 85% dos quadros) | 4,6 | 5,4 | 1,8 | 49,4 ms |

Leitura: o limite não muda esta cena (ela fica em 3 passos; o limite só corta quadros acima de 67 ms) e o quadro
é limitado pela GPU (32,9 ms parado no `SwapBuffers`). Cada passo custa ~3,8 ms, dos quais 1,5 só para sincronizar
os componentes com os corpos, com 460 corpos ainda acordados. A meta de física ≤ 3 ms depende da Tarefa 6 e de
quadros mais rápidos (Fase 24). Com a interpolação dá também para medir um passo fixo maior (1/50 s, o padrão da
Unity) sem tranco na tela.

**Tarefa 6 (22/10):** o `SyncBodies` guarda a lista dos colliders com os ponteiros da entidade, dos componentes e
do corpo, refeita só quando a estrutura muda (`SceneStructureVersion`). A cada passo, corpo sem mudança custa só
as comparações: estático e cinemático parados comparam a matriz de mundo, sem decompor. O aviso de Rigidbody sem
Collider e a procura de corpos sem entidade só rodam quando a estrutura muda ou alguma entidade é desativada. O
`WriteBack` percorre só os corpos acordados que o Jolt lista. A `Scene` ganhou `IsActiveInHierarchy` e
`WorldMatrix` por `Entity&` (sem a busca pelo id).

| Referência (mesma sessão, mesmo método) | Física por quadro | Sincronizar | Escrever | Passo do Jolt | Quadro |
|---|---|---|---|---|---|
| `main` | 15,2 ms | — | — | — | 44,3 ms |
| antes (Tarefa 5) | 13,3 ms | 4,7 | 1,9 | 6,7 | 44,5 ms |
| depois | 12,9 ms | 2,2 | 1,4 | 8,6 | 44,2 ms |

`test_desempenho`: `jogo_passo` 1,96 → 1,07 ms (−45%; 1 passo + 1 Update, a referência 2 s depois do Play).
O passo do Jolt variou de 6,7 a 8,6 ms entre rodadas sem mudança dentro dele. Ele é agora ~2/3 da física: 460 das
900 caixas continuam acordadas depois de 14 s (no `test_desempenho`, 810 depois de 2 s e 440 depois de 10 s, com e
sem interpolação). A meta de física ≤ 3 ms passa por elas dormirem (Tarefa 12) e por quadros mais rápidos (Fase 24).

**Tarefa 12 (23/10):** as caixas acordadas ficam em colunas inteiras (o número cai de 10 em 10), e a do topo
descreve um círculo a ~0,6 m/s sem sair da coluna: **a coluna inteira balança** como um pêndulo invertido que não
perde energia. Experimento (chão e as 900 caixas da referência, 20 s a 60 Hz, corpos acordados):

| Variação | Acordados depois de 20 s |
|---|---|
| como na referência (giro de 0,05 rad × (i % 7) entre caixas vizinhas) | 440 |
| sem interpolação | 440 |
| sem o giro (caixas alinhadas) | 0 (em ~6 s) |
| colunas de 5 / de 3 | 65 / 0 |
| só a Jolt, sem a engine (mesma cena) | 420 |
| quinas vivas (raio convexo 0 ou 0,01), atrito 1,0, amortecimento angular 0,5 | 360 a 410 |
| 4 ou 8 iterações de posição | 360 a 420 |
| 20 iterações de velocidade | 180 |
| sono a 0,05 m/s (Jolt: 0,03) | 250 |
| **20 iterações de velocidade e sono a 0,05 m/s** | **20** |

Conclusões: a engine não acorda as caixas (zero teleportes contados no `SyncBodies`; a interpolação não muda nada),
e a Jolt pura balança igual. É o solucionador com 10 iterações que não assenta pilhas altas de caixas giradas umas
sobre as outras. `PhysicsSettings` ganhou `velocitySteps` (20), `positionSteps` (2) e `sleepVelocity` (0,05 m/s;
a PhysX da Unity dorme com energia equivalente a ~0,1 m/s).

| `test_physics_bench` (1000 caixas, 4 s, 3 threads) | Dormindo | FixedUpdate médio | Fora da coluna |
|---|---|---|---|
| antes | 411 de 1001 | 4,70 ms | 0 |
| depois (3 rodadas) | 981 de 1001 | 0,87–1,71 ms | 0 |

Referência no Player: 60 corpos acordados a partir de 2 s (antes, 460 depois de 14 s). Os tempos de quadro dessa
rodada e o `test_desempenho` não valem: a máquina estava com outros processos usando ~5 núcleos (todas as medidas,
até as de JSON, 2 a 4 vezes mais lentas). Mesmo assim o `jogo_passo` ficou igual à base (−3%). Falta remedir a
referência com a máquina parada e regravar a base.

**Tarefa 7 (25/10):** o `test_physics_bench` passou a mostrar os 3 piores passos depois de 2 s (tudo assentado), com
o Jolt, a sincronização e a escrita de cada um. 4000 caixas, 1200 passos, três rodadas, Release, máquina ocupada (outros
programas abertos):

| Threads extras | FixedUpdate médio | Máximo (no 1º segundo) | Pior passo depois de 2 s |
|---|---|---|---|
| 0 | 4,2 a 5,2 ms | 63 a 94 ms (passos 34 a 55) | 4,3 a 5,5 ms |
| 3 | 1,1 a 2,4 ms | 10 a 25 ms (passos 1 a 58) | 1,7 a 2,0 ms |

Não há mais picos isolados em passos soltos: o máximo é o primeiro segundo, com as 4000 caixas batendo e empilhando
(trabalho real do Jolt). Os ~185 ms da Fase 5 eram de antes das pilhas dormirem (Tarefa 12: 3961 de 4001 dormem) e
medidos em Debug.

**Tarefas 8 a 10 (25/10):** a `Scene` guarda três índices, montados numa passada na primeira consulta: por tipo de
componente (`EntitiesWith`), por pai (`Children`) e por nome (`FindByName`, pelo hash do nome, conferido na busca).
Depois disso eles acompanham as mudanças: criar, destruir, `Add`, `Remove` e `SetParent` guardam o que mudou numa
fila, aplicada na próxima consulta (as listas continuam mudando só na próxima consulta, como antes). As listas ficam
em ordem de id, que é a ordem da cena (as entidades só entram no fim e os ids só crescem). `MarkSceneStructureChanged`
refaz os índices. `Entity::Add`/`Remove` acham a cena pelo `EntityCache::owner`: `kScriptApiVersion` 19. Teste:
`test_scene_index` (casos diretos e 3000 mudanças ao acaso conferidas com a varredura).

`test_desempenho`, medidas novas (Release, ms, o branch antes e depois alternados, duas rodadas, máquina ocupada):

| | antes | depois |
|---|---|---|
| `cena_filhos` (a árvore inteira pelos filhos, referência e 200 cadeias de 10) | 105 a 118 | **0,65 a 0,70** |
| `cena_nomes` (100 buscas, 25 delas de um nome que não existe) | 2,5 a 2,7 | **0,60 a 0,65** |
| `cena_estrutura` (criar 200 entidades com malha e destruí-las) | 1,1 a 1,6 | **0,37 a 0,56** |

O que sobra no `cena_nomes` é o nome que não existe, que ainda percorre a cena.

**Tarefa 11 (adiada em 25/10):** guardar os componentes em blocos por tipo muda a API de `Entity::components`, usada
pelos scripts, pelo Inspector, pelo serializador e pelos prefabs. A Fase 26 mediu que montar o quadro em várias threads
escala mal porque cada objeto lê entidade, componente, material e malha espalhados no heap, mas nesta máquina o quadro
é limitado pela GPU, então o ganho não aparece no quadro. Foi para a Fase 28 (alpha 4), com o mundo aberto e os
objetos guardados entre quadros no renderer.

## Limitações

- `FindByName` com nomes repetidos: uma entidade renomeada direto (o nome é campo livre) para o nome de outra que vem
  depois dela na cena pode perder a vez para essa outra até o índice ser refeito. Sem repetidos, é sempre a primeira.
  Nome que não existe ainda percorre a cena a cada busca.
- `Children` não vê `Entity::parent` mudado direto (use `SetParent`); nenhum código da engine e do editor faz isso.
- Os índices não são seguros entre threads: a consulta aplica a fila de mudanças (é `const`, mas escreve).

- 20 iterações de velocidade custam mais por corpo acordado (no `test_physics_bench`, a queda do primeiro segundo
  não ficou mais cara; o ganho vem de quase todos dormirem). 6 das 90 colunas da referência ainda balançam.
- `velocitySteps`, `positionSteps` e `sleepVelocity` são globais (`PhysicsSettings`), sem campo no `.mproject` nem
  no editor. Corpo que anda abaixo de 5 cm/s por meio segundo agora para (antes, 3 cm/s).

- O cache da matriz de mundo confere o transform a cada consulta: numa hierarquia funda, consultar uma folha ainda
  sobe até a raiz (sem recalcular nem buscar no mapa).
- Os `OnDestroy` de um lote rodam todos antes de tirar as entidades (como na Unity).
- Quem mexe direto em `Entity::components` precisa chamar `MarkSceneStructureChanged`. Vale também para a física:
  ela guarda ponteiros para os componentes até a estrutura mudar.
- A sincronização ainda passa por todos os colliders a cada passo (só compara, sem buscar). Para não passar, os
  componentes e o Transform precisariam avisar quando mudam.
- O contador de estrutura é global: mudar uma cena refaz as listas das outras na próxima consulta.
- Interpolação: no `OnUpdate` dos scripts, o Transform de um corpo fica até um passo atrás da física. Ela é ligada
  ou desligada para o jogo todo, não por corpo, e não aparece no `.mproject` nem no Inspector.
- Interpolação de filho de outra entidade é feita no Transform local: se o pai também se move, o filho pode
  sair um pouco do caminho entre os dois passos.
- Script que chama `AddForce`/`MoveCharacter` no `OnUpdate` para uma entidade sem corpo ainda faz os corpos
  interpolados voltarem à pose do último passo naquele quadro (um tranco de um quadro).
- Não verificado na tela: a suavidade foi conferida só pelos testes (`test_passo_fixo`), não vendo o jogo rodar
  com quadros fora do ritmo do passo fixo.
