---
tipo: fase
alpha: 4
fase: 28
titulo: Terreno e água
status: pronta
prioridade: 2
areas: [renderer, scene, physics, editor-viewport, editor-mcp]
branch: fase-28-terreno
inicio: 2026-10-26
fim: 2026-10-28
---

# Fase 28 — Terreno e água

## Objetivo

A alpha 3 deixou o renderer rápido para cenas em que a câmera vê quase tudo: o recorte testa cada objeto a cada
quadro (~1,6 ms na referência) e a CPU ainda percorre a cena inteira para montar a lista. Num mundo aberto a câmera
vê uma parte pequena, e esse custo cresce com o tamanho do mapa, não com o que aparece. Esta fase troca o teste
objeto a objeto por uma **grade de células** no chão, guarda no renderer os objetos que não mudam entre quadros e
leva as posições para a GPU relativas à câmera, para não perder precisão longe da origem. Em cima disso vêm o
**terreno** (mapa de altura em blocos, cada bloco num arquivo, alinhado às células) e a **água**. A grade, os blocos
e a renderização relativa à câmera são também a base do streaming de células ("Mundo grande", no
[índice da alpha 4](../alpha-4.md)), decidida antes para o formato do terreno não precisar mudar depois.

## Tarefas

- [x] Tarefa 1: Criar a grade de células (`SpatialGrid`) com consultas por frustum, caixa e raio — cc0dcda
- [x] Tarefa 2: Levar as posições para a GPU relativas à câmera — ea03e02
- [x] Tarefa 3: Guardar no renderer os objetos estáticos entre quadros (registrar, mover, remover) — a65c4a8
- [x] Tarefa 4: Recortar a cena e as sombras pela grade — 9a37a78, 3993896
- [x] Tarefa 5: Usar a grade no raycast do editor e nas consultas de área — 7cb35e8
- [x] Tarefa 6: Desenhar pela GPU (multi-draw indirect, buffers mapeados de forma persistente) — beaa8b8
- [x] Tarefa 7: Guardar os componentes em blocos contínuos por tipo — 261c9cd
- [x] Tarefa 8: Criar o componente Terrain com mapa de altura em blocos, um arquivo por bloco — 6c9be78
- [x] Tarefa 9: Pincéis do terreno no editor: subir, baixar, suavizar — 84bd2db
- [x] Tarefa 10: Pintar até 4 camadas de textura no terreno — 71d1d3c
- [x] Tarefa 11: Dar ao terreno um collider da física (height field da Jolt) (`e0e676e`, `37566be`)
- [x] Tarefa 12: LOD do terreno por blocos, sem frestas entre níveis (`bc51f61`)
- [x] Tarefa 13: Criar a água: ondas, reflexo do céu, refração e cor pela profundidade (`d07da6f`)
- [x] Tarefa 14: Ferramentas MCP do terreno e da água (`4cfa048`)

## Pronto quando

- `test_spatial_grid` passa: as consultas da grade dão o mesmo resultado que testar tudo, sem repetidos, com
  objetos grandes, coordenadas negativas, mover e remover.
- A cena de referência não fica mais lenta (`test_desempenho` e Player com `--perf`), e uma cena de 20 000 objetos
  espalhados em 2 x 2 km com a câmera vendo ~5% deles gasta no recorte menos de 0,3 ms (hoje cresce com os 20 000).
- Um objeto a 10 km da origem não treme na tela (verificado pelo `viewport_capture`).
- Um terreno de 1 x 1 km com 4 camadas, LOD e água roda a 60 FPS no Player na máquina de referência; o personagem
  anda nele pela física; o terreno grava um arquivo por bloco e reabre igual.

## Feito

- Tarefa 1: `motor::SpatialGrid` (`engine/include/motor/renderer/SpatialGrid.h`), grade uniforme no plano XZ
  (células de 32 m por padrão), com a altura de cada célula tirada dos objetos dela. Objeto entra em todas as
  células que toca; o que tocaria mais de 64 fica numa lista à parte (chão, terreno). `QueryFrustum` pula as
  células fora da câmera e não testa um a um os objetos de célula inteira dentro dela; `QueryBox` e `QueryRay`
  (ordenado pela distância) servem às Tarefas 4 e 5. `CellOf` dá a célula de um ponto, para o streaming.
  `test_spatial_grid`: 4041 objetos em 2 x 2 km, consultas iguais a testar todos, sem repetidos, depois de mover
  e remover. Ainda não usada pelo renderer (Tarefa 4); as consultas não podem rodar em duas threads na mesma grade.
- Tarefa 2: a GPU recebe tudo menos a posição da câmera: matrizes dos objetos (no fim do `PrepareItems`, depois da
  caixa e do LOD), luzes, contornos do editor, passadas e cascatas de sombra (`RelativeToOrigin`, em double). A vista
  vai sem translação (`Camera::ViewRotation`) e `uCameraPosition` é 0; os shaders não mudaram. Recorte, LOD e
  cascatas continuam no mundo, e o cache das cascatas vale igual. Achado: o `Camera::View()` montava a vista com
  `lookAt(position, position + Forward())`, e a soma em float perdia a direção longe da origem (~1 mrad a 10 km);
  agora sai da direção. Na tela, a 10 km: erro de 5,5e-6 antes, 8e-9 depois (`test_render`). Visto pelo
  `viewport_capture` a 100 km, câmera a 40 cm de uma esfera: antes, silhueta facetada, manchas e sombra serrilhada;
  depois, igual à origem.
