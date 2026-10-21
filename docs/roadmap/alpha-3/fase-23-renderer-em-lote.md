---
tipo: fase
alpha: 3
fase: 23
titulo: Renderer em lote
status: pronta
prioridade: 1
areas: [renderer]
branch: fase-23-renderer-em-lote
inicio: 2026-10-21
fim: 2026-10-21
---

# Fase 23 — Renderer em lote

## Objetivo

Tirar a CPU do caminho com muitos objetos. Na medição de 21/10, `cubos10000` gasta 33 ms de CPU no Render para
10 001 draws (~3,3 µs cada) e a GPU fica esperando. O estudo da Tarefa 1 (21/10) dividiu esse tempo: ~13 ms no draw
call em si (VAO e `glDrawRangeElements`), ~10 ms em montar a lista e recortar, ~4 ms nas matrizes por desenho,
~4 ms reenviando material e texturas iguais e ~0,8 ms buscando os uniforms pelo nome.

## Tarefas

- [x] Tarefa 1: Guardar as posições dos uniforms na criação do shader — ae1e7fb
- [x] Tarefa 2: Parar de copiar o `Material` em cada desenho — 947affc
- [x] Tarefa 3: Ordenar os desenhos por shader, material e malha — 435f968
- [x] Tarefa 4: Instancing para a mesma malha e material, também na sombra — 907a59e
- [ ] ~~Tarefa 5: Recorte com estrutura espacial (BVH ou grade)~~ — foi para a alpha 4 (Fase 28)
- [ ] ~~Tarefa 6: Estudar desenho guiado pela GPU (multi-draw indirect)~~ — foi para a alpha 4 (Fase 28)
- [x] Tarefa 7: Não reenviar material e texturas iguais aos do desenho anterior — ae1e7fb
- [x] Tarefa 8: Recortar sem ler o material de cada item — c1f49f4
- [x] Tarefa 9: Dar o número do material no Submit — 638f982
- [x] Tarefa 10: Baratear o buffer de objetos — 638f982
- [ ] ~~Tarefa 11: Baratear a montagem no Submit (caixa no mundo, percurso da cena)~~ — o custo é o arranjo dos dados da cena: Fase 25, Tarefa 11

## Pronto quando

`cubos10000` com CPU Render ≤ 5 ms (hoje 33) e a cena de referência ≤ 8 ms (hoje 28), medidos com `--perf`;
`test_desempenho` sem piora.

## Feito

**Resumo (21/10/2026).** O renderer passou de um draw por objeto, com uniforms procurados pelo nome e o material
copiado a cada desenho, para lotes instanciados: os objetos do quadro vão uma vez para um buffer na GPU
(`object.glsl`, `InstanceBuffers` em `DrawList.h`), a lista é ordenada por shader, material e malha (`DrawOrder.h`),
e cada sequência igual vira um draw, na cena e em cada cascata de sombra. O material vira um número no `Submit`, o
recorte não lê mais o material, e só o estado que muda é reenviado. Arquivos principais: `Renderer.cpp`,
`Shadows.cpp`, `DrawList.*`, `DrawOrder.*`, `Shader.*`, `Mesh::DrawInstanced`, os shaders `lit.vert`,
`skinned.vert`, `shadow.vert` e `object.glsl`. Testes novos no `test_render` (parâmetros do material, ordem de
desenho, matriz da normal). Do `main` (39ef6fc) ao fim da fase, Player Release (ms):

| Cena | Draws | CPU Render | Quadro |
|---|---|---|---|
| `cubos10000` | 10 001 → **2** | 33,3 → **12,7** | 33,9 → **18,6** (30 → 54 FPS) |
| `cubos1000sombra` | 2401 → **6** | 11,8 → **3,0** | 18,3 → 17,3 |
| referência | 7927 → **16** | ~25 → **8,3** | 49,4 → **44,0** |

As cenas pequenas e a referência agora são limitadas pela GPU (Fase 24). Números de cada tarefa abaixo; os quadros
vêm de rodadas da mesma sessão de cada tarefa (a máquina variou ao longo do dia; vale a comparação dentro de cada
tabela).

