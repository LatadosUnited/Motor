---
tipo: fase
alpha: 2
fase: 19
titulo: Validação de uso real
status: pronta
prioridade: 4
areas: [docs]
branch: fase-19-validacao
inicio: 2026-10-19
fim: 2026-10-20
---

# Fase 19 — Validação de uso real

## Objetivo

O editor quase só foi testado por agentes. Um jogo pequeno feito **à mão** (por uma pessoa, não por agente)
do começo ao fim, no editor instalado pelo Hub, e exportado, acha o que os testes e o MCP não acham; a lista
que sair daqui decide o resto da alpha.

## Tarefas

- [x] **Roteiro de teste à mão** em [teste-manual.md](../../teste-manual.md), reaproveitável a cada versão: câmera
  livre e seleção por clique no viewport, foco do jogo, menus, atalhos, gizmo, arrastar arquivos do painel
  Projeto e do Explorer, popups do Hub e do instalador, Ajuda > Sobre, recarga de scripts com o jogo aberto,
  recuperação depois de uma queda; também o que a Fase 18 não viu na tela (camadas de colisão, grupo de som,
  cursor preso). 13 seções, gravidade bloqueia / atrapalha / detalhe. *(origem: anotado para a alpha 2, "falta
  testar à mão"; não testados nas Fases 13 a 15)* (d53cbbe)
- [ ] ~~**Instalação limpa num segundo computador** pela rede (`MotorServidor` + `MotorSetup`), sem compilar
  nada.~~ Pulada nesta fase por decisão do usuário (19/10/2026): a instalação já foi testada à mão antes; a
  seção 1 do roteiro fica para quando o instalador mudar ou houver um segundo computador Windows.
- [x] **Teste à mão pelo roteiro**, seções 3 a 13, pelo usuário em 19/10/2026: tudo passou menos um item. Feito
  no build Debug do `main` (API de scripts 13), aberto direto e não pelo Hub: a versão publicada no servidor é a
  0.2.0 de 17/10 (API 10), e só se publica outra no fim da alpha 2 (decisão do usuário). Por isso a seção 2
  (Hub) ficou para essa publicação.
- [x] **Jogo pequeno do começo ao fim**, exportado. (A exportação em Release é da
  [Fase 21](fase-21-jogo-entregue.md); aqui vale a exportação de hoje.) Feito pelo usuário; confirmado em
  20/10/2026, com a versão 0.2.650 (Fases 16 a 21) já publicada no servidor.
- [x] **Cada problema achado** vai para a [entrada](../entrada.md) com gravidade; a triagem leva cada um para uma
  fase (na 16, se for pequeno). Um achado, **atrapalha**: o contorno verde de todos os colliders sempre visível
  no viewport; foi direto para a [Fase 20](fase-20-editor-produtivo.md) (só o selecionado). (0bb597b)

## Pronto quando

O roteiro foi seguido inteiro (menos a instalação), o jogo exportado roda com o editor fechado e a lista de
problemas está anotada e priorizada.

## Feito

Roteiro de teste à mão (`docs/teste-manual.md`, 13 seções) seguido pelo usuário, menos a instalação num segundo
computador; um achado (contorno de todos os colliders), corrigido na Fase 20. Jogo pequeno feito e exportado à
mão. Versão 0.2.650 publicada no servidor no fim da alpha 2. Fechada em 20/10/2026, pela confirmação do usuário.

## Limitações

- Instalação limpa num segundo computador (seção 1 do roteiro): fica para quando o instalador mudar ou houver
  outro computador Windows.
