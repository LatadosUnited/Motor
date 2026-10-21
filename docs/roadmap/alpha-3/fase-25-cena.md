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

### Pendentes (rascunho em [alpha-3.md](../alpha-3.md))

- [ ] `WorldMatrix` com cache. *(origem: análise de 17/10/2026)*
- [ ] Destruir em lote (`Children`, `DestroyEntity`, `FlushDestroyed`): `spawn200` gasta ~8,6 ms no Update criando e
  destruindo 200 objetos por quadro. *(origem: contadores da Fase 22)*
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

## Limitações

- As listas são refeitas inteiras (uma passada pela cena) a cada mudança de estrutura. Numa cena que cria ou
  destrói entidades todo quadro (`spawn200`), é uma passada por quadro em vez de uma por sistema. Manter as listas
  incrementais fica para a organização dos dados por tipo.
- O contador é global: mudar uma cena (um prefab, a cena do editor) refaz as listas das outras na próxima consulta.
- Com UI na cena, o layout ainda percorre a cena uma vez para pôr os filhos na ordem (0,24 ms na cena de
  referência).
- Quem mexe direto em `Entity::components` precisa chamar `MarkSceneStructureChanged`. Os lugares de hoje já chamam,
  mas um novo que esquecer deixa a lista desatualizada até a próxima mudança.
