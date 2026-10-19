---
tipo: guia
atualizado: 2026-10-19
---

# Roteiro de teste à mão

O que os testes automáticos e o servidor MCP não alcançam: mouse e teclado de verdade, menus, arrastar, foco,
janelas do Windows. Uma pessoa segue este roteiro no **editor instalado pelo Hub** (não no `build/`) a cada
versão nova, marca o que passou e anota o que não passou. Criado na
[Fase 19](roadmap/alpha-2/fase-19-validacao.md).

## Como usar

1. Anote no topo da sua cópia a **versão** (Ajuda > Sobre o Motor), a data e o computador.
2. Siga as seções na ordem; cada linha diz **o que fazer → o que deve acontecer**. Marque `[x]` quando bater.
3. Quando não bater, anote uma linha na [caixa de entrada](roadmap/entrada.md) (ou mande para o Claude anotar)
   com a gravidade, o que fez e o que viu:

   | Gravidade | Quando |
   |---|---|
   | **bloqueia** | fecha o editor, perde trabalho, ou não deixa continuar o jogo |
   | **atrapalha** | dá para continuar, mas com contorno ou retrabalho |
   | **detalhe** | texto, visual, um clique a mais |

   Ex.: `- 2026-10-20 — Leandro (teste à mão) — atrapalha — arrastar um .png do Explorer para a pasta Texturas
   copia para Assets/ em vez da pasta sob o mouse.`
4. A seção 1 (instalação) pode ser pulada quando o instalador não mudou desde o último teste.

## 1. Instalação (pule se o instalador não mudou)

- [ ] `MotorSetup.exe` num computador sem o Motor → instala sem pedir administrador, cria os atalhos no menu
  Iniciar e na área de trabalho, aparece em Aplicativos instalados.
- [ ] Hub > Versões > baixar a versão do servidor → progresso, cancelar e **Retomar** funcionam; a versão aparece
  instalada só no fim.
- [ ] Desinstalar por Aplicativos instalados → atalhos e pasta saem; os projetos ficam.

## 2. Hub

- [ ] **Novo projeto** com nome com acento ("Jogo Ação") → o editor abre com a cena inicial (chão, cubo que
  gira, câmera).
- [ ] Fechar o editor e abrir de novo pelo cartão do projeto (**Abrir** e duplo clique) → mesma versão, mesma cena.
- [ ] Menu **...** do cartão: mostrar no Explorer, remover da lista (a pasta continua no disco), **Adicionar
  pasta** de volta.
- [ ] Busca filtra por nome e por pasta.

## 3. Viewport

- [ ] Segurar o botão direito + WASD/QE → voa; Shift acelera; a roda muda a velocidade. Soltar o botão devolve
  o cursor.
- [ ] Clique num objeto → seleciona (contorno), a hierarquia acompanha; clique no vazio e Esc → limpa.
- [ ] Q/W/E/R → nenhum, mover, rotacionar, escalar; X alterna Local/Global; arrastar o gizmo move de verdade.
- [ ] Ctrl durante o arraste e o botão **Snap** → passos de 0,5 m / 15° / 0,1.
- [ ] F → enquadra a seleção. Del exclui; Ctrl+D duplica.
- [ ] Do painel Projeto para o viewport: uma textura ou um `.mmat` sobre um objeto → aplica; um `.obj`, `.glb` ou
  `.mprefab` → cria a entidade onde o mouse aponta, apoiada no chão.
- [ ] Ver > Wireframe liga e desliga.
- [ ] **Grade e encaixe:** a grade aparece no chão (botão **Grade** liga e desliga) e o cubo tapa as linhas.
  Seta ao lado do **Snap** → mudar Mover para 1 → as células da grade dobram. Com Snap ligado, um objeto em
  X = 0,3 arrastado em X para na grade (1, 2...) e o Y/Z não mudam; girar anda de 15 em 15°. Altura da grade 2
  → a grade sobe e o Y encaixa a partir dela. Fechar e abrir o editor mantém tudo.
- [ ] Botão **Colliders** da barra do viewport: em **Selecionado** (padrão) só o objeto selecionado tem contorno
  verde; um clique passa para **Todos**, outro para **Nenhum**. Fechar e abrir o editor mantém a escolha.

## 4. Hierarquia, Inspector e desfazer

