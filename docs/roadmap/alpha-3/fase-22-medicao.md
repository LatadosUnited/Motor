---
tipo: fase
alpha: 3
fase: 22
titulo: Medição
status: pronta
prioridade: 1
areas: [core, renderer, app, player, docs]
branch: fase-22-medicao
inicio: 2026-10-20
fim: 2026-10-20
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
- [ ] ~~Zonas nos scripts do jogo (a DLL de scripts hoje compila sem `MOTOR_TRACY`) e nomes nas threads da Jolt.~~
  Foi para a Fase 25 (trabalho em várias threads), onde a linha do tempo por thread passa a importar.

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
- [x] Refazer a comparação com a Unity (`bench/`) com essa correção. Neutra ou melhor em todas as cenas
  (`fisica1000` de 24,2 para 22,5 ms, `cubos1000sombra` de 18,6 para 17,4). Tabela em `bench/README.md` e em
  [alpha-3.md](../alpha-3.md). O `medir.ps1` estava quebrado desde a Fase 21 (FPS fora do título): o Player ganhou
  `--stats`. *(origem: pedido do usuário, 20/10/2026)*
- [x] **`cubos10000` com 37–53% da Unity (era 68–69% em 18/10).** *(origem: comparação com a Unity, 20/10/2026)*
  Não era regressão. Bisect contra a94da94 (commit da medição de 18/10), com o projeto gerado por ele e medido por
  `--quadros`: Player da época 63–67 ms, o de hoje sem as mudanças 67–77 ms. Os 37–53% vinham da medição: o
  `comparar.ps1` estragava o `.mproject` formato 4 com `ConvertTo-Json` sem `-Depth` (b26eeda), e a cena oscilava de
  ~45 a ~140 ms entre rodadas iguais. Mesmo assim, o Tracy mostrou duas varreduras da cena inteira a cada quadro,
  corrigidas sem mudar o comportamento:
  - **Lista de scripts no `SceneRuntime`** (390ac72): as entidades com script, refeita quando
    `SceneStructureVersion()` muda (entidade criada ou destruída, `Entity::Add`, cena copiada). `FixedUpdate` de
    3,75 para 0,2 ms por passo. Teste: `test_script_list`. Antecipa um item da Fase 25.
  - **Layout da UI só com a UI** (29998ee): `UiSystem` e `UiRenderer` montavam e ordenavam a árvore da cena inteira
    (~7,7 ms cada). `Update` de 8,7 para 0,8 ms. Teste novo em `test_ui`.
  Resultado: `cubos10000` com 98–100% da Unity (33,5 ms, estável), `fisica1000` com 84–85%, e todas as cenas acima
  de 70% (tabela em `bench/README.md` e em [alpha-3.md](../alpha-3.md)).

### Contadores e medição

- [x] **Contadores por quadro** (`FrameStats`, `motor/core/FrameStats.h`), sempre ligados: ms de CPU de cada parte
  do quadro, de cada sistema e dos scripts (`MOTOR_PROFILE_STAT`, que também é zona do Tracy); ms de GPU por passada
  (`GpuTimers`: carimbos `GL_TIMESTAMP` lidos ~4 quadros depois, sem a CPU esperar; `MOTOR_GPU_TIMERS=0` desliga);
  draws, draws de sombra, triângulos, recortados e transparentes. No Player, `--perf <arquivo.csv>` grava um quadro
  por linha ao fechar (`PerfRecorder`) e o log mostra o quadro mediano depois de 4 s; no editor, o bloco `perf` do
  `editor_status` (MCP). Teste: `test_frame_stats`. *(origem: alpha-3.md)* (440e10e, db8af42) Conferido no Player
  Release (`cubos1000sombra` e `scripts1000`, 600 quadros) e no editor pelo MCP, editando e em Play. O custo das
  consultas de GPU não apareceu num A/B com `MOTOR_GPU_TIMERS`, mas a máquina estava ruidosa (a Unity também
  dobrou de tempo): repetir com a máquina calma.
