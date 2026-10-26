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
- [ ] Tarefa 4: MCP do Hub na porta 47600 (`/mcphub`) com sessão e token
- [ ] Tarefa 5: Ferramentas do `/mcphub`: projetos, versões, abrir e fechar o editor, estado
- [ ] Tarefa 6: Lista de ferramentas do editor sem janela (`--mcp-ferramentas`), gravada no pacote da versão
- [ ] Tarefa 7: Editor numa porta interna que o Hub escolhe, avisando o Hub quando está pronto e quando fecha
- [ ] Tarefa 8: Repasse de `/mcpengine/<Projeto>` e `/mcpengine` para o editor, abrindo o editor fechado
- [ ] Tarefa 9: Autoatualização do Hub: procurar, baixar o instalador, conferir, instalar em silêncio e voltar à bandeja
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

## Limitações

(Preencher ao terminar: o que ficou para trás e para onde foi.)
