---
tipo: fase
alpha: 3
fase: 25
titulo: Cena, dados e threads
status: em andamento
prioridade: 3
areas: [scene, physics, audio, ui]
branch: fase-25-cena
inicio: 2026-10-21
---

# Fase 25 — Cena, dados e threads

## Objetivo

Tirar da CPU o trabalho que cresce com o tamanho da cena e não com o que acontece nela: sistemas que procuram os
seus componentes em todas as entidades, matrizes de mundo recalculadas subindo a hierarquia, destruir em lote
quadrático. Depois, organizar os dados por tipo e dividir o quadro entre os núcleos. Rascunho completo em
[alpha-3.md](../alpha-3.md); números de partida na linha de base da [Fase 22](fase-22-medicao.md).

## Tarefas

### Sistemas sem percorrer a cena

- [x] **Listas por tipo de componente na `Scene`** (`Scene::EntitiesWith<T>()`): os ids das entidades com o
  componente, na ordem da cena. As listas de todos os tipos são montadas numa passada só e refeitas quando a
  estrutura muda (`SceneStructureVersion`, o mesmo contador da lista de scripts da Fase 22). `Entity::Remove`
  passou a subir o contador, e quem mexe direto em `Entity::components` (prefabs, serializador, Inspector, colar,
  edição múltipla) chama `MarkSceneStructureChanged`. Teste: `test_entities_with`. *(origem: contadores da Fase 22,
  20/10/2026)* (bfc1f5f)