- [x] **Memória de vídeo por tipo de recurso** (`motor/renderer/GpuMemory.h`): cada textura, buffer e renderbuffer
  do renderer é registrado ao criar e ao apagar, com o tamanho estimado pelo formato (mips, camadas, MSAA); os
  contadores ganham `vram <tipo> MB` e `vram total MB` (texturas, malhas, framebuffers, pós-processamento, sombras,
  céu, buffers), também no CSV do `--perf` e no `editor_status`. Teste em `test_frame_stats`. *(origem: alpha-3.md)*
  (430deb0) No editor, projeto novo: 105 MB, dos quais 64 MB são as 4 cascatas de sombra de 2048² (alvo da Fase 23).
- [x] **Contadores no viewport do editor:** botão **Estatísticas** na barra do viewport, lembrado no `editor.json`
  (`viewportStats`); painel no canto direito com quadro, CPU por parte (sistemas e scripts abaixo de 0,05 ms somem),
  GPU por passada, draws e memória por tipo, suavizado por média móvel (`viewport/FrameStatsPanel`). *(origem:
  alpha-3.md)* (639ccc6) Visto em captura da janela (`PrintWindow`), editando e em Play: o painel fica abaixo da
  barra e dos botões de Play e se limita à altura do viewport. Não testado à mão: clicar no botão.
- [x] `bench/medir.ps1` lendo o `--perf` no lugar do título da janela (o título quebrou uma vez, na Fase 21). O
  Player ganhou `--segundos S` (fecha sozinho e grava o CSV); o medidor calcula a mesma estatística de antes (mediana
  das médias de 1 s depois de 4 s, como o `BenchRunner.cs` da Unity) e `-Csv` guarda o CSV completo. (b79fa00)
  Conferido com o `comparar.ps1` inteiro: todas as cenas lidas, de 92% a 134% da Unity.
- [x] `MOTOR_VSYNC=0` (variável de ambiente) desliga o vsync para medir acima da taxa do monitor. *(origem:
  alpha-3.md)* Já existia, feito junto com a comparação com a Unity (e957199).
- [x] **Cena de referência no repositório** (`bench/referencia/CenaReferencia.cpp`): 4975 entidades, com 4000
  estáticas (500 com collider), 900 caixas com corpo rígido, 50 com o script `Bobber`, 1 luz direcional com sombra
  Média e 8 pontuais, câmera com ouvinte, 10 fontes de som e UI (painel, título, botão). Fonte única: o teste a monta
  e `test_desempenho --gerar <projeto>` grava o `.mscene` para o Player e o editor. *(origem: alpha-3.md)* (8c8d595)
  Linha de base no Player (HD 530, 1280×720): 58 ms por quadro, GPU 56 ms (Cena HDR 34, sombras 15), CPU do render
  33 ms com 7927 draws, 130 MB de vídeo. Tabela em `bench/README.md`.
- [x] **Teste de desempenho no `ctest`** (`test_desempenho`, rótulo `desempenho`): mede a CPU na cena de referência
  (passo do jogo; montar, copiar, comparar, gravar e ler a cena), fica com a menor de 3 repetições e falha acima de
  +30% de `bench/referencia/base.txt` (`MOTOR_DESEMPENHO_GRAVAR=1` regrava). Só mede em Release; no Debug sai com 77 e
  o ctest mostra "pulado", sem atrasar o conjunto rápido. *(origem: alpha-3.md)* (8c8d595) Conferido: 4 execuções
  dentro da margem (a maior variação, +19%) e uma piora simulada na base reprovada. Achado: gravar a cena em JSON leva
  337 ms e ler 314 ms; o editor serializa a cena a cada 0,5 s para checar "não salvo" (Fase 26).
- [ ] ~~Medir também numa máquina com GPU dedicada, para separar CPU de GPU.~~ *(origem: alpha-3.md)* Depende de
  outra máquina: foi para o começo da Fase 23 (antes de mexer nas sombras), com os mesmos `medir.ps1` e
  `test_desempenho`.
- [x] **Linha de base refeita com os contadores** no [alpha-3.md](../alpha-3.md): as 7 cenas do bench e a de
  referência no Player, com CPU por parte, GPU por passada, draws e memória de vídeo. *(origem: "Pronto quando")*

## Pronto quando

