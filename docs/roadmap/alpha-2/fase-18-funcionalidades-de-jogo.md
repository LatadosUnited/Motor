---
tipo: fase
alpha: 2
fase: 18
titulo: Funcionalidades de jogo
status: em andamento
prioridade: 3
areas: [physics, audio, renderer, assets, scripts]
branch: fase-18-funcionalidades
inicio: 2026-10-18
fim: 
---

# Fase 18 — Funcionalidades de jogo

## Objetivo

Física, áudio, entrada, formas e transparência que jogos comuns pedem e que hoje faltam ou exigem contorno.
*(origem: análise do código de 17/10/2026, itens 3, 7, 8 e 9)*

## Tarefas

### Física

- [x] **Camadas de colisão e máscaras:** quem colide com quem; raycast que ignora camadas, não só uma entidade
  (câmera orbital que não bate no próprio jogador). Decisão do usuário: tabela no projeto, como na Unity (16
  camadas com nome no `.mproject`; cada Collider e personagem escolhe a sua). (9d4947e)
- [x] **Consultas de área:** overlap de esfera e caixa, sphere cast ("o que está nesta área", para coletas). (83fbea3)
- [x] **Personagem:** `SetCharacterVelocity` e gravidade ligável e desligável (hoje só há
  `GetCharacterVelocity`; `SetVelocity` é dos corpos rígidos). (83fbea3)

### Áudio

- [ ] **Várias vozes por fonte** e `PlayOneShot(clipe)`: hoje `Play` recomeça o som, e passos ou coletas
  seguidas se cortam.
- [ ] **Carregar os clipes ao abrir a cena**, fora da thread principal: hoje o primeiro `Play` de cada clipe
  decodifica o arquivo e trava um pouco.
- [ ] **Grupos de volume** (música, efeitos, interface), com volume por grupo para o menu de opções.

### Entrada e formas

- [ ] **Prender e esconder o cursor** pelos scripts (a `Window` já sabe; falta a API). O controle (gamepad) está
  na [Fase 21](fase-21-jogo-entregue.md).
- [ ] **Formas prontas:** cilindro, cápsula e cone, com o collider certo (hoje só Cubo, Esfera e Plano).

### Transparência 3D

- [ ] **Materiais com modo** opaco, recorte por alpha (folhas, grades) e transparente (vidro, água simples); a
  cena desenha os transparentes depois dos opacos, de trás para frente. Hoje o blend está sempre desligado na
  cena. *(origem: análise, item 3; Fase 7, limitação)*

## Pronto quando

Um jogo de exemplo tem câmera orbital que não atravessa paredes, coleta por área, passos que não se cortam,
música com volume separado, cursor preso no jogo, uma janela de vidro e uma cerca com recorte por alpha; tudo
com testes sem GPU e exemplo no guia.

## Feito

## Limitações
