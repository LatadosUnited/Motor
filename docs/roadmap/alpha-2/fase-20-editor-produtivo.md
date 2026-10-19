---
tipo: fase
alpha: 2
fase: 20
titulo: Editor produtivo
status: em andamento
prioridade: 5
areas: [editor, mcp, assets]
branch: fase-20-editor-produtivo
inicio: 2026-10-19
fim: 
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
- [ ] **Edições de material e de prefab no desfazer**, e editadas no Play voltam ao parar. *(origem: Fase 6,
  limitação)*
- [ ] **Modelos:** malhas de modelo no combo "Malha"; pré-visualizar animação fora do Play. *(origem: Fase 11,
  limitação)*
- [ ] **MCP acompanha:** seleção múltipla, copiar/colar, mover a câmera do editor para um ponto, eventos para o
  agente. *(origem: Fase 13, pendências)*

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
  enchiam a tela de quadradinhos *(origem: teste à mão do usuário, 19/10/2026)*. Testes no `test_viewport`; conferido em captura (grade, cubo tapando as linhas).
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
- **Prefab dentro de prefab:** componente novo `PrefabNested` (engine, `scene/PrefabNested`): no conteúdo do
  prefab de fora, as entidades vindas de outro prefab guardam o vínculo com ele e os overrides relativos a ele; o
  `PrefabInstance` continua sendo a identidade dentro do de fora. `Prefabs::UpdateNested` (chamado por `NeedsSync`
  e `SyncInstances`) leva as mudanças do de dentro ao de fora, nível a nível, e o de fora ganha revisão nova (as
  instâncias da cena seguem como antes). Criar e aplicar aninham as instâncias de outros prefabs em vez de
  desfazer o vínculo; aplicar recusa um prefab dentro dele mesmo; desvincular faz as entidades voltarem a ser
  instâncias do de dentro. Editar o de fora no Inspector registra overrides do aninhado
  (`RecordNestedOverrides`). Teste `test_prefab_nested` (47 verificações); conferido no editor pelo MCP (Inspector
  e arquivo `.mprefab`). Mexer à mão nos prefabs não foi testado (item na seção 6 do roteiro).

## Limitações

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
- Prefab aninhado: a Hierarquia não destaca as entidades aninhadas (só o Inspector diz de onde vieram), e para
  mudar o prefab de dentro é preciso abri-lo pelo painel Projeto. Variante de prefab (um prefab que é só outro
  com mudanças) funciona como aninhado na raiz, sem nada específico. Mudanças no conteúdo de um prefab feitas
  pelo de dentro só são gravadas no arquivo dele quando ele é salvo de novo (ao abrir, ele segue o de dentro).
- A busca não procura em valores de campos (ex.: entidades que usam um material) nem guarda buscas recentes;
  a dobra de acentos cobre as letras latinas comuns (português, espanhol), não todo o Unicode.
- A área de transferência de entidades fica na memória do editor: não passa para outro editor aberto nem
  sobrevive a fechá-lo, e não usa a do Windows. Colar entre projetos diferentes não foi pensado (materiais e
  prefabs são do projeto de origem).
