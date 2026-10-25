---
tipo: fase
alpha: 3
fase: 24
titulo: GPU — pós, sombras e LOD
status: pronta
prioridade: 2
areas: [renderer, shaders]
branch: fase-24-gpu
inicio: 2026-10-25
fim: 2026-10-25
---

# Fase 24 — GPU: pós, sombras e LOD

## Objetivo

Baixar o custo fixo da GPU (8 ms de pós numa cena vazia em 21/10) e a GPU da cena de referência (47 ms em 21/10:
2,1 milhões de triângulos na Cena HDR e 13 ms de sombras), na máquina de referência (i5-6400 com Intel HD 530, que é
limitada pela banda da memória). Cada mudança foi medida antes com as zonas de GPU (`MOTOR_PROFILE_GPU`, colunas
`gpu ...` do `--perf csv`).

## Tarefas

- [ ] ~~Tarefa 1: Medir numa máquina com GPU dedicada~~ — foi para a Fase 30 (não há uma aqui)
- [x] Tarefa 2: Bloom em meia resolução e com menos níveis — 3ba1148
- [ ] ~~Tarefa 3: Juntar tone mapping e FXAA numa passada só~~ — descartada (ver Feito)
- [x] Tarefa 4: Níveis de qualidade (Baixa, Média, Alta) nas opções do jogador — a93337e
- [x] Tarefa 5: Cascatas de sombra ajustáveis (número e distância) — a93337e
- [x] Tarefa 6: Profundidade de sombra em 16/24 bits — b77a336
- [x] Tarefa 7: Cache das cascatas distantes e dos objetos estáticos — 0ce3a8a
- [ ] ~~Tarefa 8: "Projeta sombra" e "Recebe sombra" no Mesh Renderer~~ — "Recebe sombra" foi para a Fase 30
- [x] Tarefa 9: Sombra de spot com uma face só — eb7249b
- [x] Tarefa 10: LOD por distância e geração de níveis na importação — 8374f53
- [x] Tarefa 11: Zonas de GPU do pós e das sombras — 7571389
- [x] Tarefa 12: Copiar a profundidade do MSAA só quando alguém a lê — fdf668b
- [x] Tarefa 13: Guardar a cena HDR em R11G11B10F — 86bd050
- [x] Tarefa 14: Tirar do lit.frag as leituras de textura dos materiais sem mapas — b33be89
- [ ] ~~Tarefa 15: Gerar níveis de LOD na importação de modelos~~ — foi para a Fase 30

## Pronto quando

A referência com GPU ≤ 14 ms (hoje 47) na sombra Média, e a cena vazia ≤ 5 ms.

## Feito

Player Release, 1280×720 em janela, MSAA 4x, sombra Média, 14 s por cena com `--perf csv`, medianas por quadro do
segundo 4 em diante, máquina parada. Os números de cada linha são do commit dela, contra o anterior.

| Commit | Mudança | Antes | Depois |
|---|---|---|---|
| fdf668b | profundidade do MSAA copiada só para o viewport do editor (wireframes, `KeepSceneDepth`) | vazia: GPU 9,2 ms, quadro 12,6 | GPU 5,5, quadro 8,3 |
| 3ba1148 | primeiro nível do bloom com 4 leituras bilineares e média de Karis, em vez das 13 | bloom 1,18 ms | 0,99 |
| 86bd050 | cena HDR em R11G11B10F (metade da banda do RGBA16F) | vazia: GPU 5,5; referência: pós 7,2, quadro 45 | vazia: **GPU 4,5**, quadro 7,1; referência: pós 3,9, quadro 41 |
| 8374f53 | LOD: `Mesh::SetLods`, escolha pelo tamanho na tela; esfera, cilindro, cápsula e cone com 2 níveis gerados | 2,13 milhões de triângulos, quadro 40,7 | 174 mil, 31,3 |
| b77a336 | profundidade das cascatas em 16 bits, dos cubos em 24 | sombras 8,6 ms, quadro 31,3 | 5,0, 27,5 |
| b33be89 | variante `NO_MAPS` do lit.frag para materiais sem nenhum mapa | Cena HDR 16,7 ms | 13,5 |
| a93337e | filtro das cascatas pela qualidade da direcional (variante `SHADOW_FILTER`) e 2 cascatas na Baixa | Cena HDR 12,7 ms (Alta) | 11,8 (Média) |
| eb7249b | spot: só as faces do cubo que o cone alcança | spot de 40° para baixo: 6 passadas | 1 |
| 0ce3a8a | as 2 cascatas mais longe redesenhadas em quadros alternados | sombras 4,9 ms, quadro ~25 | 2,9, 22,1 |

