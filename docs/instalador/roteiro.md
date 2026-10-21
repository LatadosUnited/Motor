---
tipo: roteiro
modulo: instalador
atualizado: 2026-10-21
---

# Instalador — roteiro

[Estado](estado.md). Uma linha por tarefa; a origem entre parênteses. Feito sai daqui e vai para o `CHANGELOG`.

## Agora

Nada em andamento.

## Próximo

- [ ] Versões por módulo no `motor-version.json` e no manifesto do servidor (engine e editor separados, se a
  [Engine](../engine/roteiro.md) e o [Editor](../editor/roteiro.md) separarem), lendo os formatos antigos.
  *(decisão de 21/10/2026)*
- [ ] **https e assinatura** dos arquivos, para servidores fora da rede local. *(Fase 30)*
- [ ] Ícones próprios do instalador e do servidor. *(Fase 30)*

## Depois

- [ ] Publicar e baixar o pacote **"Compilador"** (compilador, ligador, cabeçalhos, biblioteca padrão, `gdb`, com
  licença e link do código-fonte), compartilhado entre as versões; com ele, o instalador deixa de pedir o MSYS2 e
  o CMake ([Editor](../editor/roteiro.md)). *(Fase 29)*
- [ ] Compressão dos arquivos no servidor.
