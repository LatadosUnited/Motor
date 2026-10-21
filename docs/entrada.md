---
tipo: entrada
---

# Caixa de entrada

Achados que ainda não foram colocados numa fase: defeitos, limitações, ideias. Qualquer pessoa ou agente
acrescenta **no fim** da lista; a triagem leva cada linha para a nota de uma fase (como tarefa, com a origem)
ou a descarta com o motivo, e então apaga a linha daqui. Ver o [padrão](padrao.md), seção 6.

Formato: `- AAAA-MM-DD — quem achou — o quê — onde (arquivo:linha, se souber)`

## Para triar

- 2026-10-19 — Claude (gravação segura, Fase 16) — a falha intermitente do `Project::Save` não se repetiu em
  8 rodadas para capturar a mensagem; a correção (troca que tenta de novo, cf18356) cobre o caso do arquivo
  preso, que reproduz "Acesso negado". Se `test_export` ou `test_distribuicao` voltarem a falhar em paralelo,
  guardar a mensagem.
- 2026-10-19 — usuário (fecho da Fase 18) — o jogo de exemplo do guia que junta as funcionalidades da Fase 18
  (câmera orbital que não atravessa paredes, coleta por área, passos, música com volume separado, cursor preso,
  vidro e cerca recortada), rodado pelo `test_script_docs`, ficou de fora da fase — `docs/scripts.md`
- 2026-10-20 — Claude (Fase 22, painel de estatísticas) — com o viewport estreito (~830 px), a linha de FPS no topo
  do viewport fica por baixo dos botões de Play (os dois no mesmo lugar). Detalhe — `editor/src/viewport/SceneViewport.cpp`
  (overlay de estatísticas) e `PlayController::DrawControls`
- 2026-10-20 — Claude (Fase 22, contadores) — no editor Release, o ImGui gasta 6 a 7 ms de CPU por quadro numa cena
  de 4 entidades, mais que a cena inteira (Render ~2 ms). Medir com o Tracy o que pesa (painéis, Inspector) — alpha 3,
  Fase 26 (editor que escala)