Detalhes:

- **Sombras pela qualidade da luz:** Baixa = 2 cascatas e 1 leitura com comparação (2×2 texels pelo filtro linear);
  Média = 4 cascatas e 4 leituras a meio texel, sem mistura entre cascatas; Alta = 4 cascatas, 9 leituras e mistura
  na divisa (como antes). As opções do jogador (sombras Baixas/Médias/Altas) já limitavam a qualidade de cada luz,
  então os níveis valem lá também. A distância das sombras já era ajustável (`Environment::shadowDistance`).
- **Variantes em vez de `if` pela uniform:** no driver da Intel o `if` não evita o custo. Ler as texturas brancas de
  1×1 dos mapas que faltam custava ~4 ms por quadro na referência, e o filtro das cascatas escolhido pela uniform
  deixou a Cena HDR ~3 ms mais lenta que qualquer um dos três filtros fixos. O lit.frag ganhou `NO_MAPS` e
  `SHADOW_FILTER`, recompiladas só quando a qualidade da direcional muda.
- **Cascatas reaproveitadas:** as duas mais longe são desenhadas com a esfera 15% maior. No quadro seguinte, se o
  pedaço da câmera ainda cabe nela (`CascadeCovers`), fica o mapa já desenhado. Câmera que andou demais, outra câmera
  (o editor desenha a cena e o jogo com o mesmo renderer) ou outra luz: tudo desenhado de novo.
- **Medição com ruído:** entre rodadas iguais a Cena HDR variou de 11,0 a 13,6 ms (o relógio da GPU integrada
  divide a energia com a CPU). Os números de mudanças pequenas foram tirados alternando as variantes, três rodadas
  cada; a primeira leitura do filtro (13,5 → 11,2 ms, no commit a93337e) era desse ruído.
- **Descartado:** cena em MSAA numa textura e tone mapping por amostra, sem o resolve (pós 4,47 → 4,76 ms, mais
  lento); 1 leitura de sombra na qualidade Média (mesmo custo da mistura, serrilhado); Tarefa 3, juntar tone mapping
  e FXAA (o FXAA precisaria refazer o bloom e o tone mapping em cada uma das suas leituras; o que sobraria é uma
  gravação de tela inteira, ~0,3 ms nesta máquina).

Onde ficou a GPU da referência (Média):

| Zona | 21/10 | Agora |
|---|---|---|
| Sombras | 13 ms | 2,9 |
| Cena HDR | ~26 | 11,0 a 12,0 |
| Pós-processamento | 8 | 3,9 (resolve 1,2, bloom 0,9, tone mapping 0,8, FXAA 1,0) |
| **Total** | **47** | **19,5** |

Medição final (0ce3a8a, máquina parada): referência com quadro **22,1 ms** (44 no fim da Fase 23) e física 3,8 ms por
quadro (8,1 no fim da Fase 25: agora 1 passo fixo por quadro em vez de 3). Contra a Unity (`comparar.ps1`, uma rodada),
todas as cenas do conjunto ficaram de 157 a 290% da Unity (tabela no `bench/README.md`). O `test_desempenho` não mede
nada desta fase; duas de oito rodadas passaram do limite em medidas de cena (`cena_ler`, `cena_filhos`) por ruído, e a
base ficou como estava.

Pronto quando:

- Cena vazia ≤ 5 ms de GPU: **sim**, 4,4.
- Referência ≤ 14 ms de GPU: **não**, 19,5. O que sobra na Cena HDR são as 9 luzes pontuais (cada pixel do chão
  passa por várias, ~7 ms) e a amostragem da sombra (~2 ms). Os próximos passos são luzes por blocos da tela
  (clustered) e menos MSAA na qualidade Média, que mudam a cara do jogo: ficaram para a Fase 30.

## Limitações

- "Recebe sombra" no Mesh Renderer não foi feito ("Projeta sombra" já existia): precisa de um bit por instância e
  de outra variante do lit.frag. Fase 30.
- Níveis de LOD só para as primitivas; modelos importados ficam sem (gerar níveis precisa de um simplificador de
  malha, uma biblioteca nova). Fase 30.
- Na qualidade Média aparece um degrau na divisa entre duas cascatas (sem a mistura da Alta).
- Nas duas cascatas mais longe, objetos que se mexem atrasam um quadro.
- Trocar a qualidade da direcional recompila as três variantes do lit.frag (uma vez, ao abrir a cena ou mudar as
  opções).
- Sem medição numa GPU dedicada.