- [ ] Arrastar uma entidade sobre outra → vira filha; soltar no vazio → volta para a raiz.
- [ ] Clique direito na hierarquia → Criar, Criar filho, Renomear (F2), Excluir.
- [ ] **+ Criar** e menu Entidade: Cubo, Esfera, Plano, Cilindro, Cápsula, Cone → cada um já vem com o
  **Collider** da mesma forma.
- [ ] Inspector: mudar posição arrastando o número e digitando; adicionar e remover componente; Redefinir valores.
- [ ] Ctrl+Z / Ctrl+Y depois de: mover com o gizmo, renomear, excluir, reparentar, trocar cor → cada ação volta
  em **um** passo, com a seleção junto. Segurar Ctrl+Z repete.
- [ ] Ctrl+Z dentro de um campo de texto desfaz só o texto.
- [ ] **Seleção múltipla:** Ctrl+clique na hierarquia e no viewport alterna; Shift+clique na hierarquia pega o
  intervalo; arrastar no vazio do viewport desenha o retângulo e seleciona o que está dentro (Ctrl soma); Ctrl+A
  pega tudo. Com vários: o gizmo move/gira todos; o Inspector diz "N objetos selecionados" e mudar a Posição Y
  muda só o Y de todos; Del exclui todos e Ctrl+Z traz de volta com a seleção; arrastar o grupo na hierarquia
  reparenta todos; clique simples num deles (sem arrastar) volta a ser só ele.
- [ ] **Busca na hierarquia:** Ctrl+F põe o cursor na caixa; "cubo" mostra só o Cubo, com o caminho do pai ao
  lado se tiver; "t:luz" mostra o Sol; "t:collider" mostra as formas. Clicar num resultado seleciona; Ctrl+clique
  e "Selecionar todas" pegam vários. Esc ou "x" volta à árvore.
- [ ] **Copiar e colar:** Ctrl+C em dois objetos e Ctrl+V → cópias no mesmo lugar, selecionadas (mover com o
  gizmo mostra o original embaixo). Ctrl+X some com eles e Ctrl+V traz de volta; Ctrl+Z desfaz cada passo.
  Clique direito > Colar como filho põe sob o objeto clicado sem mudar o lugar. Abrir outra cena e Ctrl+V cola
  lá também. Num campo de texto, Ctrl+C/V copiam o texto, não as entidades.
- [ ] O título mostra `*` com alterações; Nova cena, Abrir cena e fechar a janela perguntam Salvar / Não salvar /
  Cancelar.

## 5. Painel Projeto e Explorer

- [ ] Duplo clique em pasta entra; Backspace e ▲ voltam; a trilha `Assets > ...` é clicável.
- [ ] Ctrl+clique, Shift+clique e Ctrl+A selecionam vários; Ctrl+C / Ctrl+X / Ctrl+V copiam e movem; nome
  repetido vira "nome 2".
- [ ] Arrastar itens para outra pasta (na lista, na árvore e na trilha) → move junto com o `.meta`.
- [ ] Del → pede confirmação e vai para a **Lixeira**.
- [ ] Ctrl+F busca em todo o `Assets`; o filtro Cenas / Texturas / Malhas funciona; Ctrl + roda muda o tamanho
  dos ícones na grade.
- [ ] Arrastar um `.png` e um `.glb` **do Explorer** para uma pasta do painel → copia para a pasta sob o mouse.
- [ ] Ctrl+C no Explorer e Ctrl+V no painel, e o contrário → copia.
- [ ] Renomear a cena aberta pelo painel → o título do editor acompanha.
- [ ] Mexer num arquivo pelo Explorer com o editor aberto → aparece em até 1 s.

## 6. Materiais, prefabs e modelos

- [ ] Novo material → mudar cor, metal, rugosidade e textura; o objeto que usa o material acompanha.
- [ ] Modo de alfa **Transparente** com alfa 0,4 → vê-se o que está atrás; **Recorte** com uma textura de grade
  → buracos recortados e a sombra também recortada; **Duas faces** mostra o lado de dentro.
- [ ] Criar prefab de uma entidade, pôr duas cópias na cena, mudar o prefab → as duas mudam; **Aplicar ao
  prefab**, **Reverter** e **Desfazer vínculo** fazem o que dizem.
- [ ] **Prefab dentro de prefab:** prefab Janela; pôr uma Janela como filha de um cubo Casa e criar o prefab
  Casa → o Inspector da janela diz "Do prefab aninhado: Janela". Duas casas na cena; abrir o prefab Janela e mudar
  a cor → as janelas das duas casas mudam. Mudar a escala da janela dentro do prefab Casa e depois a escala no
  prefab Janela → a da Casa fica. **Desvincular** uma casa → a janela dela volta a ser uma Janela.
