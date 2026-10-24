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
| 2 | Fase 24 — GPU: pós, sombras e LOD | planejada | 8 ms de pós numa cena vazia; 47 ms de GPU na referência | 25, 27 |
| 3 | [Fase 25 — Cena, dados e física](alpha-3/fase-25-cena.md) | em andamento | física de 11,6 ms na referência (passos fixos acumulados) | 23, 24, 27 |
| 4 | Fase 26 — Várias threads | planejada | tudo menos a física roda numa thread; 3 núcleos parados | 27 |
| 5 | Fase 27 — Editor que escala | planejada | gravar a referência em JSON leva 337 ms, a cada 0,5 s | 23, 24, 25, 26 |
| — | [Organização do código 2](alpha-3/organizacao-do-codigo-2.md) | planejada | `SetError` em 14 arquivos, leitura de arquivo em 8, `DrawScene` com 186 linhas; sem mudar comportamento | tarefas 1 a 4 já; a 5 depois da 26 e fora da 24 |

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

Objetivo: baixar o custo fixo da GPU (8 ms de pós numa cena vazia) e os 47 ms de GPU da referência.

- [ ] Tarefa 1: Medir numa máquina com GPU dedicada
- [ ] Tarefa 2: Bloom em meia resolução e com menos níveis
- [ ] Tarefa 3: Juntar tone mapping e FXAA numa passada só
- [ ] Tarefa 4: Níveis de qualidade (Baixa, Média, Alta) nas opções do jogador
- [ ] Tarefa 5: Cascatas de sombra ajustáveis (número e distância)
- [ ] Tarefa 6: Profundidade de sombra em 16/24 bits
- [ ] Tarefa 7: Cache das cascatas distantes e dos objetos estáticos
- [ ] Tarefa 8: "Projeta sombra" e "Recebe sombra" no Mesh Renderer
- [ ] Tarefa 9: Sombra de spot com uma face só
- [ ] Tarefa 10: LOD por distância e geração de níveis na importação

**Pronto quando:** a referência com GPU ≤ 14 ms (hoje 47) na sombra Média, e a cena vazia ≤ 5 ms.

## Fase 25 — Cena, dados e física

**Em andamento:** tarefas na [nota da fase](alpha-3/fase-25-cena.md).

## Fase 26 — Várias threads

Objetivo: usar os 4 núcleos; hoje só a física sai da thread principal (92% de um núcleo com 10 000 cubos).

- [ ] Tarefa 1: Criar o sistema de tarefas (job system), dividido com a Jolt
- [ ] Tarefa 2: Paralelizar a montagem do quadro (matrizes, recorte, lotes)
- [ ] Tarefa 3: Paralelizar a sincronização da física e a animação
- [ ] Tarefa 4: Estudar uma thread só para o render
- [ ] Tarefa 5: Carregar texturas, malhas e sons em segundo plano
- [ ] Tarefa 6: Documentar que os scripts continuam numa thread só
- [ ] Tarefa 7: Mostrar as threads e os scripts no Tracy

**Pronto quando:** em `cubos10000` e na referência, a thread principal abaixo de 50% de um núcleo e o quadro mais
rápido; `ctest` repetido sem corrida de dados.

## Fase 27 — Editor que escala

Objetivo: editar a cena de referência (~5000 entidades) sem travar.

- [ ] Tarefa 1: Desfazer por diferenças, sem comparar a cena inteira
- [ ] Tarefa 2: Checar "não salvo" sem gravar a cena em JSON a cada 0,5 s
- [ ] Tarefa 3: Acelerar gravar e ler a cena em JSON (hoje ~330 ms)
- [ ] Tarefa 4: Cortar os 6 a 7 ms de CPU do ImGui
- [ ] Tarefa 5: Lista virtual na Hierarquia com milhares de entidades
- [ ] Tarefa 6: Corrigir o FPS por baixo dos botões de Play no viewport estreito

**Pronto quando:** com a referência aberta no editor Release, nenhum quadro acima de 100 ms e o editor parado
abaixo de 16 ms.
