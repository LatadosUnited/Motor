---
tipo: alpha
alpha: 2
status: em andamento
foco: Funcionalidades e pendências
---

# Alpha 2 — Funcionalidades e pendências

| Alpha | Foco | Roteiro |
|---|---|---|
| 1 | Base da engine, editor, jogo exportado, Hub e instalador (Fases 0 a 15) | ✅ [alpha-1.md](alpha-1.md) |
| **2** | **Funcionalidades e pendências (Fases 16 a 21)** | **esta nota** |
| 3 | Desempenho, cenários abertos e renderização 2 (Fases 22 a 28) | rascunho: [alpha-3.md](alpha-3.md) |
| 4 | Ferramentas: compilador integrado, distribuição, scripts avançados (Fases 29 a 31) | rascunho: [alpha-4.md](alpha-4.md) |

Como esta pasta se organiza: [padrão da documentação](../padrao.md). Achados ainda sem fase:
[caixa de entrada](entrada.md). Tabela das fases com status: `fases.base` (Obsidian).

**A alpha 2 está pronta quando:** uma pessoa que não conhece o código do Motor instala tudo pelo `MotorSetup`,
faz no editor um jogo 3D pequeno que salva o progresso, aceita controle e usa objetos transparentes; um erro
num script não fecha o editor nem perde trabalho; o jogo é exportado em Release e roda em outro computador da
rede.

## Onde estamos (18/10/2026)

Pronto na alpha 1: núcleo, renderização PBR com sombras, céu HDR e pós-processamento, cena e componentes,
Play, scripts C++ com recarga, luzes, física (Jolt), materiais e prefabs, glTF com animação, HUD, áudio,
exportar o jogo (Player), documentação de scripts, servidor MCP do editor, Hub de versões, instalador e
servidor de versões. Main sem pendências de merge.

## Prioridades

| # | Fase | Status | Por quê | Pode rodar junto com |
|---|---|---|---|---|
| 1 | [Fase 16 — Robustez e correções](alpha-2/fase-16-robustez-e-correcoes.md) | pronta | hoje um erro de script fecha o editor e o trabalho não salvo se perde; contornos conhecidos | 17 (áreas diferentes) |
| 2 | [Fase 17 — Scripts mais rápidos e fáceis](alpha-2/fase-17-scripts.md) | pronta | compilações de 10 a 124 s medidas num projeto real; menos código repetido | 16, 18 |
| — | [Organização do código](alpha-2/organizacao-do-codigo.md) | pronta | arquivos de 600 a 1200 linhas e o editor numa pasta só; dividir antes da 18 e da 20, sem mudar comportamento | — (mexe em quase tudo) |
| 3 | [Fase 18 — Funcionalidades de jogo](alpha-2/fase-18-funcionalidades-de-jogo.md) | em andamento | física, áudio, cursor, formas e transparência que jogos comuns pedem | 17 |
| 4 | [Fase 19 — Validação de uso real](alpha-2/fase-19-validacao.md) | planejada | o editor quase só foi testado por agentes; a lista de defeitos decide o resto | — |
| 5 | [Fase 20 — Editor produtivo](alpha-2/fase-20-editor-produtivo.md) | planejada | multi-seleção, copiar/colar, snapping, Cena e Jogo lado a lado, prefab dentro de prefab | 21 |
| 6 | [Fase 21 — Jogo entregue](alpha-2/fase-21-jogo-entregue.md) | planejada | opções do jogador, ícone, salvar progresso, controle, exportação Release | 20 |

A 16 vem primeiro porque quem começa a programar agora perde trabalho a cada erro de script. A 17 e a 18
vêm antes da validação porque a validação é fazer um jogo, e é nelas que esse jogo vai esbarrar.

## Fora da alpha 2

- **Desempenho** (sombras, renderer em lote, cena e editor que escalam): [alpha 3](alpha-3.md).
- **Cenários abertos** (terreno, água, vegetação, partículas) e **renderização 2** (SSAO, reflexos locais): fim
  da alpha 3, depois do desempenho, porque vegetação e partículas dependem de instancing e de sombras baratas.
- **Compilador integrado**, atualização do Hub, https e scripts isolados: [alpha 4](alpha-4.md).
- **Depois da alpha 4:** rede (multiplayer), navegação (pathfinding), Lua, glTF com morph targets e câmeras,
  partículas na GPU, céu com nuvens e ciclo de dia e noite.

## Ordem e dependências

```
Fase 16 Robustez e correções ─┬─ Fase 19 Validação ──┬─ Fase 20 Editor produtivo
Fase 17 Scripts ──────────────┤                      └─ Fase 21 Jogo entregue
Fase 18 Funcionalidades ──────┘
```

Fases que não dependem uma da outra podem rodar em paralelo, cada uma no seu branch, respeitando as áreas do
`CLAUDE.md` ("Trabalho em paralelo").