- [ ] Importar um `.glb` animado (ex.: `Robo.glb`) e pôr na cena → aparece com materiais; a animação toca no Play.

## 7. Scripts

- [ ] Scripts > Novo script... → cria o `.cpp`, compila e aparece para adicionar como componente.
- [ ] **Abrir no VS Code** abre a pasta de scripts; editar e salvar → recompila sozinho; F7 recompila na mão.
- [ ] Erro de compilação → aparece no painel Scripts com arquivo e linha; o editor continua.
- [ ] Salvar um script **com o jogo rodando** → recarrega sem parar o Play; os campos guardam os valores.
- [ ] Script que lança exceção no `OnUpdate` → o Play pausa com a mensagem na barra do viewport; o editor **não**
  fecha; Parar funciona.
- [ ] Ajuda > Guia de scripts e Referência da API abrem.

## 8. Play

- [ ] ▶ / Ctrl+P → borda azul e "JOGANDO"; o jogo recebe teclado só com o viewport em foco; atalhos do editor
  (W/E/R, Del) não disparam durante o jogo.
- [ ] Clicar num campo do Inspector durante o Play → as teclas vão para o campo, não para o jogo.
- [ ] F8 alterna câmera do jogo e câmera livre sem parar.
- [ ] Jogo > **Cena e Jogo lado a lado** → a janela Jogo aparece à direita do Viewport. Play → o jogo recebe
  WASD com a janela Jogo em foco; clicar no Viewport tira o teclado do jogo e deixa mover objetos com o gizmo
  enquanto ele roda; clicar no Jogo devolve. Botões da UI do jogo respondem ao clique na janela Jogo. Cursor
  preso pelo jogo: Esc solta. Fechar a janela Jogo desliga o modo; reabrir o editor lembra a escolha.
- [ ] Pausa (Ctrl+Shift+P) e Avançar um quadro.
- [ ] Parar → a cena volta exatamente como estava antes do Play.
- [ ] Script com `SetCursorMode(CursorMode::Locked)` → cursor some e fica preso; **Esc** solta; clique no
  viewport prende de novo; Parar sempre devolve o cursor.

## 9. Física e áudio

- [ ] Arquivo > **Camadas de colisão...** → renomear camadas e desmarcar um par na tabela; salvar o projeto,
  fechar e reabrir → continua igual.
- [ ] Inspector do Collider e do Controlador de personagem → a lista **Camada** mostra os nomes novos; dois
  objetos em camadas que não se chocam se atravessam no Play.
- [ ] Scripts > Adicionar exemplo > **Jogador** num personagem → anda com WASD e pula com Espaço; não atravessa o chão.
- [ ] Fonte de áudio com **Grupo** Música e outra com Efeitos → toca; mudar o volume de um grupo por script só
  muda aquele grupo.

## 10. Interface do jogo (HUD)

- [ ] Entidade > Interface (UI) > Texto, Imagem, Botão, Painel → aparecem no jogo; as âncoras seguram a posição
  ao redimensionar a janela.
- [ ] Botão clicável no Play chama o script.

## 11. Recuperação depois de uma queda

- [ ] Fazer alterações sem salvar, esperar 2 minutos e fechar o editor à força (Gerenciador de Tarefas >
  Finalizar tarefa) → ao reabrir o projeto, oferece **recuperar**; recuperar traz as alterações.
- [ ] Mesmo teste durante o Play → recupera a cena de antes do Play.
- [ ] Descartar a recuperação → não pergunta de novo.

## 12. Exportar e rodar

- [ ] Arquivo > **Exportar jogo...** numa pasta com acento → a pasta tem o `Player.exe` e os assets
  usados.
- [ ] Rodar o jogo exportado com o editor fechado → abre na cena inicial, som, cursor e scripts funcionam.
- [ ] Levar a pasta para outro computador sem o Motor (quando houver) → roda igual.

## 13. Gerais

- [ ] Ajuda > **Sobre o Motor** mostra a versão e a da API de scripts.
- [ ] Ver > Restaurar layout volta os painéis; o layout fica salvo entre aberturas.
- [ ] Projetos recentes no menu Arquivo abre o projeto certo.
- [ ] Janela maximizada, redimensionada e em outro monitor → nada fica cortado.
