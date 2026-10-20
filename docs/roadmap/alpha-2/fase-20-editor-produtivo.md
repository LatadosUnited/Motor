---
tipo: fase
alpha: 2
fase: 20
titulo: Editor produtivo
status: pronta
prioridade: 5
areas: [editor, mcp, assets]
branch: fase-20-editor-produtivo
inicio: 2026-10-19
fim: 2026-10-19
---

# Fase 20 — Editor produtivo

## Objetivo

Montar cenas grandes só com o editor, sem cansar: selecionar vários, copiar e colar, alinhar na grade, achar
na hierarquia, e prefabs e materiais que se comportam como o resto.

## Tarefas

- [ ] ~~**Antes de tudo, dividir os arquivos que esta fase vai mexer**, um por commit, sem mudar comportamento:
  `ProjectPanel.cpp` (~1200 linhas), `SceneViewport.cpp` e `SceneDocument.cpp` (~600).~~ Feito na
  [Organização do código](organizacao-do-codigo.md). *(origem: revisão do código de 17/10/2026)*
- [x] **Seleção múltipla** na hierarquia e no viewport (Ctrl/Shift, retângulo), com gizmo e Inspector para vários. (59e5fb3)
- [x] **Copiar, recortar, colar e duplicar** várias entidades (com filhos), também entre cenas. (e433492)
- [x] **Snapping** na grade, em ângulos e em passos de escala; grade visível configurável. (bf1fae9)
- [x] **Busca na hierarquia** (nome e tipo de componente). (553051b)
- [x] **Contorno verde do collider só no objeto selecionado**, como na Unity. Hoje o viewport desenha o de todos
  os objetos com collider o tempo todo (`SceneViewport::DrawColliders`, `editor/src/viewport/SceneViewportOverlay.cpp`),
  e desde a Fase 18 toda forma criada já vem com Collider: a cena enche de linhas e o custo na CPU cresce com o
  número de objetos (12 a ~100 linhas cada, refeitas a cada quadro). O botão **Colliders** da barra vira
  **Selecionado / Todos / Nenhum** (padrão Selecionado), lembrado entre aberturas. *(origem: teste à mão da
  Fase 19, 19/10/2026, gravidade atrapalha)* (a826ec4)
- [x] **Janelas Cena e Jogo lado a lado** no Play. (0dbd735) *(origem: Fase 2, limitação)*
- [x] **Prefab dentro de prefab.** (08a1958) *(origem: Fase 6, limitação)*
- [x] **Edições de material e de prefab no desfazer**, e editadas no Play voltam ao parar. *(origem: Fase 6,
  limitação)* (69fcdd7)
- [x] **Modelos:** malhas de modelo no combo "Malha"; pré-visualizar animação fora do Play. *(origem: Fase 11,
  limitação)* (37b1574)
- [x] **MCP acompanha:** seleção múltipla, copiar/colar, mover a câmera do editor para um ponto, eventos para o
  agente. *(origem: Fase 13, pendências)* (3cf71a1)

### MCP e agentes (retorno de uso, 19/10/2026)

Um agente fez um personagem em primeira pessoa (Parado → Andar → Correr → Pular) só pelo MCP e validou tudo
por números, sem nunca ver o personagem animado. *(origem: pedido do usuário, relato de uso do MCP)*

- [x] **Captura com o editor sem foco:** `viewport_capture` desenha a cena na hora num framebuffer próprio
  (vale minimizado, sem foco ou com a janela escondida); `view = game` não precisa mais da janela Jogo, e
  `view = editor` mostra pela câmera do editor no Play. (c21dfb8)
- [x] **Estado do jogo no `entity_get` durante o Play:** `runtime` com o quadro, o Animator (clipe, tempo,
  duração; na entidade ou num filho), `grounded` e `velocity`/`speed`/`horizontal_speed`. (c21dfb8)
- [x] **Esperar o fim das ações assíncronas:** `wait: true` (com `timeout`) no `play` e no `scripts_build`. (c21dfb8)
- [x] **Documentação da API de scripts certa:** a cópia velha (API 13) estava dentro da pasta do jogo, posta à
  mão; a do Motor 0.2.650 já dizia 14 e o `test_script_docs` confere a versão. Ferramenta nova `docs_read` (a
  documentação da versão que está rodando, por seção) e `script_api` no `editor_status`; as instruções do
  servidor mandam ler por ela. (c21dfb8) O `editor_status` mostra em `script_reference` os caminhos da
  documentação e dos headers do SDK desta versão, para o agente ler com as próprias ferramentas.
