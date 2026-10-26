---
tipo: alpha
alpha: 4
status: planejada
foco: Mundo aberto e renderização 2
---

# Alpha 4 — Mundo aberto e renderização 2 (rascunho)

> **Rascunho.** Começa depois da [alpha 3](alpha-3.md). Antes do replanejamento de 21/10/2026
> ([decisão](../decisoes/2026-10-21-replanejamento.md)) estas fases eram as 27 e 28 da alpha 3. Os números e a
> ordem podem mudar.

Foco: cenários abertos e luz mais bonita, sobre o que a alpha 3 deixa pronto: instancing e recorte espacial
(Fase 23), sombras com cache e LOD (Fase 24), várias threads e carregamento em segundo plano (Fase 26).

**A alpha 4 está pronta quando:** uma ilha com terreno pintado, água, árvores, grama e uma fogueira com fumaça roda
no Player a 60 FPS na máquina de referência; e uma sala fechada com janela fica escura nos cantos, reflete o
ambiente de dentro e a câmera se adapta ao sair para o sol. Herdada da alpha 3: a cena de referência
(`bench/referencia`) a 60 FPS no Player, sombra Média (fechou em ~22,6 ms, limitada pela GPU).

## Fase 28 — Terreno e água

**Em andamento (26/10/2026):** [nota da fase](alpha-4/fase-28-terreno.md). Branch `fase-28-terreno`.

- **Recorte com estrutura espacial** (BVH ou grade para os estáticos), servindo também às sombras, ao raycast do
  editor e às consultas de área. Na alpha 3 não compensava: nas cenas do bench a câmera vê quase tudo e o recorte
  inteiro custa ~1,6 ms na referência; com terreno e mundo aberto a câmera vê uma parte pequena da cena.
  Preferir uma **grade de células**: cada célula vira depois a unidade do streaming
  (Mundo grande, abaixo). *(origem: Fase 23, Tarefa 5, adiada em 21/10/2026)*
- **Renderização relativa à câmera:** as posições vão para a GPU já relativas à câmera, para não perder precisão
  longe da origem. Sempre ligada, sem opção para o usuário; é a base da origem flutuante. *(26/10/2026)*
- **Desenho guiado pela GPU** (multi-draw indirect, buffers mapeados de forma persistente) e **objetos guardados
  entre quadros** no renderer (só reenviar o que mudou, como o GPU Resident Drawer da Unity): o que sobrou de CPU
  no fim da Fase 23 é percorrer a cena e montar a lista a cada quadro. *(origem: Fase 23, Tarefa 6 e estudo da 11)*
- **Componentes em blocos contínuos por tipo** (estilo ECS): a montagem do quadro em várias threads escala mal porque
  cada objeto lê entidade, componente, material e malha espalhados no heap. Muda a API de `Entity::components`
  (scripts, Inspector, serializador, prefabs). *(origem: Fase 25, Tarefa 11, e medição da Fase 26, adiada em 25/10/2026)*
- **Terreno:** mapa de altura editável (subir, baixar, suavizar), pintura de até 4 camadas de textura, collider da
  física, LOD por blocos. Cada bloco num arquivo próprio (altura e pintura), alinhado às células da grade, para
  poder carregar por partes sem mudar o formato depois.
- **Água:** ondas, reflexo do céu, refração e cor pela profundidade (transparência da Fase 18, `SceneDepthTexture`
  e `SceneColorTexture` da Fase 7).

## Fase 29 — Vegetação e partículas

- **Vegetação:** grama e árvores espalhadas por pincel no terreno, com instancing e sumindo com a distância.
- **Partículas:** emissor como componente (taxa, vida, velocidade, cor e tamanho no tempo, textura), atualizado em
  tarefas (Fase 26); fumaça, fogo, faíscas.

## Fase 30 — Renderização 2

*(Limitações da Fase 7.)*

- Oclusão de ambiente na tela (SSAO), com opção de qualidade (é cara na HD 530).
- Reflexos locais: probes de reflexo posicionáveis (e, depois, SSR).
- Exposição automática (adaptação do olho).
- Luzes por blocos da tela (clustered): na referência da alpha 3 as 9 pontuais custam ~7 ms na HD 530 (Fase 24).
- "Recebe sombra" no Mesh Renderer e níveis de LOD gerados na importação de modelos (simplificador de malha:
  biblioteca nova). Sobras da Fase 24.
- Medir o renderer numa máquina com GPU dedicada (Fase 24 não tinha uma).

## Mundo grande (sem número ainda)

**Planejada (26/10/2026), a pedido do usuário.** Mapas maiores que a ilha da alpha 4 sem o usuário montar nada à
mão, como o World Partition da Unreal: o editor divide o mundo em células sozinho e um mapa pequeno nem percebe.
Vem depois da Fase 28, em cima da grade de células, da renderização relativa à câmera e dos blocos do terreno.

- **Streaming de células:** carregar e descarregar as células perto do jogador (raio configurável), nas tarefas
  da Fase 26. A cena passa a aceitar subcenas por célula (`SceneSerializer`, prefabs, referências entre células):
  formato novo, com a versão subindo e as cenas antigas lidas como uma célula só.
- **Origem flutuante completa:** recentrar o mundo quando o jogador se afasta, levando junto física (Jolt), áudio,
  navmesh e partículas. Só faz falta acima de alguns km.
- **Impostores:** árvores e objetos distantes trocados por imagem plana gerada no editor.
- **Oclusão:** não desenhar o que está atrás de morros e prédios (consulta de oclusão ou buffer de profundidade
  reduzido).
- **Editor:** ver as células na viewport, carregar só a região em edição e gerar navmesh e LOD por célula.

## Entrada v2 (sem número ainda)

Em cima do `motor::Input` e do mapa de ações da Fase 21, não do zero. Andar pela ilha em primeira pessoa é o que
pede olhar com o mouse e mover na diagonal sem correr mais. Começou antes da alpha 4, no branch `entrada-base`;
o número da fase sai quando a alpha 4 for numerada de novo.

- [x] Tarefa 1: Contar os eventos do quadro, teclas e botões que faltam, texto digitado e zona morta radial — b4b3e0b
- [x] Tarefa 2: Ações de botão, eixo e vetor 2D, com o mouse como eixo e combinações (formato 2 do `"input"`) — dc7de78
- [x] Tarefa 3: Ler vários controles, avisar quando ligam e desligam e saber o último dispositivo usado — 971031e
- [x] Tarefa 4: Contextos de entrada (Jogo, Menu) e a interface consumindo o clique — a0beb1d
- [x] Tarefa 5: Segurar, toque, toque duplo e buffer nas ações — f527117
- [x] Tarefa 6: Remapear as ações no jogo exportado, salvo com as opções do jogador — 436de33
- [x] Tarefa 7: Mandar mouse, eixos e texto pelo MCP — b7791bb

## Hub residente com MCP (sem número ainda)

**Planejada (26/10/2026), a pedido do usuário, para antes da 0.4:** [nota da fase](alpha-4/hub-residente.md). O Hub
inicia com o Windows na bandeja, é dono da porta 47600 (`/mcphub` e `/mcpengine/<Projeto>`, repassado ao editor) e
o Claude Code não precisa reconectar quando o editor fecha ou reabre; o Hub se atualiza sozinho pelo instalador
silencioso. A autoatualização veio da [Fase 32](alpha-5.md). Branch `hub-residente`.
