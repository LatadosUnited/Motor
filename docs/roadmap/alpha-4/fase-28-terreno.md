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

- [ ] Tarefa 1: Criar a grade de células (`SpatialGrid`) com consultas por frustum, caixa e raio
- [ ] Tarefa 2: Levar as posições para a GPU relativas à câmera
- [ ] Tarefa 3: Guardar no renderer os objetos estáticos entre quadros (registrar, mover, remover)
- [ ] Tarefa 4: Recortar a cena e as sombras pela grade
- [ ] Tarefa 5: Usar a grade no raycast do editor e nas consultas de área
- [ ] Tarefa 6: Desenhar pela GPU (multi-draw indirect, buffers mapeados de forma persistente)
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

(Preencher ao terminar: o que mudou, arquivos principais, testes, números de antes e depois.)

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)
