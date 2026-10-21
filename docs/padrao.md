---
tipo: guia
atualizado: 2026-10-21
---

# Padrão da documentação

Regras da pasta `docs/` (o vault do Obsidian), para pessoas e agentes acharem e atualizarem tudo do mesmo jeito.

## Pastas

```
docs/
  inicio.md, padrao.md       nota inicial e este padrão
  scripts.md, scripts-api.md guia e referência dos scripts (exemplos testados em examples/)
  roadmap/
    alpha-<n>.md             índice da alpha atual; as futuras são rascunho num arquivo só
    alpha-<n>/               uma nota por fase em andamento
    entrada.md               achados que ainda não estão numa fase
    fases.base               tabela das fases (Obsidian Bases)
  decisoes/                  uma nota por decisão
  modelos/                   modelos de nota (fase, alpha, decisão)
```

`README.md` e `CLAUDE.md` ficam na raiz do repositório; o `ROADMAP.md` da raiz só aponta para `docs/roadmap/`.

## Escrita

- **Nomes de arquivo:** minúsculas, sem acento, com hífen: `fase-25-cena.md`, `2026-10-21-replanejamento.md`.
  O título com acentos vai no `# Título`.
- **Links:** Markdown relativo, `[Fase 25](alpha-3/fase-25-cena.md)`; nada de `[[wikilinks]]`. Código e arquivos
  fora de `docs/` vão entre crases, sem link: `engine/src/scene/SceneRuntime.cpp:120`.
- **Datas:** `AAAA-MM-DD` em nomes e propriedades, `21/10/2026` no texto.
- **Língua:** português.

## Propriedades (bloco `---` no topo)

Toda nota tem `tipo` (a primeira coluna; a `entrada.md` tem só ele) e mais estas:

| Tipo | Propriedades | `status` |
|---|---|---|
| `fase` | `alpha`, `fase`, `titulo`, `status`, `prioridade`, `areas`, `branch`, `inicio`, `fim` | `planejada`, `em andamento`, `pronta`, `adiada` |
| `alpha` | `alpha`, `status`, `foco` | os da fase e `fechada` |
| `decisao` | `data`, `status`, `fases` | `proposta`, `aceita`, `substituida` |
| `guia` | `atualizado` | — |

## Nota de fase

Seções (modelo em `modelos/fase.md`): **Objetivo**, **Tarefas**, **Pronto quando** (critério verificável: teste,
MCP ou número medido) e, ao terminar, **Feito** (números de antes e depois) e **Limitações**.

## Tarefas

Uma linha por tarefa, numerada dentro da fase, começando com um verbo:

```
- [ ] Tarefa 1: Paralelizar a montagem do quadro
- [ ] Tarefa 2: Limitar os passos fixos da física por quadro
- [x] Tarefa 3: Cache da matriz de mundo — b32086e
- [ ] ~~Tarefa 4: Thread de render~~ — foi para a Fase 30
```

- Feita: marque `[x]` e ponha o commit no fim.
- Mudou de fase ou desistiu: risque e diga para onde foi.
- O número não muda nem é reaproveitado; tarefa nova entra no fim.
- Sem explicação na linha: o porquê fica no Objetivo da fase, os números no Feito.

## Fluxo

1. **Achou algo:** uma linha no fim de `entrada.md` (data, quem, o quê, onde). A triagem leva para uma fase ou
   descarta.
2. **Fase começa:** nota em `alpha-<n>/`, `status: em andamento`, `branch`, `inicio`.
3. **Fase termina:** `status: pronta`, `fim`, Feito e Limitações; o que sobrou vai para outra fase ou para a
   entrada. Com o resumo já no índice da alpha, a nota pode ser apagada (fica no git).
4. **Alpha fecha:** índice e notas apagados (fica no git); a numeração das fases continua.
5. **Decisão de rumo** (biblioteca, arquitetura, ordem das alphas): nota em `decisoes/`, com link na fase.

## Quem edita o quê

- O agente de uma fase edita a nota dela e, no índice da alpha, só a linha dela.
- `entrada.md`: todos acrescentam no fim.
- Este padrão, os índices e as decisões aceitas: só com pedido do usuário.
