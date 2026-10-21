# Instalador — uso

O `MotorSetup.exe` (`installer/`), o `MotorServidor.exe` (`server/`) e o núcleo `distrib/`. Estado e roteiro:
`docs/instalador/estado.md` e `docs/instalador/roteiro.md`.

## Instalar

Para usar o Motor sem compilá-lo: o **instalador do Hub** (`MotorSetup.exe`, um arquivo só) instala o Hub para o
seu usuário, sem pedir administrador, e pode já instalar uma versão do Motor.

1. Baixe o instalador na página do servidor de versões (`http://<servidor>:47900/`, abaixo) ou use o
   `build/bin/MotorSetup.exe` de um build.
2. Rode e confira: pasta do Hub (padrão `%LOCALAPPDATA%\Programs\Motor Hub\`), pasta de dados (padrão
   `%LOCALAPPDATA%\Motor`: versões do Motor e `hub.json`), atalho no menu Iniciar e, se quiser, na área de
   trabalho. Em **Versão do Motor**, escolha baixar a mais recente de um servidor (botão **Verificar** mostra o
   que ele tem) ou instalar de uma pasta montada (`motor_package`), ou deixe para depois.
3. **Instalar**. No fim, **Abrir o Hub agora**.

O instalador leva o `MotorHub.exe` dentro dele (funciona sem servidor), registra a desinstalação em
**Configurações > Aplicativos > Aplicativos instalados** ("Motor Hub") e grava `instalacao.json` na pasta do Hub
com o que criou. Ele avisa se faltam o **MSYS2 (UCRT64)** e o **CMake**, que o editor usa para compilar os
scripts dos projetos (<https://www.msys2.org/>; no terminal UCRT64:
`pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake make`), mas não os instala.

**Desinstalar:** pelo Aplicativos instalados ou `MotorSetup.exe --desinstalar`. A pasta do Hub vai para a
Lixeira e os atalhos e o registro saem; a pergunta "também as versões do Motor e o hub.json" decide se
`Versões\` e `hub.json` da pasta de dados vão junto. Projetos nunca são tocados. Rodando de dentro da pasta do
Hub, o desinstalador continua numa cópia em `%TEMP%` (o Windows não move a pasta de um programa aberto), que se
apaga sozinha no fim.

Sem janela (testes e agentes; o resultado sai no console):

```bash
MotorSetup --silencioso --pasta "C:/temp/Hub" --dados "C:/temp/Dados" --servidor http://127.0.0.1:47900 --sem-atalhos --sem-registro
MotorSetup --silencioso --versao-local build/package/Motor-0.2.0 ...   # versão de uma pasta montada
MotorSetup --desinstalar --silencioso --pasta "C:/temp/Hub" [--apagar-dados]
```

Também: `--area-de-trabalho`, `--abrir-hub`, e para testes `--atalhos-em <pasta>` (atalhos numa pasta qualquer)
e `--chave-registro <nome>` (outra chave em `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall`).
Código em `installer/` (`Setup`: instalar e desinstalar; `SetupApp`: janela) sobre o plano de
`distrib/InstallPlan` (testado em `tests/test_distribuicao.cpp`).

## Servidor de versões

`MotorServidor.exe` distribui as versões do Motor e o Hub numa rede local, por HTTP (sem internet, sem
bibliotecas novas: cpp-httplib e o SHA-256 do Windows). Ele serve uma **pasta de publicações**: a de
`--pasta <dir>`; sem ela, a pasta **`Publicacoes` ao lado do `MotorServidor.exe`**, se existir (instalação
portátil: copie o `.exe` para `C:\MotorServidor`, publique em `C:\MotorServidor\Publicacoes` e os dois cliques
no `.exe` já servem essa pasta); senão `%LOCALAPPDATA%\Motor\Servidor`:

```bash
# 1. Monte a versão (de preferência Release: ~25 MB em vez de ~200 MB) e publique
cmake -S . -B build-release -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j8 --target motor_package MotorHub MotorSetup MotorServidor
build-release/bin/MotorServidor --pasta C:/MotorServidor --publicar build-release/package/Motor-0.2.0
build-release/bin/MotorServidor --pasta C:/MotorServidor --publicar-hub build-release/bin/MotorHub.exe
# 2. Sirva (porta 47900; sem --local, aceita a rede local e o Firewall do Windows pode pedir permissão)
build-release/bin/MotorServidor --pasta C:/MotorServidor
```

- `--publicar <pasta montada> [--canal <nome>]` copia a versão para `versoes/<versão>/` (via `.tmp`, renomeada
  no fim), calcula o tamanho e o SHA-256 de cada arquivo e refaz o `manifesto.json`. Versão já publicada:
  `--remover <versão>` antes (a pasta vai para a Lixeira).
- `--publicar-hub <MotorHub.exe>` publica o Hub (a versão vem do recurso VERSIONINFO do .exe) e o
  `MotorSetup.exe` que estiver ao lado (ou `--setup <arquivo>`), para baixar pela página.
- `--listar`, `--regenerar` (refaz o manifesto a partir das pastas), `--porta <n>`, `--local` (só 127.0.0.1).

Rotas: `GET /` (página com as versões e o link do instalador), `GET /api/versoes` (manifesto
`{"format": "motor-releases", "version": 1, "releases": [{"version", "channel", "scriptApiVersion",
"buildType", "date", "size", "files": [{"path", "size", "sha256"}]}], "hub": {...}}`),
`GET /arquivos/<versão>/<caminho>` (aceita `Range`, para retomar), `GET /hub/latest` e `GET /hub/MotorSetup.exe`.
Os arquivos vão um a um, sem compressão, e só o que está no manifesto é servido. Nos outros computadores, ponha
`http://<IP do servidor>:47900` em **Servidores** no Hub ou no instalador. O servidor relê o manifesto quando
ele muda: dá para publicar com ele no ar.