- [x] **Esconder só a malha:** campo **Sombra** no Mesh Renderer: Com sombra, Sem sombra ou **Só sombra** (some da
  imagem e continua fazendo sombra; o Animator segue). `kScriptApiVersion` = 15. (5ca1280)
- [x] **Primitivas com collider:** as do `entity_create` já vinham com Collider desde a Fase 18; o que vinha sem
  era a cena inicial dos projetos novos (Chão e Cubo), agora com Collider. (aeafe4e)
- [x] **Rotação devolvida como foi dada:** `Transform::EulerDegrees` escolhe a leitura que menos gira em X e Z
  (`[0, 180, 0]` volta `[0, 180, 0]`); usada no `entity_get`, no Inspector e na edição de vários. (500a4f6)
- [ ] **Importar FBX:** hoje só glTF, então personagens do Character Creator 5 ou do Mixamo passam pelo Blender
  antes. Precisa de biblioteca nova (aprovação do usuário) ou de conversão automática na importação.

## Pronto quando

Montar uma cena com 50 objetos alinhados na grade, duplicados em grupo e reorganizados por busca é rápido só
com o editor, e o desfazer cobre tudo, inclusive materiais e prefabs.

## Feito

- **Contorno dos colliders por modo:** o botão da barra do viewport alterna **Selecionado** (padrão), **Todos** e
  **Nenhum** (`editor/src/viewport/ColliderOverlay`); em Selecionado o desenho não percorre a cena. A escolha fica
  no `editor.json` (`viewportColliders`; arquivo antigo sem o campo abre em Selecionado). Teste `test_viewport`
  e item novo na seção 3 do [roteiro à mão](../../teste-manual.md). Conferido numa captura da janela: só o objeto
  selecionado tem contorno. O clique no botão e a troca de modo não foram testados (o MCP não clica na barra).
- **Seleção múltipla:** `EditorContext::selection` com a principal em `selected` (Inspector, gizmo, foco);
  `SyncSelection` reduz a seleção à principal quando alguém troca só ela (criar, MCP, desfazer), então o código
  antigo continua certo. Hierarquia: Ctrl+clique alterna, Shift+clique pega o intervalo, arrastar leva o grupo,
  clique simples num dos selecionados só reduz ao soltar sem arrastar. Viewport: Ctrl/Shift+clique alterna,
  arrastar no vazio faz o retângulo (centro da malha ou ícone dentro). Gizmo aplica a mudança da principal nos
  outros; Del, Ctrl+D e Ctrl+A (novo, menu Editar) agem em todos; o desfazer guarda a seleção. Inspector: mostra a
  principal e repete nos outros o que mudou nela, campo a campo, eixo a eixo no Transform, e componentes
  adicionados/removidos (`panels/MultiEdit`). MCP: `select` com `entities` e `add`, e `selection` no `scene_tree`
  e no estado do editor. Testes `test_selection` e casos no `test_mcp`; conferido numa captura com dois objetos
  selecionados pelo MCP. Ctrl/Shift+clique, retângulo e o gizmo com vários **não foram testados** (precisam de
  mouse; itens novos na seção 4 do roteiro à mão).
- **Copiar, recortar e colar:** Ctrl+C/X/V, menu Editar e clique direito da Hierarquia ("Colar como filho");
  `EntityClipboard` guarda cópias das sub-árvores numa cena própria com as raízes na posição do mundo, então colar
  sob outro pai ou em outra cena mantém o lugar. Referências entre entidades da cópia seguem as coladas; para
  fora da cópia, valem só na mesma cena (`EditorContext::sceneTag`), senão ficam vazias. Duplicar (Ctrl+D) já
  agia em vários desde a seleção múltipla. MCP: `entity_copy`, `entity_cut`, `entity_paste`. Testes em
  `test_selection` e `test_mcp`; conferido numa captura depois de copiar e colar pelo MCP. Os atalhos de teclado
  não foram testados (precisam de teclado; item novo na seção 4 do roteiro).
- **Encaixe e grade:** passos de mover, girar e escalar configuráveis na seta ao lado do Snap (com valores comuns
  num clique) e salvos no `editor.json` junto com o Snap ligado, a grade e a altura dela (`ViewportPrefs`).
  Mover com encaixe põe a posição na grade (`SnapToGrid`, só nos eixos mexidos; o Y conta a partir da altura da
  grade). Grade desenhada pelo renderer como wireframe (`CreateGridMesh`, linhas como triângulos degenerados), então
  respeita a profundidade da cena: uma linha a cada 10 passos do mover, eixos X e Z; de longe sobe de 10 em 10.
  As linhas finas (uma por passo) ficam desligadas por padrão ("Linhas finas" na seta do Snap): perto da câmera
  enchiam a tela de quadradinhos; a grade inteira começa desligada (botão **Grade**) *(origem: teste à mão do
  usuário, 19/10/2026)*. Testes no `test_viewport`; conferido em captura (grade, cubo tapando as linhas).
  O popup, o arraste com encaixe e a troca de altura não foram testados (precisam de mouse; item na seção 3).
