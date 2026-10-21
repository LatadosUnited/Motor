---
tipo: decisao
data: 2026-10-21
status: aceita
fases: [23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33]
---

# Replanejamento das alphas 3 a 5

## Contexto

O usuário apagou o roteiro para replanejar. Na mesma data, uma medição geral do Player Release (cena vazia, 1000 e
10 000 cubos, física, cena de referência; CPU por thread) mostrou: a CPU do render é o maior custo com muitos
objetos (33 ms para 10 000 draws); quase tudo roda numa thread só (só a Jolt usa os outros núcleos); o pós-
processamento custa 8 ms de GPU numa cena vazia; e a física da cena de referência custa 11,6 ms porque o quadro
lento pede vários passos fixos. Números em [alpha-3.md](../roadmap/alpha-3.md), "Medição geral".

Substitui, na parte das alphas 3 e 4, a [divisão das alphas](2026-10-17-divisao-das-alphas.md).

## Decisão

- **Alpha 3 só com desempenho** (Fases 22 a 27), na ordem do que a medição mostrou: renderer em lote (23), GPU (24),
  cena e física (25, em andamento), **várias threads como fase própria** (26), editor que escala (27).
- **Alpha 4 — mundo aberto e renderização 2** (Fases 28 a 30): terreno e água, vegetação e partículas, SSAO e probes.
- **Alpha 5 — ferramentas** (Fases 31 a 33): compilador integrado, distribuição, scripts avançados.
- As Fases 22 e 25 mantêm o número (já há commits e o branch `fase-25-cena`).

## Alternativas

- Manter ambiente e renderização 2 na alpha 3: deixaria a alpha longa e com metas misturadas; o ambiente depende
  de instancing, LOD e threads, que vêm antes de qualquer forma.
- Threads dentro da Fase 25: a fase já estava grande, e a medição mostrou que é um assunto próprio (sistema de
  tarefas, thread de render, carregamento em segundo plano).
- Ferramentas antes do mundo aberto: não é a prioridade enquanto só o usuário usa o Motor; pode subir se isso mudar.

## Consequências

Os números de fase de 26 em diante mudaram: notas, commits e conversas antigas que citam "Fase 26 (editor)" ou
"Fase 29 (compilador)" falam das fases antigas (hoje 27 e 31). O editor instalado continua dependendo do MSYS2 até
a alpha 5.
