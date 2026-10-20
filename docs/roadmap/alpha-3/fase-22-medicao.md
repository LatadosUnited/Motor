---
tipo: fase
alpha: 3
fase: 22
titulo: Medição
status: em andamento
prioridade: 1
areas: [core, renderer, app, player, docs]
branch: fase-22-medicao
inicio: 2026-10-20
fim: 
---

# Fase 22 — Medição

## Objetivo

A regra da alpha 3 é medir antes de otimizar. Esta fase dá ao Motor os instrumentos para isso: uma linha do
tempo por quadro, contadores por sistema e por passada, uma cena de referência e um teste que acusa quando um
número piora. A comparação com a Unity já existe (`bench/`); o que falta é ver *dentro* do Motor onde vai cada
milissegundo. Rascunho e linha de base em [alpha-3.md](../alpha-3.md).

## Tarefas

### Profiler

- [ ] **Tracy como profiler por quadro**, atrás dos macros `MOTOR_PROFILE_*` (`motor/core/Profile.h`) e das zonas
  de GPU (`renderer/GpuProfile.h`). Opção `MOTOR_TRACY` no CMake, desligada por padrão. Zonas: o loop
  (`Eventos`, `FixedUpdate`, `Update`, `Render`, `ImGui`, `SwapBuffers`), cada sistema do `SceneRuntime` pelo
  nome, os scripts, as passadas do renderer na CPU e na GPU (`Sombras`, `Céu`, `Cena HDR`, `Transparentes`,
  `Pós-processamento`), e no editor o desfazer e a checagem de "não salvo"; gráficos de draws e triângulos.
  Decisão: [Profiler por quadro: Tracy](../../decisoes/2026-10-20-profiler-tracy.md). *(origem: conversa de
  18/10/2026; escolha do usuário em 20/10/2026)* Código feito (f25b061, c389a08): compila sem avisos no MSYS2 e
  escuta só em `127.0.0.1:8086`. Falta conferir as zonas no visualizador 0.14.1 para marcar.
- [ ] Zonas nos scripts do jogo (a DLL de scripts hoje compila sem `MOTOR_TRACY`) e nomes nas threads da Jolt.

### Contadores e medição

- [ ] Contadores por quadro: tempo de CPU por sistema, tempo de GPU por passada (timer queries), draws,
  triângulos e memória de vídeo por recurso. Visíveis no viewport, no MCP (`editor_status`) e no Player
  (`--perf` grava um CSV). *(origem: alpha-3.md)*
- [ ] `MOTOR_VSYNC=0` (variável de ambiente) desliga o vsync para medir acima da taxa do monitor. *(origem:
  alpha-3.md)*
- [ ] Cena de referência no repositório: ~5000 entidades (estáticas e com física), 1 luz direcional com sombra
  e 8 pontuais, 50 scripts simples, UI e som. *(origem: alpha-3.md)*
- [ ] Teste de desempenho no `ctest` (fora do conjunto rápido) que falha se um número piorar além da margem.
  *(origem: alpha-3.md)*
- [ ] Medir também numa máquina com GPU dedicada, para separar CPU de GPU. *(origem: alpha-3.md)*

## Pronto quando

A cena de referência abre no Player com `--perf` e grava o CSV; o Tracy mostra as zonas de CPU e GPU de um
quadro do editor e do Player; o teste de desempenho roda no `ctest`; e a tabela de linha de base do
[alpha-3.md](../alpha-3.md) é refeita com os contadores novos.

## Feito

## Limitações
