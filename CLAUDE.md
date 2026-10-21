# Motor — instruções para o Claude

Engine 3D em C++20 + OpenGL 4.5 feita do zero, com editor (Dear ImGui), Hub e instalador. O usuário fala
português: responda, comente o código e escreva textos de interface e logs em **português**.

## Módulos

O desenvolvimento é dividido em quatro módulos ([decisão](docs/decisoes/2026-10-21-desenvolvimento-por-modulo.md)).
Cada um tem o seu `CLAUDE.md` (regras), `README.md` (uso) e, em `docs/<módulo>/`, `estado.md` e `roteiro.md`:

| Módulo | Pastas | Regras |
|---|---|---|
| Engine | `engine/`, `player/`, `sandbox/` | `engine/CLAUDE.md` |
| Editor | `editor/` | `editor/CLAUDE.md` |
| Hub | `hub/` | `hub/CLAUDE.md` |
| Instalador | `distrib/`, `server/`, `installer/` | `installer/CLAUDE.md` |

Comuns a todos: `common/` (`motor_common`, só a gravação segura, sem dependências), `cmake/` (`MotorPackage.cmake`,
alvo `motor_package`), `tests/`, `bench/` e `docs/`. O Editor usa a Engine; o Hub e o Instalador **não** ligam na
`libMotor.dll` nem incluem headers da engine.

Documentação: `docs/` é também o vault do Obsidian e segue o **padrão em `docs/padrao.md`** (leia antes de criar
ou mudar notas). Comece por `docs/inicio.md`. Achados fora da sua tarefa vão no fim de `docs/entrada.md`.

## Compilar e testar (Windows, MSYS2 UCRT64)

