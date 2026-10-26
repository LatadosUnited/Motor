---
tipo: fase
alpha: 4
fase:
titulo: Hub residente com MCP
status: em andamento
prioridade: 1
areas: [hub, distrib, editor-mcp]
branch: hub-residente
inicio: 2026-10-26
fim:
---

# Hub residente com MCP

## Objetivo

Hoje o MCP só existe dentro do editor (`editor/src/mcp/`, porta 47800): com o editor fechado, o servidor `motor`
do Claude Code falha com conexão recusada, e cada editor aberto de novo pede para reconectar. O Hub também não fica
aberto: só se abre quando o usuário clica nele, e para se atualizar só avisa e abre o link do instalador no
navegador (`hub/src/HubVersionsTab.cpp`, `DrawHubUpdateNotice`), sem conferir o SHA-256.

Esta fase deixa o Hub **sempre ligado**: inicia com o Windows na bandeja e é dono de uma porta fixa, a **47600**,
com dois endereços que o Claude Code configura uma vez e nunca mais reconecta:

- `http://127.0.0.1:47600/mcphub`: o MCP do Hub (projetos, versões, abrir e fechar o editor, estado);
- `http://127.0.0.1:47600/mcpengine/<Projeto>`: o MCP do editor daquele projeto, repassado pelo Hub. O editor
  continua com o servidor dele, mas numa porta interna que o Hub escolhe (47801 a 47899) e que o Claude não vê.
  `/mcpengine`, sem nome, vai para o editor aberto por último.

O Hub responde ao `initialize` e guarda a sessão (`Mcp-Session-Id`); o editor só atende `tools/call`. Assim a
sessão do Claude sobrevive ao editor fechar, travar ou reabrir. O `tools/list` de `/mcpengine/...` também sai do
Hub, da lista de ferramentas que cada versão do Motor traz no pacote, para funcionar com o editor fechado. Uma
chamada com o editor fechado **abre o editor sozinho** (janela normal), espera ele ficar pronto e repassa. O nome
na URL é o nome do projeto na lista do Hub, codificado na URL (espaços e acentos); o Hub recusa dois projetos com o
mesmo nome e mostra a URL pronta para copiar.

Com o Hub sempre aberto, atualizá-lo pelo instalador fica mais difícil (o Windows não deixa trocar um `.exe` em
uso). Por isso a **autoatualização do Hub**, que estava na [Fase 32](../alpha-5.md), vem para cá: o Hub baixa o
`MotorSetup.exe` novo pelo `distrib::DownloadRelease` (retomada e SHA-256), roda o instalador em modo silencioso
(`--silencioso --esperar <pid> --abrir-hub`, que já existem) e fecha; o instalador troca o Hub e o reabre na
bandeja. Durante a troca a porta 47600 fica fora do ar por alguns segundos: o Claude volta sem reconectar.

**Segurança:** com um programa sempre ligado que abre outros programas, qualquer processo da máquina chegaria à
porta. Continua só em 127.0.0.1 e recusando `Origin`/`Host` de fora, como o editor, e passa a exigir um token
(`Authorization: Bearer`) gravado em `%LOCALAPPDATA%\Motor\mcp-token`, que o Hub mostra para colar na configuração
do Claude Code. O `--mcp` direto do editor (porta 47800, sem token) continua para os testes e para quem usa sem o
Hub.

O Hub não liga na `libMotor.dll` e continua assim: o protocolo e o servidor HTTP do MCP saem do editor para uma
biblioteca pequena sem a engine, usada pelos dois.

## Tarefas

- [x] Tarefa 1: Tirar o `McpProtocol` e o `McpServer` do editor para uma biblioteca sem a engine (log por callback) — 18ac51a
- [x] Tarefa 2: Iniciar com o Windows (chave `Run` do HKCU) pelo instalador e pelas configurações do Hub — 8da3a80
- [x] Tarefa 3: Modo bandeja (`--bandeja`): ícone, menu, janela escondida, uma instância só — 8da3a80
- [x] Tarefa 4: MCP do Hub na porta 47600 (`/mcphub`) com sessão e token — dd6f26b
- [x] Tarefa 5: Ferramentas do `/mcphub`: projetos, versões, abrir e fechar o editor, estado — 2a17c2f
- [x] Tarefa 6: Lista de ferramentas do editor sem janela (`--mcp-ferramentas`), gravada no pacote da versão — 0c33bae
- [x] Tarefa 7: Editor numa porta interna que o Hub escolhe, avisando o Hub quando está pronto e quando fecha — eabe5dc
- [x] Tarefa 8: Repasse de `/mcpengine/<Projeto>` e `/mcpengine` para o editor, abrindo o editor fechado — d3640f7
- [x] Tarefa 9: Autoatualização do Hub: procurar, baixar o instalador, conferir, instalar em silêncio e voltar à bandeja — fd4bea4
- [ ] Tarefa 10: README, guia de configuração do Claude Code e `CLAUDE.md` (servidores `motor-hub` e `motor`)

