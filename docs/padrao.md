---
tipo: guia
atualizado: 2026-10-17
---

# Padrão da documentação

Como organizar a pasta `docs/` (o vault do Obsidian) para que pessoas e agentes achem e atualizem tudo do
mesmo jeito. Vale para notas novas; quem mexer numa nota antiga aproveita e a ajusta.

## 1. Pastas

```
docs/
  inicio.md                  nota inicial (links para tudo)
  padrao.md                  este padrão
  scripts.md                 guia de scripts        ┐
  scripts-api.md             referência da API      ├ documentação de uso
  examples/                  exemplos .cpp testados ┘
  roadmap/
    alpha-2.md               índice da alpha: objetivo, prioridades, links para as fases
    alpha-2/                 uma nota por fase: fase-16-robustez-e-correcoes.md ...
    alpha-3.md, alpha-4.md   rascunhos (um arquivo só até a alpha começar)
    alpha-1.md               histórico (alpha fechada: não se edita)
    entrada.md               caixa de entrada: achados ainda não colocados numa fase
    fases.base               tabela das fases (Obsidian Bases)
  decisoes/                  uma nota por decisão: 2026-10-17-hub-fora-da-engine.md
  modelos/                   modelos do Obsidian (não são documentação)
```

O `README.md` e o `CLAUDE.md` continuam na raiz do repositório (fora do vault); o `ROADMAP.md` da raiz só
aponta para `docs/roadmap/`.

## 2. Nomes de arquivo

- Minúsculas, sem acento, palavras separadas por hífen: `fase-17-scripts.md`, `2026-10-17-instalador-proprio.md`.
  O título bonito, com acentos, vai no `# Título` da nota.
- Fases: `fase-<número>-<assunto>.md`. Decisões: `<AAAA-MM-DD>-<assunto>.md`.
- Motivo: links Markdown com espaço ou acento precisam de codificação (`%20`) e quebram fácil; nomes simples
  funcionam igual no Obsidian, no GitHub, no terminal e para os agentes.

## 3. Links

- **Markdown com caminho relativo:** `[Fase 17](alpha-2/fase-17-scripts.md)`. Nada de `[[wikilinks]]` (não
  funcionam no GitHub nem para os agentes). O vault já está configurado assim.
- Código e arquivos fora de `docs/` vão entre crases, sem link: `engine/src/scene/SceneRuntime.cpp`,
  `PhysicsSystem.cpp:566`. (Links para fora do vault não abrem no Obsidian.)

## 4. Propriedades (o bloco `---` no topo)

Toda nota de roteiro e de decisão começa com propriedades; é delas que a tabela `fases.base` e as buscas do
Obsidian tiram as colunas.

| Tipo | Propriedades |
|---|---|
| `fase` | `tipo`, `alpha`, `fase` (número), `titulo`, `status`, `prioridade`, `areas` (lista), `branch`, `inicio`, `fim` |
| `alpha` (índice) | `tipo`, `alpha`, `status`, `foco` |
| `decisao` | `tipo`, `data`, `status`, `fases` (lista de números relacionados) |
| `guia` | `tipo`, `atualizado` |
| `entrada` | `tipo` |

Valores de `status`:

- fase e alpha: `planejada`, `em andamento`, `pronta`, `adiada`, `fechada` (só alpha);
- decisão: `proposta`, `aceita`, `substituida` (com link para a que a substituiu no texto).

Datas nas propriedades e nos nomes: `AAAA-MM-DD`. No texto corrido: `17/10/2026`.

## 5. Nota de fase

Seções, nesta ordem (modelo em `modelos/fase.md`):

1. **Objetivo:** um parágrafo; por que a fase existe.
2. **Tarefas:** caixas de seleção, agrupadas por assunto com subtítulos `###` se forem muitas.
   - `- [ ] O que fazer. *(origem: Fase 6, limitação)*` — a **origem** diz de onde veio: fase da alpha anterior,
     `entrada.md`, análise do código, pedido do usuário.
   - Feita: `- [x] ... (abc1234)` com o commit curto no fim.
   - Desistiu ou mudou de fase: risque (`~~texto~~`) e diga para onde foi.
3. **Pronto quando:** o critério verificável (teste, verificação pelo MCP, número medido).
4. **Feito** (no fim da fase): resumo do que mudou, arquivos principais, testes, números de antes e depois.
5. **Limitações** (no fim da fase): o que ficou para trás; cada uma vira tarefa noutra fase ou linha em
   `entrada.md`.

## 6. Fluxo

1. **Achou algo** (defeito, limitação, ideia), seja pessoa ou agente: uma linha em `roadmap/entrada.md` com
   data, quem achou, o quê e onde (arquivo:linha). Não precisa decidir a fase na hora.
2. **Triagem** (o usuário, ou um agente quando pedido): cada linha da entrada vira tarefa de uma fase (com a
   origem) ou é descartada com o motivo; a linha sai da entrada.
3. **A fase começa:** `status: em andamento`, `branch`, `inicio`; o índice da alpha atualiza a linha dela.
4. **Durante a fase:** marque as tarefas com o commit; tarefa nova que surgir entra na própria fase.
5. **A fase termina:** `status: pronta`, `fim`, seções **Feito** e **Limitações**; tarefas não feitas mudam de
   fase (riscadas aqui, com o destino) ou voltam para a entrada.
6. **Decisão que muda o rumo** (biblioteca, arquitetura, ordem das alphas): uma nota em `decisoes/` (modelo
   `modelos/decisao.md`) e o link para ela na fase.

## 7. Alphas

- **Alpha futura:** um arquivo só (`alpha-3.md`), rascunho, com as fases como seções.
- **Alpha que começa:** o arquivo vira índice (`tipo: alpha`) e cada fase ganha a sua nota em `alpha-<n>/`.
- **Alpha que fecha:** `status: fechada` no índice; as notas das fases ficam como estão (histórico). A
  numeração das fases continua de uma alpha para a outra.

## 8. Quem edita o quê

- O agente de uma fase edita a **nota da sua fase** à vontade e, no índice da alpha, **só a linha dela**
  (ponto de coordenação, como no `CLAUDE.md`).
- `entrada.md`: todos acrescentam no fim; ninguém reescreve linhas dos outros.
- `padrao.md`, os índices e as decisões aceitas: mudam só com o pedido do usuário.
- Língua: português, como o resto do projeto.
