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
- [ ] **Busca na hierarquia** (nome e tipo de componente).
- [x] **Contorno verde do collider só no objeto selecionado**, como na Unity. Hoje o viewport desenha o de todos
  os objetos com collider o tempo todo (`SceneViewport::DrawColliders`, `editor/src/viewport/SceneViewportOverlay.cpp`),
  e desde a Fase 18 toda forma criada já vem com Collider: a cena enche de linhas e o custo na CPU cresce com o
  número de objetos (12 a ~100 linhas cada, refeitas a cada quadro). O botão **Colliders** da barra vira
  **Selecionado / Todos / Nenhum** (padrão Selecionado), lembrado entre aberturas. *(origem: teste à mão da
  Fase 19, 19/10/2026, gravidade atrapalha)* (a826ec4)
- [ ] **Janelas Cena e Jogo lado a lado** no Play. *(origem: Fase 2, limitação)*
- [ ] **Prefab dentro de prefab.** *(origem: Fase 6, limitação)*
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
  respeita a profundidade da cena: célula = passo do mover, linha forte a cada 10, eixos X e Z; de longe as
  camadas sobem de 10 em 10. Testes no `test_viewport`; conferido em captura (grade, cubo tapando as linhas).
  O popup, o arraste com encaixe e a troca de altura não foram testados (precisam de mouse; item na seção 3).

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
- A área de transferência de entidades fica na memória do editor: não passa para outro editor aberto nem
  sobrevive a fechá-lo, e não usa a do Windows. Colar entre projetos diferentes não foi pensado (materiais e
  prefabs são do projeto de origem).
