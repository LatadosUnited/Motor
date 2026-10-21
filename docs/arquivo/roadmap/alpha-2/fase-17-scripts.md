---
tipo: fase
alpha: 2
fase: 17
titulo: Scripts mais rápidos e fáceis
status: pronta
prioridade: 2
areas: [scripts, editor, docs]
branch: fase-17-scripts
inicio: 2026-10-18
fim: 2026-10-18
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

- [x] **Medição:** o painel Scripts e o `scripts_status` do MCP mostram o tempo de cada etapa (configurar,
  compilar cada arquivo, ligar, recarregar). (db3d443)
- [x] **Cabeçalho pré-compilado** com o `ScriptModule.h` e a GLM, gerado uma vez por versão do Motor no SDK. (41eff65, eae7fdf)
- [x] **Headers mais leves:** tirar `<format>` e o que não for preciso de `Script.h`, `Scene.h` e `Component.h`;
  declarar adiante o que der. Um teste mede o tempo de compilar um script vazio, para não piorar sem perceber. (e619b90)
- [x] **Configurar uma vez só:** o CMake só reconfigura quando entra ou sai arquivo ou muda a versão do Motor. (67f1830)
- [x] **Ligação mais rápida:** comparar `ld` com `lld` (`-fuse-ld=lld`, já vem no MSYS2) e "MSYS Makefiles" com
  Ninja. Ferramenta nova só com aprovação do usuário. *(aprovado: ninja e lld instalados no MSYS2 em 18/10)* (da01caf)

### Facilidade

- [x] **Novo script pelo editor:** painel Projeto > Criar > Script, com nome e modelo (vazio, comportamento, que
  segue, coletável...), já com `MOTOR_SCRIPT` e aberto no editor de código. *(hoje é criar o `.cpp` à mão)* (10110ba)
- [x] **Editor de código configurado:** `compile_commands.json` e `.vscode/` (IntelliSense com os headers do
  Motor, tarefa de compilar, depurar com gdb anexando ao editor) gerados ao criar ou abrir o projeto; Abrir no
  VS Code pelo menu Scripts e pelo duplo clique num `.cpp`. (10110ba)
- [x] **Erros clicáveis:** cada erro do painel Scripts abre o arquivo na linha. (10110ba)
- [x] **Menos código repetido:** registrar campos com menos texto (ex.: `MOTOR_FIELDS(Jogador, velocidade,
  pulo)` ao lado do `Field(...)` completo), referências a entidades como campos do Inspector (`EntityRef`,
  arrastar da Hierarquia) no lugar de buscar pelo nome, `GetComponent<T>()` em outras entidades. (8a27e95, 10110ba)
- [x] **API que falta em jogos comuns:** temporizadores e "chamar daqui a N segundos", mensagens entre scripts,
  números aleatórios; cada um com exemplo no guia, compilado pelo `test_script_docs`. (8a27e95, 3b72a57)
- [x] **Recarga durante o Play:** recarregar a DLL com o jogo rodando, preservando o estado dos componentes como
  a recarga fora do Play já faz. *(origem: análise do código, item 6: hoje a recarga espera o Play parar)* (8a27e95, 10110ba)
- [x] **Recarga que avisa:** campo renomeado ou com tipo trocado vira aviso no painel em vez de perder o valor. (8a27e95, 10110ba)

### Para fechar a fase

- [ ] ~~**Menos de 1 s do salvar à recarga** com 10 scripts: hoje 1,1 a 1,5 s no editor aberto (0,59 s de
  compilar, 0,23 s de ligar e o resto do editor e do processo). O que sobra: o `.gch` tem ~107 MB e só
  carregá-lo leva ~0,4 s por arquivo (um script vazio compila em 0,45 s). Caminhos: PCH menor (menos headers no
  `ScriptModule.h`), ou compilar o arquivo salvo num compilador que fica aberto. *(origem: medição de 18/10/2026)*
  No Island (mesmo dia, com outro programa ocupando ~2 núcleos): salvar → recarregado em 1,5 a 1,9 s no
  `Girar.cpp`, mas **5 a 6,5 s** no `JogadorIlha.cpp` e no `HudVila.cpp`. Perceber o arquivo salvo leva menos de
  0,2 s e recarregar 0,02 s: quase tudo é compilar. O peso é o `<format>` dos scripts: só incluir custa ~1,8 s
  por arquivo, usar `std::format` mais ~1 s, e gerar o objeto em Debug quase dobra (JogadorIlha: 3,4 s só a
  sintaxe, 6,9 s com o objeto). Os headers da engine fora do PCH (`PhysicsSystem.h`, `UiSystem.h`,
  `Animator.h`) custam 0,1 a 0,25 s cada. Próximo passo: medir com o `<format>` no PCH (e o tamanho/carga do
  `.gch` que isso traz), ou oferecer aos scripts um formatador da engine compilado na `libMotor.dll`.~~ Vai para a [Fase 29 — Compilador integrado](../alpha-4.md): a recarga fica como está na alpha 2
  (decisão do usuário, 18/10/2026).

- [x] **Medir o projeto de jogo real** (88 a 124 s do zero no começo da fase; a meta é menos de 30 s). O de
  referência com 50 scripts compila do zero em 11,5 s. *(origem: Pronto quando)* Medido em 18/10/2026 no
  Island (9 arquivos, 6 scripts; cópia na pasta temporária, editor de teste pelo MCP): **9,9 s do zero**
  (configurar 3,4 s, compilar 5,6 s, ligar 0,8 s). Meta cumprida. (5fe1a10)

## Pronto quando

