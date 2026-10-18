---
tipo: decisao
data: 2026-10-17
status: aceita
fases: [15]
---

# Instalador e servidor de versões próprios

## Contexto

O Hub instalado pelo app do Claude não aparecia para o usuário (o app é um pacote MSIX e redireciona o que
grava em `AppData`). O usuário pediu um instalador de verdade e um servidor local para distribuir as versões.

## Decisão

Instalador próprio (`MotorSetup.exe`, por usuário, sem administrador, Hub embutido) e servidor próprio
(`MotorServidor.exe`, cpp-httplib, que já era dependência do MCP). Arquivos um a um, sem compressão,
conferidos por SHA-256 da API do Windows (BCrypt).

## Alternativas

- Inno Setup ou NSIS: robustos, mas trazem uma ferramenta nova para o build.
- Pacotes em zip: menos arquivos para baixar, mas exigiriam uma biblioteca de compressão.

## Consequências

Nenhuma biblioteca nova. Downloads maiores (sem compressão), aceitáveis na rede local. Só http por enquanto;
https e assinatura ficaram para a alpha 4.
