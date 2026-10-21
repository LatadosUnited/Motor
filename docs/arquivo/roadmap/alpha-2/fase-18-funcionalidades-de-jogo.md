---
tipo: fase
alpha: 2
fase: 18
titulo: Funcionalidades de jogo
status: pronta
prioridade: 3
areas: [physics, audio, renderer, assets, scripts]
branch: fase-18-funcionalidades
inicio: 2026-10-18
fim: 2026-10-19
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

- [x] **Várias vozes por fonte** e `PlayOneShot(clipe)`: hoje `Play` recomeça o som, e passos ou coletas
  seguidas se cortam. (7c6c616)
- [x] **Carregar os clipes ao abrir a cena**, fora da thread principal: hoje o primeiro `Play` de cada clipe
  decodifica o arquivo e trava um pouco. (7c6c616)
- [x] **Grupos de volume** (música, efeitos, interface), com volume por grupo para o menu de opções. (7c6c616)

### Entrada e formas

- [x] **Prender e esconder o cursor** pelos scripts (a `Window` já sabe; falta a API). O controle (gamepad) está
  na [Fase 21](fase-21-jogo-entregue.md). (bc15343)
- [x] **Formas prontas:** cilindro, cápsula e cone, com o collider certo (hoje só Cubo, Esfera e Plano). As formas do menu Criar
  agora vêm com o Collider, como na Unity. (bc15343)

### Transparência 3D

- [x] **Materiais com modo** opaco, recorte por alpha (folhas, grades) e transparente (vidro, água simples); a
  cena desenha os transparentes depois dos opacos, de trás para frente. Hoje o blend está sempre desligado na
  cena. *(origem: análise, item 3; Fase 7, limitação)* (aabc1c5)

### Para fechar a fase

- [ ] ~~**Jogo de exemplo no guia** que use tudo isso (câmera orbital que não atravessa paredes, coleta por
  área, passos que não se cortam, música com volume separado, cursor preso, vidro e cerca recortada),
  rodado pelo `test_script_docs` como os outros exemplos.~~ Fora da fase por decisão do usuário (19/10/2026):
  voltou para a [entrada](../../../entrada.md); o jogo feito à mão da [Fase 19](fase-19-validacao.md) usa essas
  funcionalidades na prática. *(origem: Pronto quando)*

## Pronto quando

Um jogo de exemplo tem câmera orbital que não atravessa paredes, coleta por área, passos que não se cortam,
música com volume separado, cursor preso no jogo, uma janela de vidro e uma cerca com recorte por alpha; tudo
com testes sem GPU e exemplo no guia.

## Feito

- **Camadas de colisão** (9d4947e): 16 camadas com nome e a tabela de quem se choca no `.mproject` (formato 3),
  como na Unity; campo **Camada** no Collider e no Controlador de personagem; **Arquivo > Camadas de colisão** e
  a ferramenta MCP `physics_layers`; `Raycast(..., layerMask)` e `LayerMask({nomes})`.
- **Consultas e personagem** (83fbea3): `OverlapSphere`, `OverlapBox` e `SphereCast` com máscara (as de área
  acham triggers e personagens); `SetCharacterVelocity` e o campo **Usa gravidade** (mudar não refaz o corpo).
- **Áudio** (7c6c616): `PlayOneShot` (até 8 vozes extras por fonte, outro clipe opcional); os clipes curtos da
  cena são decodificados numa thread ao iniciar (um toque antes do fim espera só o seu clipe); grupos de volume
  Efeitos, Música e Interface (`SetGroupVolume`, vale entre cenas).
- **Cursor e formas** (bc15343): `Script::SetCursorMode(Locked/Hidden/Normal)`, aplicado pelo Player e pelo
  editor (com o jogo em foco; Esc solta, clique prende); Cilindro, Cápsula e Cone, collider de cilindro, e as
  formas do menu Criar e do MCP vêm com o Collider certo.
- **Transparência** (aabc1c5): modo de alfa no material (opaco, recorte, transparente), limite do recorte e
  duas faces; sombra recortada; transparentes depois do céu, de trás para frente; glTF lê `alphaMode`.
- `kScriptApiVersion` = 13. Testes novos ou ampliados: `test_physics`, `test_project`, `test_audio`,
  `test_script_api`, `test_materials`, `test_gltf`, `test_mcp`. Cada parte conferida pelo MCP no editor (as de
  imagem, com captura).

## Limitações

- Sem o jogo de exemplo no guia (ver acima): cada parte tem teste e exemplo curto na referência, mas não há um
  jogo do guia que junte todas.
- Transparentes ordenados pelo centro de cada objeto: dois objetos que se cruzam, ou um objeto transparente
  côncavo, podem aparecer na ordem errada (o normal nas engines sem ordem por pixel).
- Transparente não faz sombra nem é recortado na sombra pelo alfa.
- Não visto na tela: a janela **Camadas de colisão**, o combo da camada e do grupo de som no Inspector, e o
  cursor preso com Esc no editor (o MCP não clica em menus nem prende o mouse). Entraram no
  [roteiro de teste à mão](../../../teste-manual.md) (seções 8 e 9).
- Mudança de comportamento: Cubo, Esfera e Plano criados pelo menu e pelo MCP agora já vêm com Collider.
- `test_export` e `test_distribuicao` falharam uma vez cada com os testes em paralelo (na caixa de entrada).
