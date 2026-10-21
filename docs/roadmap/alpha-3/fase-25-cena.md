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
- [ ] Tarefa 5: Limitar os passos fixos da física por quadro (e interpolar)
- [ ] Tarefa 6: Sincronizar com a física só os corpos que se mexeram
- [ ] Tarefa 7: Investigar os picos de ~185 ms do `test_physics_bench`
- [ ] Tarefa 8: Índice de nomes para `FindEntity`/`FindByName`
- [ ] Tarefa 9: `Scene::Children` sem percorrer a cena
- [ ] Tarefa 10: Listas por tipo incrementais
- [ ] Tarefa 11: Guardar componentes em blocos contínuos por tipo (estilo ECS)

## Pronto quando

Cena de referência com física ≤ 3 ms por quadro (hoje 11,6), `fisica1000` acima de 85% da Unity, e
`test_desempenho` sem piora (`mundo_cena`, `jogo_passo`), medidos com o mesmo método de 21/10.

## Feito

Números do que já foi feito: histórico desta nota no git (commit 0220479) e `bench/README.md`.

## Limitações

- O cache da matriz de mundo confere o transform a cada consulta: numa hierarquia funda, consultar uma folha ainda
  sobe até a raiz (sem recalcular nem buscar no mapa).
- Os `OnDestroy` de um lote rodam todos antes de tirar as entidades (como na Unity).
- Quem mexe direto em `Entity::components` precisa chamar `MarkSceneStructureChanged`.
- O contador de estrutura é global: mudar uma cena refaz as listas das outras na próxima consulta.
