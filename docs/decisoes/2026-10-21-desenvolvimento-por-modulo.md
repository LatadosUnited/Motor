---
tipo: decisao
data: 2026-10-21
modulos: [engine, editor, hub, instalador]
status: aceita
---

# Desenvolvimento por módulo, não por alphas

## Contexto

Até 21/10/2026 o roteiro era dividido em alphas (Fases 0 a 31). Cada alpha misturava engine, editor, Hub e
instalador, e o plano, o histórico e as medições ficavam nas mesmas notas: para saber o estado do Hub era
preciso ler quatro alphas, o `README.md` chegou a 991 linhas e o `CLAUDE.md` a 256, e faltava um objetivo claro
para cada parte. O código, por outro lado, já estava dividido: o Hub e a distribuição não ligam na engine
([Hub fora da engine](2026-10-17-hub-fora-da-engine.md), [Instalador e servidor próprios](2026-10-17-instalador-e-servidor-proprios.md)).

## Decisão

O desenvolvimento passa a ser dividido em **quatro módulos**, cada um com objetivo, versão, estado e roteiro
próprios:

| Módulo | Pastas | Docs |
|---|---|---|
| Engine | `engine/`, `player/`, `sandbox/` | `docs/engine/` |
| Editor | `editor/` | `docs/editor/` |
| Hub | `hub/` | `docs/hub/` |
| Instalador | `distrib/`, `server/`, `installer/` | `docs/instalador/` |

- **Versão por módulo** (escolha do usuário). Hoje: Engine e Editor 0.2.650 (a do `project()` do CMake), Hub
  0.2.0 (o instalador leva a mesma), servidor 0.1.0.
- **Docs antigos arquivados intactos** em `docs/arquivo/` (escolha do usuário); os docs novos foram escritos a
  partir deles, e as pendências das alphas 3 e 4 viraram itens dos roteiros dos módulos.
- Cada módulo tem `README.md` e `CLAUDE.md` na sua pasta; os da raiz ficam curtos e com o que é comum.
- Git: branch por assunto com o prefixo do módulo (`engine/...`, `editor/...`, `hub/...`, `instalador/...`,
  `docs/...`) e commits com o módulo no começo da mensagem quando ajudar (`Hub: ...`).

## Consequências

- O roteiro de cada módulo tem três blocos curtos (agora, próximo, depois), com tarefas de uma linha; o que
  foi feito fica no git e no `CHANGELOG` do módulo, não no roteiro.
- Trabalho que cruza módulos (o formato do `motor-version.json`, o pacote "Compilador") entra no roteiro de
  cada módulo envolvido, com link entre eles.
- A numeração de fases acaba; as notas antigas continuam valendo como histórico.
- Falta escrever o objetivo do projeto inteiro ([visão](../visao.md)): fica como primeira pendência.
