---
tipo: estado
modulo: instalador
versao: 0.2.0 (MotorSetup) / 0.1.0 (MotorServidor)
atualizado: 2026-10-21
---

# Instalador — estado

A distribuição do Motor: o `MotorSetup.exe` (instala o Hub), o `MotorServidor.exe` (publica e serve versões numa
rede local) e o núcleo sem janela `distrib/` (manifesto, SHA-256, download com retomada, plano de instalação).
Como o Hub, **não liga na `libMotor.dll`** ([decisão](../decisoes/2026-10-17-instalador-e-servidor-proprios.md)).
Código em `distrib/src/`, `server/src/` e `installer/src/`. Como usar: `installer/README.md`.
[Roteiro](roteiro.md).

## O que faz hoje

- **MotorSetup:** um arquivo só, com o Hub dentro; instala por usuário, sem administrador; atalhos, registro de
  desinstalação e, se pedido, uma versão do Motor (do servidor ou de uma pasta montada). Desinstala mandando a
  pasta para a Lixeira. Modo silencioso para testes e agentes. Avisa se faltam MSYS2 e CMake (não instala).
- **MotorServidor:** pasta de publicações com `manifesto.json` (`motor-releases` 1); publica versões
  (`motor_package`) e o Hub com o instalador; serve por HTTP na porta 47900, com `Range` para retomar; página
  com as versões e o link do instalador.
- **distrib:** núcleo testado em `tests/test_distribuicao.cpp` (manifesto, SHA-256 com BCrypt, download, plano
  de instalação).
- Versões: o MotorSetup leva a versão do Hub que ele embute (0.2.0); o servidor tem a sua (0.1.0).

## Limitações conhecidas

- Só http e sem assinatura: para rede local.
- Não instala o MSYS2 nem o CMake, que o editor precisa para compilar scripts.
- Arquivos servidos um a um, sem compressão.
- Sem ícones próprios.

## Histórico

Fase 15 na [alpha 1](../arquivo/roadmap/alpha-1.md).
