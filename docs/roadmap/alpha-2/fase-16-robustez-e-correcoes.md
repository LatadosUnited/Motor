---
tipo: fase
alpha: 2
fase: 16
titulo: Robustez e correções
status: pronta
prioridade: 1
areas: [scene, editor, physics, scripts, distribuicao]
branch: fase-16-robustez-e-correcoes
inicio: 2026-10-18
fim: 2026-10-18
---

# Fase 16 — Robustez e correções

## Objetivo

Hoje um erro de script fecha o editor e o trabalho que não foi salvo se perde; quem começa a programar agora
esbarra nisso a cada erro. Junto, as correções pequenas que a alpha 1 deixou, várias com contorno conhecido
em jogos de verdade.

## Tarefas

### Erro de script não derruba o editor

- [x] **Proteger as chamadas de script e de sistema.** `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy` e os
  eventos de física rodam sem `try/catch` (`SceneRuntime::Update` chama `s.OnUpdate(dt)` direto); uma exceção
  sobe até o `main.cpp` do editor e ele fecha. Exemplo: `Get<T>()` de um tipo sem `MOTOR_SCRIPT` lança. No
  erro, o Play pausa e o console mostra script, entidade e mensagem; dá para parar o Play normalmente. No
  Player, o erro vai para o log e o jogo segue sem aquele script. *(origem: análise do código de 17/10/2026,
  item 1)* (71daff6)
- [x] **Salvamento automático e recuperação.** O estado de antes do Play só existe na memória (`PlayMode`).
  Gravar a cena aberta em `<projeto>/.motor/recuperacao/` ao dar Play e a cada N minutos com alterações; ao
  abrir o editor depois de uma queda, oferecer recuperar. *(origem: análise do código, item 1)* (8bf81ca, d5332e2)
- Limite conhecido: um ponteiro inválido no script ainda derruba o processo (a DLL roda no mesmo processo).
  Isolar os scripts fica para a alpha 4 ([Fase 31](../alpha-4.md)).

### Correções

- [x] **Teleporte do personagem guarda a velocidade.** `SyncCharacters` (`engine/src/physics/PhysicsSystem.cpp`,
  "teleporte pelo script ou pelo editor") só chama `SetPosition`: o personagem reaparece com a velocidade da
  queda. Contorno usado por um agente: mudar a massa do controlador de 70 para 70,001 (o Motor recria o corpo
  parado). Zerar a velocidade (a do `CharacterVirtual` e a guardada no registro) no teleporte; teste em
  `tests/test_physics.cpp`. *(origem: agente fazendo um jogo, 17/10/2026)* (4e1055b)
- [x] **Caminho longo na compilação dos scripts:** revisar e mesclar o branch `fix/caminho-longo-objetos`
  (objetos com nome curto, sem acentos, abaixo de 260 caracteres). *(origem: correção de 16/10/2026, nunca
  mesclada)* (2232bf1)
- [x] **Pasta vazia vira projeto:** abrir o editor numa pasta vazia oferece criar o projeto ali (hoje mostra a
  cena de exemplo). O `project_create` do MCP e o `--create` já existem. *(origem: anotado para a alpha 2)* (d5332e2)
- [x] **Scripts instanciam prefabs:** `Instantiate(prefab, posição)` com campo `PrefabRef` no Inspector, e
  exemplo no guia (projétil, inimigo que nasce). *(origem: Fase 6, limitação)* (d2b3427)
- [x] **Documentação junto da versão instalada:** `docs/` no `motor_package`; Ajuda > Guia de scripts abre o
  guia da versão do editor. *(origem: pedido do usuário, 17/10/2026)* (e74b332)
- [x] **Câmera do jogo com roll** (rotação em torno do próprio eixo). *(origem: Fase 2, limitação)* (e7e292b)
- [x] **Editor avisa antes de abrir projeto de versão mais nova** que a dele, e não o rebaixa sem perguntar.
  *(origem: Fase 14, depois)* (7b06830, d5332e2)
- [x] **Valores inválidos pelo MCP:** `component_set` com valor de tipo errado responde com erro e o formato
  esperado do campo, em vez de ignorar (aconteceu com o clipe de som antes da correção 2e4733a). *(origem:
  verificação das Fases 8 e 10)* (014cecb)

## Pronto quando