A cena de referência abre no Player com `--perf` e grava o CSV; o Tracy mostra as zonas de CPU e GPU de um
quadro do editor e do Player; o teste de desempenho roda no `ctest`; e a tabela de linha de base do
[alpha-3.md](../alpha-3.md) é refeita com os contadores novos.

## Feito

O Motor passou a mostrar onde vai cada milissegundo, e a primeira rodada de medição já rendeu três correções.

- **Profiler:** Tracy 0.14.1 opcional (`-DMOTOR_TRACY=ON`), atrás dos macros `MOTOR_PROFILE_*`; zonas de CPU no loop,
  nos sistemas, nos scripts e no editor, zonas de GPU nas passadas do renderer. Só localhost, sob demanda.
  Decisão em `docs/decisoes/2026-10-20-profiler-tracy.md`.
- **Contadores por quadro, sempre ligados** (`FrameStats`, `GpuTimers`, `GpuMemory`): CPU por parte, GPU por passada,
  draws, triângulos e memória de vídeo por tipo. Saem no CSV do Player (`--perf`, com `--segundos`), no `editor_status`
  do MCP e no painel **Estatísticas** do viewport.
- **Cena de referência** (`bench/referencia`, 4975 entidades) e **`test_desempenho`** no ctest (rótulo `desempenho`,
  só Release, +30% sobre `bench/referencia/base.txt` reprova).
- **Correções achadas pela medição:** `glDrawRangeElements` (o driver da Intel varria os índices na CPU a cada draw:
  ~310 µs para 2 µs por draw de esfera); lista de scripts no `SceneRuntime` (`FixedUpdate` de 3,75 para 0,2 ms por passo
  em `cubos10000`); layout da UI só com a UI (`Update` de 8,7 para 0,8 ms). Comparação com a Unity: `cubos10000` de
  68–69% para 98–100%, `fisica1000` de 76–81% para 84–85%, todas as cenas acima de 70%.
- **Medição confiável:** o `bench/comparar.ps1` estragava o `.mproject` formato 4 (`ConvertTo-Json` sem `-Depth`) e o
  `medir.ps1` dependia do título da janela, que a Fase 21 tirou; os dois corrigidos. Bisect contra a medição de 18/10
  mostrou que não houve regressão de 2× desde a 0.2.0 (era a medição).
- Testes novos: `test_script_list`, `test_frame_stats`, `test_desempenho` e um caso em `test_ui`. Arquivos
  principais: `engine/include/motor/core/{Profile,FrameStats}.h`, `engine/src/renderer/{GpuProfile.h,GpuTimers,
  GpuMemory}`, `engine/src/scene/SceneRuntime.cpp`, `engine/src/ui/UiLayout.cpp`, `editor/src/viewport/FrameStatsPanel`,
  `player/src/main.cpp`, `bench/`.

Números de antes e depois: tabelas em `bench/README.md` (comparação com a Unity e cena de referência) e no
[alpha-3.md](../alpha-3.md) (linha de base com os contadores).

## Limitações

- **Ruído da máquina de referência:** a HD 530 divide a energia com a CPU e mudou de estado várias vezes na sessão (a
  Unity variou de 12 para 22 ms na cena vazia). Os números absolutos de dias diferentes não se comparam; as razões e o
  `test_desempenho` (menor de 3 repetições, margem de 30%) aguentam melhor.
- **Custo das consultas de GPU** (`GpuTimers`) não isolado: o A/B com `MOTOR_GPU_TIMERS=0` caiu num período ruidoso.
  Repetir com a máquina calma (entra junto com a medição da Fase 23).
- **GPU dedicada:** não medido; foi para o começo da Fase 23.
- **Zonas nos scripts e nomes nas threads da Jolt:** foram para a Fase 25.
- **Sistemas que percorrem a cena** (Áudio, Ui, Animação: ~7 ms por quadro em 10 000 entidades) e o JSON da cena
  (337 ms para gravar a de referência, que o editor faz a cada 0,5 s): anotados nas Fases 25 e 26.
- **Achados na caixa de entrada:** o ImGui gasta 6 a 7 ms de CPU no editor Release; a linha de FPS do viewport fica
  por baixo dos botões de Play com o viewport estreito.
- Não testado à mão: clicar no botão Estatísticas (visto só com a preferência ligada pelo arquivo de configuração).
