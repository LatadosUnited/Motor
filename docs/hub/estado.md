---
tipo: estado
modulo: hub
versao: 0.2.0
atualizado: 2026-10-21
---

# Hub — estado

O `MotorHub.exe`, como o Unity Hub. Programa separado: **não liga na `libMotor.dll`** (GLFW, ImGui e
nlohmann_json estáticos) e fala com o editor só pela linha de comando e lendo `.mproject` e
`motor-version.json` ([decisão](../decisoes/2026-10-17-hub-fora-da-engine.md)). Código em `hub/src/`. Como usar:
`hub/README.md`. [Roteiro](roteiro.md) · [Mapa de tudo o que ele faz](mapa.md).

## O que faz hoje

- **Projetos:** lista com versão do Motor e última abertura; abrir com a versão do projeto (ou escolher
  outra), adicionar pasta, remover da lista, mostrar no Explorer, busca.
- **Novo projeto:** chama `MotorEditor --create` da versão escolhida.
- **Versões instaladas** em `%LOCALAPPDATA%\Motor\Versões\`: adicionar de uma pasta montada (`motor_package`),
  versão padrão, remover (Lixeira).
- **Baixar versões** dos servidores da lista (`MotorServidor`), com progresso, cancelar e retomar; cada arquivo
  conferido pelo SHA-256. Avisa quando há um Hub mais novo publicado.
- **Linha de comando** com as mesmas ações, sem janela (agentes e testes), e `--data` para outra pasta de dados.
- Configuração em `%LOCALAPPDATA%\Motor\hub.json` (formato `motor-hub` 2).
- Lógica sem janela no `HubCore`, testada em `tests/test_hub.cpp`.

## Limitações conhecidas

- **Não se atualiza sozinho:** só avisa e abre o link do instalador.
- Servidores só por http, sem assinatura dos arquivos: serve para rede local.
- Sem ícone próprio.
- Versão própria (0.2.0) já separada da do Motor, mas definida à mão no `hub/CMakeLists.txt`.

## Histórico

Fases 14 e 15 na [alpha 1](../arquivo/roadmap/alpha-1.md).
