---
tipo: decisao
data: 2026-10-17
status: aceita
fases: [14]
---

# Hub fora da engine

## Contexto

Precisávamos de um lugar para criar e abrir projetos sem abrir o editor. A primeira ideia era uma tela inicial
dentro do editor. O usuário quer várias versões do Motor instaladas lado a lado, com cada projeto abrindo na
versão em que foi feito (versionamento).

## Decisão

O Hub é um programa separado (`MotorHub.exe`, pasta `hub/` no mesmo repositório), que não liga na
`libMotor.dll`: só lê o `.mproject` e o `motor-version.json` e chama o editor da versão certa.

## Alternativas

- Tela inicial dentro do editor: mais simples, mas presa à versão do editor aberto.
- Repositório próprio desde já: só compensa se outras pessoas mexerem no Hub; dá para separar depois.

## Consequências

O `.mproject` ganhou `engineVersion` (formato 2) e o editor ganhou `--project`, `--create`, `--settings` e
`--no-settings`. O Hub funciona com qualquer versão do editor, inclusive as futuras.
