---
tipo: fase
alpha: 4
fase: 28
titulo: Terreno e água
status: em andamento
prioridade: 2
areas: [renderer, scene, physics, editor-viewport, editor-mcp]
branch: fase-28-terreno
inicio: 2026-10-26
fim:
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
- [ ] Tarefa 7: Guardar os componentes em blocos contínuos por tipo
- [ ] Tarefa 8: Criar o componente Terrain com mapa de altura em blocos, um arquivo por bloco
- [ ] Tarefa 9: Pincéis do terreno no editor: subir, baixar, suavizar
- [ ] Tarefa 10: Pintar até 4 camadas de textura no terreno
- [ ] Tarefa 11: Dar ao terreno um collider da física (height field da Jolt)
- [ ] Tarefa 12: LOD do terreno por blocos, sem frestas entre níveis
- [ ] Tarefa 13: Criar a água: ondas, reflexo do céu, refração e cor pela profundidade
- [ ] Tarefa 14: Ferramentas MCP do terreno e da água

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

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)

- `RenderWorld::QueryBox` ainda não tem quem a use: a seleção por retângulo do viewport e as consultas de área do
  terreno (Tarefas 8 e 9) são as candidatas. O raycast continua testando os não estáticos um a um.
- Desenho pela GPU (Tarefa 6): materiais diferentes ainda são draws separados (os parâmetros vão em uniforms e as
  texturas em unidades fixas); juntá-los pediria os materiais num buffer e texturas sem vínculo (bindless). Com pele,
  um draw por item. Os objetos ainda são copiados inteiros para o anel a cada cena, mesmo os estáticos parados.
- `test_desempenho` falhou em 27/10/2026 com o binário antigo e o novo do mesmo jeito (testes de cena que não passam
  pelo renderer 30-140% acima da base): a máquina estava mais lenta. Repetir antes de fechar a fase.