- **Busca na hierarquia:** caixa ao lado do "+ Criar" (Ctrl+F, também no menu Editar). Palavras no nome e
  `t:componente` pelo rótulo ou pelo nome no código, todas precisam bater, sem diferença de maiúsculas e acentos
  (`panels/HierarchyFilter`). Com busca, a Hierarquia vira uma lista dos resultados na ordem da árvore, com o
  caminho do pai; clique, Ctrl/Shift, arrastar e o menu de contexto funcionam como na árvore, e "Selecionar todas"
  pega os resultados. MCP: `scene_search` (com `select`). Testes `test_hierarchy_search` e casos no `test_mcp`;
  conferida a caixa numa captura. Digitar na busca não foi testado (precisa de teclado; item na seção 4).
- **Cena e Jogo lado a lado:** Jogo > "Cena e Jogo lado a lado" abre a janela **Jogo** (`play/GameView`, com
  framebuffer e HUD próprios), dividindo o nó do Viewport na primeira vez (depois o `imgui.ini` lembra). Nesse
  modo o Viewport fica na câmera do editor (sem F8 nem o botão de câmera) e o jogo recebe a entrada e o mouse da
  UI com a janela Jogo em foco (`PlayController::GameHasInput`); o Play põe o foco nela. A escolha fica no
  `editor.json` (`playSideBySide`). MCP: `view_side_by_side` e `viewport_capture` com `view = game`. Conferido
  numa captura no Play (Viewport com a grade e a câmera do editor, Jogo pela câmera do jogo).
  Teclado, mouse e cursor preso na janela Jogo não foram testados (item na seção 8 do roteiro à mão).
- **Materiais e prefabs no desfazer:** `UndoHistory::PushAction` põe passos de fora da cena (funções de desfazer e
  refazer) na mesma fila das fotos. O Inspector registra um passo quando termina a edição de um material de asset
  ou de um prefab aberto nele (e grava o arquivo), e o desfazer põe o valor de antes de volta e grava de novo. No
  Play essas edições não são gravadas nem viram passo: o valor de antes fica guardado e volta ao parar
  (`RevertPlayAssetEdits`). Teste da fila no `test_selection`. A edição pelo Inspector não foi testada com o
  mouse (item novo na seção 4 do roteiro).
- **Modelos:** o combo "Malha" ganhou a seção "Modelos", com os `.glb`/`.gltf` do projeto; abrir um lista as malhas
  dele (o modelo só é lido nessa hora) e a escolhida fica como `Robo.glb#Corpo`, mostrada "Corpo (Robo.glb)". Abaixo
  do Animator, **Pré-visualizar** toca um clipe no viewport fora do Play: o Inspector avança o Animator a cada
  quadro; a pose é estado interno (não é campo), então a cena não muda nem entra no desfazer, e volta ao repouso
  ao parar, trocar a seleção ou dar Play. Conferido em captura (linha de pré-visualização no Inspector do Robo);
  tocar e o combo de malhas não foram testados com o mouse (item na seção 6 do roteiro).
- **MCP acompanha:** seleção múltipla (`select` com `entities` e `add`, `selection` no estado) e copiar/colar
  (`entity_copy`, `entity_cut`, `entity_paste`) vieram com os itens deles; `scene_search` com a busca;
  `view_side_by_side` e `viewport_capture` com `view = game` com o lado a lado. Novos: `view_camera` (move a câmera
  do editor para um ponto e a vira para outro; sem argumentos, diz onde ela está) e `events_read` (o que mudou no
  editor desde a última leitura: seleção, cena aberta/salva/modificada, entidades a mais ou a menos, Play e
  compilação dos scripts, inclusive o que o usuário fez à mão; comparado a cada 0,2 s com o servidor ligado).
  Conferidos no editor pelo MCP. Os eventos são lidos sob pedido: o servidor não empurra notificações.
