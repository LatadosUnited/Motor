---
tipo: fase
alpha: 2
fase: 16
titulo: Robustez e correções
status: em andamento
prioridade: 1
areas: [scene, editor, physics, scripts, distribuicao]
branch: fase-16-robustez-e-correcoes
inicio: 2026-10-18
fim: 
---

# Fase 16 — Robustez e correções

## Objetivo

Hoje um erro de script fecha o editor e o trabalho que não foi salvo se perde; quem começa a programar agora
esbarra nisso a cada erro. Junto, as correções pequenas que a alpha 1 deixou, várias com contorno conhecido
em jogos de verdade.

## Tarefas

### Erro de script não derruba o editor

- [ ] **Proteger as chamadas de script e de sistema.** `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy` e os
  eventos de física rodam sem `try/catch` (`SceneRuntime::Update` chama `s.OnUpdate(dt)` direto); uma exceção
  sobe até o `main.cpp` do editor e ele fecha. Exemplo: `Get<T>()` de um tipo sem `MOTOR_SCRIPT` lança. No
  erro, o Play pausa e o console mostra script, entidade e mensagem; dá para parar o Play normalmente. No
  Player, o erro vai para o log e o jogo segue sem aquele script. *(origem: análise do código de 17/10/2026,
  item 1)*
- [ ] **Salvamento automático e recuperação.** O estado de antes do Play só existe na memória (`PlayMode`).
  Gravar a cena aberta em `<projeto>/.motor/recuperacao/` ao dar Play e a cada N minutos com alterações; ao
  abrir o editor depois de uma queda, oferecer recuperar. *(origem: análise do código, item 1)*
- Limite conhecido: um ponteiro inválido no script ainda derruba o processo (a DLL roda no mesmo processo).
  Isolar os scripts fica para a alpha 4 ([Fase 31](../alpha-4.md)).

### Correções

- [ ] **Teleporte do personagem guarda a velocidade.** `SyncCharacters` (`engine/src/physics/PhysicsSystem.cpp`,
  "teleporte pelo script ou pelo editor") só chama `SetPosition`: o personagem reaparece com a velocidade da
  queda. Contorno usado por um agente: mudar a massa do controlador de 70 para 70,001 (o Motor recria o corpo
  parado). Zerar a velocidade (a do `CharacterVirtual` e a guardada no registro) no teleporte; teste em
  `tests/test_physics.cpp`. *(origem: agente fazendo um jogo, 17/10/2026)*
- [x] **Caminho longo na compilação dos scripts:** revisar e mesclar o branch `fix/caminho-longo-objetos`
  (objetos com nome curto, sem acentos, abaixo de 260 caracteres). *(origem: correção de 16/10/2026, nunca
  mesclada)* (2232bf1)
- [ ] **Pasta vazia vira projeto:** abrir o editor numa pasta vazia oferece criar o projeto ali (hoje mostra a
  cena de exemplo). O `project_create` do MCP e o `--create` já existem. *(origem: anotado para a alpha 2)*
- [ ] **Scripts instanciam prefabs:** `Instantiate(prefab, posição)` com campo `PrefabRef` no Inspector, e
  exemplo no guia (projétil, inimigo que nasce). *(origem: Fase 6, limitação)*
- [ ] **Documentação junto da versão instalada:** `docs/` no `motor_package`; Ajuda > Guia de scripts abre o
  guia da versão do editor. *(origem: pedido do usuário, 17/10/2026)*
- [ ] **Câmera do jogo com roll** (rotação em torno do próprio eixo). *(origem: Fase 2, limitação)*
- [ ] **Editor avisa antes de abrir projeto de versão mais nova** que a dele, e não o rebaixa sem perguntar.
  *(origem: Fase 14, depois)*
- [ ] **Valores inválidos pelo MCP:** `component_set` com valor de tipo errado responde com erro e o formato
  esperado do campo, em vez de ignorar (aconteceu com o clipe de som antes da correção 2e4733a). *(origem:
  verificação das Fases 8 e 10)*

## Pronto quando

Um script que lança exceção ou chama `Get<T>()` errado pausa o Play com a mensagem no console e o editor
continua aberto; matar o editor no meio do trabalho e reabrir oferece a recuperação; cada correção tem teste
no `ctest` ou verificação pelo MCP.

## Feito

## Limitações
