---
tipo: guia
atualizado: 2026-10-21
---

# Visão

O Motor é uma engine 3D em C++20 e OpenGL 4.5, feita do zero, com editor próprio e as ferramentas para
instalar e distribuir as versões. Roda no Windows.

## Objetivo do projeto

> **A definir.** O roteiro por alphas cumpriu a meta de desempenho (todas as cenas da comparação com a Unity
> acima de 70%), mas nunca disse *para quê* a engine existe: um jogo específico, um gênero, uma ferramenta para
> agentes de IA, uma engine de estudo. Esta seção deve responder em um parágrafo e orientar os roteiros dos
> módulos. Ver [Desenvolvimento por módulo](decisoes/2026-10-21-desenvolvimento-por-modulo.md).

## Os módulos

| Módulo | O que é | Para quem | Versão |
|---|---|---|---|
| [Engine](engine/estado.md) | `libMotor.dll`: cena, componentes, render, física, áudio, UI do jogo, scripts; o `Player.exe` que roda o jogo exportado | quem faz o jogo (scripts) e o editor | 0.2.650 |
| [Editor](editor/estado.md) | `MotorEditor.exe`: criar e editar cenas, compilar e recarregar scripts, Play, exportar, servidor MCP | quem faz o jogo | 0.2.650 |
| [Hub](hub/estado.md) | `MotorHub.exe`: lista de projetos, versões instaladas, baixar versões | quem usa o Motor | 0.2.0 |
| [Instalador](instalador/estado.md) | `MotorSetup.exe` (instala o Hub), `MotorServidor.exe` (publica e serve versões), núcleo `distrib/` | quem usa e quem publica | 0.2.0 / 0.1.0 |

Dependências entre eles: o Editor usa a Engine; o Hub abre o Editor de cada versão pela linha de comando e lê
o `motor-version.json`; o Instalador leva o Hub dentro e baixa versões do servidor. O Hub e o Instalador não
ligam na `libMotor.dll` (dividem só o `common/`, a gravação segura de arquivos).

## Princípios que valem para todos

- **Bibliotecas mínimas,** fixadas por versão; biblioteca nova só com aprovação.
- **Medir antes de otimizar** (Tracy, contadores por quadro, `test_desempenho`; ver [Engine](engine/estado.md)).
- **Texto e caminhos em UTF-8,** pastas com acento como caso de teste ("Jogo Ação").
- **Formatos versionados** e leitura tolerante: mudou o formato, sobe a versão e continua lendo as antigas.
- **Nada se apaga de vez:** exclusões vão para a Lixeira; gravação de arquivos importantes é atômica.