Num projeto com 10 scripts, mudar um arquivo e ver o efeito leva menos de 1 s (do salvar à recarga, também
durante o Play); o projeto de jogo real que levava 88 a 124 s compila do zero em menos de 30 s; criar um
script, editar com IntelliSense e clicar num erro para ir à linha funciona sem configurar nada à mão.

## Feito

- **Novo script com modelo** (`ScriptProject::NewScriptTemplates`, `NewScriptSource(nome, modelo)`): Vazio,
  Comportamento, Que segue (campo `EntityRef`), Coletável (trigger e mensagem) e Gerador (temporizador,
  sorteio e `PrefabRef`). Popup "Novo script..." com a lista de modelos, também em **+ Criar** do painel
  Projeto e na ferramenta MCP `script_create`; o arquivo criado abre no editor de código. Os cinco modelos
  compilam sem avisos no `test_script_docs`.
- **VS Code** (`editor/src/scripts/ScriptIde`): ao abrir o projeto, `.vscode/` com `c_cpp_properties.json` (o
  `compile_commands.json` da compilação dos scripts), `tasks.json` (compilar com o mesmo comando do editor,
  erros no painel Problemas), `launch.json` (gdb anexado ao MotorEditor) e `extensions.json`. Só reescreve os
  arquivos que começam com `// Gerado pelo Motor Editor`. Menu Scripts > Abrir no VS Code; duplo clique num
  arquivo do painel Scripts ou num `.cpp`/`.h` do painel Projeto abre no VS Code (`Code.exe <projeto> --goto`);
  sem o VS Code, o programa padrão.
- **Erros clicáveis:** o clique num erro do painel Scripts abre o arquivo na linha e na coluna.
- **Menos código repetido:** `MOTOR_FIELDS(Struct, campos...)` (registro de uma linha), `.Field(chave,
  &T::membro)` com o rótulo tirado da chave (`detail::LabelFromKey`), campo `motor::EntityRef` (desenhador com
  a lista da cena e arrastar da Hierarquia; duplicar, criar prefab, instanciar e sincronizar ajustam as
  referências internas; overrides comparam pela entidade correspondente), `Script::GetEntity(id)` e
  `GetComponent<T>()` / `GetComponent<T>(id)`.
- **API:** temporizadores `After`/`Every`/`CancelTimer`/`CancelTimers` (tempo do jogo, protegidos contra
  exceção, somem com o script), mensagens `Send`/`Broadcast`/`OnMessage` (`motor::Message`), sorteio
  `Random`/`RandomInt`/`Chance`/`SetRandomSeed`. Exemplos no guia (seção 10 nova): `docs/examples/Interruptor.cpp`
  e `Chuva.cpp`, rodados pelo `test_script_docs`. `kScriptApiVersion` = 12.
- **Recarga durante o Play:** `ScriptController` recarrega com o jogo rodando; `SceneRuntime::ForgetScripts`
  solta temporizadores e scripts com erro antes, e a cena guardada do Play e os prefabs do jogo também são
  religados (`PlayMode::ForEachScene`). Os scripts novos mantêm os campos do jogo e recebem `OnStart` de novo.
  Verificado pelo MCP: Play, editar o `.cpp`, a versão nova roda sem parar o jogo e Parar volta à cena.
- **Recarga que avisa:** `ResolveMissingComponents` devolve os valores que não couberam (`FieldWarning`: campo
  sumido ou com outro tipo); o painel Scripts e o `scripts_status` mostram um aviso por campo, com o valor
  perdido. `FieldOptions::formerKey` lê o valor de um campo renomeado (na recarga e ao abrir cenas).
- **Velocidade no editor** (10110ba, eae7fdf): vigia da pasta com o aviso do Windows (`FolderWatch`, sem a
  espera de até 0,75 s),
  editor a 10 FPS em segundo plano e a 20 FPS durante a compilação (fora do Play; `Application::SetFrameLimit`,
  `Window::IsFocused`), PCH com `-femit-struct-debug-baseonly`. Do salvar à recarga, com 10 scripts: de ~3 s
  para 1,1 a 1,5 s.
- **Testes:** `test_script_api` (novo: temporizadores, mensagens, sorteio, `MOTOR_FIELDS`, `EntityRef` em cópias
  e prefabs, `formerKey` e avisos), `test_script_project` (modelos, arquivos do VS Code, recarga durante o Play
  com uma DLL de verdade), `test_script_docs` (modelos gerados e os exemplos novos). 28/28 no `ctest`.

## Limitações

- **Recarga acima de 1 s:** 1,5 a 1,9 s num script simples e 5 a 6,5 s num que usa `<format>` (projeto Island).
  Fica para a [Fase 29 — Compilador integrado](../alpha-4.md); detalhes na tarefa riscada acima.
- Na recarga durante o Play, os membros não registrados voltam ao padrão e o `OnStart` roda de novo (sem
  `OnDestroy`): um `OnStart` que cria entidades cria outra vez.
- Um campo `EntityRef` num prefab que aponta para fora dele fica vazio no arquivo do prefab (na instância
  original vira override e é mantido).
- O MCP aceita `EntityRef` só pelo id (`component_set` com o nome da entidade é recusado com o formato
  esperado).
- Depurar com o gdb precisa do pacote `mingw-w64-ucrt-x86_64-gdb` do MSYS2 (não instalado aqui; ferramenta nova
  só com aprovação).
- Não testado na janela (o MCP não cobre): o popup Novo script com a lista de modelos, + Criar > Novo script no
  painel Projeto, o clique no erro e o duplo clique abrindo o VS Code na linha, o desenhador do `EntityRef`
  (lista e arrastar da Hierarquia) e o limite de FPS com a janela sem foco.