## Pronto quando

- O `test_hub` passa sem janela: a rota leva `/mcphub`, `/mcpengine/Jogo Ação` (codificado) e `/mcpengine` ao lugar
  certo; nome desconhecido ou repetido dá erro claro; pedido sem token ou com `Origin` de fora é recusado; a sessão
  continua válida depois do editor (simulado) fechar e voltar; o plano da chave `Run` e da atualização (versão
  mais nova, SHA-256 errado recusado, argumentos do instalador) sai como esperado.
- Com o Claude Code configurado só com as duas URLs fixas: depois de reiniciar o Windows o Hub está na bandeja; uma
  chamada a `/mcpengine/<Projeto>` com o editor fechado abre o editor e responde; fechar e reabrir o editor não pede
  reconexão.
- Um Hub mais novo publicado num `MotorServidor` local é baixado, instalado em silêncio e volta à bandeja com a
  porta 47600 de novo no ar em menos de 15 s, sem reconectar o Claude Code.
- Parado na bandeja, o Hub fica abaixo de 1% de um núcleo e de 50 MB de memória.

## Feito

- Tarefa 1: o protocolo e o transporte HTTP saíram de `editor/src/mcp/` para a biblioteca `mcp/` (`motor_mcp`,
  namespace `motor::mcp`), sem a engine: só nlohmann_json, o header da cpp-httplib e o Winsock. O log de ligado e
  desligado vai por `Server::SetLog` (o editor manda para o `motor::log`); o editor continua com os nomes de
  `editor::mcp` pelo `editor/src/mcp/Mcp.h`. Sem mudar comportamento: `test_mcp` e `test_mcp_nav` passam como antes.
- Tarefa 2: valor `MotorHub` no `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` com
  `"<MotorHub.exe>" --bandeja [--data "<pasta>"]` (`distrib::StartupCommand`). O instalador grava (caixa marcada por
  padrão, `--sem-iniciar-com-windows`; `--chave-registro` também troca o nome do valor, para testes) e guarda o nome
  no `instalacao.json` (versão 2; a 1 continua lendo); a desinstalação apaga. No Hub, a caixa "Iniciar com o
  Windows" no canto do cabeçalho; marcada só quando o valor aponta para este `.exe` e esta pasta de dados
  (`SameStartupCommand`). Conferido com o `MotorSetup` de verdade e o nome `MotorHubTeste`: valor gravado com a
  linha certa, `startupValue` no `instalacao.json`, valor apagado ao desinstalar.
- Tarefa 3: `--bandeja` abre com a janela escondida e o ícone na bandeja (`hub/src/HubTray`): clique abre, botão
  direito dá "Abrir o Hub" e "Sair do Hub"; o ícone volta se o Explorer reiniciar. Residente (com `--bandeja` ou
  iniciando com o Windows), fechar a janela só a esconde, com um balão na primeira vez. Um Hub por pasta de dados:
  trava e janela escondida com a `InstanceKey`; abrir de novo traz a janela do que está aberto e sai, e um segundo
  `--bandeja` só sai. Conferido com o `MotorHub.exe` e `--data` numa pasta temporária: os cinco casos acima, e
  fechar a janela sem estar residente fecha o Hub. Parado na bandeja: 47 ms de CPU em 10 s (0,5% de um núcleo) e
  52,3 MB de memória. Hub 0.3.0.