- Tarefa 3: `motor::RenderWorld` (`renderer/RenderWorld.h`) guarda objetos com caixa no mundo, matriz da normal e
  número de material já prontos (Add, Update, SetTransform, Remove); `Renderer::SubmitWorld` os copia para o quadro
  e troca os números de material uma vez por material diferente, não por objeto. Na cena, o Mesh Renderer ganhou o
  campo **Estático** (`isStatic`): os estáticos ficam num `RenderWorld` da `Scene`, refeito só quando a estrutura
  muda ou depois de `Scene::MarkRenderDirty` (no editor, a cada entrada do usuário, pedido do MCP, recarga de assets
  e enquanto o botão esquerdo está apertado; voar com o direito não refaz). No jogo, script que mexe num estático
  chama `MarkRenderDirty`, como o "Static" da Unity. `kScriptApiVersion` 22. Cena de referência no Player (Release,
  HD 530, mediana de 2 x 15 s) com os 4001 objetos sem corpo nem script marcados como estáticos: CPU do Render
  5,5 -> 4,6 ms (enviar a cena 1,8 -> 1,2; preparar 0,51 -> 0,33); o quadro continua em ~27 ms, preso na GPU
  (~22 ms). Visto pelo MCP: estático criado, movido, recolorido, desativado e desfeito aparece certo.
  `test_render_world`.
- Tarefa 4: o `RenderWorld` guarda os objetos numa `SpatialGrid`. O `ShadowRenderer` foi dividido em `Plan` (luzes
  e passadas do quadro) e `Render` (listas e desenho), para o renderer saber o frustum das passadas antes de montar
  a lista: no `EndScene` a câmera e cada passada consultam a grade, e só entram no quadro os objetos que alguém vê,
  com a máscara de passadas já pronta (o `BuildPasses` não testa o frustum deles). A consulta da grade passou a olhar
  só as células sob a caixa do frustum (os 8 cantos pelo encontro dos planos): percorrer as ~3900 células de um
  mundo de 2 x 2 km custava mais que os objetos. O `SubmitTo` guarda a lista dos não estáticos e não percorre mais
  os estáticos a cada quadro. Números (Release, HD 530, mediana de 2 x 15 s):
  - Mundo de 20 000 cubos em 2 x 2 km, câmera vendo ~4% (879): consulta da câmera 0,06 ms e das 4 cascatas ~0,1 ms
    (sonda fora do repositório); no Player, "recortar os guardados" (consultas e a cópia dos escolhidos para o quadro)
    0,56-0,83 ms. Todos normais -> todos estáticos: CPU do Render 23,6 -> 3,2-5,3 ms, enviar a cena 10,7 -> 0 ms,
    quadro 31 -> 19 ms (preso na GPU, ~13,5 ms).
  - Referência: CPU do Render 4,4 (normais) -> 3,5-4,2 ms (estáticos); o quadro continua preso na GPU (~22 ms).
  - Visto pelo MCP: torre estática fora da imagem com o sol baixo atrás dela: a sombra dela na imagem sai igual, pixel
    a pixel, com e sem Estático.
- Tarefa 5: o `Scene::Raycast` (clique no viewport) testa os não estáticos um a um e depois
  os estáticos pela grade do `RenderWorld`, do mais perto ao mais longe pela entrada na caixa, e para quando a caixa
  seguinte começa depois do melhor acerto: só os triângulos de poucos objetos são testados. `RenderWorld` ganhou
  `QueryBox` e `QueryRay`; o `QueryRay` da `SpatialGrid` devolve também a distância de entrada. `test_scene_raycast`:
  400 raios numa cena de 1500 objetos (filhos, desativados, chão grande) dão o mesmo acerto e distância com e sem
  estáticos; mover ou desativar um estático vale depois do `MarkRenderDirty`. Tempo do raycast não medido.
- Tarefa 6: as malhas sem pele ficam num buffer de vértices e num de índices de todas (`MeshPool`, privado do
  renderer, com um VAO comum; cresce copiando, e o intervalo de uma malha apagada volta a ser usado); as com pele
  continuam com VAO próprio. O que muda a cada quadro (objetos, índices das passadas e comandos de desenho) vai num
  anel mapeado de forma persistente (`StreamBuffer`), com uma cerca no fim de cada cena: a CPU só espera se o anel
  der a volta antes de a GPU terminar, e ele cresce se uma cena não cabe. O índice do objeto virou atributo por
  instância (location 5), para o `baseInstance` do comando valer (o `gl_InstanceID` não o inclui); saiu o
  `uDrawBase`. Lotes seguidos com o mesmo shader, material e VAO saem num `glMultiDrawElementsIndirect` só, mesmo com
  malhas diferentes, na cena e nas sombras. As contas de alocação (`motor/renderer/GpuAllocators.h`) são testadas
  sem GPU (`test_gpu_allocators`). Números (Release, HD 530, antigo e novo alternados, 2 x 15 s cada):
  - 3000 pedras com 300 malhas diferentes e um material: draws ~600 -> 5 (sombras ~290 -> 3), CPU de "cena e
    sombras" 18-26 -> 2,3-2,5 ms, CPU do Render 20-29 -> 4,7-4,9 ms. O quadro fica em ~30 ms, preso na GPU.
  - Referência: draws 15 -> 9; CPU e GPU iguais (o instancing da Fase 23 já juntava quase tudo).
  - Visto pelo `viewport_capture` (editor antigo e novo, mesma câmera): pedras, vidro transparente, plano de duas
    faces e o Robo.glb com pele saem iguais; as poucas diferenças são bordas de sombra, do mesmo tamanho que entre
    duas capturas do mesmo editor (as cascatas distantes alternam quadros).