```bash
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug   # só na primeira vez
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

- O build deve sair **sem avisos** do nosso código (`-Wall -Wextra -Wpedantic`). Avisos de depreciação do CMake
  vindos de `build/_deps` (GLM) são esperados.
- Dependências via FetchContent em `CMakeLists.txt`, sempre fixadas (tag, commit ou URL com SHA256).
  Filosofia de **libs mínimas**: pergunte antes de adicionar uma nova.
- `build/` é gerado: pode ser apagado e recriado. Nunca rode scripts que reescrevem arquivos em massa sem excluir
  `build/`.

## Convenções comuns

- **Caminhos e texto são UTF-8.** Nunca use `path::string()` nem `char*` de APIs ANSI do Windows: use
  `motor::ToUtf8` / `PathFromUtf8` e as versões wide (`_wgetenv`, `...W`). Acentos em nomes de pasta são o caso
  de teste padrão ("Jogo Ação").
- JSON com `nlohmann::ordered_json`; formatos versionados (`"format"`, `"version"`) e leitura tolerante (campo
  ausente = padrão). Mudou o formato? Suba a versão e continue lendo as antigas.
- Gravação de arquivos importantes: `motor::WriteFileAtomic` (`motor/common/FileWrite.h`): escreve o `.tmp` e
  troca, tentando de novo se o Windows estiver com o arquivo preso; nunca apague o original para "tentar de novo".
  Para pôr no lugar um arquivo ou pasta já copiado para `.tmp`, `motor::ReplaceFileAtomic`. Não use `fs::rename`
  para trocar um arquivo.
- Exclusão de arquivos do usuário vai sempre para a **Lixeira**, nunca definitiva.
- Estilo: o do código vizinho (4 espaços, `m_` em membros, comentários curtos explicando o porquê).

## Testes

- Cada `tests/test_*.cpp` usa `TestUtil.h` (`CHECK`, `SECTION`, `TestDir`, `TestResult`) e é registrado em
  `tests/CMakeLists.txt` com `motor_add_test`. Testes não criam janela nem contexto OpenGL.
- Funcionalidade nova com lógica testável sem GPU ganha teste. Um arquivo novo por assunto; não reescreva testes
  de outro módulo.

## Regras de segurança ao testar

- **Nunca simule teclado ou mouse globais** (SendKeys, keybd_event, SetCursorPos...). Para ver e testar o editor,
  use o servidor MCP dele (detalhes em `editor/CLAUDE.md`).
- O editor, o Hub e o instalador gravam a configuração real do usuário: em testes, use `--no-settings`/`--settings`
  (editor), `--data` (Hub) e as pastas e chaves de teste do instalador (`installer/CLAUDE.md`).

## Grafo de conhecimento (graphify)

- `graphify-out/` (fora do git, na pasta principal do repositório) tem o grafo do código: `GRAPH_REPORT.md`,
  `graph.json`, `graph.html`.
- Para perguntas de arquitetura ("quem chama X?", "como Y chega em Z?"), consulte antes de varrer arquivos:
  `graphify query "<pergunta>"`, `graphify path "A" "B"`, `graphify explain "X"`.
- Depois de mudanças grandes (e de cada merge no `main`), atualize com `/graphify . --update`. O que não indexar
  fica em `.graphifyignore`.

## Armadilhas do ambiente

- O `windows.h` define macros com nomes comuns (`ReplaceFile`, `CreateFile`, `GetObject`, `min`/`max`...): uma
  função nossa com esse nome vira outra num `.cpp` que inclui o `windows.h` (erro de link "undefined reference").
- No Bash deste ambiente, sequências como `\0` e `\n` dentro de comandos/heredocs chegam já convertidas em bytes
  reais. Para editar C++ com barras invertidas, use as ferramentas Edit/Write, não scripts.
- Python no Windows grava CRLF em modo texto: use `newline=''` ou `newline='\n'`. Os fontes usam LF.
- Constantes `CFSTR_*` do Win32 são ANSI sem `UNICODE`: use a string wide literal.

## Fluxo de trabalho

1. **Branch por assunto, com o prefixo do módulo:** `engine/<assunto>`, `editor/<assunto>`, `hub/<assunto>`,
   `instalador/<assunto>`, `docs/<assunto>`; a partir do `main` atualizado, numa worktree própria quando houver
   trabalho em paralelo. Nunca trabalhe direto no `main`.
2. **Commits pequenos em português**, com o módulo no começo quando ajudar (`Hub: ...`).
3. **Ao terminar:** traga o `main` mais recente, build sem avisos, `ctest` inteiro passando (incluindo
   `test_layers`), o `README.md` do módulo atualizado se mudou o uso, a linha saiu do `docs/<módulo>/roteiro.md`,
   o que mudou entrou no `CHANGELOG.md` do módulo, e o `estado.md` mudou se o módulo passou a fazer algo novo.
4. Não mude o comportamento de outro módulo "de passagem": anote em `docs/entrada.md`.
5. Depois do merge no `main`, atualize o grafo (`/graphify . --update`).

## Trabalho em paralelo (vários agentes)

Cada tarefa tem um **módulo**: as pastas dele (tabela acima) ela muda à vontade. Fora dele, só os pontos de
coordenação abaixo, e só **acrescentando** (uma linha ou um bloco no fim), num commit pequeno separado, sem
reordenar nem reformatar. Precisa mudar mais que isso? Pare e pergunte ao usuário.

- `CMakeLists.txt` da raiz (biblioteca nova só com aprovação do usuário), `engine/CMakeLists.txt` (`MOTOR_LAYERS`),
  `tests/CMakeLists.txt`.
- `engine/include/motor/Motor.h` e `engine/include/motor/scene/Components.h`.
- `README.md` e `CLAUDE.md` da raiz, `docs/inicio.md`, `docs/visao.md`.
- Headers públicos da engine (`engine/include/motor/...`) usados por outro módulo: mudança de API é tarefa
  própria, combinada antes. Interface com a DLL de scripts mudou? `kScriptApiVersion` (ver `engine/CLAUDE.md`).
- Formatos lidos por outro módulo (`.mproject`, `motor-version.json`, manifesto do servidor): combine com o
  módulo que lê.