- Tarefa 4: a `motor_mcp` ganhou caminho (`SetPath`), token (`SetToken`: `Authorization: Bearer`, 401 sem ele,
  comparação em tempo constante) e sessão (`SetSessions`: `Mcp-Session-Id` no `initialize`, `DELETE` encerra). O
  servidor não guarda nada por sessão e aceita um id que não deu: o Hub reiniciado (atualizado) não faz o Claude
  reconectar. No Hub, `HubMcp` liga `http://127.0.0.1:47600/mcphub` junto com a janela ou a bandeja e é atendido no
  loop do hub, que o `glfwPostEmptyEvent` acorda. Token em `<dados>\mcp-token`, criado na primeira vez
  (`LoadOrCreateMcpToken`); sem token o MCP não liga. Por ora só a ferramenta `hub_status`; a aba MCP mostra o
  endereço e copia o token e o comando `claude mcp add`. `--mcp-porta <n>` (0 desliga) para um Hub de teste.
  Testes: `test_mcp_servidor` (novo, sem a engine: caminho, token, sessão, origem) e o token no `test_hub`.
  Conferido com o `MotorHub.exe` na bandeja (porta 47650): sem token 401, `initialize` com sessão, `tools/list` e
  `hub_status` respondendo em 12 ms com a janela escondida; memória 53,8 MB.
- Tarefa 5: o `/mcphub` ganhou `projects_list`, `project_open`, `project_create`, `project_add`, `project_remove`,
  `editor_close`, `versions_list`, `version_set_default`, `version_remove`, `versions_available` e `version_download`,
  e o `hub_status` mostra os editores abertos e o download. O Hub agora acompanha os editores que abre
  (`platform::ChildProcess`): não abre o mesmo projeto duas vezes, não manda para a Lixeira uma versão em uso, e
  `editor_close` pede para fechar como o X da janela (`WM_CLOSE`: com algo não salvo o editor pergunta) ou, com
  `force`, encerra o processo; com `wait`, a resposta espera o editor sair (`ToolResult::later`). Os servidores de
  versões são lidos numa thread e o `version_download` usa o mesmo download da aba Versões. A escolha da versão
  (`PickVersion`, `OpenProjectAuto`) saiu do `main.cpp` para o `HubApp`, a mesma para a linha de comando e o MCP.
  O hub virou a biblioteca `motor_hub_app` (tudo menos o `main.cpp`) para o `test_hub_mcp` (novo): chama as
  ferramentas pelo `Protocol` no `HubApp` de verdade, sem janela, com uma cópia do próprio teste fazendo de editor.
  Conferido com o `MotorHub.exe` na bandeja (porta 47650) e o `MotorEditor.exe` do build: `project_create` criou
  "Jogo Ação", `editor_close` sem `force` fechou o editor em 0,4 s, `version_remove` recusou com ele aberto, `force`
  fechou; `version_download` com `wait` baixou e instalou 57,7 MB de um `MotorServidor` local em 1,2 s. Não
  conferido: `editor_close` sem `force` com algo não salvo (o editor deve perguntar e a ferramenta dar o erro de
  tempo esgotado).
- Tarefa 6: `MotorEditor --mcp-ferramentas <arquivo>` grava, sem janela nem OpenGL e sem tocar no `editor.json`, o
  resultado do `initialize` e do `tools/list` (`{"format": "motor-mcp-tools", "version": 1, ...}`). A lista sai do
  mesmo `McpController` do editor aberto: `editor/src/mcp/McpToolList` monta as peças como o `EditorApp`, sem o
  `Init`. O `motor_package` roda o editor copiado e grava `mcp-ferramentas.json` na pasta da versão (falha se não
  der); a instalação e a publicação no `MotorServidor` levam o arquivo junto. No Hub, `ReadEditorTools` (`HubCore`)
  lê a lista (versão antiga sem o arquivo dá um erro claro; formato mais novo lê o que conhece) e o `versions_list`
  diz quantas ferramentas cada versão tem. Testes: `editor_mcp_ferramentas` (novo, roda o editor de verdade e
  confere o JSON com um script CMake), `ReadEditorTools` no `test_hub` e o `editorTools` no `test_hub_mcp`.
  Conferido: 53 ferramentas em 0,24 s; o `tools/list` e o `initialize` do arquivo são idênticos aos do editor aberto
  com `--mcp`; o `motor_package` gerou o arquivo, o Hub o instalou com `--add-version` e mostrou 53 no
  `versions_list`; a publicação no `MotorServidor` o lista no manifesto.
