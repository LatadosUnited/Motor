---
tipo: alpha
alpha: 3
status: em andamento
foco: Desempenho
---

# Roteiro da Alpha 3 — Desempenho

> **Começou em 20/10/2026**, depois da alpha 2 ([alpha-2.md](alpha-2.md)). Cada fase ganha a sua nota em
> `alpha-3/` quando começa; as seções abaixo das fases que ainda não começaram continuam sendo rascunho.

Foco: o Motor rodar bem em máquinas modestas (referência: i5 de 4 núcleos com Intel HD 530, 1 GB de vídeo
compartilhado) com cenas de milhares de objetos. Só depois disso vêm os cenários abertos e a renderização 2,
que dependem de instancing e de sombras baratas.

**Regra da alpha:** medir antes de otimizar. Cada fase começa com a medição na cena de referência e termina com
os números de antes e depois no README.

**Meta principal: pelo menos 70% do desempenho da Unity** nas mesmas cenas, na mesma máquina. Em cada cena
do conjunto de comparação (Fase 22), o FPS do Player do Motor (Release) tem de ser **≥ 70% do FPS do player da
Unity** com configurações equivalentes; nenhuma cena abaixo de 50%. *(origem: pedido do usuário, 18/10/2026)*

**A alpha 3 está pronta quando:** a meta dos 70% é cumprida no conjunto de comparação; a cena de referência
(Fase 22) roda a 60 FPS no Player na máquina de referência com sombras na qualidade Média; o editor edita uma
cena de 5000 entidades sem travar; e uma ilha com terreno, água, vegetação e partículas roda a 60 FPS.

## Fase 22 — Medição

**Pronta (20/10/2026):** nota da fase em [alpha-3/fase-22-medicao.md](alpha-3/fase-22-medicao.md) (as tarefas
abaixo foram para lá). Profiler escolhido: [Tracy](../decisoes/2026-10-20-profiler-tracy.md).

- Cena de referência no repositório: ~5000 entidades (estáticas e com física), 1 luz direcional com sombra e
  8 pontuais, 50 scripts simples, UI e som.
- Contadores por quadro: tempo de CPU por sistema (scripts, física, animação, áudio, render), tempo de GPU por
  passada (timer queries), draws, triângulos e memória de vídeo por recurso; no viewport, no MCP
  (`editor_status`) e no Player (`--perf` grava um CSV).
- Teste de desempenho no `ctest` (fora do conjunto rápido) que falha se um número piorar além da margem.
- `MOTOR_VSYNC=0` (variável de ambiente) desliga o vsync para medir acima da taxa do monitor.
- **Profiler por quadro:** zonas de tempo por sistema e por função, numa linha do tempo, para saber onde vai
  cada milissegundo (hoje só há o FPS no título da janela). Candidato: Tracy (biblioteca nova: decidir com o
  usuário); alternativa própria: marcadores gravados num arquivo e vistos no `chrome://tracing`.
  *(origem: conversa de 18/10/2026 sobre o que falta otimizar)*
- **Medir também numa máquina com GPU dedicada**, para separar o que é CPU do que é GPU (na HD 530 quase toda
  cena esbarra na GPU).

### Comparação com a Unity

- **Projeto gêmeo na Unity** (Unity 6, a versão instalada mais recente que for LTS; URP, o pipeline padrão
  dos projetos novos), com as mesmas cenas do Motor, geradas por script dos dois lados a partir de uma mesma
  descrição (posições, escalas, luzes, câmera). Fica no repositório em `bench/unity/`.
- **Configurações equivalentes:** 1280×720 em janela, vsync desligado (`QualitySettings.vSyncCount = 0`,
  `targetFrameRate = -1`), HDR ligado, MSAA 4x, mesmo número de cascatas e resolução de sombra, bloom e
  tone mapping ACES, FXAA. Build de Windows em Release (IL2CPP) contra o Player Release do Motor. O que não tiver
  equivalente exato fica anotado na tabela.
