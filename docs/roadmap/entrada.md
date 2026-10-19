---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](../padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

## Para triar

- 2026-10-18 — Claude (correção do projeto antigo; Fase 18) — testes intermitentes com o `ctest -j4`:
  `test_export`, `test_distribuicao` e `test_project_version` falharam uma vez cada e passaram sozinhos e nas
  rodadas seguintes. No `test_project_version` foi o `Project::Save` (grava `.tmp` e renomeia): provável
  antivírus ou indexador do Windows segurando o arquivo novo por um instante. Se for isso, o mesmo pode
  acontecer com o usuário ao salvar cena, projeto ou material. Investigar e, se confirmar, repetir a troca de
  nome algumas vezes antes de desistir — `engine/src/project/Project.cpp` (Save), `SceneSerializer::Save`,
  `AssetLibrary` (WriteTextAtomic)
