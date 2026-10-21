---
tipo: guia
atualizado: 2026-10-21
---

# Motor — documentação

Pasta `docs/` do repositório, aberta também como vault do Obsidian. Comece pela [visão](visao.md): o que é o
Motor e os quatro módulos. Como escrever e organizar as notas: [padrão](padrao.md).

## Módulos

| Módulo | Estado (o que faz hoje) | Roteiro (o que vem) | Como usar |
|---|---|---|---|
| Engine | [estado](engine/estado.md) | [roteiro](engine/roteiro.md) | `engine/README.md` |
| Editor | [estado](editor/estado.md) | [roteiro](editor/roteiro.md) | `editor/README.md` |
| Hub | [estado](hub/estado.md) | [roteiro](hub/roteiro.md) | `hub/README.md` |
| Instalador | [estado](instalador/estado.md) | [roteiro](instalador/roteiro.md) | `installer/README.md` |

- [Caixa de entrada](entrada.md): achados que ainda não entraram no roteiro de um módulo.
- Decisões de rumo: pasta `decisoes/` (a mais recente: [desenvolvimento por módulo](decisoes/2026-10-21-desenvolvimento-por-modulo.md)).

## Para quem faz jogos

- [Guia de scripts](scripts.md): do primeiro script a um mini jogo.
- [Referência da API dos scripts](scripts-api.md).
- Exemplos testados: pasta `examples/` (compilados pelo `test_script_docs`).

## Testar

- [Roteiro de teste à mão](teste-manual.md): o que uma pessoa confere no editor a cada versão.
- Desempenho: `bench/README.md` (comparação com a Unity, cena de referência, `test_desempenho`).

## Histórico

- [Arquivo](arquivo/leiame.md): o roteiro antigo por alphas (Fases 0 a 31), como estava em 21/10/2026.
