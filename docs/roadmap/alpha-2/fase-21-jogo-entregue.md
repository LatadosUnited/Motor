---
tipo: fase
alpha: 2
fase: 21
titulo: Jogo entregue
status: planejada
prioridade: 6
areas: [player, exportacao, core, scripts]
branch: 
inicio: 
fim: 
---

# Fase 21 — Jogo entregue

## Objetivo

O jogo exportado deixa de ser uma "janela de teste": opções para o jogador, identidade própria, progresso
salvo e controle.

## Tarefas

- [ ] **Opções do jogador:** tela cheia, resolução, vsync e qualidade (sombras, MSAA) num arquivo de
  configurações do jogo, com API para os scripts montarem um menu de opções. O MSAA hoje é sempre 4x: o
  `Renderer::SetSamples` existe, mas o projeto não o expõe. Título da janela sem o FPS. *(origem: Fase 10,
  limitações; análise do código, item 3)*
- [ ] **Identidade:** ícone e nome do `.exe` nas configurações do projeto; versão do jogo.
- [ ] **Exportação sempre Release:** Player e `libMotor.dll` de Release da versão instalada, e a DLL de scripts
  recompilada em Release ao exportar. *(origem: Fase 10, limitação)*
- [ ] **Salvar progresso:** API de dados persistentes para scripts (chave/valor e arquivos em
  `%APPDATA%\<Jogo>\`), com exemplo no guia.
- [ ] **Controle (gamepad)** pela GLFW, com mapeamento de ações ("Pular" = Espaço ou botão A) configurável no
  projeto. *(origem: análise do código, item 9: "WASD ou analógico")*
- [ ] **Tela de carregamento** simples ao trocar de cena.

## Pronto quando

O jogo da [Fase 19](fase-19-validacao.md) exportado abre em tela cheia com ícone próprio, tem menu de opções e
de continuar (save), e é jogável com controle.

## Feito

## Limitações
