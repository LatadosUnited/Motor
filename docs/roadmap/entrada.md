---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](../padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

## Para triar

- 2026-10-18 — Claude (verificação da Organização do código) — projeto compilado antes da Fase 17 (build com
  "MSYS Makefiles" e sem `toolchain.stamp`) nunca mais compila os scripts: o carimbo só força a reconfiguração
  quando já existia, o cache antigo fica e o `cmake --build` falha em 0,05 s com "0 erro(s)", sempre. Deve
  pegar o projeto de jogo real do usuário. Corrigir: reconfigurar também sem carimbo, ou quando o
  `CMAKE_GENERATOR` do cache não for o do toolchain — `editor/src/scripts/ScriptProject.cpp:241`
