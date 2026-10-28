---
tipo: fase
alpha: 5
fase:
titulo: Interface em documentos
status: em andamento
prioridade: 2
areas: [ui, editor, scene, editor-mcp]
branch: interface-documentos
inicio: 2026-10-27
fim:
---

# Interface em documentos

## Objetivo

Hoje a UI do jogo é feita de entidades (`UiRect`, `UiText`, `UiImage`, `UiButton` em `engine/include/motor/ui/`):
cada texto e botão aparece na Hierarquia junto com o cenário, o HUD é desenhado por cima do viewport da cena
(`m_showUi = true` em `editor/src/viewport/SceneViewport.h`) e o clique nele tem prioridade sobre a seleção 3D
(`editor/src/viewport/SceneViewport.cpp`). Um menu no centro da tela atrapalha editar a fase. O layout é só por
âncoras, então cada botão de um menu é posicionado à mão, e o estilo (cores, fonte, tamanho) é repetido em cada
elemento.

Esta fase troca isso por um modelo no estilo do **UI Toolkit da Unity** (pedido do usuário em 27/10/2026):

- **Documento de UI como asset:** um `.mui` (o papel do UXML) guarda a árvore de elementos (painel, texto, imagem,
  botão, barra), em JSON versionado, referenciado por GUID.
- **Folha de estilo:** um `.mstyle` (o papel do USS) com regras por tipo, classe (`.titulo`) e nome (`#vida`) e
  estados (`:hover`, `:active`, `:disabled`, `:focus`). Mudar o visual de todos os botões é mudar uma regra.
- **Componente `UiDocument`:** aponta para um `.mui` e tem ordem de desenho. A Hierarquia fica com uma entidade
  ("HUD", "Menu de pausa"); trocar de tela é trocar de documento ou desligar a entidade.
- **Layout flex:** coluna e linha, `gap`, crescer e encolher, alinhar, margem e preenchimento, tamanhos em pixels
  ou porcentagem. Implementação própria só com esse subconjunto, sem biblioteca nova (o Unity usa a Yoga; se o
  subconjunto não bastar, a troca vira uma nota em `decisoes/`).
- **Editor de UI em janela própria** (o papel do UI Builder), com prévia de resoluções. O viewport da cena volta a
  ser só 3D: a UI aparece na Game View e nessa janela, e no viewport da cena só se o botão "UI" for ligado.
- **Scripts por consulta e eventos:** `ui->Q("#jogar")` e `ui->Query(".item")` acham elementos; `OnClick`,
  `OnChange` e `OnHover` substituem o `WasClicked` lido todo quadro.

Do que já existe, continuam a `Font` SDF, o lote do `UiCanvas` sobre o `Renderer2D`, o hit test e o `UiPointer`;
muda de onde vem a árvore (do documento, não das entidades) e quem calcula as caixas (o flex, não as âncoras). Os
contextos de entrada da alpha 4 (Jogo, Menu) e os controles dão a base da navegação por foco. A UI por entidades
continua funcionando durante a fase (as cenas e os exemplos do guia usam), e uma conversão leva para o documento.

## Tarefas

### Etapa 1 — UI fora do caminho na cena

- [x] Tarefa 1: Viewport da cena com a UI desligada por padrão (preferência salva), a Game View sempre com UI — 7047d57

### Etapa 2 — Documento e componente

- [x] Tarefa 2: Formato `.mui` (árvore de elementos, JSON versionado, GUID no `.meta`) com leitura e gravação — 05f861d
- [x] Tarefa 3: Árvore de elementos em memória (`UiElement`: tipo, nome, classes, filhos, propriedades) — 05f861d
- [x] Tarefa 4: Componente `UiDocument` (documento, ordem, visível) e o `UiCanvas` desenhando documentos — 05f861d
- [x] Tarefa 5: `UiSystem` com hit test, hover e clique sobre os elementos do documento — 05f861d

### Etapa 3 — Layout flex

- [x] Tarefa 6: Layout flex próprio (direção, gap, grow e shrink, align e justify, margem, padding, porcentagem) — b0a7068
- [x] Tarefa 7: Posição absoluta dentro do pai, para o que hoje usa âncora (HUD nos cantos) — b0a7068
- [x] Tarefa 8: Texto com quebra de linha, corte e alinhamento vertical, medido pelo layout — b0a7068
- [x] Tarefa 9: Recorte dos filhos (`overflow: hidden`) e lista com rolagem — b0a7068

