---
tipo: decisao
data: 2026-10-20
status: aceita
fases: [22, 25]
---

# Profiler por quadro: Tracy

## Contexto

A alpha 3 começa medindo ([Fase 22](../roadmap/alpha-3.md)), e até agora o Motor só mostrava
o FPS no título da janela. Para saber onde vai cada milissegundo é preciso uma linha do tempo por quadro, com
zonas de CPU por sistema e de GPU por passada. As opções eram o Tracy (biblioteca nova) ou um profiler próprio
(marcadores gravados em JSON e vistos no Perfetto ou no `chrome://tracing`).

## Decisão

Usar o **Tracy 0.14.1** (BSD-3), atrás de macros próprios (`MOTOR_PROFILE_*` em `motor/core/Profile.h`). O código
do Motor nunca chama o Tracy direto. Ele é ligado pela opção do CMake `MOTOR_TRACY` (desligada por padrão) e grava
sob demanda (`TRACY_ON_DEMAND`): só coleta com o visualizador conectado. *(Escolha do usuário em 20/10/2026.)*

## Alternativas

- **Próprio** (JSON para o Perfetto): sem dependência, mas sem linha do tempo ao vivo, sem estatísticas por zona
  e sem memória e travas. Ficou como plano B: os macros permitem trocar o que há por trás sem mexer no resto.

## Consequências

- Linha do tempo ao vivo, estatísticas por zona e zonas de GPU (OpenGL) prontas. Vai servir também para o
  trabalho em várias threads (Fase 25).
- O cliente fica na `libMotor.dll` (a mesma em que estão o ImGui e o GLFW). O editor e o Player o usam com
  `TRACY_IMPORTS`. A DLL de scripts não recebe `MOTOR_TRACY`: os scripts ainda não têm zonas próprias.
- O visualizador tem de ser da mesma versão do cliente (o protocolo muda entre versões). Ao subir a versão do
  Tracy no `CMakeLists.txt`, baixe o visualizador da mesma versão.
- Com `MOTOR_TRACY=ON` o programa escuta na porta 8086 só do localhost (`TRACY_ONLY_LOCALHOST`): o visualizador
  roda na mesma máquina, sem porta aberta na rede nem aviso do Firewall do Windows. Para medir outra máquina,
  rode o visualizador nela. Fica fora dos builds distribuídos.
- Os contadores que precisam estar dentro do Motor (`--perf` em CSV, `editor_status` do MCP, teste de desempenho
  no `ctest`) continuam sendo nossos: o Tracy não os substitui.
