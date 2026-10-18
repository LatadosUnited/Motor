---
tipo: fase
alpha: 2
fase: 17
titulo: Scripts mais rápidos e fáceis
status: planejada
prioridade: 2
areas: [scripts, editor, docs]
branch: 
inicio: 
fim: 
---

# Fase 17 — Scripts mais rápidos e fáceis

## Objetivo

Quem programa o jogo espera a compilação a cada mudança. Hoje a DLL de scripts é gerada por um CMakeLists
(`ScriptProject.cpp`, `GenerateCMakeLists`) com "MSYS Makefiles", em Debug, sem cabeçalho pré-compilado, e cada
`.cpp` inclui `ScriptModule.h`, que puxa a cena, os componentes, `<format>` e a GLM. Medido: 14 a 45 s na
primeira compilação de projetos de teste, **10, 88 e 124 s** num projeto de jogo real, ~1,4 s para mudar um
arquivo pequeno. *(origem: pedido do usuário; análise do código de 17/10/2026, item 6)*

O compilador continua o do MSYS2: integrar o compilador é a alpha 4 ([Fase 29](../alpha-4.md)).

## Tarefas

### Velocidade

Medir antes e depois de cada passo com um projeto de referência de 1, 10 e 50 scripts; os números entram no
README.

- [ ] **Medição:** o painel Scripts e o `scripts_status` do MCP mostram o tempo de cada etapa (configurar,
  compilar cada arquivo, ligar, recarregar).
- [ ] **Cabeçalho pré-compilado** com o `ScriptModule.h` e a GLM, gerado uma vez por versão do Motor no SDK.
- [ ] **Headers mais leves:** tirar `<format>` e o que não for preciso de `Script.h`, `Scene.h` e `Component.h`;
  declarar adiante o que der. Um teste mede o tempo de compilar um script vazio, para não piorar sem perceber.
- [ ] **Configurar uma vez só:** o CMake só reconfigura quando entra ou sai arquivo ou muda a versão do Motor.
- [ ] **Ligação mais rápida:** comparar `ld` com `lld` (`-fuse-ld=lld`, já vem no MSYS2) e "MSYS Makefiles" com
  Ninja. Ferramenta nova só com aprovação do usuário.

### Facilidade

- [ ] **Novo script pelo editor:** painel Projeto > Criar > Script, com nome e modelo (vazio, comportamento, que
  segue, coletável...), já com `MOTOR_SCRIPT` e aberto no editor de código. *(hoje é criar o `.cpp` à mão)*
- [ ] **Editor de código configurado:** `compile_commands.json` e `.vscode/` (IntelliSense com os headers do
  Motor, tarefa de compilar, depurar com gdb anexando ao editor) gerados ao criar ou abrir o projeto; Abrir no
  VS Code pelo menu Scripts e pelo duplo clique num `.cpp`.
- [ ] **Erros clicáveis:** cada erro do painel Scripts abre o arquivo na linha.
- [ ] **Menos código repetido:** registrar campos com menos texto (ex.: `MOTOR_FIELDS(Jogador, velocidade,
  pulo)` ao lado do `Field(...)` completo), referências a entidades como campos do Inspector (`EntityRef`,
  arrastar da Hierarquia) no lugar de buscar pelo nome, `GetComponent<T>()` em outras entidades.
- [ ] **API que falta em jogos comuns:** temporizadores e "chamar daqui a N segundos", mensagens entre scripts,
  números aleatórios; cada um com exemplo no guia, compilado pelo `test_script_docs`.
- [ ] **Recarga durante o Play:** recarregar a DLL com o jogo rodando, preservando o estado dos componentes como
  a recarga fora do Play já faz. *(origem: análise do código, item 6: hoje a recarga espera o Play parar)*
- [ ] **Recarga que avisa:** campo renomeado ou com tipo trocado vira aviso no painel em vez de perder o valor.

## Pronto quando

Num projeto com 10 scripts, mudar um arquivo e ver o efeito leva menos de 1 s (do salvar à recarga, também
durante o Play); o projeto de jogo real que levava 88 a 124 s compila do zero em menos de 30 s; criar um
script, editar com IntelliSense e clicar num erro para ir à linha funciona sem configurar nada à mão.

## Feito

## Limitações