- Tarefa 7: `motor::BlockPool` (`scene/BlockPool.h`) guarda objetos de um tamanho em blocos de 64 KB, na ordem de
  criação, com lista de livres; há um pool por tipo de componente (nome, tamanho e alinhamento: um script recarregado
  com a mesma forma volta para o mesmo) e um para as entidades (`Entity::operator new`). O `Register<T>` constrói os
  componentes nele em vez de `new`. Escolhido assim para a API de `Entity::components` não mudar: scripts,
  Inspector, serializador e prefabs ficam iguais. Os blocos nunca voltam ao sistema (componentes vivos na saída do
  processo não acham memória já liberada). Números (Release, HD 530, antigo e novo alternados):
  - Mundo de 20 000 cubos não estáticos: "enviar a cena" 10,5 -> 9,1 ms, Update 3,4 -> 2,3 ms. Referência: igual.
  - `test_desempenho`: `cena_copiar` 3,4 -> 2,0-2,2 ms e `cena_montar` 4,5 -> 2,1-2,4 ms (o desfazer e o Play copiam
    a cena), `cena_comparar` 6-11% menor; base regravada só nessas três linhas.
  - Pelo MCP: criar 50, duplicar, apagar, desfazer, Play e Stop no editor saem certos. `test_block_pool`.
- Tarefa 8: `motor::TerrainData` (`assets/TerrainData.h`, sem OpenGL) guarda o terreno em blocos de 32 m com 33 x 33
  alturas (1 m entre elas); blocos vizinhos repetem a linha da borda, para cada um ficar completo sozinho (o streaming
  da Fase 29 carrega um sem os outros), e `SetSample` muda todas as cópias. Arquivos: `Nome.mterrain` (cabeçalho
  JSON versionado: tamanho, resolução e lista de blocos) e a pasta `Nome.blocos` com um `x_z.mtbloco` binário por bloco;
  `Save` regrava só os blocos mudados (e os que sumiram do disco), cada um com `WriteFileAtomic`; bloco faltando ou
  estragado vira plano com aviso, sem impedir o resto. `Open` divide o mesmo objeto entre a cena, as cópias do
  desfazer e o Play. Malha por bloco com vértices a partir do canto e normais pelas amostras vizinhas, também do
  bloco ao lado (`NormalAt`: sem emenda de luz), refeita só quando o bloco ou um vizinho muda. `HeightAt` interpola
  nos mesmos triângulos da malha.
  - Componente `Terrain` (`scene/Terrain.h`): arquivo pelo GUID (`TerrainRef`, como o `NavMeshData`), material
    (asset ou cor, textura e rugosidade próprias; padrão grama) e sombra. A cena põe cada bloco no `RenderWorld` dos
    estáticos, na matriz da entidade com o canto do bloco; a lista dos estáticos guarda a origem de cada objeto
    (entidade, malha e matriz), e o `Raycast` usa isso para acertar os blocos. Alturas mudadas refazem os estáticos
    sozinhas (versão dos terrenos conferida a cada quadro). `kScriptApiVersion` 23 (o tamanho de `Scene` mudou).
  - `AssetType::Terrain` ("Terreno") e `AssetCompanions`: o painel Projeto leva a pasta `.blocos` junto ao renomear,
    mover, copiar, duplicar e mandar para a Lixeira; a exportação a copia. Entidade > Terreno e `entity_create` com
    `kind: "terrain"` criam um plano de 4 x 4 blocos (128 m) num `.mterrain` novo em `Assets/Terrenos`. Inspector com
    a escolha do arquivo e o tamanho do terreno ligado.
  - Pelo MCP ("Terreno Ação"): criar o terreno grava o cabeçalho, os 16 blocos e o `.meta`; trocar para um arquivo de
    morros gerado fora aparece no viewport com luz e sombra certas; salvar, duplicar, desfazer, Play e Stop saem
    certos. `test_terrain` (bordas divididas, altura, normais, arquivos, regravação parcial, cena e raio).