**Tarefas 1 e 7 (21/10/2026).** `Shader::Uniform` resolve a posição uma vez e os `Set*` aceitam a posição; o
`Renderer` e as sombras guardam as posições na criação. `MaterialParams` (`Pbr.h`, testado no `test_render`) junta
o que o lit.frag recebe do material, e o desenho só reenvia uniforms e texturas quando isso muda. Player Release,
A/B alternado, duas rodadas, medianas por quadro (ms):

| Cena              | CPU Render antes | depois          | Quadro antes | depois          |
| ----------------- | ---------------- | --------------- | ------------ | --------------- |
| `cubos10000`      | 33,4 a 33,5      | **29,6 a 30,1** | 33,9         | **30,1 a 30,7** |
| `cubos1000sombra` | 11,7 a 11,9      | 10,8 a 11,2     | 18,0 a 18,2  | 18,0            |
| referência        | 24,2 a 25,1      | 23,3 a 23,9     | 48,9 a 49,2  | 48,8 a 48,9     |

Só as posições fixas valem ~0,8 ms em `cubos10000`; o resto é o filtro. Onde está o resto dos ~30 ms (estudo com
variantes): ~13 ms no draw call (Tarefa 4), ~10 ms montando a lista e recortando (Tarefa 2), ~4 ms nas matrizes por
desenho (Tarefa 4).

**Tarefa 2 (21/10/2026).** O `DrawItem` guarda um ponteiro para o material de quem chamou o `Submit` (vivo até o
`EndScene`, como a malha), sem copiar o `Material` e os seus 6 `shared_ptr`. O mesmo material seguido nem recalcula
os parâmetros, e o emissivo preto não faz as 3 potências. Mesmo método, três versões alternadas (ms):

| Versão | `cubos10000` CPU Render | Quadro | referência CPU Render |
|---|---|---|---|
| `main` (39ef6fc) | 33,2 a 33,4 | 33,6 a 33,9 | 24,5 a 25,8 |
| Tarefas 1 e 7 (ae1e7fb) | 30,1 a 30,5 | 30,7 a 31,0 | 24,0 a 25,3 |
| Tarefa 2 (947affc) | **27,5 a 27,9** | **28,0 a 28,4** | **23,4 a 23,5** |

Na fase até aqui: `cubos10000` de 33,3 para 27,7 ms de CPU no Render (−17%). A referência continua limitada pela
GPU (47 ms).

**Tarefa 4 (21/10/2026).** Os itens do quadro vão uma vez para um buffer de objetos (SSBO: matriz de mundo e da
normal) e cada passada grava só a sua lista de índices; itens seguidos com a mesma malha e o mesmo material viram
um draw instanciado, na cena e em cada cascata de sombra. Os shaders acham o objeto em `object.glsl`
(`uDrawBase + gl_InstanceID`). Com pele e transparentes, um item por draw. A imagem da referência saiu igual à do
`main` (captura das duas). Três versões alternadas, duas rodadas, máquina calma (ms):

| Cena | Versão | Draws | CPU Render | GPU | Quadro |
|---|---|---|---|---|---|
| `cubos10000` | `main` | 10 001 | 33,2 a 33,5 | 27,2 a 27,8 | 33,8 a 33,9 |
| | Tarefa 2 | 10 001 | 27,6 a 27,8 | 23,6 a 23,9 | 28,2 a 28,3 |
| | **Tarefa 4** | **2** | **12,7** | **14,8 a 15,0** | **18,7 a 18,8** |
| `cubos1000sombra` | `main` | 2401 | 11,6 a 12,0 | 14,5 a 15,8 | 18,2 a 18,5 |
| | Tarefa 2 | 2401 | 10,4 a 10,9 | 14,5 a 14,6 | 18,3 |
| | **Tarefa 4** | **6** | **2,9** | 13,0 a 13,2 | **17,1 a 17,2** |
| referência | `main` | 7927 | 24,6 a 25,3 | 47,3 a 47,5 | 49,1 a 49,2 |
| | Tarefa 2 | 7927 | 23,7 a 24,1 | 47,2 | 49,0 |
| | **Tarefa 4** | 5485 | 23,1 a 23,6 | 46,8 a 46,9 | 48,5 a 48,6 |