- Tarefa 7: o combinado fica em `mcp/include/motor/mcp/HubLink.h` (Hub e editor usam o mesmo). Com o MCP do Hub
  ligado, abrir um editor de uma versão com `"hubLink": 1` no `mcp-ferramentas.json` acrescenta
  `--mcp=<porta> --hub <porta do Hub>`: a porta é a primeira livre de 47801 a 47899 sem outro editor do Hub
  (`PortFree` com um `bind` cru, sem `SO_REUSEADDR`: com ele, no Windows, a porta de quem já escuta parece livre), e
  o token, um por editor (`NewToken`), vai só no ambiente do processo (`MOTOR_MCP_TOKEN`, bloco próprio no
  `CreateProcessW`), não na linha de comando. O editor exige esse token no MCP (nem o do Hub serve) e avisa o Hub
  por `POST /mcphub/editor` com o mesmo token: "pronto" ao ligar, "projeto" ao abrir outro projeto (o Hub segue e
  o põe na lista) e "fechando" ao desligar. No editor ligado ao Hub, Ver > Servidor MCP fica fixo. A `motor_mcp`
  ganhou rotas a mais no servidor (`AddRoute`, atendidas no `Pump`, com o token decidido pela rota; a Tarefa 8 usa
  para o repasse) e o cliente `PostJson`. Versões sem o `hubLink` abrem como antes (o `--hub` as faria sair com
  "opção desconhecida"). No `/mcphub`: `linked`, `ready` e `mcpPort` no `hub_status`, `editorReady` no
  `projects_list` e `wait` no `project_open`. Testes: a rota e o cliente no `test_mcp_servidor`; no `test_hub_mcp`
  o editor falso avisa o Hub por HTTP de verdade (pronto, outro projeto, token errado recusado, versão sem a
  ligação). Conferido com o `MotorHub.exe` na bandeja e o editor do build: `project_open` com `wait` respondeu
  pronto em 2,5 s na porta 47801, um segundo editor ficou na 47802, a porta interna recusou sem token e com o
  token do Hub (401), os dois fecharam pelo `editor_close`, e `--hub` sem o token é recusado. Não conferido com o
  editor real: o aviso "projeto" (Arquivo > Abrir projeto pela interface) e o item fixo no menu Ver.
- Tarefa 8: `/mcpengine/<Projeto>` e `/mcpengine` no servidor do Hub (`hub/src/HubEngineProxy.cpp`), com o token do
  Hub. O nome é o da lista do Hub, sem diferenciar maiúsculas (`SameProjectName`); a httplib entrega o caminho já
  decodificado. Nome desconhecido ou repetido dá erro JSON-RPC claro; `/mcpengine` vai para o editor aberto por
  último. O Hub responde sozinho o `initialize` (com a versão do protocolo negociada, `Protocol::NegotiateVersion`),
  o `ping` e o `tools/list` com o `mcp-ferramentas.json` da versão do projeto (sem editor aberto, a padrão), e as
  notificações com 202. O resto é repassado ao editor na porta interna, com o token dele; com o editor fechado, o
  Hub o abre (`OpenProjectAuto`) e espera o "pronto" (até 90 s). O repasse roda numa thread solta (um future do
  `std::async` travaria o Hub fechando) que acorda o Hub ao terminar. Versão sem a lista: tudo é repassado. A
  `motor_mcp` ganhou nas rotas o caminho, padrão de expressão regular, resposta para depois (`RouteReply::later`,
  conferida já no mesmo `Pump`), `Mcp-Session-Id` no `initialize`, 202, GET 405 e DELETE. URLs: `mcpUrl` no
  `projects_list` (e `mcpUrlError` com nome repetido), `engineUrl` no `hub_status`, e na aba MCP o comando do
  servidor `motor` e a URL de cada projeto. Teste: no `test_hub_mcp` o editor falso serve um MCP de verdade na porta
  interna e o teste faz o papel do Claude Code pelo HTTP (sem token 401, initialize com sessão e versão negociada,
  notificação 202, tools/list sem abrir o editor, a primeira ferramenta abre e repassa, maiúsculas, `/mcpengine`,
  nome desconhecido, fechar e reabrir sem novo initialize, `/mcpengine` sem editor). Conferido com o `MotorHub.exe`
  na bandeja e o editor do build: `initialize` e `tools/list` (53 ferramentas) com o editor fechado; o primeiro
  `editor_status` abriu o editor e respondeu em 3,2 s; `scene_tree` e `viewport_capture` (PNG de 430 KB, íntegro)
  repassados; fechado pelo `editor_close`, a chamada seguinte o reabriu (2,7 s). Com o editor aberto, cada chamada
  leva de 20 a 120 ms (era 150 a 200 ms antes de conferir a resposta no mesmo `Pump` e acordar o Hub). Não
  conferido: o Claude Code de verdade como cliente (o `claude mcp add` muda a configuração do usuário; fica para a
  Tarefa 10) e um editor aberto fora do Hub com o mesmo projeto (o Hub não o vê e abriria outro).