- Tarefa 9: pincéis Subir, Baixar, Suavizar (média das 8 vizinhas) e Nivelar (na altura de onde a pincelada começou),
  com raio e força, queda em cosseno do centro à borda e o mesmo resultado com quadros curtos ou longos.
  `motor::TerrainStroke` (`assets/TerrainBrush.h`, sem editor) guarda a altura de antes de cada amostra mexida; no
  fim, as mudanças viram um passo de desfazer (`ApplyTerrainChanges` põe e tira). `TerrainData::Raycast` acha a
  superfície vindo de cima, sem parar em outros objetos na frente.
  - Editor: no Inspector do Terreno, Selecionar / Subir / Baixar / Suavizar / Nivelar, raio e força. No Viewport
    (`viewport/TerrainBrushTool`), o círculo segue o mouse sobre o terreno selecionado (no espaço da entidade, que
    pode estar movida ou girada) e o botão esquerdo pinta em vez de selecionar; o gizmo some com o pincel ligado, Esc
    volta a selecionar.
  - Desfazer e salvar: `UndoHistory::PushAction` ganhou `unsaved`, que muda a versão do desfazer (a cena fica "não
    salva") e desfazer devolve a de antes. Salvar a cena grava antes as alturas mudadas (`SaveTerrains`), só os
    blocos que mudaram. MCP: `terrain_brush` (uma pincelada por uma lista de pontos do mundo).
  - Pelo MCP ("Pincel Ação"): subir, baixar, suavizar e nivelar aparecem no viewport; desfazer as cinco pinceladas
    volta ao plano e ao "salvo", refazer volta ao morro e ao "não salvo". Uma pincelada dentro do bloco `1_1`
    regravou só ele (1 de 16 arquivos). Inspector visto pelo `PrintWindow`. `test_terrain` (pincéis, desfazer,
    `SaveTerrains`, raio do terreno) e `test_undo` (versão com `unsaved`).
- Tarefa 10: cada amostra do terreno tem os pesos de 4 camadas (somam 1; tudo na camada 1 no começo), divididos na
  borda como as alturas. O arquivo do bloco passou à versão 2 (os pesos depois das alturas, um byte por camada); a
  versão 1 ainda é lida, toda na camada 1. Cada bloco dá uma textura 33 x 33 com os pesos
  (`TerrainData::BlockWeights`), criada sem OpenGL (`Texture2D::CreateDeferred`, enviada no primeiro uso, como a
  `Mesh`) e refeita só quando os pesos mudam; pintar não refaz a malha.
  - Renderer: `Material::terrain` (`TerrainLayers`: pesos, 4 texturas, cores, escalas e o canto do bloco) escolhe a
    variante TERRAIN do `lit.frag` (shader 3), que mistura as camadas pela textura dos pesos lida nos metros do
    terreno. As outras partes do material (rugosidade, luz, sombra, neblina) seguem as do lit.
  - Componente: a camada 1 é a textura e a cor do material, com escala própria; as camadas 2 a 4 (textura, cor,
    escala) ficam no componente, com cores padrão de terra, rocha e areia. A cena monta um material por bloco.
    `kScriptApiVersion` 24.
  - Pincel Pintar (Inspector, com a camada 1 a 4; `terrain_brush` com `layer`), no mesmo passo de desfazer
    (`TerrainEdit` com alturas e pesos).
  - Pelo MCP ("Pintura Ação"): estrada de terra, rocha no topo do morro e areia aparecem com a mistura suave nas
    bordas; texturas nas camadas 1 e 2 com escalas diferentes; desfazer volta à camada 1; salvar regravou só os 9
    blocos tocados (de 16), na versão 2, com o topo do morro gravado como camada 3 pura. `test_terrain` (pesos,
    normalização, desfazer, malha não refeita, arquivo novo e antigo, camadas do componente).
- Tarefa 11: com Colisão (padrão), cada bloco do terreno vira um corpo estático da Jolt com um `HeightFieldShape`
  (`engine/src/physics/PhysicsTerrain.cpp`), na pose da entidade e com ela como dono (`mUserData`): o `Raycast` da
  física, os contatos e o personagem acertam o terreno sem Collider. Campos novos no componente: Colisão, Atrito e
  Camada (de colisão). Os corpos são refeitos quando a versão das alturas, a matriz de mundo, o atrito ou a camada
  mudam, e saem quando a entidade some, é desativada ou perde a Colisão. Só um terreno com colisão já cria o mundo
  da física. `kScriptApiVersion` 25.
  - A malha desenhada e o `HeightAt` passaram a dividir cada célula pela diagonal da Jolt ((0,0)-(1,1)), para o
    corpo ter os mesmos triângulos que se vê.
  - A compressão das alturas da Jolt usa os bits que deixam o erro abaixo de 5 mm, calculados pela engine: o
    `CalculateBitsPerSampleForError` da Jolt lê além do vetor quando o número de amostras é ímpar (33) e derrubava o
    processo.
  - Renderer: as uniforms das camadas só são buscadas na variante TERRAIN (os outros shaders avisavam no log que não
    as achavam, desde a Tarefa 10).
  - `test_terrain_physics`: 200 raios da física acertam a altura desenhada com até 1,6 mm de diferença (entidade
    movida) e 1,2 mm (girada 90° e escalada); uma esfera para no terreno; o personagem fica no chão e anda 12 m
    subindo e descendo o morro sem atravessar nem voar; mudar uma amostra em Play muda o raio; Colisão desligada ou
    entidade desativada tiram os 16 corpos.
  - Pelo MCP ("Física Ação"): com o plano "Chão" desativado, cinco esferas soltas a 70 m pousam no topo de um morro
    de ~50 m feito com `terrain_brush`, rolam e param no terreno (y = 0,5, o raio); imagem por `viewport_capture`.
- Tarefa 12: `TerrainData::BlockMesh` monta, junto com o bloco cheio, 4 níveis de detalhe (`Mesh::Lods`, os da Fase
  24: o renderer escolhe pelo tamanho na tela, também nas sombras) com uma amostra a cada 2, 4, 8 e 16 no miolo,
  abaixo de 0,5, 0,25, 0,125 e 0,0625 da tela (~80, 160, 320 e 640 m com blocos de 32 m e 60° de visão). As células
  encostadas na borda usam todas as amostras do lado da borda, num leque a partir da amostra do meio: a borda de todo
  nível é a do bloco cheio, então vizinhos em níveis diferentes dividem os mesmos vértices, sem fresta, sem vértice
  em T e sem precisar saber o nível do vizinho (nem saias). Cada nível guarda só os vértices que usa. Triângulos por
  bloco: 2048, 696, 280, 168 e 136. A física, os raios e os pincéis continuam no bloco cheio.
  - `test_terrain`: em cada nível os triângulos ficam virados para cima e cobrem a área do bloco uma vez; as arestas
    de um triângulo só são exatamente os 4 x 32 pedaços da borda, com as alturas das amostras; as outras são de dois
    triângulos. Resolução 9 dá só os passos 2 e 4.
  - Pelo editor ("LOD Ação", contagem do canto do viewport pelo `PrintWindow`): terreno de 128 m com morros, perto
    17 896 triângulos (níveis misturados; 32 768 sem LOD), a ~250 m 4 480 (16 blocos x 280), sem frestas visíveis.
- Tarefa 13: componente `Water` (`scene/Water.h`): tamanho, cores rasa e funda, transparência (metros até o fundo
  sumir), altura, comprimento, velocidade e direção das ondas, reflexo, refração e espuma; a grade (`CreateWaterGrid`,
  células de até 1 m, no máximo 256 por lado) é refeita só quando o tamanho muda. Entidade > Água e `entity_create`
  com `kind: "water"`. `kScriptApiVersion` 26.
  - Renderer (`RendererWater.cpp`, `water.vert`, `water.frag`): `SubmitWater` guarda a água; depois dos opacos e do
    céu, se alguma está na tela, a cena é copiada (cor e profundidade, já sem MSAA: `PostProcess::CopyForRefraction`,
    texturas criadas no primeiro uso) e cada água é desenhada por cima, gravando profundidade, antes dos
    transparentes. Vértices: 4 ondas de Gerstner em volta da direção, no espaço da entidade (não andam com a câmera).
    Pixel: ondulações pequenas na normal; refração pela cópia deslocada pela normal (sem deslocar onde o ponto
    deslocado está na frente da água); cor pela espessura no raio de visão (o fundo some na cor rasa e depois na
    funda), iluminada pelo céu, a ambiente e as direcionais; reflexo do céu pré-filtrado com Fresnel; brilho do sol;
    espuma pela profundidade vertical até o fundo; neblina como no `lit.frag`. O bloco das luzes saiu do `lit.frag`
    para `lights.glsl`, usado pelos dois.
  - `WaterSurface::WaveOffset` faz na CPU a conta do `water.vert`. `test_water`: altura até a configurada, média zero
    no tempo, passo lateral de Gerstner, velocidade 0 parada, direção; grade (tamanho, limite de 256, virada para
    cima); componente (grade refeita só com tamanho novo, cena gravada e lida, cópia e `operator==`).
  - Pelo editor ("Água Ação"): lago cavado com o pincel num terreno com morros em volta, água em y = -0,3, esfera e
    cubo meio submersos: ondas, reflexo do céu, espuma na beira e em volta dos objetos, turquesa no raso e azul
    escuro no fundo, a parte submersa da esfera vista pela refração; também na visão do jogo (Play). Sem avisos de
    shader no log.
- Tarefa 14: ferramentas MCP em `editor/src/mcp/McpTerrainTools.cpp`, além do `terrain_brush` (Tarefa 9):
  - `terrain_info`: tamanho do bloco, resolução, espaçamento, blocos (até 1024 na lista), caixa, "não salvo" e, sob
    pontos (x, z do mundo), altura, inclinação, pesos das camadas e a amostra da grade.
  - `terrain_heights`: lê ou grava um retângulo de amostras (até 257 x 257), com um número ou as linhas, trocando
    (`set`) ou somando (`add`); amostra fora dos blocos lê `null` e não é gravada. Grava pela mesma edição dos pincéis
    (malha, física e "não salvo"), um passo de desfazer; recusada durante o Play.
  - `terrain_blocks`: põe ou tira blocos por lista e/ou retângulo (até 4096), na altura dada; desfazer devolve as
    alturas e os pesos dos blocos tirados. `TerrainData::RemoveBlock` é novo, e `AddBlock` copia as alturas e os pesos
    da borda dos vizinhos que já existem (sem degrau). Blocos que entram ou saem marcam o terreno como mudado (o
    cabeçalho é gravado no próximo Save).
  - `water_info`: tamanho, cantos, altura de repouso e das ondas e, sob pontos, se está dentro, a superfície com as
    ondas no tempo pedido (`WaveOffset`), o fundo, a profundidade e se está seco (algo acima da água ali).
  - `test_mcp_terrain`: as quatro ferramentas sem janela, com erros, desfazer e refazer.
  - Pelo editor ("Ferramentas Ação"): terreno aumentado para 8 x 8 blocos (256 m), ilha de 129 x 129 alturas gravada
    de uma vez, mar a -6 m e água de 256 m; leituras, pontos e desfazer conferidos e a ilha vista pelo
    `viewport_capture`.
- Critérios de "Pronto quando" (27/10/2026, Release, máquina de referência: i5 de 4 núcleos e HD 530; Player
  1280×720 em janela, MSAA 4x, vsync desligado, sombras altas, mediana depois de 4 s):
  - `test_spatial_grid` passa (ctest do Debug, 68/68).
  - `test_desempenho` passa (todas as medidas dentro de +30% da base; `cena_ler` +21%, `cena_json` +12%, o resto
    entre -6% e +16%). Cena de referência no Player: 22,0 e 22,1 ms em duas rodadas (GPU 17,9 ms; Cena HDR 11,0;
    sombras 3,0; CPU do Render 4,7), contra 22,1 ms na Fase 24 e 22,6 ms na Fase 26: não ficou mais lenta.
  - Recorte de 20 000 objetos em 2 x 2 km vendo ~4%: consultas da câmera e das 4 cascatas ~0,16 ms (Tarefa 4),
    dentro de 0,3 ms; com a cópia dos escolhidos para o quadro, 0,56-0,83 ms.
  - Objeto longe da origem: visto pelo `viewport_capture` a 100 km igual à origem (Tarefa 2).
  - Terreno de 1 x 1 km ("Terreno Ação", montado pelo MCP do editor Release): 32 x 32 blocos, morros de -13 a 22 m,
    camadas 2 a 4 pintadas (terra em manchas, rocha no alto, areia na beira), água de 1024 m em y = 0, personagem
    com Controlador de personagem andando sozinho por script e a câmera atrás (`Seguir`). Gravar a cena gravou os
    1024 arquivos de bloco (12,5 s); reaberta, as 1 050 625 alturas saem iguais às geradas (os pesos voltam
    arredondados para 1 byte por camada, como o formato guarda). No Player, o personagem anda 40 s pelo terreno
    sempre no chão, subindo e descendo os morros.
  - **60 FPS não alcançado:** quadro mediano 27,1 ms (~37 FPS), preso na GPU: Renderer 22,7 ms (Cena HDR 15,8 com
    326 draws e 218 mil triângulos, água 5,7, cópia para a água 3,1, pós 4,0, sombras 2,9); CPU do Render 7,9 ms e
    física 1,7 ms. Sem MSAA: 21,4 ms (~47 FPS; GPU 17,7: Cena HDR 11,8, água 4,0, cópia 1,7). Ver Limitações.
- Otimização da GPU do terreno e da água (27/10/2026, `2572b64`, depois dos critérios; medido por partes, uma mudança de cada
  vez, no mesmo Player e opções):
  - Pesos numa textura só: `TerrainWeights` (`motor/renderer/TerrainWeights.h`) guarda os pesos de todos os blocos
    num array de texturas RGBA8 com mipmaps (uma camada por bloco, capacidade em potências de 2, camadas livres
    reaproveitadas) e um índice R32F de onde está cada bloco (-1 sem bloco). `TerrainData::WeightsTexture()` a
    mantém em dia (blocos novos, mudados e tirados). O `lit.vert` passa o canto do bloco (`flat`) e o `lit.frag`
    acha a camada pelo índice. Com isso há um material para o terreno inteiro: os blocos entram no mesmo lote
    (326 draws → 7 no quadro todo) e a CPU do Render cai de 7,9 para 2,3-2,8 ms; os blocos do terreno vão do mais
    perto ao mais longe (menos pixels desenhados por cima). Cena HDR -2,5 ms e ~-0,5 ms da ordem.
  - Pixel do terreno: só luz difusa direta (sem o GGX: ~1,6 ms), o reflexo do céu continua; neblina cheia sai cedo,
    só com a cor da neblina (também na água).
  - Sombra do terreno pelas faces de costas para o sol (`ShadowParams::backFacesOnly`, culling das faces da frente
    nas cascatas; não nas faces do cubo das luzes pontuais, que são espelhadas): sombras de 2,9-3,3 para 0,75 ms,
    com os morros ainda fazendo sombra (visto pelo `viewport_capture` com o sol baixo).
  - Água: a cópia é só da cor; a profundidade é lida direto do alvo da cena (com MSAA, a profundidade virou textura
    com amostras, `sampler2DMS`, amostra 0) e a água não grava profundidade na passada de cor; se algo ainda vai por
    cima (transparentes, fios do editor), uma passada só de profundidade (`water_depth.frag`) vem depois. Cópia de
    3,1 para 1,1 ms com MSAA 4x.
  - `kScriptApiVersion` 27 (`TerrainLayers` e `ShadowParams` mudaram). `test_terrain` cobre a textura dos pesos
    (camadas por bloco, reaproveitar, retângulo do índice ao pôr e tirar blocos).
  - Conferido pelo `viewport_capture` no editor Release: cores das camadas iguais às de antes, sombra dos morros,
    sombra do personagem no chão, espuma e cor pela profundidade da água; sem erros de shader no log.
  - Resultado no Player ("Terreno Ação", duas rodadas intercaladas de 20 s): **MSAA 4x 19,1 e 19,5 ms (~52 FPS)**,
    antes 27,1 (GPU 16,6-16,9: Cena HDR 11,4, água com a cópia 4,4, pós 4,2, sombras 0,75); **sem MSAA 14,5 e 14,5
    ms (~69 FPS)**, antes 21,4 (GPU 11,4-11,5: Cena HDR 7,7, água 3,2, pós 2,8). Cena de referência:
    22,1 e 22,1 ms (GPU 18,1; a mesma de antes).
- Segunda rodada, para os 60 FPS com MSAA 4x (27/10/2026, `e1850a3`):
  - Sem FXAA com MSAA (`PostProcess::Apply`): as bordas já saem suavizadas, e o FXAA custava ~1,1 ms. Vale também no
    editor; a caixa do FXAA no painel Cena só age sem MSAA.
  - Janela do Player sem amostras (`player/src/main.cpp`): a cena tem o próprio alvo com MSAA, e a janela só recebe
    o pós e o HUD (texto pelo SDF). Com as 4 amostras de antes, o tone mapping que escreve na janela custava 1,3 ms
    em vez de 0,8 (medido nas subzonas, nas duas cenas).
  - Cópia para a água só do retângulo da tela que as águas visíveis cobrem (os cantos das caixas projetados, com a
    folga da refração; tela toda se um canto está atrás da câmera). No "Terreno Ação" o mar de 1 km cobre quase a
    tela toda e quase não muda (1,15 → 1,1 ms); conferido pelo `viewport_capture` com a água encolhida para 60 m
    (sem restos de outros quadros na borda) e na praia de antes.
  - Tentados e desfeitos, sem ganho: grade da água de 64 x 64 (o custo dela não está nos vértices) e o terreno sem o
    reflexo especular do céu (Cena HDR igual).
  - Resultado: **MSAA 4x 18,3-19,0 ms (~53-54 FPS)**, antes 19,1-19,5 (GPU 14,9-15,4: Cena HDR 11,1-11,3, água com a
    cópia 4,2-4,3, pós 2,9-3,0); sem MSAA 14,1-14,7 ms. Cena de referência 21,9-22,3 ms (pós de 3,7 para 2,8 ms).
    Medidas com a máquina ora calma ora carregada (outros programas): rodadas descartadas quando até a CPU do Render,
    que não mudou, subia junto. `test_desempenho` falhou duas vezes em testes de cena que não passam pelo renderer
    (`cena_montar`, `cena_copiar`, `cena_ler`, um diferente a cada vez) e passou na terceira.
- Limitações rápidas (27/10/2026, `c1a611a`):
  - Física por bloco: o corpo de cada bloco guarda a versão dele (`TerrainData::BlockVersion`); mudar alturas refaz
    só os blocos tocados e os vizinhos de borda (até 4 num ponto), pintar não refaz nenhum, tirar e pôr bloco mexe
    só nele. `PhysicsStats::terrainBlocksBuilt` conta os refeitos no passo (no `Stats()` dos scripts).
  - Raio da cena pela grade: os estáticos guardados sabem de qual bloco vieram, e o `Scene::Raycast` anda pelas
    células que o raio cruza (Amanatides-Woo, 2 triângulos por célula, com uma folga de 1e-4 que fecha as frestas
    num vértice da grade) em vez dos 2048 triângulos do bloco. O teste confere 40 raios de raspão: o ponto está na
    superfície e não há cruzamento antes (o `TerrainData::Raycast`, de meia célula, perde alguns desses).
  - Clique seleciona a água: `Scene::RaycastWater` (o retângulo da água no espaço dela), usado pelo viewport quando
    está mais perto que o acerto do `Raycast`.
  - Salvar manda para a Lixeira os arquivos dos blocos tirados (`TerrainData::UnusedBlockFiles`, só os de nome
    `x_z.mtbloco`; outros arquivos na pasta ficam).
  - Ondas no tempo do jogo: `Renderer::SetWaterTime` com o `elapsed` do runtime no Play do editor e no Player
    (com o jogo pausado, param); fora do Play, o relógio do GLFW.
  - Autossalvamento com as alturas: a recuperação grava de cada terreno mudado só os blocos sujos e a lista de blocos
    (`TerrainData::SaveChanges`, `alteracoes.json` por último); ao recuperar, `ApplyRecoveryTerrains` aplica por cima
    do terreno lido do disco (blocos tirados e postos, alturas e pesos) e a cena fica não salva.
  - `kScriptApiVersion` 28. `test_terrain` cobre os blocos tirados, a recuperação (mudar, gravar, "cair", aplicar,
    salvar e reler) e o raio pela grade e na água; `test_terrain_physics`, os blocos refeitos.
  - MSAA 2x no Player ("Terreno Ação", rodadas intercaladas de 20 s): **15,9 e 17,3 ms (~58-63 FPS)**; MSAA 4x 18,9 e
    19,4; sem MSAA 14,2 (GPU 13,0-13,6 com 2x: Cena HDR 9,4-9,6, água 3,8, pós 2,8-3,0).
- Pincelada sem remontar a cena (27/10/2026, `201942f`, com o ok do usuário para mexer no `EditorApp`):
  - `Scene::SyncRenderLists` com só a versão de um terreno mudada troca no `RenderWorld` os blocos de outra versão
    (`RenderWorld::Update` com a malha nova), tira os que saíram e põe os novos; pintar só põe os pesos na textura.
    Cada terreno registrado guarda o objeto base dos blocos (material e matriz) e cada bloco a versão dele.
  - O `EditorApp` não marca a cena para remontar a cada quadro com o botão esquerdo apertado durante uma pincelada
    (`EditorContext::terrainBrush.stroking`, posto pelo `TerrainBrushTool`).
  - `kScriptApiVersion` 29 (o tamanho de `Scene` mudou). `test_terrain` confere sem `MarkRenderDirty`: alturas,
    pesos, bloco tirado e posto, duas entidades com o mesmo terreno, e o resultado igual ao de remontar tudo.
  - Medido no Release (teste temporário, 1 km de terreno em 1024 blocos e 2000 estáticos, 60 quadros mudando 1 a 4
    blocos): **2,2 ms por quadro contra 3,5 remontando tudo** (rodada com a máquina carregada: 4,2 contra 5,9). O
    resto é refazer a malha e os 4 níveis de cada bloco tocado.

## Limitações

- `RenderWorld::QueryBox` ainda não tem quem a use: a seleção por retângulo do viewport e as consultas de área do
  terreno (Tarefas 8 e 9) são as candidatas. O raycast continua testando os não estáticos um a um.
- Desenho pela GPU (Tarefa 6): materiais diferentes ainda são draws separados (os parâmetros vão em uniforms e as
  texturas em unidades fixas); juntá-los pediria os materiais num buffer e texturas sem vínculo (bindless). Com pele,
  um draw por item. Os objetos ainda são copiados inteiros para o anel a cada cena, mesmo os estáticos parados.
- Blocos por tipo (Tarefa 7): os componentes ficam juntos por tipo, mas o acesso continua pela entidade (lista de
  componentes de cada uma), e não em arrays por campo como num ECS. O envio dos 20 000 objetos ainda custa ~9 ms:
  ~0,45 µs por objeto, a maior parte no `SubmitMany` (matriz de mundo conferida, material e chave por item). Um ECS
  de verdade muda a API de `Entity::components`; fica para quando um mundo grande dinâmico precisar.
- `test_desempenho` falhou em 27/10/2026 com o binário antigo e o novo do mesmo jeito (testes de cena que não passam
  pelo renderer 30-140% acima da base): a máquina estava mais lenta. Repetido 4 vezes depois das Limitações rápidas:
  falhou sempre, em testes diferentes a cada vez (`cena_ler`, `cena_montar`, `cena_copiar`, `cena_json`,
  `jogo_passo`, 5-30% além do limite), com um processo de outro programa (`python host.py`) usando mais de um núcleo o
  tempo todo. Repetir com a máquina calma antes de regravar a base.
- Terreno (Tarefa 8): mexer na pasta `.blocos` pelo Explorer, ou só nela pelo painel Projeto, separa os dois
  (o painel ainda a mostra como pasta comum). Alturas mudadas não entram no desfazer nem marcam a cena como não
  salva (vêm com os pincéis, Tarefa 9); um terreno sem projeto aberto fica só na memória. A textura repete a cada
  metro, sem escala própria (vem com a pintura, Tarefa 10).
  Pincéis (Tarefa 9): o arraste com o mouse no Viewport (círculo e pintura) não foi testado, só a mesma pincelada
  pelo MCP. Fechar sem salvar descarta as alturas
  só quando ninguém mais usa o terreno (as alturas ficam na `TerrainData` aberta enquanto houver referência). Cada
  quadro da pincelada refaz a malha e os níveis de detalhe dos blocos tocados inteiros. Um terreno girado ou
  escalado recebe o raio e a força no espaço dele.
  Pintura (Tarefa 10): o pixel do terreno lê sempre as 4 texturas e a dos pesos, mesmo onde só há uma camada. Os pesos têm a resolução das
  alturas (1 m): bordas de pintura mais finas que isso não existem. As camadas só têm cor (sem mapa de normal nem
  rugosidade por camada). Pintar a cada quadro reenvia os pesos de cada bloco tocado (desde a otimização da GPU, só
  a camada dele no array).
  Física (Tarefa 11): esculpir refaz o corpo dos blocos tocados (desde as Limitações rápidas), mas o bloco inteiro,
  não só as células mudadas. Sem material físico por camada pintada (o atrito é um só por terreno). As alturas guardadas pela Jolt têm até 5 mm de
  erro. Não testado na mão: andar com o personagem pelo teclado sobre o terreno no Play do editor.
  LOD (Tarefa 12): o nível troca de uma vez (sem transição suave), e a borda cheia limita o nível mais simples a 136
  triângulos por bloco (um terreno de 1 x 1 km longe ainda tem ~140 mil). Mudar uma altura refaz o bloco e os seus 4
  níveis. (Os blocos num lote só vieram com a otimização da GPU, abaixo.)
  Água (Tarefa 13): sem reflexo dos objetos e do terreno (só o céu; reflexo em tela ou planar fica para depois); sem
  sombra na água; as ondas não mexem em nada da física (sem empuxo: `WaveOffset` está pronto para quem quiser boias por script). A cópia da cena custa um blit de cor por
  quadro com água na tela (só o retângulo que ela cobre, desde a otimização; um mar grande cobre quase tudo). Água
  maior que 256 m tem células maiores que 1 m (ondas curtas ficam serrilhadas de perto). A espuma vem da profundidade
  até o fundo, sem espuma nas cristas. Visto de baixo d'água só há um tom escuro, sem efeito de mergulho. O clique na
  água seleciona a água mesmo com um objeto embaixo dela, mais longe (o `RaycastWater` não sabe da transparência).
  Ferramentas MCP (Tarefa 14): o "seco"
  do `water_info` é qualquer coisa acima da superfície no ponto (terreno, cubo ou outra entidade com colisão de
  raio), não só o terreno. O tempo das ondas é o pedido (0 por padrão), não o relógio do renderer, então a
  superfície devolvida não é a da tela naquele instante.
- Meta de 60 FPS com 1 km de terreno e água no Player (HD 530): alcançada **sem MSAA** (14,1-14,7 ms, ~69 FPS), **não
  com MSAA 4x** (18,3-19,0 ms, ~53 FPS), depois das duas rodadas de otimização da GPU. Com MSAA o gargalo segue na
  GPU: o terreno na Cena HDR (~11 ms contra 7,8 sem MSAA: o preenchimento com 4 amostras), a água (~3 ms além da
  cópia, o mesmo com um shader trivial ou menos vértices: é ler a profundidade com amostras na tela toda) e o pós (2,9
  ms: resolve 1,25, bloom 0,85, tone mapping 0,8). Com MSAA 2x fica perto (15,9-17,3 ms, ~58-63 FPS). Caminhos que
  sobram: MSAA 2x como padrão em GPU integrada (as opções do jogador ainda vêm com 4x), bloom em menos níveis, sombra recebida mais barata no terreno (~1,4 ms). O quadro tem ~3,5 ms além da GPU medida (composição
  da janela, provavelmente; não testado em tela cheia, que trocaria o modo do monitor). Medidas com ±1 ms de ruído
  entre rodadas iguais (programas abertos na máquina). Gravar 1024 blocos novos leva 12,5 s (um `WriteFileAtomic` por
  bloco).
- Otimização da GPU (27/10/2026): o pixel do terreno não tem brilho especular direto (só o difuso e o reflexo do
  céu): terreno molhado ou metálico não brilha com o sol. A sombra pelas faces de costas supõe um mapa de altura
  (uma superfície só, sem cavernas nem saliências), e faces de costas que mal passam do horizonte podem dar acne
  na beira da sombra. A água com MSAA lê só a amostra 0 da profundidade (a borda da espuma em volta de objetos tem
  o serrilhado da tela). O array dos pesos tem no máximo `GL_MAX_ARRAY_TEXTURE_LAYERS` blocos (2048 na HD 530; além
  disso os blocos a mais ficam com a camada 1 e há um aviso no log); pintar refaz o mipmap da camada inteira do
  bloco a cada quadro.
- `test_runtime_systems` travou uma vez no `ctest -j4` de 27/10/2026 (encerrado pelo PID); sozinho passou 150 vezes
  seguidas e o `ctest` inteiro passou de novo. Ficar de olho. Travou de novo no `ctest` da Tarefa 14 (o processo não
  sai nem pelo PID; os arquivos presos foram renomeados para `*.presa-12844` em `build/bin`); com `--timeout 120` o
  `ctest` inteiro passou.
- Limitações rápidas (27/10/2026): a recuperação das alturas só vale para terreno com arquivo (o de um terreno novo, sem `.mterrain`, ainda
  não entra) e pede o mesmo tamanho de bloco e resolução do arquivo; se o `.mterrain` mudou por fora, as mudanças
  são aplicadas por cima do que estiver lá.
- Pincelada por bloco (27/10/2026): a pincelada pelo MCP (`terrain_brush`) ainda remonta a cena inteira, porque o
  editor marca a cena depois de cada pedido do MCP; só o arraste no Viewport usa a troca por bloco, e esse arraste
  não foi testado na mão (o MCP não move o mouse). Mover, girar ou escalar o terreno por script sem `MarkRenderDirty`
  continua sem aparecer, como nos outros estáticos.
