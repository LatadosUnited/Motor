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

- [x] Tarefa 10: Formato `.mstyle` com seletores por tipo, classe e nome e estados `:hover`, `:active`, `:disabled`, `:focus` — e4995c3
- [x] Tarefa 11: Cascata (folhas do documento, estilo padrão da engine, valor no próprio elemento) e herança de cor e fonte — e4995c3
- [x] Tarefa 12: Propriedades visuais: fundo, borda, cantos arredondados, 9-slice, opacidade de grupo, contorno e sombra do texto — e4995c3
- [x] Tarefa 13: Transições simples de cor, opacidade e escala entre estados — e4995c3

### Etapa 5 — Editor de UI

- [x] Tarefa 14: Janela Editor de UI: árvore do documento, prévia e propriedades do elemento selecionado — cb6e16d
- [x] Tarefa 15: Prévia por resolução (16:9, 21:9, 4:3, retrato) e escala da tela — cb6e16d
- [x] Tarefa 16: Arrastar da paleta, reordenar na árvore, redimensionar na prévia, com desfazer — cb6e16d
- [x] Tarefa 17: Edição de classes e regras do `.mstyle` na janela, vendo o resultado na hora — cb6e16d
- [x] Tarefa 18: Recarregar `.mui` e `.mstyle` alterados fora do editor, também no Play — cb6e16d

### Etapa 6 — Scripts, controles e MCP

- [x] Tarefa 19: Consultas `Q` e `Query` por nome, classe e tipo, e eventos `OnClick`, `OnChange`, `OnHover`
- [x] Tarefa 20: Controles barra de progresso, slider, caixa de seleção e campo de texto
- [x] Tarefa 21: Navegação por foco com teclado e controle, pelo contexto de entrada Menu
- [x] Tarefa 22: Elemento que segue uma entidade 3D (barra de vida e nome sobre o personagem)
- [x] Tarefa 23: Ferramentas MCP para criar e editar `.mui` e `.mstyle`, e `ui_click` por consulta
- [x] Tarefa 24: Converter a UI por entidades de uma cena num documento
- [x] Tarefa 25: Seção do guia de scripts e menu do jogo de exemplo com documento, rodados pelo `test_script_docs`

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

