---
tipo: alpha
alpha: 3
status: em andamento
foco: Desempenho
---

# Alpha 3 — Desempenho

> **Replanejada em 21/10/2026** ([decisão](../decisoes/2026-10-21-replanejamento.md)), a partir da medição geral
> de 21/10 (abaixo). A alpha 3 ficou só com desempenho; ambiente e renderização 2 foram para a
> [alpha 4](alpha-4.md), ferramentas para a [alpha 5](alpha-5.md). As Fases 22 e 25 mantêm o número (já há commits
> e branch com ele); as outras foram refeitas.

Foco: o Motor rodar bem em máquinas modestas (referência: i5 de 4 núcleos com Intel HD 530) com cenas de milhares
de objetos, e usar os 4 núcleos em vez de um.

**Regra da alpha:** medir antes de otimizar. Cada fase começa medindo a cena de referência e as cenas do `bench/`
e termina com os números de antes e depois aqui e no `bench/README.md`.

**Pronta quando:**

- a cena de referência (`bench/referencia`) roda a **60 FPS** no Player, sombra Média, na máquina de referência
  (hoje 49 ms, 20 FPS);
- `cubos10000` roda a **60 FPS** (hoje 33,6 ms, 30 FPS);
- todas as cenas do conjunto de comparação continuam com **pelo menos 70% do FPS da Unity**, nenhuma abaixo de 50%;
- o quadro **usa mais de um núcleo**: com 10 000 objetos, a thread principal abaixo de 50% de um núcleo e o trabalho
  de montar o quadro dividido entre as threads;
- o editor edita a cena de referência (~5000 entidades) **sem travar** (nenhum quadro acima de 100 ms parado).

## Fases

| # | Fase | Status | Por quê | Pode rodar junto com |
|---|---|---|---|---|
| — | Fase 22 — Medição | pronta (20/10) | Tracy, `--perf`, cena de referência, `test_desempenho` | — |
| — | [Fase 23 — Renderer em lote](alpha-3/fase-23-renderer-em-lote.md) | pronta (21/10) | `cubos10000`: 10 001 → 2 draws, Render 33 → 12,7 ms; referência 7927 → 16 draws, quadro 49 → 44 ms | — |
| — | [Fase 24 — GPU: pós, sombras e LOD](alpha-3/fase-24-gpu.md) | pronta (25/10) | GPU da cena vazia 9,2 → 4,4 ms, da referência 47 → ~19,5 ms (quadro 44 → 22 ms); todas as cenas do bench de 157 a 290% da Unity | — |
| — | [Fase 25 — Cena, dados e física](alpha-3/fase-25-cena.md) | pronta (25/10) | `fisica1000` 84 → 104% da Unity, `jogo_passo` 1,37 → 0,77 ms, filhos e nomes sem percorrer a cena; física da referência 8,1 ms (3 passos por quadro, espera a Fase 24) | — |
| — | [Fase 26 — Várias threads](alpha-3/fase-26-threads.md) | pronta (25/10) | `cubos10000`: thread principal ~80 → 46–50% de um núcleo; referência 23,3 → 22,6 ms; texturas da cena decodificadas em paralelo (−22% na carga) | — |
| — | [Fase 27 — Editor que escala](alpha-3/fase-27-editor.md) | pronta (25/10) | referência no editor: quadros acima de 100 ms parado 37 em 20 s → 0; CPU por quadro ~21 → ~10,5 ms; JSON 255 → 31 ms; salvar ainda 90–157 ms | — |
| — | [Organização do código 2](alpha-3/organizacao-do-codigo-2.md) | em andamento | `SetError` em 14 arquivos, leitura de arquivo em 8, `DrawScene` com 186 linhas; sem mudar comportamento | tarefas 1 a 6 e 10 feitas, menos o `Input.cpp` e o `DrawControlsWindow` (esperam o `entrada-base`) |

## Ordem e dependências

