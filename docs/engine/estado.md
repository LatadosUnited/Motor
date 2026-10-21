---
tipo: estado
modulo: engine
versao: 0.2.650
atualizado: 2026-10-21
---

# Engine — estado

A `libMotor.dll` e o `Player.exe`. Código em `engine/` (camadas core → renderer → project → assets → scene →
app, mais physics, ui e audio por cima), `player/` e `sandbox/`. Como usar: `engine/README.md`.
[Roteiro](roteiro.md).

## O que faz hoje

- **Cena:** entidades com hierarquia, componentes registrados (Inspector, serialização, cópia e desfazer saem
  dos campos), prefabs aninhados, cenas `.mscene` versionadas, assets por GUID (`.meta`).
- **Render:** OpenGL 4.5, PBR, sombras em cascata (direcional) e de luzes pontuais e spot, céu em degradê ou
  imagem HDR com reflexos, HDR com bloom, ACES e FXAA, MSAA, transparência e recorte, neblina; malhas animadas por
  esqueleto.
- **Modelos:** glTF e FBX (malhas, materiais, esqueleto e clipes), OBJ próprio, texturas PNG/JPG.
- **Física:** Jolt (corpos rígidos, colliders com malha, personagem, triggers, raios e consultas de área,
  camadas de colisão).
- **Áudio:** miniaudio (som 3D, grupos de volume, música em streaming).
- **UI do jogo:** retângulos com âncoras, imagens, texto (fontes SDF), botões.
- **Scripts em C++:** `motor::Script` com ciclo de vida, entrada e ações, temporizadores, mensagens, saves,
  opções do jogador; guia e referência em [scripts.md](../scripts.md) e [scripts-api.md](../scripts-api.md)
  (`kScriptApiVersion` 16).
- **Player:** roda o jogo exportado, com opções do jogador e tela de carregamento.
- **Medição:** Tracy opcional (`MOTOR_TRACY`), contadores por quadro (CPU por sistema, GPU por passada, memória
  de vídeo), `Player --perf` em CSV, cena de referência de ~5000 entidades e o `test_desempenho` no ctest
  (`bench/README.md`).

## Números (máquina de referência: i5 de 4 núcleos, Intel HD 530)

- **Contra a Unity 6.3 (URP), 20/10/2026:** todas as 7 cenas da comparação acima da meta de 70%: vazia 100 a
  102%, 1000 cubos 107 a 112%, 10 000 cubos 98 a 100%, física 1000 corpos 84 a 85% (a pior), scripts 112%.
  Ressalvas e detalhes em `bench/README.md`.
- **Cena de referência no Player:** ~56 ms (18 FPS), limitada pela GPU (Cena HDR 33 ms, sombras 15 ms); 7927
  draws; 130 MB de vídeo (64 MB das sombras).
- **Depois de 20/10** (ainda fora da tabela da Unity): `cubos10000` com Update de ~7 para 0,02 ms; destruir 200
  entidades por quadro de 8 para 0,6 ms; matriz de mundo numa hierarquia funda de 0,9 para 0,23 ms.

## Limitações conhecidas

- Só Windows e OpenGL 4.5.
- **CPU por desenho:** um draw por objeto, uniforms procurados por nome; 10 000 objetos custam ~40 ms de CPU no
  Render.
- **Custo fixo de GPU:** ~9 ms de pós-processamento numa cena vazia a 1280×720 (MSAA 4x em HDR).
- **Sombras caras:** 4 cascatas em 32 bits; a qualidade Alta usa ~256 MB de vídeo.
- **Física:** sincronização cena–física a cada passo; picos isolados de até ~185 ms no `test_physics_bench`.
- **Dados:** cada entidade é um objeto separado na memória; o quadro roda numa thread só (a Jolt usa as outras).
- Sem terreno, água, vegetação, partículas, SSAO, reflexos locais, navegação, rede.
- O `Project::Save` falhou de forma intermitente em testes paralelos antes da troca que tenta de novo
  (cf18356); se voltar, guardar a mensagem (`docs/entrada.md`).

## Histórico

Fases 0 a 25 do roteiro antigo, em [arquivo](../arquivo/leiame.md). As últimas: [Medição](../arquivo/roadmap/alpha-3/fase-22-medicao.md)
(Tracy, contadores, cena de referência) e [Cena, dados e threads](../arquivo/roadmap/alpha-3/fase-25-cena.md)
(listas por tipo, destruir em lote, matriz de mundo com cache).