- Tarefa 9: autoatualização do Hub (`hub/src/HubUpdate.cpp`). O Hub mais novo que este entre os servidores lidos, e
  só os publicados com o `MotorSetup.exe` (`HubApp::NewestHub`), aparece na aba Versões com **Atualizar o Hub** e nas
  ferramentas `hub_update_check` e `hub_update` (`wait`, padrão true, responde quando o instalador abriu; o
  `hub_status` ganhou `hubUpdate`). O instalador é baixado para `<dados>\Atualizacao\MotorSetup-<versão>.exe` pelo
  `Client::DownloadFile` (retomada e SHA-256 do manifesto; um já baixado e conferido não vem de novo), aberto com
  `--silencioso --atualizar --pasta <pasta do hub> --esperar <pid> --log <dados>\Atualizacao\atualizacao.log
  --abrir-hub -- <a linha deste hub com --bandeja>` (`UpdateInstallerArgs`) e o Hub sai meio segundo depois (a
  resposta do MCP sai antes). Só se atualiza o Hub com o `instalacao.json` ao lado (`CanSelfUpdate`; o de uma pasta
  de build mostra o link, como antes), e nunca com um download do Motor ou uma cópia em andamento. No instalador,
  `--atualizar` (`setup::Update`) relê o `instalacao.json` e refaz a instalação com as mesmas escolhas
  (`distrib::PlanUpdate`: pasta de dados, atalhos onde estão, chave de desinstalação com a versão nova, servidores, e
  iniciar com o Windows como está agora no Run, que o Hub pode ter mudado); instalar e atualizar dividem as etapas
  (`ApplyPlan`). Depois abre o Hub com os argumentos depois de `--`, também se falhar (o `MotorHub.exe` só é trocado
  pronto); `--log` escreve o que fez e o Hub novo mostra a última linha (ou o erro) ao abrir, e apaga o log e os
  instaladores de versões que não são mais novas. Os editores abertos continuam: ao sair (sempre, não só ao
  atualizar), o Hub grava `<dados>\editores.json` (formato `motor-hub-editors` 1: pid, projeto, versão, .exe, porta
  interna, token, pronto) e o Hub seguinte os volta a acompanhar (`ChildProcess::Adopt`: só se o processo ainda roda
  e é o mesmo .exe). Testes: `PlanUpdate` no `test_distribuicao` (mesmas escolhas, versão nova, iniciar com o Windows
  desligado depois de instalar, instalação de teste com atalho em pasta própria e outra chave, sem atalhos);
  `editores.json` e a linha do instalador no `test_hub`; `NewestHub`, `CanSelfUpdate` e as duas ferramentas no
  `test_hub_mcp`. O SHA-256 errado recusado já era coberto pelo `DownloadFile` no `test_distribuicao`. Conferido de
  verdade: o Hub do build (0.3.0) e o `MotorSetup.exe` publicados num `MotorServidor` local com a versão do Hub
  trocada para 0.3.1 no manifesto; um Hub de teste instalado pelo `MotorSetup` (`--chave-registro MotorHubTeste`,
  `--sem-iniciar-com-windows`, `--atalhos-em`) na bandeja, MCP na porta 47650, com o editor de "Jogo Ação" aberto
  por ele. O `hub_update` baixou o instalador (115,7 MB), o Hub fechou, o instalador trocou o `MotorHub.exe` e abriu o
  Hub novo na bandeja com a mesma linha (mesma porta, `-- --no-settings`); o Hub novo voltou a acompanhar o editor
  (pid e porta interna iguais), o `scene_tree` por `/mcpengine/Jogo%20A%C3%A7%C3%A3o` foi repassado e o
  `editor_close` o fechou. Com o instalador já baixado, do pedido ao Hub novo respondendo: 4,1 s (1,4 s para conferir
  o SHA-256 e abrir o instalador, 2,7 s para trocar e subir). O `instalacao.json` manteve atalho, chave e pasta de
  dados; depois o Hub de teste foi desinstalado (Lixeira) e a chave saiu do registro. Não conferido: um Hub
  realmente de outra versão (o publicado era o mesmo .exe com outro número, então o Hub novo ainda oferece o
  0.3.1), o botão da janela (só pelo MCP), o caminho de erro de verdade (instalador falhando com o Hub antigo de
  volta) e o Claude Code como cliente durante a troca (Tarefa 10).

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)