- **Conjunto de comparação** (cada cena isola um custo):
  1. cena vazia (custo fixo do pipeline);
  2. 1000 cubos estáticos sem sombra (desenho);
  3. 1000 cubos com sombra Média (sombras);
  4. 10 000 cubos estáticos (agrupamento e instancing);
  5. 1000 corpos rígidos caindo numa pilha (física: Jolt contra PhysX);
  6. 1000 entidades com um script que as move a cada quadro (scripts: C++ contra C#);
  7. criar e destruir 200 objetos por quadro (alocação, prefabs).
- **Medição igual dos dois lados:** mediana do tempo de quadro em 10 s depois de 5 s de aquecimento,
  gravada num CSV (no Motor, o `--perf` desta fase; na Unity, um script de medição no projeto gêmeo).
- **Resultado** numa tabela com a razão Motor/Unity por cena, refeita ao fim de cada fase da alpha 3 para
  mostrar o caminho até os 70%.

Feito antes da alpha 3 (18/10/2026): projeto gêmeo, geradores e scripts de medição em `bench/` (como rodar e a
tabela completa em `bench/README.md`). Unity 6000.3 (URP 17.3, Direct3D 11, Mono) com o SSAO do template
desligado. Motor em % da Unity:

| Data | vazia | cubos1000 | cubos1000sombra | cubos10000 | fisica1000 | scripts1000 | spawn200 |
|---|---|---|---|---|---|---|---|
| 18/10/2026 (0.2.0) | 110–113% | 121–125% | 124–126% | **68–69%** | **76–81%** | 121% | 114–122% |
| 20/10/2026 (0.2.650, Fase 22) | 100–102% | 107–112% | 110–112% | **98–100%** | **84–85%** | 112% | 101% |

Em 20/10, depois de três achados do Tracy na [Fase 22](alpha-3/fase-22-medicao.md) (`glDrawRangeElements`, lista de
scripts no `SceneRuntime`, layout da UI só com a UI), **todas as cenas passam da meta de 70%**: `cubos10000` foi de
68–69% para 98–100% (de 54 para 33,5 ms) e `fisica1000` de 76–81% para 84–85%. A máquina estava mais rápida nesta
sessão, a Unity também; vale a razão. Detalhes em `bench/README.md`.

O que falta para a meta ser folgada: `fisica1000` (sincronização cena–física: Fase 25) e, em `cubos10000`, os ~22 ms
de CPU dos 10 000 draws e os ~8 ms de montar a lista deles (Fases 24 e 25). Nas cenas dominadas pela GPU o Motor
ganha, mas na HD 530 elas medem mais o desenho que os scripts; repetir numa máquina com GPU dedicada.

### Linha de base com os contadores (20/10/2026)

Refeita com os contadores da Fase 22: Player Release, 1280×720 em janela, MSAA 4x, vsync desligado, 12 s com
`--perf` (`bench/medir.ps1`), medianas por quadro do segundo 4 em diante, cenas do `bench/` e a cena de referência
(`bench/referencia`). A máquina estava no estado mais lento da sessão (a Unity mediu `vazia` em 22 ms nessa hora,
contra 12 a 13,5 ms em outras); compare as colunas entre si, não com a tabela de 18/10.

| Cena | Quadro | CPU Update | CPU FixedUpdate | CPU Render | CPU Swap | GPU total | GPU Sombras | GPU Cena HDR | GPU Pós | Draws | Vídeo |
|---|---|---|---|---|---|---|---|---|---|---|---|
| vazia | 15,9 | 0,1 | 0,0 | 2,1 | 10,9 | 10,4 | — | 1,1 | 9,3 | 0 | 66 MB |
| cubos1000 | 17,0 | 0,2 | 0,2 | 9,0 | 4,1 | 11,2 | — | 3,0 | 8,2 | 1001 | 66 MB |
| cubos1000sombra | 20,6 | 0,1 | 0,1 | 12,4 | 4,7 | 13,9 | 1,5 | 4,2 | 8,1 | 2401 | 130 MB |
| cubos10000 | 49,5 | 6,8 | 1,3 | 41,1 | 0,5 | 32,4 | — | 23,5 | 9,0 | 10001 | 66 MB |
| fisica1000 | 20,3 | 0,2 | 2,6 | 7,8 | 1,3 | 12,0 | — | 2,9 | 8,9 | 1001 | 66 MB |
| scripts1000 | 17,2 | 1,1 | 0,8 | 8,9 | 2,7 | 11,2 | — | 2,9 | 8,3 | 1001 | 66 MB |
| spawn200 | 15,5 | 9,2 | 0,1 | 2,9 | 1,4 | 10,5 | — | 2,2 | 8,2 | 202 | 66 MB |
| **referência** | **56,5** | 0,9 | 14,9 | 32,4 | 4,2 | **54,5** | 14,7 | 33,1 | 6,7 | 7927 | 130 MB |

(ms, exceto draws e memória de vídeo. "CPU Render" inclui montar a lista de desenhos e enviar ao OpenGL; "CPU Swap" é
a espera pela GPU no fim do quadro: alta = limitado pela GPU.)

Conclusões:

- **Custo fixo da GPU:** a cena vazia já gasta ~10 ms de GPU, dos quais ~9 ms são o pós-processamento (bloom, tone
  mapping, FXAA a 1280×720 com MSAA 4x em HDR). É o maior custo de quase todas as cenas pequenas (Fase 23 e opções de
  qualidade).
- **CPU por desenho:** `cubos10000` gasta 41 ms de CPU no Render para 10 001 draws (~4 µs cada, mais a montagem):
  é a Fase 24 (lotes e instancing).
- **Sistemas que percorrem a cena:** o `Update` de 6,8 ms em `cubos10000` são Áudio, Ui e Animação procurando seus
  componentes em 10 000 entidades (Fase 25). `spawn200` gasta 9,2 ms no Update criando e destruindo 200 objetos por
  quadro (Fase 25, destruir em lote).
- **Cena de referência:** 56,5 ms (18 FPS), limitada pela GPU (54,5 ms: Cena HDR 33, sombras 15) e com 32 ms de CPU
  no Render. Física: 14,9 ms por quadro, porque o quadro lento pede 3 a 4 passos fixos. Para chegar a 60 FPS:
  Fases 23 (sombras), 24 (lotes, LOD) e 25.
- **Memória de vídeo:** 66 MB sem sombras (57 do pós-processamento com MSAA 4x), 130 MB com elas (64 das 4 cascatas
  de 2048²).

### Linha de base (18/10/2026)

Máquina de referência (i5 de 4 núcleos, Intel HD 530), vsync desligado, FPS lido do título da janela (mediana
de 8 a 14 segundos). Cenas geradas: um chão, N cubos em grade, 1 luz direcional, câmera de cima. O Player abre
em 1280×720; o viewport do editor é menor, por isso o custo fixo dele é menor.

| Cena | Player Release | Player Debug | Editor Release | Editor Debug |
|---|---|---|---|---|
| vazia (só câmera e luz) | 12,7 ms | — | 7,7 ms | 7,7 ms |
| 100 cubos, sem sombra | 13,5 ms | 14,1 ms | 8,9 ms | 9,3 ms |
| 100 cubos, sombra Média | 15,1 ms | 15,2 ms | 11,8 ms | 15,2 ms |
| 100 cubos, sombra Alta | 18,7 ms | 18,7 ms | 20,4 ms | 20,9 ms |
| 1000 cubos, sem sombra | 14,1 ms | 24,1 ms | 12,0 ms | **84,0 ms** |
| 1000 cubos, sombra Média | 17,3 ms | 24,4 ms | 15,6 ms | **82,0 ms** |
| 1000 cubos, sombra Alta | 21,6 ms | 26,7 ms | 26,8 ms | **95,1 ms** |

Conclusões:

- **O editor Debug com muitas entidades é o gargalo que se sente:** 84 ms (12 FPS) com 1000 cubos, contra
  12 ms no editor Release. É trabalho de CPU do editor que cresce com a cena (desfazer comparando a cena
  inteira a cada quadro, checagem de "não salvo" em JSON; Fase 26), multiplicado pelo `-O0`. Para trabalhar,
  usar o editor Release (o que o Hub instala).
- **Custo fixo de GPU alto:** a cena vazia já gasta 12,7 ms a 1280×720 (MSAA 4x em HDR, céu, bloom, tone
  mapping, FXAA). Sobram ~4 ms para a cena a 60 FPS. Opções de qualidade (MSAA desligado ou 2x com FXAA, bloom
  em resolução menor) entram nas opções do jogador (alpha 2, Fase 21) e na Fase 23.
- **Sombras:** a Média custa 2 a 3 ms, a Alta 5 a 11 ms na HD 530. Padrão Média; Alta só em GPU dedicada.
- **CPU do jogo em Release aguenta 1000 objetos:** de 100 para 1000 cubos sem sombra, o Player Release só
  passa de 13,5 para 14,1 ms. O agrupamento de desenhos (Fase 24) importa a partir de milhares.
- Os 135 ms da análise de 17/10 eram de outra cena (ilhas, sombra Alta), muito provavelmente no editor Debug.

## Fase 23 — Sombras

- **Antes de mexer:** medir numa máquina com GPU dedicada (`bench/medir.ps1`, `test_desempenho`, Player com
  `--perf`), para separar CPU de GPU, e repetir o A/B do custo das consultas de GPU (`MOTOR_GPU_TIMERS=0`) com a
  máquina calma. *(origem: Fase 22, não feito)*

*(Análise de 17/10/2026, item 2. Medido: 135 ms por quadro com sombra Alta contra 21 ms sem sombra.)*

- Memória: a qualidade Alta usa 4 cascatas de 4096² em `DEPTH_COMPONENT32F` (~256 MB de vídeo). Usar
  `DEPTH_COMPONENT16`/24 onde der, e tamanhos menores por padrão.
- Número de cascatas ajustável (1 a 4) e distância por cascata.
- Cache: refazer as cascatas distantes a cada N quadros e só redesenhar objetos que se mexeram (estáticos
  ficam numa camada guardada).
- Por objeto: "Projeta sombra" e "Recebe sombra" no Mesh Renderer.
- Sombras de spot com uma face só (hoje um cubo inteiro). *(Limitação da Fase 7.)*

## Fase 24 — Renderer em lote

*(Análise, item 3; ver [Modularizar a engine](../decisoes/2026-10-18-modularizar-a-engine.md), passo 4.)*

- Ordenar os desenhos por shader, material e malha; instancing para a mesma malha com o mesmo material.
- Uniforms sem procurar por texto: hoje `Shader::Location` cria uma `std::string` e busca num
  `unordered_map` a cada chamada, ~10 vezes por desenho. Guardar as posições na criação do shader (ou usar
  UBOs por material e por objeto).
- `Material` sem cópia por item (hoje copiado em cada desenho, com 6 `shared_ptr`).
- Recorte por frustum já existe; acrescentar LOD por distância para malhas com níveis.
- **Recorte com estrutura espacial:** hoje o recorte testa objeto por objeto. Uma árvore de volumes (BVH) ou
  grade para os objetos estáticos descarta regiões inteiras de uma vez; a mesma estrutura serve às sombras, ao
  raycast do editor e às consultas de área. *(origem: conversa de 18/10/2026)*
- **Desenho guiado pela GPU,** depois do instancing: multi-draw indirect com os dados dos objetos em buffers
  mapeados de forma persistente, para milhares de objetos em poucas chamadas e quase nada de CPU por objeto (o
  equivalente ao GPU Resident Drawer da Unity). *(origem: conversa de 18/10/2026)*
- Alvo medido: `cubos10000` da comparação com a Unity (hoje 68 a 69%) acima de 70%.

## Fase 25 — Cena, dados e threads

**Em andamento (21/10/2026):** nota da fase em [alpha-3/fase-25-cena.md](alpha-3/fase-25-cena.md).

*(Análise, item 4; pendência da Fase 5; ver [Modularizar a engine](../decisoes/2026-10-18-modularizar-a-engine.md) e [Módulos em DLLs](../decisoes/2026-10-18-modulos-em-dlls.md).)*

- ~~`WorldMatrix` com cache (marca de "sujo" ao mexer no transform ou no pai), em vez de subir a hierarquia a
  cada chamada.~~ Feito em 21/10/2026, sem marca de "sujo" (o cache confere o transform e o pai a cada consulta):
  `mundo_hierarquia` de 0,9 para 0,23 ms no `test_desempenho`.
- ~~Laço dos scripts sem criar vetor a cada chamada e sem refazer `IsActiveInHierarchy` por componente
  (`SceneRuntime`, `ForEachScript`).~~ Feito na [Fase 22](alpha-3/fase-22-medicao.md): lista das entidades com script,
  refeita só quando a estrutura da cena muda (`SceneStructureVersion`).
- ~~**Sistemas sem percorrer a cena inteira:** cada sistema procura os seus componentes em todas as entidades a cada
  quadro. Em `cubos10000` (sem som, UI ou animação): Áudio 2,9 ms, Ui 2,8 ms, Animação 1,0 ms por quadro.~~ Feito
  em 21/10/2026 ([nota da fase](alpha-3/fase-25-cena.md)): `Scene::EntitiesWith`, listas por tipo refeitas pelo
  `SceneStructureVersion`. `cubos10000`: Update de ~7 para 0,02 ms, quadro de 51–61 para 42–44 ms.
- ~~Destruir em lote: `DestroyEntity` e `FlushDestroyed` deixam de ser lineares na cena (destruir
  muitas entidades no mesmo quadro hoje é quadrático).~~ Feito em 21/10/2026: `spawn200` com Destruir entidades de 8
  para 0,6 ms. Falta o `Children` (duplicar e copiar sub-árvores grandes).
- Índice de nomes para `FindEntity`/`FindByName`.
- Física: os picos isolados de até ~185 ms no `test_physics_bench`. *(Pendência da Fase 5.)*
- Física: a sincronização entre a cena e a física a cada passo (transformações para a Jolt e de volta). Alvo
  medido: `fisica1000` da comparação com a Unity (hoje 76 a 81%) acima de 85%.
- **Organização dos dados:** hoje cada entidade é um objeto separado na memória (`unique_ptr` com os
  componentes dentro), e percorrer milhares delas gera falhas de cache. Guardar transforms e componentes em
  blocos contínuos por tipo (no estilo ECS), mantendo a API dos scripts e o registro de componentes (Inspector,
  serialização e desfazer continuam automáticos). Medir antes com a cena de referência. *(origem: conversa de
  18/10/2026)*
- **Trabalho em várias threads:** hoje o quadro roda numa thread só (scripts, sincronização da física,
  recorte, montagem dos desenhos e envio), e só a Jolt usa os outros núcleos. Um sistema de tarefas (job
  system) divide entre os núcleos o que não depende de ordem: matrizes de mundo, recorte, montagem dos lotes,
  animação e sincronização da física; os scripts continuam numa thread (regra simples para quem programa o
  jogo). Depois da organização dos dados, que é o que permite dividir o trabalho sem travas.
  *(origem: conversa de 18/10/2026)*
- **Profiler nas threads e nos scripts:** nomes das threads da Jolt e do sistema de tarefas na linha do tempo do
  Tracy (`MOTOR_PROFILE_THREAD`), e zonas dentro dos scripts do jogo (a DLL de scripts hoje compila sem
  `MOTOR_TRACY`: passar a opção pelo SDK). *(origem: Fase 22, não feito)*

## Fase 26 — Editor que escala

*(Análise, item 5; revisão do código de 17/10/2026.)*

- Desfazer por diferenças: hoje `UndoHistory::Track` compara a cena inteira a cada quadro e guarda até 100
  cópias completas. Marcar entidades alteradas, guardar só as diferenças, limitar o histórico por memória. O
  componente novo continua entrando no desfazer sem código extra.
- "Alterações não salvas" sem serializar a cena inteira em JSON a cada 0,5 s (`SceneDocument`): usar a mesma
  marca de alteração do desfazer. Medido na Fase 22: gravar a cena de referência em JSON leva 337 ms e ler, 314 ms
  (`test_desempenho`, `cena_json` e `cena_ler`); com ela aberta, o editor travaria 1/3 de segundo a cada meio segundo.
- ImGui com 6 a 7 ms de CPU por quadro no editor Release, numa cena de 4 entidades (contadores da Fase 22; ver a
  caixa de entrada). *(origem: Fase 22)*
- ~~Dividir os arquivos grandes que sobraram da Fase 20: `PhysicsSystem.cpp` (~940 linhas), `McpController.cpp`,
  `AssetLibrary.cpp`, `SceneSerializer.cpp`.~~ Feito na [Organização do código](alpha-2/organizacao-do-codigo.md)
  (alpha 2).

## Fase 27 — Ambiente

Cenários abertos sobre a base da Fase 7 (`SceneDepthTexture`, `SceneColorTexture`, `SkyCubemap`,
`ReflectionCubemap`), com o instancing da Fase 24 e as sombras da 23.

- **Terreno:** mapa de altura editável (subir, baixar, suavizar), pintura de até 4 camadas de textura,
  collider da física, LOD.
- **Água:** ondas, reflexo do céu, refração e cor pela profundidade (usa a transparência da Fase 18).
- **Vegetação:** grama e árvores espalhadas por pincel no terreno, com instancing e sumindo com a distância.
- **Partículas:** emissor como componente (taxa, vida, velocidade, cor e tamanho no tempo, textura), na CPU
  primeiro; fumaça, fogo, faíscas.

**Pronto quando:** uma ilha com terreno pintado, água, árvores, grama e uma fogueira com fumaça roda no
Player a 60 FPS na máquina de referência.

## Fase 28 — Renderização 2

*(Limitações da Fase 7.)*

- Oclusão de ambiente na tela (SSAO).
- Reflexos locais: probes de reflexo posicionáveis (e, depois, SSR).
- Exposição automática (adaptação do olho).

**Pronto quando:** uma sala fechada com janela fica escura nos cantos (SSAO), reflete o ambiente de dentro e
não o céu (probe), e a câmera se adapta ao sair para o sol.
