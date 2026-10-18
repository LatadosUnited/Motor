---
tipo: fase
alpha: 2
fase: 19
titulo: Validação de uso real
status: planejada
prioridade: 4
areas: [docs]
branch: 
inicio: 
fim: 
---

# Fase 19 — Validação de uso real

## Objetivo

O editor quase só foi testado por agentes. Um jogo pequeno feito **à mão** (por uma pessoa, não por agente)
do começo ao fim, no editor instalado pelo Hub, e exportado, acha o que os testes e o MCP não acham; a lista
que sair daqui decide o resto da alpha.

## Tarefas

- [ ] **Roteiro de teste à mão** em `docs/teste-manual.md`, reaproveitável a cada versão: câmera livre e seleção
  por clique no viewport, foco do jogo, menus, atalhos, gizmo, arrastar arquivos do painel Projeto e do
  Explorer, popups do Hub e do instalador, Ajuda > Sobre, recarga de scripts com o jogo aberto, recuperação
  depois de uma queda. *(origem: anotado para a alpha 2, "falta testar à mão"; não testados nas Fases 13 a 15)*
- [ ] **Instalação limpa num segundo computador** pela rede (`MotorServidor` + `MotorSetup`), sem compilar nada.
- [ ] **Jogo pequeno do começo ao fim**, exportado em Release.
- [ ] **Cada problema achado** vai para a [entrada](../entrada.md) com gravidade; a triagem leva cada um para uma
  fase (na 16, se for pequeno).

## Pronto quando

O jogo exportado roda no segundo computador e a lista de problemas está anotada e priorizada.

## Feito

## Limitações