```
Fase 22 (pronta) ─┬─ Fase 23 Renderer em lote ──┐
                  ├─ Fase 24 GPU                 ├─ Fase 26 Várias threads
                  ├─ Fase 25 Cena, dados, física ┘
                  └─ Fase 27 Editor que escala (independente)
```

A 26 vem depois da 23 e da 25: dividir o quadro entre núcleos só compensa com a lista de desenho montada em lotes
e os dados da cena guardados por tipo (sem travas). A 23 e a 24 mexem no `renderer/`: podem correr juntas se a 24
ficar nos shaders, no pós e nas sombras e a 23 na montagem e no envio dos desenhos.

## Medição geral (21/10/2026)

Player Release (commit 39ef6fc), 1280×720 em janela, MSAA 4x, vsync desligado, 14 s por cena com `--perf`, medianas
por quadro do segundo 4 em diante. CPU por thread amostrada pelo sistema entre os segundos 6 e 12. Máquina de
referência.

| Cena | Quadro | CPU Render | Física | Swap (espera da GPU) | GPU total | GPU Cena HDR | GPU Sombras | GPU Pós | Draws |
|---|---|---|---|---|---|---|---|---|---|
| vazia | 12,8 ms | 2,1 | — | 10,0 | 9,2 | 0,9 | — | 8,2 | 0 |
| cubos1000 | 15,2 ms | 8,5 | — | 5,6 | 11,1 | 2,8 | — | 8,1 | 1001 |
| cubos10000 | **33,6 ms** | **33,1** | — | 0,4 | 26,6 | 17,5 | — | 8,9 | 10 001 |
| fisica1000 | 15,6 ms | 8,0 | 1,6 | 2,4 | 11,6 | 2,8 | — | 8,6 | 1001 |
| **referência** | **49,1 ms** | **28,3** | **11,6** | 8,8 | **47,3** | 26,9 | 13,1 | 6,6 | 7927 (3372 de sombra) |

Threads (CPU de cada thread do Player, em % de um núcleo):

| Cena       | Threads no processo | Trabalhando (≥ 1%) | Thread principal | Outras                       |
| ---------- | ------------------- | ------------------ | ---------------- | ---------------------------- |
| vazia      | 10                  | 1                  | 31%              | —                            |
| cubos1000  | 10                  | 1                  | 62%              | —                            |
| cubos10000 | 10                  | 1                  | **92%**          | —                            |
| fisica1000 | 13                  | 4                  | 76%              | 3 workers da Jolt, ~28% cada |
| referência | 13                  | 4                  | 82%              | 3 workers da Jolt, ~11% cada |

Conclusões:

- **A CPU do render é o maior custo** assim que a cena cresce: ~3,3 µs por draw, um draw por objeto. Com 10 000
  cubos ela sozinha passa de 33 ms e a GPU fica esperando (Swap de 0,4 ms). Fase 23.
- **Uma thread só:** render, scripts, UI, áudio e o passo do jogo rodam na thread principal. Só a física usa outros
  núcleos (o pool da Jolt, núcleos − 1, `PhysicsWorld.cpp:53`). Com 10 000 cubos o processo usa 23% da máquina.
  Fase 26.
- **Custo fixo da GPU:** a cena vazia gasta 8 ms só no pós-processamento (HDR com MSAA 4x, bloom, FXAA), metade do
  orçamento de 60 FPS. Fase 24.
- **Cena de referência limitada pelos dois lados:** GPU 47 ms (2,1 milhões de triângulos na Cena HDR, 13 ms de
  sombras) e CPU 28 ms de render + 11,6 ms de física. A física é cara porque o quadro lento pede 3 a 4 passos fixos
  (em `fisica1000`, com quadro rápido, custa 1,6 ms). Fases 23, 24 e 25.
- **Barato:** scripts, UI, áudio e eventos ficam abaixo de 0,4 ms em todas as cenas.

Medições anteriores (18/10 e 20/10, comparação com a Unity e linha de base da Fase 22): `bench/README.md` e o
histórico deste arquivo no git.

## Fase 22 — Medição