### Etapa 4 — Estilos

- [ ] Tarefa 10: Formato `.mstyle` com seletores por tipo, classe e nome e estados `:hover`, `:active`, `:disabled`, `:focus`
- [ ] Tarefa 11: Cascata (folhas do documento, estilo padrão da engine, valor no próprio elemento) e herança de cor e fonte
- [ ] Tarefa 12: Propriedades visuais: fundo, borda, cantos arredondados, 9-slice, opacidade de grupo, contorno e sombra do texto
- [ ] Tarefa 13: Transições simples de cor, opacidade e escala entre estados

### Etapa 5 — Editor de UI

- [ ] Tarefa 14: Janela Editor de UI: árvore do documento, prévia e propriedades do elemento selecionado
- [ ] Tarefa 15: Prévia por resolução (16:9, 21:9, 4:3, retrato) e escala da tela
- [ ] Tarefa 16: Arrastar da paleta, reordenar na árvore, redimensionar na prévia, com desfazer
- [ ] Tarefa 17: Edição de classes e regras do `.mstyle` na janela, vendo o resultado na hora
- [ ] Tarefa 18: Recarregar `.mui` e `.mstyle` alterados fora do editor, também no Play

### Etapa 6 — Scripts, controles e MCP

- [ ] Tarefa 19: Consultas `Q` e `Query` por nome, classe e tipo, e eventos `OnClick`, `OnChange`, `OnHover`
- [ ] Tarefa 20: Controles barra de progresso, slider, caixa de seleção e campo de texto
- [ ] Tarefa 21: Navegação por foco com teclado e controle, pelo contexto de entrada Menu
- [ ] Tarefa 22: Elemento que segue uma entidade 3D (barra de vida e nome sobre o personagem)
- [ ] Tarefa 23: Ferramentas MCP para criar e editar `.mui` e `.mstyle`, e `ui_click` por consulta
- [ ] Tarefa 24: Converter a UI por entidades de uma cena num documento
- [ ] Tarefa 25: Seção do guia de scripts e menu do jogo de exemplo com documento, rodados pelo `test_script_docs`

## Pronto quando

- O `test_ui` passa sem janela: `.mui` e `.mstyle` gravados e lidos de volta iguais (e um arquivo da versão
  anterior lido); o layout flex dá as caixas esperadas em coluna, linha, gap, grow, porcentagem e absoluto, em
  1920x1080 e 1080x1920; as regras de estilo se aplicam na ordem da cascata, `:hover` muda a cor; o texto quebra
  dentro da largura; o clique e o foco por teclado chegam ao botão certo; a conversão de uma cena com UI por
  entidades dá as mesmas caixas.
- Pelo MCP, num projeto de teste: um menu criado só com as ferramentas novas aparece na Game View
  (`viewport_capture`), o viewport da cena não mostra UI nem pega o clique, `ui_click` no `#jogar` dispara o
  `OnClick` do script (conferido no `log_read`) e o foco anda entre os botões com `game_gamepad`.
- Um menu com 50 elementos e 10 regras de estilo não refaz geometria nem layout em quadros sem mudança (contadores
  do `UiCanvasStats`).

## Feito

- Tarefa 1: o botão **UI** do Viewport virou preferência lembrada (`ViewportPrefs::ui`, `viewportUi` no
  `editor.json`), desligada por padrão, também para quem já tinha um `editor.json`. Na cena, a UI aparece com o
  botão ligado ou com um elemento de UI selecionado (quem cria um pelo menu ou escolhe na Hierarquia vê e arrasta
  sem ligar o botão); na visão do jogo e na janela Jogo, sempre. O `test_viewport` confere a gravação e o padrão.
  Conferido pelo MCP num projeto de teste com um painel de 900x700 no centro: na cena sem seleção o painel some e o
  cubo atrás dele aparece; selecionado, aparece; `view = game` e o Viewport no Play (sem nada selecionado) mostram
  o painel.
