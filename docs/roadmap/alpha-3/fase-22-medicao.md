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

- [x] **Tracy como profiler por quadro**, atrás dos macros `MOTOR_PROFILE_*` (`motor/core/Profile.h`) e das zonas
  de GPU (`renderer/GpuProfile.h`). Opção `MOTOR_TRACY` no CMake, desligada por padrão. Zonas: o loop
  (`Eventos`, `FixedUpdate`, `Update`, `Render`, `ImGui`, `SwapBuffers`), cada sistema do `SceneRuntime` pelo
  nome, os scripts, as passadas do renderer na CPU e na GPU (`Sombras`, `Céu`, `Cena HDR`, `Transparentes`,
  `Pós-processamento`), e no editor o desfazer e a checagem de "não salvo"; gráficos de draws e triângulos.
  Decisão: [Profiler por quadro: Tracy](../../decisoes/2026-10-20-profiler-tracy.md). *(origem: conversa de
  18/10/2026; escolha do usuário em 20/10/2026)* (f25b061, c389a08) Compila sem avisos no MSYS2 e
  escuta só em `127.0.0.1:8086`. Conferido em 20/10/2026 com o `tracy-capture` e o `tracy-csvexport` 0.14.1
  no Sandbox e no editor (edição e Play): todas as zonas de CPU, os sistemas um a um pelo nome e as zonas de GPU.
  Sistemas como zonas "transientes", para as estatísticas saírem uma linha por sistema.
- [ ] Zonas nos scripts do jogo (a DLL de scripts hoje compila sem `MOTOR_TRACY`) e nomes nas threads da Jolt.

### Achados do profiler

- [x] **Sombras com 20 ms de CPU no Sandbox.** O driver da Intel (HD 530) varria o buffer de índices na CPU a
  cada `glDrawElements`, para achar o intervalo de vértices, lendo a memória de vídeo: ~310 µs por draw de esfera
  (2300 triângulos) e 5 µs por cubo. Não era espera pela GPU: com a sombra Baixa, a GPU das sombras caía de 9,2
  para 2,0 ms e a CPU continuava em 15,5 ms. A correção é o `glDrawRangeElements` com o intervalo conhecido, em
  `Mesh::Draw` e no `Renderer2D`. Por draw: de ~310 µs para 2 µs. Sandbox, vsync desligado, CPU por quadro:
  Sombras de 19,9 para 3,6 ms, Cena HDR de 4,9 para 1,6 ms, Render de 31 para 15 ms (o resto do tempo agora é
  espera pela GPU no `SwapBuffers`, como deve). O FPS do Sandbox não muda na HD 530 (a GPU é o limite, ~28 ms),
  mas a CPU fica livre: deve pesar nas cenas limitadas pela CPU e com malhas grandes. *(origem: Tracy,
  20/10/2026)*
- [ ] Refazer a comparação com a Unity (`bench/`) com essa correção.

### Contadores e medição

- [ ] Contadores por quadro: tempo de CPU por sistema, tempo de GPU por passada (timer queries), draws,
  triângulos e memória de vídeo por recurso. Visíveis no viewport, no MCP (`editor_status`) e no Player
  (`--perf` grava um CSV). *(origem: alpha-3.md)*
- [x] `MOTOR_VSYNC=0` (variável de ambiente) desliga o vsync para medir acima da taxa do monitor. *(origem:
  alpha-3.md)* Já existia, feito junto com a comparação com a Unity (e957199).
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
