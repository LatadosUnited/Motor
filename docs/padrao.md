---
tipo: guia
atualizado: 2026-10-21
---

# Padrão da documentação

Como organizar a pasta `docs/` (o vault do Obsidian) para que pessoas e agentes achem e atualizem tudo do
mesmo jeito. O desenvolvimento é dividido em quatro módulos: Engine, Editor, Hub e Instalador
([decisão](decisoes/2026-10-21-desenvolvimento-por-modulo.md)).

## 1. Pastas

```
docs/
  inicio.md          nota inicial: os módulos e onde ler cada coisa
  visao.md           o objetivo do projeto e o que é cada módulo
  padrao.md          este padrão
  entrada.md         caixa de entrada: achados ainda não colocados num roteiro
  engine/            estado.md (o que faz hoje) e roteiro.md (o que vem)   ┐
  editor/            (o mesmo)                                              │ um par por
  hub/               (o mesmo)                                              │ módulo
  instalador/        (o mesmo)                                              ┘
  decisoes/          uma nota por decisão: 2026-10-21-desenvolvimento-por-modulo.md
  scripts.md         guia de scripts        ┐ documentação de uso; vai junto com cada versão
  scripts-api.md     referência da API      │ (motor_package) e é conferida pelo test_script_docs:
  examples/          exemplos .cpp testados ┘ não mude os caminhos
  teste-manual.md    roteiro de teste à mão
  modelos/           modelos do Obsidian (não são documentação)
  arquivo/           roteiro antigo por alphas (Fases 0 a 31): só leitura
```

Fora do vault: o `README.md` e o `CLAUDE.md` da raiz (curtos, com o que é comum) e um `README.md` e um
`CLAUDE.md` na pasta de cada módulo (`engine/`, `editor/`, `hub/`, `installer/`), com o uso e as regras dele.

## 2. Nomes de arquivo

- Minúsculas, sem acento, palavras separadas por hífen. O título bonito, com acentos, vai no `# Título`.
- Decisões: `<AAAA-MM-DD>-<assunto>.md`.
- Motivo: links com espaço ou acento precisam de `%20` e quebram fácil; nomes simples funcionam igual no
  Obsidian, no GitHub, no terminal e para os agentes.

## 3. Links

- **Markdown com caminho relativo:** `[roteiro do Hub](hub/roteiro.md)`. Nada de `[[wikilinks]]`.
- Código e arquivos fora de `docs/` vão entre crases, sem link: `engine/src/scene/SceneRuntime.cpp`. (Links para
  fora do vault não abrem no Obsidian.)

## 4. Propriedades (o bloco `---` no topo)

| Tipo | Propriedades |
|---|---|
| `estado` | `tipo`, `modulo`, `versao`, `atualizado` |
| `roteiro` | `tipo`, `modulo`, `atualizado` |
| `decisao` | `tipo`, `data`, `status`, `modulos` (lista) |
| `guia` | `tipo`, `atualizado` |
| `entrada` | `tipo` |

`status` de decisão: `proposta`, `aceita`, `substituida` (com link para a que a substituiu). Datas nas
propriedades e nos nomes: `AAAA-MM-DD`; no texto corrido: `21/10/2026`.

## 5. Estado e roteiro de um módulo

**`estado.md`** — o retrato de hoje, curto (uma tela ou duas): o que o módulo é e onde está o código; o que faz
hoje; números medidos, se houver; limitações conhecidas; links para o histórico. Atualize quando algo entra
ou sai de verdade, não a cada commit.

**`roteiro.md`** — o que vem, em blocos:

- **Agora:** o que está em andamento (poucos itens; idealmente um assunto por vez).
- **Próximo:** o que vem a seguir, em ordem.
- **Depois:** o que já se sabe que precisa, sem ordem firme.
- **Ideias** (opcional): sem data.

Cada tarefa é uma linha: `- [ ] O que fazer. *(origem)*`. A origem diz de onde veio: uma fase antiga
(`Fase 23`), `entrada.md`, uma medição, um pedido do usuário. Detalhe que não cabe numa linha vai num link (uma
nota do arquivo, uma decisão) ou numa nota nova ao lado do roteiro. **Feito sai do roteiro:** o histórico fica
no git e no `CHANGELOG.md` do módulo (na pasta do código), com o commit.

Trabalho que cruza módulos (formato do `motor-version.json`, o pacote "Compilador") entra no roteiro de cada
módulo envolvido, com link entre eles.

## 6. Fluxo

1. **Achou algo** (defeito, limitação, ideia): uma linha no fim de `entrada.md` com data, quem achou, módulo,
   o quê e onde.
2. **Triagem** (o usuário, ou um agente quando pedido): cada linha vira tarefa no roteiro de um módulo (com a
   origem) ou é descartada com o motivo; a linha sai da entrada.
3. **Começar uma tarefa:** ela sobe para "Agora"; branch com o prefixo do módulo (`engine/...`, `editor/...`,
   `hub/...`, `instalador/...`, `docs/...`).
4. **Terminar:** a linha sai do roteiro; o que mudou vai para o `CHANGELOG.md` do módulo; o `estado.md` muda se
   o módulo passou a fazer algo novo ou perdeu uma limitação; o que ficou para trás vira linha no roteiro.
5. **Decisão que muda o rumo** (biblioteca, arquitetura, formato): uma nota em `decisoes/` (modelo
   `modelos/decisao.md`) com o link no roteiro.

## 7. Versões

Cada módulo tem a sua versão (Engine e Editor 0.2.650 vindas do `project()` do CMake; Hub 0.2.0 em
`hub/CMakeLists.txt`, que o instalador leva; servidor 0.1.0 em `server/CMakeLists.txt`). A versão aparece na
propriedade `versao` do `estado.md` e no `CHANGELOG.md` do módulo.

## 8. Quem edita o quê

- Quem trabalha num módulo edita o `estado.md` e o `roteiro.md` dele à vontade.
- `entrada.md`: todos acrescentam no fim; ninguém reescreve linhas dos outros.
- `visao.md`, `padrao.md` e decisões aceitas: mudam só com o pedido do usuário.
- `arquivo/`: não se edita.
- Língua: português, como o resto do projeto.
