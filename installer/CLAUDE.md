# Instalador — instruções para o Claude

Módulo Instalador: `distrib/` (núcleo), `server/` (`MotorServidor.exe`) e `installer/` (`MotorSetup.exe`).
Estado e roteiro em `docs/instalador/`; uso em `installer/README.md`. As regras comuns estão no `CLAUDE.md` da raiz.

- `distrib/` é o núcleo sem janela (manifesto `motor-releases`, SHA-256 com BCrypt, download com retomada, plano
  de instalação), testado em `tests/test_distribuicao.cpp`. Lógica nova vai aqui, com teste.
- `server/` publica e serve versões (cpp-httplib); `installer/` instala o Hub (embutido como recurso), atalhos e
  registro de desinstalação no HKCU.
- Como o Hub, **não ligam na `libMotor.dll`**; usam o `common/` (gravação segura: `motor::WriteFileAtomic` e
  `ReplaceFileAtomic`, nunca `fs::rename` para trocar um arquivo).
- Formatos versionados: `manifesto.json` (`motor-releases`), `instalacao.json`, `motor-version.json` (lido pelo Hub).
  Mudou, sobe a versão e continua lendo o antigo.
- Versões: o MotorSetup leva a do Hub (`MOTOR_HUB_VERSION`); o servidor tem `MOTOR_SERVER_VERSION` em
  `server/CMakeLists.txt`.
- Testes e agentes: `MotorSetup --silencioso` com `--pasta`, `--dados`, `--sem-atalhos`, `--sem-registro` (ou
  `--atalhos-em` e `--chave-registro` de teste); `MotorServidor --pasta <dir> --local`. Nunca instale nem
  registre nada nas pastas e chaves reais do usuário sem ele pedir.
