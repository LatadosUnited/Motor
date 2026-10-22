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
- [ ] Tarefa 7: Investigar os picos de ~185 ms do `test_physics_bench`
- [ ] Tarefa 8: Índice de nomes para `FindEntity`/`FindByName`
- [ ] Tarefa 9: `Scene::Children` sem percorrer a cena
- [ ] Tarefa 10: Listas por tipo incrementais
- [ ] Tarefa 11: Guardar componentes em blocos contínuos por tipo (estilo ECS)
- [ ] Tarefa 12: Investigar por que metade das caixas da referência não dorme

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

## Limitações

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
