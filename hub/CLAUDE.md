# Hub — instruções para o Claude

Módulo Hub: `hub/` (`MotorHub.exe`). Estado e roteiro em `docs/hub/`; uso em `hub/README.md`. As regras comuns
estão no `CLAUDE.md` da raiz.

- Núcleo sem janela em `hub/src/HubCore` (testado em `tests/test_hub.cpp`), interface em `HubApp` (abas em
  `HubProjectsTab` e `HubVersionsTab`, cores e botões em `HubWidgets`), Windows em `HubPlatform`, linha de comando
  em `main.cpp`. Lógica nova vai no `HubCore`, com teste.
- Só GLFW, ImGui e nlohmann_json estáticos: **não** liga na `libMotor.dll` nem inclui headers da engine (pode
  virar repositório próprio). Do repositório, usa só o `common/` (gravação segura) e o `distrib/` (manifesto e
  download).
- Fala com o editor só pela linha de comando (`--project`, `--create`, `--version`) e lendo `.mproject` e
  `motor-version.json`. Mudou o que o editor aceita ou grava? Combine com o módulo Editor.
- Versão própria: `MOTOR_HUB_VERSION` em `hub/CMakeLists.txt` (o instalador leva a mesma). Configuração do usuário
  em `%LOCALAPPDATA%\Motor\hub.json` (formato `motor-hub`, versionado: mudou, sobe e continua lendo o antigo).
- Testes e agentes: `--data <pasta>` para não mexer no `%LOCALAPPDATA%\Motor` real.