`cubos10000` caiu 45% no quadro desde o `main` (de 30 para 53 FPS), e a GPU também ficou mais rápida (menos
trabalho de driver por draw). A referência quase não muda: os objetos vêm com malhas e materiais intercalados, e
só ordenar (Tarefa 3) junta os lotes; o quadro dela é limitado pela GPU.

**Tarefa 3 (21/10/2026).** A cada quadro, cada item ganha o número do seu material (valores iguais = o mesmo
número, mesmo vindo de objetos diferentes) e da sua chave de sombra (recorte, alfa e textura), e a lista é ordenada
por (shader, material, malha): uma ordem para a cena e outra para as sombras (`DrawOrder.h`, testado no
`test_render`). Os lotes saem dessas ordens. A primeira versão deixava `cubos10000` 5,6 ms mais lento (10 000
consultas em tabela e duas ordenações por quadro); ficou barato com três atalhos: valores iguais aos do item
anterior não consultam a tabela, lista já em ordem não ordena, e as mesmas chaves do quadro anterior reaproveitam a
ordem. A imagem da referência saiu igual. Tarefa 4 contra esta, alternadas, duas rodadas (ms):

| Cena | Versão | Draws | CPU Render | GPU | Quadro |
|---|---|---|---|---|---|
| referência | Tarefa 4 | 5485 | 22,3 a 24,0 | 47,0 a 47,1 | 48,7 a 48,8 |
| | **Tarefa 3** | **16** | **9,8** | **42,1 a 42,2** | **43,9 a 44,0** |
| `cubos10000` | Tarefa 4 | 2 | 12,8 a 12,9 | 14,9 a 15,1 | 18,8 a 18,9 |
| | Tarefa 3 | 2 | 14,3 a 14,6 | 14,7 a 14,8 | 18,5 a 18,6 |
| `cubos1000sombra` | Tarefa 4 | 6 | 3,1 | 13,1 a 13,2 | 17,3 a 17,4 |
| | Tarefa 3 | 6 | 2,8 a 3,2 | 13,1 a 13,3 | 17,3 a 17,5 |

Na fase até aqui, contra o `main`: referência de 7927 para 16 draws, CPU Render de ~25 para 9,8 ms e quadro de
49,4 para 44,0 ms (agora só a GPU limita); `cubos10000` de 33,9 para 18,6 ms no quadro.

**Estudo da Tarefa 5 e Tarefa 8 (21/10/2026).** Antes de montar uma BVH, uma sonda temporária mediu o recorte (ms
por quadro): na referência, 0,86 na cena e 1,82 nas 4 cascatas (as luzes pontuais dela não fazem sombra); em
`cubos10000`, 2,35. Isso dá ~235 ns por item, contra ~10 ns do teste dos 6 planos: o custo era ler o `Material` de
cada item (espalhado na memória, dentro do componente) só para saber se é transparente. Com o sinal guardado no item
no `Submit` (Tarefa 8): referência 0,56 + 1,05, `cubos10000` 1,29. O que resta (~60 ns por item e passada) é o passo
largo do `DrawItem` e a ordem indireta. Uma BVH só cortaria parte disso nas cenas do bench (a câmera vê quase tudo)
e precisaria ser refeita com os 900 corpos que se mexem; a mesma sonda mostrou onde está o resto do Render: as chaves
de material (2,4 ms na referência, 3,4 em `cubos10000`), o buffer de objetos (0,8 e 1,8) e, fora do `EndScene`,
percorrer a cena e calcular as caixas no `Submit` (~6 ms em `cubos10000`). A BVH foi para a alpha 4.

