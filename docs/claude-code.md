---
tipo: guia
atualizado: 2026-10-26
---

# Configurar o Claude Code com o Motor

O Hub do Motor fica aberto na bandeja e atende o Claude Code (ou outro agente MCP) em dois endereços fixos, na porta
**47600**. Configure uma vez; depois o Claude Code não precisa reconectar quando o editor fecha, abre outro projeto
ou o Hub se atualiza.

| Servidor | Endereço | Para quê |
|---|---|---|
| `motor-hub` | `http://127.0.0.1:47600/mcphub` | projetos e versões: listar, criar, abrir e fechar o editor, baixar versões, atualizar o Hub |
| `motor` | `http://127.0.0.1:47600/mcpengine` | o editor aberto por último: cena, Play, scripts, captura da tela (as ferramentas do editor) |
| (um por projeto) | `http://127.0.0.1:47600/mcpengine/<Projeto>` | o editor daquele projeto, aberto sozinho se estiver fechado |

## Antes de começar

- O Hub instalado pelo `MotorSetup.exe` e aberto (o instalador marca **Iniciar com o Windows**: depois de entrar no
  Windows ele fica na bandeja). Para conferir: o ícone do Hub na bandeja, ou a aba **MCP** da janela do Hub, que
  diz se o servidor está ligado.
- O **token**: todo pedido leva `Authorization: Bearer <token>`. Ele fica em `%LOCALAPPDATA%\Motor\mcp-token`
  (criado na primeira vez que o Hub abre) e a aba MCP copia o token e os comandos prontos. Trate como uma senha:
  com ele, um programa abre projetos e mexe na cena.

## Configurar (uma vez)

No terminal, com o token da aba MCP no lugar de `<token>`:

```bash
claude mcp add --transport http --scope user motor-hub http://127.0.0.1:47600/mcphub --header "Authorization: Bearer <token>"
```

```bash
claude mcp add --transport http --scope user motor http://127.0.0.1:47600/mcpengine --header "Authorization: Bearer <token>"
```

`--scope user` vale para todas as pastas. Quem já tinha o servidor `motor` antigo, direto no editor
(`http://127.0.0.1:47800/mcp`), tira ele antes de pôr o novo:

```bash
claude mcp remove motor
```

Para falar sempre com o mesmo projeto, mesmo com outro editor aberto depois, use a URL dele (o nome da lista do
Hub, codificado: espaço vira `%20`, `ç` vira `%C3%A7`, `ã` vira `%C3%A3`). O `projects_list` do `motor-hub` e a aba
MCP mostram a URL pronta:

```bash
claude mcp add --transport http --scope user motor-jogo "http://127.0.0.1:47600/mcpengine/Jogo%20A%C3%A7%C3%A3o" --header "Authorization: Bearer <token>"
```

### Por repositório, sem o token no git

Um `.mcp.json` na pasta do repositório vale para quem abrir o Claude Code ali. O token vem de uma variável de
ambiente (o Claude Code troca `${...}` pelo valor), então o arquivo pode ir para o git:

```json
{
  "mcpServers": {
    "motor-hub": {
      "type": "http",
      "url": "http://127.0.0.1:47600/mcphub",
      "headers": { "Authorization": "Bearer ${MOTOR_HUB_TOKEN}" }
    },
    "motor": {
      "type": "http",
      "url": "http://127.0.0.1:47600/mcpengine",
      "headers": { "Authorization": "Bearer ${MOTOR_HUB_TOKEN}" }
    }
  }
}
```

A variável, uma vez por usuário do Windows (vale para os terminais abertos depois):

```bash
setx MOTOR_HUB_TOKEN "<token>"
```

## Conferir

```bash
claude mcp list
```

Os dois servidores aparecem como conectados. Dentro de uma conversa, `/mcp` mostra o mesmo e as ferramentas. Um
teste rápido: peça "liste os projetos do Motor" (`projects_list` no `motor-hub`) e depois "mostre a árvore da cena"
(`scene_tree` no `motor`).

## Como se comporta

- **Editor fechado:** o Hub responde sozinho o `initialize` e a lista de ferramentas (a da versão do projeto), então
  o Claude Code conecta mesmo assim. A primeira ferramenta do editor abre o editor (janela normal), espera ele ficar
  pronto (alguns segundos) e repassa. `/mcpengine` sem projeto e sem editor aberto pede o nome: use
  `project_open` no `motor-hub` ou a URL do projeto.
- **Fechar e abrir o editor**, ou abrir outro projeto: a sessão continua; nada a reconectar.
- **Hub se atualizando** (`hub_update` ou o botão da aba Versões): o servidor fica fora do ar por uns 3 s e volta
  no mesmo endereço, com os editores abertos. Se o Claude Code mostrar o servidor como desconectado, `/mcp` reconecta.
- **Hub fechado** ("Sair do Hub" no ícone): conexão recusada até abrir o Hub de novo. Os editores abertos por ele
  continuam e o Hub reaberto volta a acompanhá-los.
- **Editor aberto fora do Hub** (direto pelo `MotorEditor.exe`): o Hub não o vê; uma ferramenta do `motor` abre
  outro editor no mesmo projeto. Abra os projetos pelo Hub.

## Problemas

| O que aparece | Por quê | O que fazer |
|---|---|---|
| conexão recusada (`ECONNREFUSED`) | o Hub está fechado, ou outro programa ficou com a porta 47600 (a aba MCP diz) | abra o Hub; feche o outro programa |
| `401`, token ausente ou errado | o `mcp-token` mudou (apagado, outra pasta de dados) | copie o token de novo na aba MCP e refaça o `claude mcp add` (ou a variável) |
| "nenhum projeto '...' na lista do Hub" | o nome da URL não está na lista | `projects_list` mostra os nomes; adicione o projeto no Hub |
| "há 2 projetos chamados ..." | dois projetos com o mesmo nome | renomeie um deles |
| "aberto sem a ligação com o Hub" | versão do Motor de antes do Hub residente | abra o projeto com uma versão nova, ou use o MCP direto do editor (abaixo) |

## Sem o Hub

O editor continua com o servidor dele: **Ver > Servidor MCP** ou `MotorEditor <projeto> --mcp`, em
`http://127.0.0.1:47800/mcp`, sem token (qualquer programa da máquina pode usar enquanto estiver ligado). Serve para
testes automáticos e para quem não usa o Hub, mas cai a cada vez que o editor fecha:

```bash
claude mcp add --transport http motor-direto http://127.0.0.1:47800/mcp
```

Detalhes das ferramentas: `README.md`, seções "Servidor MCP" e "Hub".