- Tarefas 10 a 13: `engine/include/motor/ui/UiStyle.h`. O `.mstyle` (`AssetType::UiStyle`, GUID no `.meta`) é JSON
  `{"format": "motor-style", "version": 1, "rules": [{"selector": "Button.menu:hover", "radius": 12, ...}]}`; o
  seletor é composto (tipo ou `*`, `.classe`, `#nome`, `:hover`, `:active`, `:disabled`, `:focus`), com lista por
  vírgula e especificidade como no CSS (nome, depois classes e estados, depois tipo; empate: a regra e a folha de
  depois). Uma tabela só de propriedades de estilo (nome, leitura e gravação no JSON, herdada ou não) serve o `.mui`
  e o `.mstyle`. O `.mui` passou para a versão 3 (`"styles"`: folhas por GUID ou caminho); cada elemento guarda o
  conteúdo e as declarações escritas nele (`inlineStyle`), e os campos de estilo do `UiElement` viraram os valores
  calculados por `ComputeUiStyles`: padrão do tipo, folha padrão da engine (no código, com o visual de sempre),
  folhas do documento e o elemento; `textColor`, `font`, `fontSize` e `align` descem do pai. As versões 1 e 2 são
  lidas com o mesmo resultado de antes (as chaves viram declarações; o que o pai declarava de texto não desce). O
  gravador grava só o declarado; documento montado em código (sem `ComputeUiStyles`) continua usando os campos, e o
  gravador grava os que diferem do padrão do tipo. `ResolveUiDocumentAssets`/**Recarregar** leem as folhas e as fontes
  citadas nelas. A cascata roda só quando muda algo (estado de um elemento, `SetStyle`, folhas); o layout guarda o
  resultado por documento (assinatura do que muda as caixas) e só refaz quando ela muda. Visual novo: `radius` (um
  círculo de 128x128 feito na CPU, desenhado em 9 pedaços, sem mexer no shader), `borderWidth`/`borderColor`,
  `slice` (9-slice da imagem), `opacity` (desce para os filhos), `textOutlineWidth`/`textOutlineColor` (8 cópias),
  `textShadowOffset`/`textShadowColor`, `scale` (só no desenho, em volta do centro) e `transition` (fundo, cor do
  texto, borda, opacidade e escala, com ease-out, andando no `UiSystem` durante o Play; fora dele pula). Scripts:
  `UiElement::SetStyle`/`SetStyleValue`/`ClearStyle`. Teste novo: `test_ui_estilo` (também o menu de 50 elementos e
  10 regras: quadros parados não refazem estilo, layout nem geometria, conferido pelo `UiCanvasStats` e pelos
  contadores `styleRuns`/`layoutRuns` do documento).
  Conferido pelo MCP no projeto de teste com um `Tema.mstyle` e um `.mui` versão 3 ligado por `component_add`: na
  visão do jogo o painel e os botões aparecem arredondados e com borda, o título com contorno e sombra, o "Sair" (não
  clicável) mais apagado pelo `:disabled` e o texto da dica com a cor herdada do painel; no Play, dois `ui_click` em
  `jogar` deram dois `jogar clicado` no `log_read` e "Cliques: 2"; depois do Stop, mudar a cor dos botões no
  `.mstyle` e religar o documento (`component_set`, o mesmo `ReloadUiDocument` do **Recarregar**) mostrou a cor nova.
  O `:hover` e a transição não dão para ver pelo MCP (o `ui_click` só deixa o mouse no botão por dois quadros):
  conferidos no `test_ui_estilo`.

- Tarefas 14 a 18: janela **Editor de UI** (`editor/src/uidoc/`), aberta por Ver > Editor de UI ou com duplo clique
  (ou "Abrir no Editor de UI") num `.mui` no painel Projeto. À esquerda a paleta (Painel, Texto, Imagem, Botão: clique
  põe no selecionado, arrastar põe na árvore ou na prévia) e a árvore (tipo, nome e classes; arrastar reordena e muda de
  pai, com faixa de antes/depois/dentro; menu com adicionar dentro, duplicar e excluir). No meio a prévia: uma cena só
  com um fundo e uma entidade com o documento, desenhada pelo mesmo `UiRenderer` do jogo num framebuffer na resolução
  escolhida (16:9, 16:9 HD, 21:9, 4:3, 16:10, retrato 1080x1920 ou personalizada; zoom "Ajustar" ou 25 a 150%; a UI
  escala pela altura, como no jogo). O clique seleciona pelo `UiHitTestNode` (o caminho do elemento na árvore viva vale
  no documento editado) e as 8 alças redimensionam: grava `width`/`height` em pixels da referência e, por âncora, a
  posição que mantém o lado parado (com `Stretch`, as margens). À direita as propriedades (tipo, nome, classes digitadas,
  texto, visível, interativo, imagem e as 36 propriedades de estilo por grupo, com o valor calculado em cinza; mudar
  declara no elemento, "x" tira) e a aba **Estilos** (folhas do documento, acrescentar e tirar folha; regras com seletor
  validado, propriedades com o mesmo editor, regra nova, "*" nas regras que valem para o selecionado), com a prévia
  mudando na hora. Desfazer próprio (`UiEditHistory`: fotos do documento, das folhas e da seleção; Ctrl+Z/Ctrl+Y com a
  janela em foco, que também tem Ctrl+S, Ctrl+D e Del), título com `*`, e fechar a janela, abrir outro documento ou sair
  do editor pergunta Salvar/Descartar/Cancelar. Gravar escreve o `.mui` (`SaveUiDocument`) e só as folhas mudadas.
  Recarga (Tarefa 18): `UiDocReloader` confere a data dos `.mui` usados na cena e das folhas deles na mesma varredura dos
  materiais (pasta mudada, a cada 2 s; no Play, sempre), e logo depois de o Editor de UI gravar; o documento mudado é
  relido uma vez e posto em todos os `UiDocument` da cena aberta, da cena de antes do Play e das fotos do desfazer (a
  árvore viva é refeita, também no Play). A janela relê sozinha o arquivo mudado fora se não tiver alterações; senão
  avisa e oferece reler. Engine: `UiStylePropertyEnumNames` (nomes dos valores de uma propriedade enum, para os combos).
  MCP: `ui_editor_open`, `ui_editor_state`, `ui_editor_capture`, `ui_editor_select` (por nome ou pelo ponto da prévia),
  `ui_editor_element` (add, move, delete, duplicate, resize pelas alças, set), `ui_editor_style` e `ui_editor_history`.
  Teste novo: `test_ui_editor` (caminhos, inserir/mover/duplicar com nomes únicos, classes, alças no fluxo, por âncora e
  esticado, seletor, desfazer com a versão de "não salvo" e a recarga dos arquivos mudados em duas cenas).
  Conferido pelo MCP no projeto de teste com o `Menu estilo.mui` e o `Tema.mstyle`: a prévia igual à visão do jogo em
  1920x1080, 2560x1080, 1440x1080 e 1080x1920; um botão da paleta no menu (classes, texto "Opções", movido para o
  terceiro lugar), uma imagem presa no canto de baixo à direita e redimensionada pelas alças de cima e da esquerda (a
  posição ficou, o tamanho foi para 200x150), o "Continuar" redimensionado no fluxo (400x94), a regra `Button` com fundo
  azul e uma regra `.destaque` nova; dois desfazer voltaram as duas regras (verde de novo), refazer trouxe o azul, e
  gravar escreveu o `.mui` e o `.mstyle` com isso. No Play, com o documento numa entidade com o script `MenuDoc`, mudar
  o título no `.mui` e a cor dos botões no `.mstyle` direto no disco apareceu na visão do jogo em menos de 4 s (log "UI:
  documento relido"), o `ui_click` continuou contando, e depois do Stop a cena ficou com o arquivo novo. A janela,
  capturada com o `PrintWindow`, mostra a árvore, a prévia com as alças no selecionado, as propriedades e o `*` no título.

- Tarefas 19 a 25: **consultas e eventos** no `UiSystem`: `Q`/`Query` por seletor (o parser das folhas; um nome
  sozinho acha pelo nome; `ParseUiQuery`, `UiQueryMatches` e `UiDocumentData::Q`/`Query` em `UiDocument.h`) e
  `OnClick`, `OnChange`, `OnHover` (por seletor ou num elemento), `ClearEvents`. Os callbacks ficam nos elementos da
  árvore viva (`UiElement::handlers`), com o script e a entidade que estavam rodando ao registrar
  (`SceneRuntime::RunningScript`); o `UiSystem` junta os eventos do passo e chama depois do seu `OnUpdate` pelo
  `SceneRuntime::CallScriptCallback` (que pula script parado, com erro ou inativo, e diz se ele sumiu: o callback sai).
  Na recarga da DLL, o `ForgetScripts` chama o gancho novo `RuntimeSystem::OnScriptsForgotten` antes de descarregar, e
  o `UiSystem` solta todos os callbacks (o `std::function` aponta para código da DLL antiga); no `OnStop` também. A
  árvore relida do arquivo começa sem eles. **Controles** (`UiElementType::ProgressBar`, `Slider`, `Toggle`,
  `TextField`): campos `value`, `minValue`, `maxValue`, `step`, `checked`, `placeholder`, `maxLength`, a propriedade de
  estilo `fillColor` e regras na folha padrão; o `UiRenderer` desenha as partes (preenchimento, trilho e pino, caixa e
  marca, placeholder apagado e cursor) como retângulos extras do mesmo lote. Slider arrastado com o mouse (também fora
  dele, até soltar), Toggle troca no clique, TextField ganha o foco no clique e edita em UTF-8 (texto do
  `Input::TextInput`, Backspace, Delete, setas, Home, End, `maxLength` em caracteres; Enter = `OnClick`, Esc ou Voltar
  sai). **Foco:** `UiElement::focused` (`:focus`, com borda amarela na folha padrão); só com a ação Navegar ativa (o
  contexto Menu) e o jogo com a entrada ligada; direção pela ação (setas, WASD, direcional, analógico, com repetição a
  0,4 s e depois 0,12 s) e o vizinho pelo centro das caixas (distância na direção + 2x a de lado); Tab e Shift+Tab na
  ordem do layout; Confirmar clica o botão e troca a caixa; no slider, esquerda e direita mudam o valor (`step` ou 5%).
  **Seguir entidade:** `follow` e `followOffset` num elemento `Absolute`; depois do layout flex (o cache continua
  valendo), o `ComputeUiLayout` projeta a posição da entidade pela câmera principal da cena (na proporção da tela) e
  desloca a subárvore; atrás da câmera ou sem câmera, a subárvore sai da lista. Formato `.mui` versão 4 (lê 1 a 3;
  campos novos só gravados quando diferem do padrão do tipo). **Editor de UI:** os quatro controles na paleta e as
  propriedades deles (valor, mínimo e máximo, passo, marcado, placeholder, máximo de caracteres, segue entidade e
  deslocamento). **MCP:** `ui_document_edit` e `ui_style_edit` criam e mudam `.mui`/`.mstyle` direto no arquivo por
  uma lista de operações (tudo ou nada; `editor/src/uidoc/UiDocFileOps`, a mesma lógica do `set` do
  `ui_editor_element`) e avisam a recarga; `ui_click` aceita `query`; `ui_convert_entities`. **Converter:**
  `ConvertUiEntities` (`motor/ui/UiConvert.h`: cada entidade de UI vira um elemento `Absolute` com a mesma âncora,
  posição e tamanho, sem padding; botão com as cores e o texto do UiText; imagem com UiText ganha um Text esticado
  filho; irmãos pela `order`) e, no editor, "Converter UI em documento" na Hierarquia (`editor/src/uidoc/UiDocConvert`):
  grava `Assets/UI/<nome>.mui` (com número se já existir) e troca as entidades por uma com UI: Documento, com desfazer
  da cena. `kScriptApiVersion` 22. Guia: seção "Menus em documento de UI" no `docs/scripts.md`, exemplo
  `docs/examples/MenuDocumento.cpp` rodado no `test_script_docs` (eventos do script, slider, caixa, campo, foco pelo
  teclado, troca de cena e a limpeza na recarga), e a referência no `docs/scripts-api.md`. Teste novo:
  `test_ui_controles` (consultas, controles, formato 4 e leitura do 3, eventos e a limpeza, campo de texto, foco,
  seguir entidade, conversão com as mesmas caixas e as operações JSON das ferramentas MCP).
  Na folha padrão, o puxador do slider é claro, e a borda (`:focus`) do slider e da caixa de seleção vai em volta do
  puxador e da caixa (o slider não tem fundo e o da caixa é transparente). Conferido pelo MCP no projeto de teste: o
  `Opcoes.mui` (painel com título, três botões `.menu`, slider, caixa, campo e a barra `#vida` seguindo o "Cubo") e o
  `Opcoes.mstyle` criados só com `ui_document_edit` e `ui_style_edit` aparecem na visão do jogo e não na cena (sem
  seleção); no Play, com o script `OpcoesDoc` (eventos no `OnStart`, `SetInputContext("Menu")`, `Focus("#jogar")`),
  `ui_click` com `query` "#jogar" e "Button.menu#jogar" deram "OnClick jogar (1)" e "(2)" no `log_read` e o título virou
  "Cliques: 2" (`#naoexiste` responde erro); `game_key` baixo, baixo, direita, direita levou o foco ao slider e deu
  "OnChange volume 0.60" e "0.70"; `game_gamepad` DpadDown e A trocaram a caixa ("OnChange som false"); clique no
  campo, `game_text` "Léo Ação", Backspace e Enter deram "OnChange nome" e "OnClick nome (enviar) 'Léo Açã'", com o
  cursor desenhado; a barra de vida acompanhou o cubo indo de um lado ao outro. Recompilar o script no Play trocou o
  código sem chamar o callback antigo (só "OnClick jogar (1) [código novo]" depois); um botão acrescentado ao `.mui` por
  `ui_document_edit` no Play apareceu (log "UI: documento relido") e o `ui_click` achou ele. `ui_convert_entities` num
  "HUD" com dois filhos de UI por entidades gravou `Assets/UI/HUD.mui` (versão 4) e a captura da visão do jogo ficou
  igual pixel a pixel nos dois cantos; `undo` trouxe as entidades de volta e `redo` o documento.

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)

Da Etapa 3, por ora: sem `flex-wrap` (os filhos não pulam de linha), sem tamanho mínimo e máximo, sem rolagem
horizontal nem barra de rolagem desenhada; `grow` reparte a sobra além do tamanho do conteúdo (como o
`flex-basis: auto` do CSS: dois botões com `grow` 1 e rótulos diferentes ficam com larguras diferentes); palavra
maior que a largura passa dela em vez de quebrar no meio; o `ui_click` mira o centro do elemento mesmo se ele
estiver meio recortado numa lista.

Da Etapa 4, por ora: seletores só compostos (sem descendente, filho nem irmão: `Panel .item` é recusado com aviso);
`:hover` e `:active` só no botão clicável sob o mouse (os pais e os painéis não ficam com `:hover`); `:focus` existe
no seletor, mas nada liga o foco ainda (Etapa 6); a opacidade é por elemento, multiplicada nos filhos, não uma camada
de grupo de verdade (filhos que se sobrepõem aparecem um através do outro); a borda é a forma inteira na cor da borda
por baixo do fundo encolhido, então um fundo translúcido deixa ver a cor da borda; cantos arredondados só no fundo de
cor (a imagem com textura não é recortada nos cantos); cada elemento arredondado usa a textura do círculo e troca de
textura no lote com o texto (mais chamadas de desenho num menu com muitos botões arredondados); `scale` não muda o
clique nem o recorte dos filhos de uma lista; `SetStyle("font", ...)` só acha uma fonte que o documento já carregou
(numa folha ou num elemento); o cache do layout guarda um tamanho de tela por documento (a cena com a UI ligada e a
janela Jogo ao mesmo tempo refazem o layout a cada quadro); campo de estilo mudado direto num script volta ao da
cascata no próximo cálculo (use `SetStyle`); a exportação do jogo ainda não segue as referências de dentro do `.mui`
e do `.mstyle` (folhas, fontes e imagens citadas só neles).

Da Etapa 5, por ora: o desfazer da janela é por fotos inteiras do documento (até 200 passos; documentos grandes gastam
memória) e recomeça quando o arquivo é relido do disco; a prévia não tem `:hover` nem transições (sem `UiSystem` nela) e
não mostra a cena do jogo atrás; fonte nova declarada numa regra só aparece na prévia depois de gravar (as fontes das
folhas são carregadas a partir do disco); as alças gravam pixels mesmo num tamanho em porcentagem; não dá para
arrastar um elemento na prévia para mudar de lugar (só pelas alças e pela árvore), nem criar uma folha `.mstyle` nova
pela janela (só acrescentar uma que existe); a recarga no Play refaz a árvore viva do arquivo, então o que os scripts
mudaram nela (`SetText`, `SetVisible`, estilo) volta ao do arquivo, sem avisar o script (o "Cliques" voltou a 0 no
teste); as fotos antigas do desfazer da cena ganham o documento novo, então desfazer não traz o conteúdo velho do
arquivo; arrastar na árvore e na paleta, as alças com o mouse, os atalhos, a aba Estilos e o popup de salvar foram
conferidos só pela lógica e pelo MCP, não com o mouse.

Da Etapa 6, por ora: `kScriptApiVersion` subiu para 22 e o `.mui` para a versão 4 (o editor antigo não abre um `.mui`
novo com controles). Os callbacks são soltos na recarga da DLL e na releitura do documento, e o script registra de novo
no `OnStart`; mas um callback registrado fora de um script (do próprio jogo, sem dono) também sai na recarga. O
`OnHover` vale só para controles (botão, slider, caixa, campo), como o `:hover`. A borda da caixa de seleção sempre vai na
caixa, nunca em volta do elemento inteiro, mesmo com um fundo opaco. O campo de texto não tem seleção,
copiar e colar nem rolagem: o texto maior que a caixa encosta à direita (mostra o fim), e não dá para clicar numa
posição do texto (o clique põe o cursor no fim); Backspace e as setas seguradas não repetem. O foco não pula para dentro de listas com rolagem (não rola até o elemento) e a navegação
espacial olha só os centros das caixas. O elemento que segue uma entidade usa a câmera principal da cena (não a da
janela Jogo de outra cena) e o nome da entidade (a primeira com esse nome; renomear quebra a ligação), e o `ui_click`
continua mirando o centro do elemento. A conversão de UI por entidades recusa entidades com componentes que não são de
UI (um script no meio da UI) e documentos aninhados; a imagem com textura vira `image` com o GUID, mas o 9-slice e as
fontes de UiText com fonte própria passam como estão; o `.mui` gravado não sai com o desfazer da cena (só a troca das
entidades). `ui_document_edit` e `ui_style_edit` não passam pelo desfazer da janela do Editor de UI (a janela relê o
arquivo se não tiver alterações; com alterações, avisa).
