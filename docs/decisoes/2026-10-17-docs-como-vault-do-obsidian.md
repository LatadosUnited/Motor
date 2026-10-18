---
tipo: decisao
data: 2026-10-17
status: aceita
fases: []
---

# Pasta docs/ como vault do Obsidian

## Contexto

O usuário quis o Obsidian para organizar o roteiro e a documentação, sem perder o que já estava no git e que os
agentes leem.

## Decisão

O vault é a pasta `docs/` do repositório (não o repositório inteiro), com Markdown comum, links relativos e
nomes de arquivo simples, seguindo o [padrão da documentação](../padrao.md). O roteiro mora em
`docs/roadmap/`, com uma nota por fase da alpha atual.

## Alternativas

- Vault na raiz do repositório: indexaria código e pastas de build.
- Vault separado, fora do repositório: duas cópias que se desencontram.
- Wikilinks (`[[nota]]`): mais cômodos no Obsidian, mas quebram no GitHub e para os agentes.

## Consequências

O `README.md` e o `CLAUDE.md` ficam fora do vault (na raiz). O layout de janelas do Obsidian não vai para o git.
