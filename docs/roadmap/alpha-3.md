# Roteiro da Alpha 3 — Desempenho (rascunho)

> **Rascunho.** Começa depois da alpha 2 ([alpha-2.md](alpha-2.md)). Os números das fases e a ordem
> podem mudar com o que a validação de uso real (Fase 19) encontrar.

Foco: o Motor rodar bem em máquinas modestas (referência: i5 de 4 núcleos com Intel HD 530, 1 GB de vídeo
compartilhado) com cenas de milhares de objetos. Só depois disso vêm os cenários abertos e a renderização 2,
que dependem de instancing e de sombras baratas.

**Regra da alpha:** medir antes de otimizar. Cada fase começa com a medição na cena de referência e termina com
os números de antes e depois no README.

**A alpha 3 está pronta quando:** a cena de referência (Fase 22) roda a 60 FPS no Player na máquina de
referência com sombras na qualidade Média, o editor edita uma cena de 5000 entidades sem travar, e uma ilha
com terreno, água, vegetação e partículas roda a 60 FPS.

## Fase 22 — Medição

- Cena de referência no repositório: ~5000 entidades (estáticas e com física), 1 luz direcional com sombra e
  8 pontuais, 50 scripts simples, UI e som.
- Contadores por quadro: tempo de CPU por sistema (scripts, física, animação, áudio, render), tempo de GPU por
  passada (timer queries), draws, triângulos e memória de vídeo por recurso; no viewport, no MCP
  (`editor_status`) e no Player (`--perf` grava um CSV).
- Teste de desempenho no `ctest` (fora do conjunto rápido) que falha se um número piorar além da margem.

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