**Tarefas 9 e 10 (21/10/2026).** O `Submit` dá ao item os números do material e da chave de sombra (com o material
ainda no cache) e grava as 2 matrizes do objeto direto no vetor do buffer de objetos; o `DrawItem` perde a matriz
(64 bytes a menos no recorte) e a matriz da normal virou a cofatora (`NormalMatrix`, testada contra a inversa
transposta, inclusive com espelho). `c1f49f4` contra esta, alternadas, duas rodadas (ms; a máquina estava mais lenta
que de manhã nas duas versões):

| Cena | CPU Render antes | depois |
|---|---|---|
| `cubos10000` | 13,8 a 14,1 | **12,6 a 12,8** |
| referência | 8,6 a 8,9 | **8,2 a 8,4** |
| `cubos1000sombra` | 3,2 a 3,3 | 3,0 a 3,1 |

O ganho é menor que a sonda sugeria: ler o material de cada objeto continua custando, só mudou do `EndScene` para o
`Submit`.

**Estudo da Tarefa 11 (21/10/2026).** Com uma sonda e o `Submit` desligado por variável de ambiente (ms por
quadro): em `cubos10000`, percorrer a cena custa 5,5 a 6,1 (~590 ns por entidade) e os 10 000 `Submit` ~4,3
(~430 ns cada); na referência, 1,8 a 2,4 e ~1,7. O tempo não está nas contas (caixa, matrizes), e sim nos saltos de
memória: entidade no heap, vetor de componentes, dados do `MeshRenderer`, malha, material, cada um uma falta de cache
provável. Isso é o arranjo dos dados da cena (Fase 25, Tarefa 11: componentes em blocos contínuos por tipo), não o
renderer. Outra saída, do lado do renderer, é guardar os objetos entre quadros e só reenviar o que mudou (como o GPU
Resident Drawer da Unity): mudança grande, a decidir.

## Limitações

- **Pronto quando, em parte:** a referência ficou em 8,2 a 8,4 ms de CPU no Render (alvo 8) e `cubos10000` em
  ~12,7 (alvo 5). Dos ~12,7, ~10 são percorrer a cena e ler os componentes no `Submit`: vai com a Fase 25,
  Tarefa 11 (componentes em blocos contínuos).
- Recorte com estrutura espacial (Tarefa 5), desenho guiado pela GPU (Tarefa 6) e guardar os objetos entre
  quadros: alpha 4, Fase 28.
- ~~`MaterialParamsOf` calcula o emissivo linear a cada desenho~~: resolvido na Tarefa 2.
- O critério de pronto (`cubos10000` com CPU Render ≤ 5 ms) não sai só do renderer: dos ~12,7 ms, ~10 são
  percorrer a cena e ler os componentes no `Submit` (estudo da Tarefa 11).
- `Submit` não copia mais o material: quem chama precisa mantê-lo vivo e sem mudar até o `EndScene` (a cena e o
  sandbox já fazem; documentado no `Renderer.h`).
- Com 2 draws, `cubos10000` ainda gasta ~14 ms de CPU no Render: é a montagem (percorrer a cena, a caixa no mundo
  de cada objeto, a matriz da normal de todos os itens, o material de cada item virando chave, a cada quadro). O
  quadro já é limitado pela GPU nessa cena, mas na cena de referência é o maior custo de CPU que sobra.
- As chaves são refeitas a cada quadro, mesmo com a cena parada (só a ordenação é reaproveitada). Guardar o número
  do material entre quadros precisaria saber quando um material muda.
- `test_script_project` falhou uma vez no `ctest -j4` (passou sozinho e na rodada seguinte); não tem relação com o
  renderer. Anotado na [entrada](../entrada.md).
- Malha com pele não testada na tela (nenhuma cena do bench tem); os shaders dela compilam ao abrir o Player.
- Boa parte do que sobra de "montar a lista" está fora do renderer: `Scene::SubmitTo` percorre todas as entidades
  (sem a lista por tipo `EntitiesWith<MeshRenderer>`) e o `Submit` calcula a caixa no mundo de cada objeto a cada
  quadro. Anotado na [entrada](../entrada.md) (área da cena).
- Os `Set*` por nome continuam (pós, céu, 2D): são poucas chamadas por quadro.