- **Prefab dentro de prefab:** componente novo `PrefabNested` (engine, `scene/PrefabNested`): no conteúdo do
  prefab de fora, as entidades vindas de outro prefab guardam o vínculo com ele e os overrides relativos a ele; o
  `PrefabInstance` continua sendo a identidade dentro do de fora. `Prefabs::UpdateNested` (chamado por `NeedsSync`
  e `SyncInstances`) leva as mudanças do de dentro ao de fora, nível a nível, e o de fora ganha revisão nova (as
  instâncias da cena seguem como antes). Criar e aplicar aninham as instâncias de outros prefabs em vez de
  desfazer o vínculo; aplicar recusa um prefab dentro dele mesmo; desvincular faz as entidades voltarem a ser
  instâncias do de dentro. Editar o de fora no Inspector registra overrides do aninhado
  (`RecordNestedOverrides`). Teste `test_prefab_nested` (47 verificações); conferido no editor pelo MCP (Inspector
  e arquivo `.mprefab`). Mexer à mão nos prefabs não foi testado (item na seção 6 do roteiro).

- **MCP e agentes (retorno de uso):** conferido pelo MCP num projeto com o Robo.glb e o modelo Jogador: `play`
  com `wait` respondeu já no quadro 1; `entity_get` mostrou Parado → Andando → Pulando → Parado com `grounded`
  e a velocidade (4 m/s andando, 8 correndo); `viewport_capture` com a janela minimizada trouxe o robô na pose
  do momento; "Só sombra" nas malhas do robô o tirou da imagem e deixou a sombra no chão. Testes novos no
  `test_mcp` (rotação, collider, estado do jogo, sombra e a resposta com espera pelo HTTP).

## Limitações

- Fechada e juntada ao main sem o teste à mão dos itens novos (decisão do usuário, 19/10/2026): ficam no roteiro
  ([teste-manual.md](../../teste-manual.md), seções 3, 4, 6 e 8) e o que aparecer vai para a [entrada](../entrada.md).
- Em Selecionado, os colliders dos filhos do objeto selecionado não aparecem (como na Unity): selecione o filho
  ou use Todos.
- Com vários selecionados, girar e escalar pelo gizmo usam o pivô da principal (a Unity também oferece o centro do
  grupo e o pivô de cada um). Escala em objetos girados de forma diferente pode distorcê-los.
- O Inspector com vários mostra os valores da principal, sem marcar os campos que diferem entre os selecionados;
  o nome não é repetido. "Salvar como asset" do material com vários faz todos usarem o asset novo.
- O retângulo usa o centro de cada objeto, não a forma: um objeto grande só entra com o centro dentro.
- A grade não esmaece com a distância (o shader das linhas é do renderer): ela termina a ~10 células grossas da
  câmera para não virar ruído no horizonte. Tamanho da célula da grade e passo do mover são o mesmo valor.
- Com vários selecionados, só a principal vai para a grade; os outros andam o mesmo tanto.
- Lado a lado, a cena é desenhada duas vezes por quadro no Play (as sombras também); fora do Play, a janela Jogo
  só desenha quando está visível. Os botões de Play ficam no Viewport e, com ele estreito, cobrem as
  estatísticas. A janela Jogo usa a proporção dela (não há resoluções fixas como 16:9 ou 1920x1080).
- Ficam fora do desfazer: "Aplicar ao prefab" e criar prefab ou material (mexem em arquivos), materiais mudados por
  scripts no Play e mudanças em materiais e prefabs feitas pelo MCP.
- Prefab aninhado: a Hierarquia não destaca as entidades aninhadas (só o Inspector diz de onde vieram), e para
  mudar o prefab de dentro é preciso abri-lo pelo painel Projeto. Variante de prefab (um prefab que é só outro
  com mudanças) funciona como aninhado na raiz, sem nada específico. Mudanças no conteúdo de um prefab feitas
  pelo de dentro só são gravadas no arquivo dele quando ele é salvo de novo (ao abrir, ele segue o de dentro).
- A busca não procura em valores de campos (ex.: entidades que usam um material) nem guarda buscas recentes;
  a dobra de acentos cobre as letras latinas comuns (português, espanhol), não todo o Unicode.
- A área de transferência de entidades fica na memória do editor: não passa para outro editor aberto nem
  sobrevive a fechá-lo, e não usa a do Windows. Colar entre projetos diferentes não foi pensado (materiais e
  prefabs são do projeto de origem).
- `viewport_capture` desenha de novo a cena a cada pedido (custa um quadro a mais) e não mostra as janelas do
  editor nem o gizmo; para ver a interface, a captura da janela (PrintWindow) continua valendo.
- O estado do jogo no `entity_get` mostra o Animator mais próximo (a entidade ou o primeiro filho com um), não
  todos, e não diz qual entidade é a dele.
