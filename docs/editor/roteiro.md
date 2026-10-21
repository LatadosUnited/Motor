---
tipo: roteiro
modulo: editor
atualizado: 2026-10-21
---

# Editor — roteiro

[Estado](estado.md). Uma linha por tarefa; a origem entre parênteses. Detalhes dos itens antigos no
[arquivo](../arquivo/leiame.md). Feito sai daqui e vai para o `CHANGELOG`.

## Agora

Nada em andamento.

## Próximo

- [ ] **Editor que escala:** desfazer por diferenças (marcar entidades alteradas, guardar só o que mudou,
  limitar por memória), sem perder o desfazer automático dos componentes novos. *(Fase 26)*
- [ ] "Alterações não salvas" sem serializar a cena em JSON a cada 0,5 s: usar a marca de alteração do
  desfazer. *(Fase 26; medido: 337 ms por gravação na cena de referência)*
- [ ] Medir com o Tracy o que faz o ImGui gastar 6 a 7 ms por quadro numa cena pequena (painéis, Inspector) e
  cortar. *(Fase 22)*
- [ ] Linha de FPS do painel de estatísticas por baixo dos botões de Play com o viewport estreito.
  *(Fase 22, `SceneViewport.cpp` e `PlayController::DrawControls`)*

## Depois

- [ ] **Compilador integrado:** escolher entre GCC (MSYS2) e Clang (llvm-mingw) medindo; o editor chama o
  compilador direto (sem CMake), só os `.cpp` que mudaram; pacote "Compilador" separado, baixado pelo Hub e
  pelo instalador ([Instalador](../instalador/roteiro.md)). Sem o pacote, continua o MSYS2.
  *(Fase 29, [detalhes](../arquivo/roadmap/alpha-4.md))*
- [ ] Recarga de script em menos de 1 s do salvar (`<format>` no PCH, formatador na `libMotor.dll`, compilador
  que fica aberto). *(Fase 17)*
- [ ] **Scripts isolados:** um ponteiro inválido não derruba o editor (Play num processo filho, ou exceções
  estruturadas do Windows). *(Fase 16 / 31)*
- [ ] Geração de código para os campos dos scripts (marcadores no `.cpp`, registro gerado). *(Fase 31)*
- [ ] Estudo: recarga no estilo Live Coding (só as funções alteradas). *(Fase 31)*
- [ ] Ícone próprio do editor. *(Fase 30)*
- [ ] Versão própria do editor, separada da engine; decidir com a [Engine](../engine/roteiro.md) o que o
  `motor-version.json` e a tela Sobre mostram. *(decisão de 21/10/2026)*