Um script que lança exceção ou chama `Get<T>()` errado pausa o Play com a mensagem no console e o editor
continua aberto; matar o editor no meio do trabalho e reabrir oferece a recuperação; cada correção tem teste
no `ctest` ou verificação pelo MCP.

## Feito

- **Proteção de chamadas de script e de sistema (`SceneRuntime`):** `OnStart`, `OnUpdate`, `OnFixedUpdate`,
  `OnDestroy` e os eventos de física (`OnCollision*`/`OnTrigger*`) rodam dentro de `InvokeScript`/`InvokeSystem`
  com `try/catch`. Quando um script lança exceção, o erro sai no log com script, método, entidade e mensagem,
  aquele componente de script é desativado pelo resto do Play (`m_faultedScripts`) e, no editor, o Play pausa na
  hora com a mensagem visível na barra do viewport; no Player o jogo segue sem aquele script.
- **Salvamento automático e recuperação (`SceneSerializer::SaveRecovery`/`CheckRecovery`/`ClearRecovery`):** o
  editor grava `<projeto>/.motor/recuperacao/cena.mscene` e `recuperacao.json` (com escrita atômica via `.tmp`)
  ao iniciar o Play (a partir da cena de backup) e a cada 2 minutos com alterações não salvas fora do Play;
  remove ao salvar, descartar alterações, parar o Play sem alterações pendentes ou fechar limpo. Ao reabrir o
  projeto após uma queda, oferece recuperar ou descartar.
- **Teleporte de personagem (`PhysicsSystem::SyncCharacters`):** mudar o `Transform` de uma entidade com
  `CharacterControllerComponent` agora zera tanto `rec.character->SetLinearVelocity(JPH::Vec3::sZero())` quanto
  `rec.velocity` e `rec.jumpSpeed`.
- **Caminho longo nos scripts (`ScriptProject.cpp`):** objetos gerados pelo Ninja ficam em `.motor/build/o/` com
  nomes ASCII curtos e hash FNV-1a quando necessário, evitando o limite de 260 caracteres do Windows.
- **Pasta vazia e aviso de versão mais nova (`SceneDocument`, `Project::IsNewerThanCurrentEngine`, `Version.h`):**
  abrir uma pasta vazia abre o modal "Novo projeto" já preenchido com aquela pasta; abrir um `.mproject` salvo
  numa versão mais nova do Motor pede confirmação antes de abrir e atualizar o `engineVersion`.
- **Prefabs em scripts (`PrefabRef`, `Script::Instantiate`, `kScriptApiVersion` = 11):** novo tipo de campo
  `motor::PrefabRef` com desenhador no Inspector (`PrefabRefDrawer.cpp`), `Script::Instantiate(prefab)` e
  `Script::Instantiate(prefab, posicao)` devolvendo `Entity*`, sincronização sob demanda na física para aceitar
  `AddForce`/`AddImpulse`/`SetVelocity` no mesmo passo em que o prefab foi instanciado, e exemplo atualizado em
  `docs/examples/Disparador.cpp`.
- **Documentação na versão instalada (`MotorPackage.cmake`, `EngineDocsDir`):** `motor_package` copia `docs/` e o
  menu Ajuda abre o guia (`docs/scripts.md`) e a referência (`docs/scripts-api.md`) locais.
- **Roll na câmera do jogo (`Camera.h`, `SceneViewport.cpp`, `player/src/main.cpp`):** `PerspectiveCamera` usa o
  vetor `up` da rotação da entidade câmera (`SetUp`), preservando o roll no editor e no Player.
- **Validação de campos no MCP (`SceneSerializer::FieldValueFromJson`, `McpSceneTools.cpp`):** `component_set` e
  `component_add` recusam valores de tipo/formato inválido sem alterar o componente e devolvem mensagem clara com
  o formato esperado.
- **Testes:** `test_scripts`, `test_physics`, `test_project`, `test_project_version`, `test_prefabs`, `test_mcp`,
  `test_render`, `test_script_project` e `test_script_docs` (27/27 testes passando no `ctest`).

## Limitações

- Desreferenciar ponteiro nulo ou memória inválida num script C++ ainda encerra o processo, porque a DLL de
  scripts roda no mesmo processo do editor; o isolamento em processo separado fica para a alpha 4
  ([Fase 31](../alpha-4.md)).

