---
tipo: alpha
alpha: 5
status: planejada
foco: Ferramentas
---

# Alpha 5 — Ferramentas (rascunho)

> **Rascunho.** Era a alpha 4 (Fases 29 a 31) antes do replanejamento de 21/10/2026
> ([decisão](../decisoes/2026-10-21-replanejamento.md)). Começa depois da [alpha 4](alpha-4.md); se a instalação
> sem MSYS2 ficar urgente (outras pessoas usando o Motor), a Fase 31 pode passar na frente.

Foco: quem instala o Motor não precisa instalar mais nada para programar, as ferramentas se atualizam sozinhas e
escrever scripts fica mais próximo do que engines grandes oferecem.

**A alpha 5 está pronta quando:** num computador sem MSYS2 nem CMake, o `MotorSetup` instala tudo, o editor compila
os scripts de um projeto novo, o Hub se atualiza para uma versão publicada no servidor, e um erro grave num script
(ponteiro inválido) não fecha o editor.

## Fase 31 — Compilador integrado

Hoje o editor depende do MSYS2 (GCC 15.2 UCRT64) e do CMake instalados nos caminhos do `MotorSDK.json`.

- **Escolher o compilador medindo:** GCC do MSYS2 contra Clang (llvm-mingw, com `lld` e `clangd`): tempo de
  compilação, tamanho e desempenho do jogo. O compilador dos scripts tem de ser o mesmo que gerou a `libMotor.dll`;
  MSVC fica de fora (não pode ser redistribuído).
- **Compilação feita pelo editor**, sem CMake nem make para os scripts: chama o compilador direto, compila em
  paralelo só os `.cpp` que mudaram (dependências pelo `-MMD`), com o cabeçalho pré-compilado, e liga a DLL.
- **Pacote "Compilador"** separado da versão do Motor (compilador, ligador, cabeçalhos, `gdb`, licenças), publicado
  pelo `MotorServidor` e compartilhado entre versões. Estimativa: 150 a 200 MB. Sem ele, o editor usa o MSYS2.
- **Recarga em menos de 1 s do salvar:** hoje 1,5 a 1,9 s num script simples e 5 a 6,5 s num que usa `<format>`.
  Caminhos: `<format>` no PCH, um formatador da engine na `libMotor.dll`, compilador que fica aberto.
  *(origem: Fase 17, medição de 18/10)*

## Fase 32 — Distribuição

*(Limitações das Fases 14 e 15.)*

- **Hub se atualiza** sozinho a partir do servidor (hoje só avisa e abre o link do instalador).
- **https e assinatura** dos arquivos, para servidores fora da rede local.
- **Ícones próprios** do Hub, do instalador e do editor.
- Hub como repositório próprio (opcional).

## Fase 33 — Scripts avançados

- **Scripts isolados:** um ponteiro inválido num script não derruba o editor (Play num processo filho, ou exceções
  estruturadas do Windows). *(Limite que sobrou da Fase 16.)*
- **Geração de código para os campos**, no estilo do UnrealHeaderTool: marcadores no `.cpp` geram o registro.
- **Recarga no estilo Live Coding** (só as funções alteradas): estudo primeiro.
- **Jogo de exemplo do guia** que junta as funcionalidades da Fase 18 (câmera orbital que não atravessa paredes,
  coleta por área, passos, música com volume separado, cursor preso, vidro e cerca recortada), rodado pelo
  `test_script_docs`. *(origem: entrada de 19/10, fecho da Fase 18)*
