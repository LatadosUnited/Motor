# Hub — uso

O `MotorHub.exe` (`hub/`). Estado e roteiro: `docs/hub/estado.md` e `docs/hub/roteiro.md`.

## Hub do Motor

`MotorHub.exe` é um programa separado do editor, como o Unity Hub: guarda a lista de projetos e as versões do
Motor instaladas, e abre cada projeto com o editor da versão em que ele foi feito. Ele não liga na
`libMotor.dll` (GLFW, ImGui e nlohmann_json estáticos, runtime do GCC estático): roda sozinho e funciona com
qualquer versão do editor, inclusive as futuras.

- **Projetos:** cartões com nome, pasta, versão do Motor e última abertura; **Abrir** (ou duplo clique) usa a
  versão do projeto. Versão ausente ou desconhecida: o hub pergunta com qual versão abrir e avisa que o
  editor vai passar o projeto para ela (faça uma cópia de segurança antes). No menu **...**: abrir com outra
  versão, mostrar no Explorer, remover da lista (a pasta fica). **Adicionar pasta** põe um projeto existente
  na lista; a busca filtra por nome ou pasta.
- **Novo projeto:** nome, pasta (padrão `Documentos\Motor`) e versão; o hub chama
  `MotorEditor.exe --create <pasta> <nome>` daquela versão.
- **Versões:** instaladas em `%LOCALAPPDATA%\Motor\Versões\<versão>\`. **Adicionar de uma pasta** copia uma
  pasta montada (abaixo) para lá; **Tornar padrão** escolhe a versão dos projetos novos; **Remover** manda a
  pasta para a Lixeira.
- **Baixar versões:** em **Disponíveis para baixar**, as versões publicadas nos servidores da lista
  **Servidores** (padrão `http://127.0.0.1:47900`, o `MotorServidor` desta máquina). **Baixar** mostra o
  progresso e pode ser cancelado; o que já veio fica guardado e o botão vira **Retomar**. Cada arquivo é
  conferido pelo SHA-256 do manifesto, e a versão só aparece instalada quando tudo confere (é baixada em
  `Versões\.baixando\<versão>\` e renomeada no fim). Um Hub mais novo publicado num servidor aparece no topo,
  com o link do instalador.
- **Configuração:** `%LOCALAPPDATA%\Motor\hub.json` (`{"format": "motor-hub", "version": 2, ...}`, com a lista
  `servers`; o formato 1 é lido com o servidor local na lista), separada da do editor.

**Montar a pasta de instalação de uma versão** (a do código atual):

```bash
cmake --build build --target motor_package     # build/package/Motor-0.2.0/
```

A pasta é autossuficiente: `MotorEditor.exe`, `Player.exe`, `libMotor.dll` e o runtime do GCC, `engine/`
(shaders e fontes), `templates/`, `sdk/` (headers e biblioteca de importação para compilar os scripts dos
projetos, com caminhos relativos) e `motor-version.json` (`{"version": "0.2.0", "scriptApiVersion": 10, ...}`),
que o hub lê. Outra pasta de saída: `-DMOTOR_PACKAGE_DIR=...` no configure. Montada de um build que não é de
Release, ela leva também `release/` (Player, DLLs e SDK de um build de Release, de `MOTOR_RELEASE_BIN_DIR`) para
os jogos exportados saírem em Release; sem esse build, o `motor_package` avisa.
**O editor instalado continua precisando do MSYS2 (UCRT64) e do CMake** nos mesmos caminhos do build que montou
a pasta (estão em `sdk/MotorSDK.json`) para compilar os scripts dos projetos; o jogo exportado não precisa.

**Linha de comando** (as mesmas ações dos botões, sem abrir a janela; para agentes e testes):

```bash
MotorHub --add-version build/package/Motor-0.2.0 --list
MotorHub --create "C:/Jogos" "Jogo Ação"                 # com a versão padrão (ou --engine 0.2.0)
MotorHub --open "C:/Jogos/Jogo Ação"                     # com a versão do projeto
MotorHub --open "C:/Jogos/Antigo" --engine 0.2.0         # outra versão (o editor atualiza o engineVersion)
MotorHub --data "C:/temp/Hub Teste" ... -- --no-settings --mcp   # outra pasta de dados; depois de "--": argumentos para o editor
```

Também: `--remove-version`, `--set-default`, `--add-project`, `--remove-project`, `--aba versões` (abre a janela
nessa aba), `--version`, `--disponiveis [--servidor URL]` (versões publicadas nos servidores) e
`--baixar <versão|mais-recente> [--servidor URL]` (baixa, confere e instala; retoma um download interrompido).
O hub instalado pelo `MotorSetup` com outra pasta de dados a lê do `instalacao.json` ao lado do `MotorHub.exe`. Código em `hub/`: `HubCore` (lógica sem janela, `tests/test_hub.cpp`),
`HubPlatform` (Windows: processos, Explorer, Lixeira), `HubApp` (janela e ações), `main.cpp` (linha de comando).
