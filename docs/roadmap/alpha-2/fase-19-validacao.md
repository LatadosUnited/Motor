---
tipo: fase
alpha: 2
fase: 19
titulo: Validação de uso real
status: em andamento
prioridade: 4
areas: [docs]
branch: fase-19-validacao
inicio: 2026-10-19
fim: 
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
- [ ] **Teste à mão pelo roteiro** no editor instalado pelo Hub, seções 2 a 13.
- [ ] **Jogo pequeno do começo ao fim**, exportado. (A exportação em Release é da
  [Fase 21](fase-21-jogo-entregue.md); aqui vale a exportação de hoje.)
- [ ] **Cada problema achado** vai para a [entrada](../entrada.md) com gravidade; a triagem leva cada um para uma
  fase (na 16, se for pequeno).

## Pronto quando

O roteiro foi seguido inteiro (menos a instalação), o jogo exportado roda com o editor fechado e a lista de
problemas está anotada e priorizada.

## Feito

## Limitações
