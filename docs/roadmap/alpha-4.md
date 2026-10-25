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
ambiente de dentro e a câmera se adapta ao sair para o sol.

## Fase 28 — Terreno e água

- **Recorte com estrutura espacial** (BVH ou grade para os estáticos), servindo também às sombras, ao raycast do
  editor e às consultas de área. Na alpha 3 não compensava: nas cenas do bench a câmera vê quase tudo e o recorte
  inteiro custa ~1,6 ms na referência; com terreno e mundo aberto a câmera vê uma parte pequena da cena.
  *(origem: Fase 23, Tarefa 5, adiada em 21/10/2026)*
- **Desenho guiado pela GPU** (multi-draw indirect, buffers mapeados de forma persistente) e **objetos guardados
  entre quadros** no renderer (só reenviar o que mudou, como o GPU Resident Drawer da Unity): o que sobrou de CPU
  no fim da Fase 23 é percorrer a cena e montar a lista a cada quadro. *(origem: Fase 23, Tarefa 6 e estudo da 11)*
- **Componentes em blocos contínuos por tipo** (estilo ECS): a montagem do quadro em várias threads escala mal porque
  cada objeto lê entidade, componente, material e malha espalhados no heap. Muda a API de `Entity::components`
  (scripts, Inspector, serializador, prefabs). *(origem: Fase 25, Tarefa 11, e medição da Fase 26, adiada em 25/10/2026)*
- **Terreno:** mapa de altura editável (subir, baixar, suavizar), pintura de até 4 camadas de textura, collider da
  física, LOD por blocos.
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
