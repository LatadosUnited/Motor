---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](../padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

## Para triar

- 2026-10-18 — Claude (correção do projeto antigo) — testes intermitentes com o `ctest -j4`: `test_export` e, noutra
  rodada, `test_distribuicao` falharam uma vez cada; o `test_export` falhou uma vez com o `ctest -j4` e passou
  sozinho e nas duas rodadas inteiras seguintes: intermitente com os testes em paralelo (pasta ou arquivo
  disputado com outro teste?). As mensagens não foram guardadas — `tests/test_export.cpp`, `tests/test_distribuicao.cpp`