- Tarefas 2 a 5: `engine/include/motor/ui/UiDocument.h`. O `.mui` (`AssetType::UiDocument`, GUID no `.meta`) é
  JSON `{"format": "motor-ui", "version": 1, "elements": [...]}`; cada `UiElement` tem tipo (`Panel`, `Text`,
  `Image`, `Button`), nome, classes, âncora/posição/tamanho no pai, cores, imagem, texto e fonte, e filhos. Só o
  que difere do padrão do tipo é gravado; a leitura é tolerante (tipo desconhecido vira painel, com aviso) e
  recusa versão mais nova. O componente **UI: Documento** (`UiDocument`: documento, ordem, visível) lê o arquivo
  ao carregar a cena ou ao escolher no Inspector (com **Recarregar**) e guarda uma cópia viva, refeita a cada
  cópia do componente: o Play começa do arquivo e não suja a cena. O layout põe os elementos no lugar da entidade
  (ocupando o pai), o `UiCanvas` desenha no mesmo lote e cache por elemento, o hit test diz quem bloqueia
  (botão, imagem, painel com fundo; texto não) e o `UiSystem` acende e clica os botões dos documentos. Nos
  scripts, `WasClicked`, `SetText`, `GetText`, `SetVisible` e `IsVisible` caem no elemento com o nome quando não
  há entidade com ele, e `FindElement` dá o elemento. Entidade > Interface (UI) > Documento cria a entidade; o
  `ui_click` do MCP aceita `element`. Teste novo: `test_ui_documento`.
  Conferido pelo MCP num projeto de teste: um menu `.mui` ligado por `component_add` (o caminho achou o GUID e criou
  o `.meta`) aparece na visão do jogo e some da cena sem seleção; no Play, dois `ui_click` em `jogar` deram dois
  `jogar clicado` no `log_read` e "Cliques: 2" na tela pelo `SetText`; `sair` (não clicável) não clicou, e um nome
  que não existe deu erro. Depois do Stop a cena voltou a "Cliques: 0".
- Tarefas 6 a 9: o `.mui` passou para a versão 2 (a 1 continua lida: tudo por âncora, `size` em pixels, texto sem
  quebra). `engine/src/ui/UiFlexLayout.cpp` faz um subconjunto do flex, sem biblioteca: `direction` (coluna ou
  linha), `gap`, `padding`, `margin`, `grow` e `shrink` (na proporção do tamanho), `justify` (Start, Center, End,
  SpaceBetween, SpaceAround), `alignItems`/`alignSelf` (Stretch é o padrão), `width`/`height` em pixels,
  porcentagem do pai ou automáticos (o texto medido pela fonte e os filhos). `placement: Absolute` tira o elemento
  do fluxo e o prende por âncora no pai inteiro. O texto quebra nos espaços (`wrap`, padrão do Text), tem
  `verticalAlign` e é medido com a mesma fonte que desenha (`LoadUiFont`, compartilhada entre o layout e o
  `UiCanvas`). `overflow: Hidden` recorta os filhos na CPU (o quadrado e as coordenadas de textura, sem mexer no
  `Renderer2D`) e no clique; `Scroll` também rola na vertical com a roda (`kUiScrollStep` por entalhe, preso entre
  0 e `scrollMax`), e numa lista que rola os filhos não encolhem. MCP: `ui_scroll` (mouse no elemento e a roda).
  Teste novo: `test_ui_flex`, em 1920x1080 e 1080x1920.
  Conferido pelo MCP com um menu flex (título, parágrafo que quebra, linha de botões com `grow`, lista com 6 fases
  de altura 200): tudo no lugar na visão do jogo; `ui_click` em `jogar` contou o clique; `ui_scroll` de -2 mostrou
  as fases 3 a 5 recortadas na lista e de -20 parou na 6, sem passar do fim.

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)

Da Etapa 3, por ora: sem `flex-wrap` (os filhos não pulam de linha), sem tamanho mínimo e máximo, sem rolagem
horizontal nem barra de rolagem desenhada; `grow` reparte a sobra além do tamanho do conteúdo (como o
`flex-basis: auto` do CSS: dois botões com `grow` 1 e rótulos diferentes ficam com larguras diferentes); palavra
maior que a largura passa dela em vez de quebrar no meio; o `ui_click` mira o centro do elemento mesmo se ele
estiver meio recortado numa lista.
