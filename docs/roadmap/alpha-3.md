---
tipo: alpha
alpha: 3
status: planejada
foco: Desempenho
---

# Roteiro da Alpha 3 — Desempenho (rascunho)

> **Rascunho.** Começa depois da alpha 2 ([alpha-2.md](alpha-2.md)). Os números das fases e a ordem
> podem mudar com o que a validação de uso real (Fase 19) encontrar.

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

- Cena de referência no repositório: ~5000 entidades (estáticas e com física), 1 luz direcional com sombra e
  8 pontuais, 50 scripts simples, UI e som.
- Contadores por quadro: tempo de CPU por sistema (scripts, física, animação, áudio, render), tempo de GPU por
  passada (timer queries), draws, triângulos e memória de vídeo por recurso; no viewport, no MCP
  (`editor_status`) e no Player (`--perf` grava um CSV).
- Teste de desempenho no `ctest` (fora do conjunto rápido) que falha se um número piorar além da margem.
- `MOTOR_VSYNC=0` (variável de ambiente) desliga o vsync para medir acima da taxa do monitor.

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

| vazia | cubos1000 | cubos1000sombra | cubos10000 | fisica1000 | scripts1000 | spawn200 |
|---|---|---|---|---|---|---|
| 110–113% | 121–125% | 124–126% | **68–69%** | **76–81%** | 121% | 114–122% |

Só `cubos10000` está abaixo da meta (custo por desenho na CPU: Fase 24), e `fisica1000` está perto do limite
(sincronização cena–física: Fase 25). Nas cenas dominadas pela GPU o Motor ganha, mas na HD 530 elas medem mais
o desenho que os scripts; repetir numa máquina com GPU dedicada para separar CPU e GPU.

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

*(Análise de 17/10/2026, item 2. Medido: 135 ms por quadro com sombra Alta contra 21 ms sem sombra.)*

- Memória: a qualidade Alta usa 4 cascatas de 4096² em `DEPTH_COMPONENT32F` (~256 MB de vídeo). Usar
  `DEPTH_COMPONENT16`/24 onde der, e tamanhos menores por padrão.
- Número de cascatas ajustável (1 a 4) e distância por cascata.
- Cache: refazer as cascatas distantes a cada N quadros e só redesenhar objetos que se mexeram (estáticos
  ficam numa camada guardada).
- Por objeto: "Projeta sombra" e "Recebe sombra" no Mesh Renderer.
- Sombras de spot com uma face só (hoje um cubo inteiro). *(Limitação da Fase 7.)*

## Fase 24 — Renderer em lote

*(Análise, item 3.)*

- Ordenar os desenhos por shader, material e malha; instancing para a mesma malha com o mesmo material.
- Uniforms sem procurar por texto: hoje `Shader::Location` cria uma `std::string` e busca num
  `unordered_map` a cada chamada, ~10 vezes por desenho. Guardar as posições na criação do shader (ou usar
  UBOs por material e por objeto).
- `Material` sem cópia por item (hoje copiado em cada desenho, com 6 `shared_ptr`).
- Recorte por frustum já existe; acrescentar LOD por distância para malhas com níveis.

## Fase 25 — Cena e scripts que escalam

*(Análise, item 4; pendência da Fase 5.)*

- `WorldMatrix` com cache (marca de "sujo" ao mexer no transform ou no pai), em vez de subir a hierarquia a
  cada chamada.
- Laço dos scripts sem criar vetor a cada chamada e sem refazer `IsActiveInHierarchy` por componente
  (`SceneRuntime`, `ForEachScript`).
- Destruir em lote: `Children`, `DestroyEntity` e `FlushDestroyed` deixam de ser lineares na cena (destruir
  muitas entidades no mesmo quadro hoje é quadrático).
- Índice de nomes para `FindEntity`/`FindByName`.
- Física: os picos isolados de até ~185 ms no `test_physics_bench`. *(Pendência da Fase 5.)*

## Fase 26 — Editor que escala

*(Análise, item 5; revisão do código de 17/10/2026.)*

- Desfazer por diferenças: hoje `UndoHistory::Track` compara a cena inteira a cada quadro e guarda até 100
  cópias completas. Marcar entidades alteradas, guardar só as diferenças, limitar o histórico por memória. O
  componente novo continua entrando no desfazer sem código extra.
- "Alterações não salvas" sem serializar a cena inteira em JSON a cada 0,5 s (`SceneDocument`): usar a mesma
  marca de alteração do desfazer.
- Dividir os arquivos grandes que sobraram da Fase 20: `PhysicsSystem.cpp` (~940 linhas), `McpController.cpp`,
  `AssetLibrary.cpp`, `SceneSerializer.cpp`.

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
