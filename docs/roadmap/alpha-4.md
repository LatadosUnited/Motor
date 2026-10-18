---
tipo: alpha
alpha: 4
status: planejada
foco: Ferramentas
---

# Roteiro da Alpha 4 — Ferramentas (rascunho)

> **Rascunho.** Começa depois da alpha 3 ([`alpha-3.md`](alpha-3.md)). Os números das fases e a ordem podem
> mudar.

Foco: quem instala o Motor não precisa instalar mais nada para programar, as ferramentas se atualizam
sozinhas e escrever scripts fica mais próximo do que engines grandes oferecem.

**A alpha 4 está pronta quando:** num computador sem MSYS2 nem CMake, o `MotorSetup` instala tudo, o editor
compila os scripts de um projeto novo, o Hub se atualiza para uma versão publicada no servidor, e um erro grave
num script (ponteiro inválido) não fecha o editor.

## Fase 29 — Compilador integrado

Hoje o editor depende do MSYS2 (GCC 15.2 UCRT64) e do CMake instalados nos caminhos do `MotorSDK.json`.

- **Escolher o compilador medindo:** compilar o mesmo projeto de scripts (e a engine) com o GCC do MSYS2 e
  com o Clang (llvm-mingw, com `lld` e `clangd`); comparar tempo de compilação, tamanho e desempenho do jogo.
  O compilador dos scripts tem de ser **o mesmo que gerou a `libMotor.dll`** (mesma `libstdc++`/`libc++`):
  trocar de compilador é recompilar e republicar a engine. MSVC fica de fora (não pode ser redistribuído).
- **Compilação feita pelo editor**, sem CMake nem make para os scripts: chama o compilador direto, compila em
  paralelo só os `.cpp` que mudaram (dependências pelo `-MMD`), com o cabeçalho pré-compilado da Fase 17, e
  liga a DLL. Sem o passo de configurar.
- **Pacote "Compilador"** separado da versão do Motor: só o que os scripts usam (compilador, ligador,
  cabeçalhos do C++ e do Windows, biblioteca padrão, `gdb`), com licença e link do código-fonte. O
  `motor-version.json` diz qual compilador a versão precisa; o `MotorServidor` publica, o Hub e o instalador
  baixam uma vez e as versões que usam o mesmo compilador compartilham. Estimativa: 150 a 200 MB.
- Sem o pacote, o editor continua usando o MSYS2, como hoje.

## Fase 30 — Distribuição

*(Limitações das Fases 14 e 15.)*

- **Hub se atualiza** sozinho a partir do servidor (hoje só avisa e abre o link do instalador).
- **https e assinatura** dos arquivos, para servidores fora da rede local.
- **Ícones próprios** do Hub, do instalador e do editor.
- Hub como repositório próprio (opcional).

## Fase 31 — Scripts avançados

- **Scripts isolados:** um ponteiro inválido num script não derruba o editor. Estudar e escolher: rodar o jogo
  (Play) num processo filho que o editor comanda, ou proteger as chamadas com tratamento de exceções
  estruturadas do Windows. *(Limite que sobrou da Fase 16.)*
- **Geração de código para os campos**, no estilo do UnrealHeaderTool: o editor lê marcadores no `.cpp` e gera
  o registro (`Register`/`Field`), sem escrever a lista à mão.
- **Recarga mais rápida** no estilo Live Coding: aplicar só as funções alteradas no processo que está rodando.
  Estudo primeiro; só entra se o ganho sobre a recarga da Fase 17 compensar.
