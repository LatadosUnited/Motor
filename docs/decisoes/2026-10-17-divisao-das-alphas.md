---
tipo: decisao
data: 2026-10-17
status: aceita
fases: [16, 17, 18, 19, 20, 21, 29]
---

# Divisão das alphas 2, 3 e 4

## Contexto

Com a alpha 1 fechada (Fases 0 a 15), sobraram pendências da alpha 1, uma análise do código com problemas de
robustez e desempenho, e ideias de ferramentas (compilador integrado ao Motor).

## Decisão

- **Alpha 2:** funcionalidades e pendências (Fases 16 a 21).
- **Alpha 3:** desempenho e, depois dele, cenários abertos e renderização 2 (Fases 22 a 28).
- **Alpha 4:** ferramentas, incluindo o compilador integrado (Fases 29 a 31).

A proteção contra erro de script e o salvamento automático ficaram na alpha 2 (Fase 16), por ser o maior
risco no dia a dia.

## Alternativas

- Compilador integrado já na Fase 17: facilitaria a instalação, mas é grande e não era a prioridade do usuário.
- Ambiente (terreno, água, vegetação) na alpha 2: depende de instancing e de sombras baratas (alpha 3).

## Consequências

Até a alpha 4, o editor instalado depende do MSYS2 e do CMake para compilar scripts. A alpha 2 não tem
terreno nem água.