**Pronta (20/10/2026).** Profiler [Tracy](../decisoes/2026-10-20-profiler-tracy.md) (`build-tracy`), contadores por
quadro (CPU por sistema, GPU por passada, draws, memória de vídeo) no viewport, no MCP e no Player (`--perf`), cena
de referência em `bench/referencia`, `test_desempenho` no `ctest` (rótulo `desempenho`). Três achados do Tracy
(`glDrawRangeElements`, lista de scripts, layout da UI só com a UI) levaram todas as cenas acima de 70% da Unity
(`cubos10000` de 68 para 98–100%).

Pendências que foram para outras fases: medir numa máquina com GPU dedicada (Fase 24), threads e scripts no Tracy
(Fase 26).

## Fase 23 — Renderer em lote

**Pronta (21/10/2026):** [nota da fase](alpha-3/fase-23-renderer-em-lote.md). Lotes instanciados na cena e nas
sombras, ordenados por shader, material e malha. `cubos10000` de 10 001 para 2 draws (CPU Render 33,3 → 12,7 ms,
quadro 33,9 → 18,6); referência de 7927 para 16 draws (Render ~25 → 8,3, quadro 49,4 → 44,0, agora limitada pela
GPU). O que sobra de CPU é percorrer a cena (Fase 25); recorte espacial e desenho guiado pela GPU foram para a
alpha 4.

## Fase 24 — GPU: pós, sombras e LOD

**Pronta (25/10/2026):** [nota da fase](alpha-3/fase-24-gpu.md). Profundidade do MSAA copiada só quando alguém lê,
cena HDR em R11G11B10F, bloom mais barato, LOD por distância (primitivas com níveis gerados), sombras em 16/24 bits,
filtro e número de cascatas pela qualidade, cascatas distantes reaproveitadas, spot só nas faces que o cone alcança,
lit.frag sem leituras de mapas que não existem. Cena vazia: GPU 4,4 ms (meta ≤ 5). Referência: GPU ~19,5 ms (meta
≤ 14, não atingida: luzes pontuais e MSAA, Fase 30), quadro 44 → 22 ms, física 8,1 → 3,8 ms por quadro.

## Fase 25 — Cena, dados e física

**Pronta (25/10/2026):** [nota da fase](alpha-3/fase-25-cena.md). Passos fixos limitados e interpolados, física
sincronizando só o que mudou, índices de filhos, nomes e tipos na `Scene`. Todas as cenas do bench acima de 100% da
Unity. Falta a física da referência ≤ 3 ms por quadro, que depende do quadro mais rápido (Fase 24).

## Fase 26 — Várias threads

**Pronta (25/10/2026):** [nota da fase](alpha-3/fase-26-threads.md). Sistema de tarefas único (`JobSystem`, dividido
com a Jolt), montagem do quadro, sombras, animação e envio da cena em paralelo, texturas da cena decodificadas em
paralelo, scripts com nome no Tracy. `cubos10000`: thread principal ~80% → 46–50% de um núcleo, quadro igual
(~15 ms); referência: quadro 23,3 → 22,6 ms, thread principal 50%. Nesta máquina a GPU integrada limita o quadro;
thread de render e componentes em blocos contínuos ficaram para depois.

## Fase 27 — Editor que escala

**Pronta (25/10/2026):** [nota da fase](alpha-3/fase-27-editor.md). "Não salvo" pela versão do desfazer (sem gravar
a cena em JSON a cada 0,5 s), desfazer guardando só as entidades que mudaram e comparando a cena só depois de
entrada, JSON escrito direto no texto (mesmo arquivo), Hierarquia como lista virtual, arquivos do projeto relidos só
quando a pasta muda. Referência no editor Release: parado, 0 quadros acima de 100 ms (antes 37 em 20 s) e ~10,5 ms
de CPU por quadro (o quadro fica em ~30 ms pela GPU do viewport); editando pelo MCP, só o salvar passa de 100 ms
(90–157 ms, antes ~460).
