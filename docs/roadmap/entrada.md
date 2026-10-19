---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](../padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

## Para triar

- 2026-10-19 — Claude (gravação segura, Fase 16) — a falha intermitente do `Project::Save` não se repetiu em
  8 rodadas para capturar a mensagem; a correção (troca que tenta de novo, cf18356) cobre o caso do arquivo
  preso, que reproduz "Acesso negado". Se `test_export` ou `test_distribuicao` voltarem a falhar em paralelo,
  guardar a mensagem. Hub, distribuição e instalador ainda trocam com `fs::rename` simples
  (`hub/src/HubCore.cpp:430`, `distrib/src/Manifest.cpp:44`, `installer/src/Setup.cpp:61`)