- [x] Áudio (fontes, ouvinte, pré-carga), animação, UI (botões e layout), física (corpos, personagens, "a cena tem
  física?"), câmera principal e luzes usam as listas. (10c2343)

### Destruir em lote

- [x] **`Scene::DestroyEntities` e `Scene::Subtrees`:** várias sub-árvores numa passada só pela cena, com vetores
  ordenados e busca binária (sem conjunto com hash por entidade). O `FlushDestroyed` do `SceneRuntime` chama o
  `OnDestroy` de todas as entidades pedidas no passo (cada raiz seguida dos descendentes, na ordem dos pedidos) e
  depois tira todas de uma vez; `DestroyEntity` virou o caso de uma raiz. Teste: `test_destroy_batch`. *(origem:
  contadores da Fase 22: `spawn200` com ~8,6 ms no Update)* (4cff640)

### Matriz de mundo

- [x] **`WorldMatrix` com cache por entidade** (`Entity::cache`), conferido a cada consulta: recalcula só quando o
  transform, o pai ou o mundo do pai mudaram (cada recálculo ganha um número de um contador global, e o filho
  guarda o do pai). Não precisa de aviso: scripts e editor continuam mexendo direto em `Entity::transform` e
  `Entity::parent`. O ponteiro do pai também fica guardado (vale enquanto a estrutura da cena não muda), e
  `IsActiveInHierarchy` sobe a hierarquia sem buscar no mapa de ids; `Find(kNullEntity)` não busca. A montagem dos
  desenhos usa a entidade direto. A cópia de uma entidade começa sem cache. `Entity` mudou de tamanho:
  `kScriptApiVersion` 16. Teste: `test_world_cache`. *(origem: análise de 17/10/2026)* (b32086e)

### Pendentes (rascunho em [alpha-3.md](../alpha-3.md))

- [ ] Índice de nomes para `FindEntity`/`FindByName`.
- [ ] Física: picos do `test_physics_bench` e sincronização cena–física. *(Pendência da Fase 5.)*
- [ ] Organização dos dados por tipo, trabalho em várias threads, profiler nas threads e nos scripts.

## Pronto quando

O rascunho em [alpha-3.md](../alpha-3.md) feito, com a cena de referência e `fisica1000` medidos antes e depois.

## Feito

**Listas por tipo de componente (21/10/2026).** Player Release, A/B alternado (o build do `main` e o desta branch,
um depois do outro, duas rodadas, medianas por quadro do segundo 4 em diante, ms):

| Cena | Quadro | CPU Update | Áudio + Ui + Animação | CPU FixedUpdate |
|---|---|---|---|---|
| `cubos10000` antes | 50,8 a 60,9 | 6,7 a 7,2 | 6,3 a 6,6 | 1,3 a 2,9 |
| `cubos10000` depois | **41,7 a 43,8** | **0,02** | 0,00 | **0,01** |
| referência antes | 55,4 a 56,0 | 0,92 a 0,94 | 0,86 a 0,89 | 18,8 a 19,7 |
| referência depois | 55,0 a 55,7 | 0,28 a 0,29 | 0,25 | 13,9 a 15,8 |

Em `cubos10000` o `FixedUpdate` também zerou: a física percorria as 10 000 entidades a cada passo procurando
colliders, numa cena sem nenhum. A cena de referência não muda o quadro (a GPU é o limite). `spawn200` e
`fisica1000` ficam iguais (o custo deles é destruir em lote e a Jolt). `test_desempenho`: `jogo_passo` de 2,65 para
1,85 a 1,99 ms na mesma sessão (−27%); base regravada só nesse número.

**Destruir em lote (21/10/2026).** `spawn200`, mesmo A/B (ms):

| | Quadro | CPU Update | Destruir entidades | CPU Render |
|---|---|---|---|---|
| antes | 15,6 a 15,8 | 8,5 | 7,9 a 8,0 | 2,8 a 3,0 |
| depois | 15,5 a 16,2 | **1,2 a 1,3** | **0,6** | 4,9 a 5,2 |

O quadro não muda: a cena é limitada pela GPU (~10,5 ms de GPU mais a apresentação), e o tempo de CPU que sobra
vira espera pelo driver no Render. A primeira versão, com conjuntos e mapas com hash, ficava em 2,6 ms: medido por
partes, 1 ms era o mapa por entidade para achar os descendentes (calculado duas vezes) e o resto, alocar um nó por
entidade nos conjuntos.

**Matriz de mundo (21/10/2026).** Medido antes no Player, por zonas temporárias: em `cubos10000`, montar os
desenhos custa ~5,5 ms dos ~45 ms de Render, e passar só "ativa na hierarquia" e a matriz de mundo pelas 10 000
entidades, ~7 ms frios. O custo não era a conta da matriz, eram as buscas: 4 por entidade no mapa de ids (duas delas
`Find(0)` para o "pai" de quem está na raiz). `test_desempenho`, Release (ms, a mesma conta para todas as entidades):

| | antes | depois |
|---|---|---|
| `mundo_cena` (cena de referência, quase tudo na raiz) | 0,53 a 0,64 | **0,31 a 0,38** |
| `mundo_hierarquia` (200 cadeias de 10) | 0,88 a 1,01 | **0,23 a 0,30** |

No Player, o Render de `cubos10000` ficou igual dentro do ruído (44,2 contra 45,6 ms na rodada limpa; a segunda
rodada pegou a máquina esquentando): o resto do Render é o envio dos 10 000 draws ao OpenGL (Fase 24).

## Limitações

- O cache confere o transform a cada consulta (10 floats por nível da hierarquia): numa hierarquia funda, consultar
  uma folha ainda sobe até a raiz, só que sem recalcular nem buscar no mapa. Projetos de jogo mudam de
  `kScriptApiVersion` (15 → 16): a DLL de scripts precisa ser compilada de novo (o editor faz ao abrir).

- Os `OnDestroy` de um lote rodam todos antes de tirar as entidades: o `OnDestroy` de uma ainda acha as outras do
  mesmo lote (antes, as já destruídas tinham sumido). É o que a Unity faz (destruição no fim do quadro).
- `Scene::Children` continua percorrendo a cena: duplicar e copiar sub-árvores grandes (prefabs) segue quadrático.
- As listas são refeitas inteiras (uma passada pela cena) a cada mudança de estrutura. Numa cena que cria ou
  destrói entidades todo quadro (`spawn200`), é uma passada por quadro em vez de uma por sistema. Manter as listas
  incrementais fica para a organização dos dados por tipo.
- O contador é global: mudar uma cena (um prefab, a cena do editor) refaz as listas das outras na próxima consulta.
- Com UI na cena, o layout ainda percorre a cena uma vez para pôr os filhos na ordem (0,24 ms na cena de
  referência).
- Quem mexe direto em `Entity::components` precisa chamar `MarkSceneStructureChanged`. Os lugares de hoje já chamam,
  mas um novo que esquecer deixa a lista desatualizada até a próxima mudança.
