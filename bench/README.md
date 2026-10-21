# Comparação de desempenho com a Unity

Meta da alpha 3: o Motor com **pelo menos 70% do desempenho da Unity** nas mesmas cenas, na mesma máquina
(`docs/roadmap/alpha-3.md`, Fase 22). Esta pasta tem as duas metades da comparação:

- `gerar_motor.py` + `motor/Scripts/`: gera o projeto "Motor Bench" com as 7 cenas.
- `unity/MotorBench/`: projeto gêmeo na Unity 6.3 (URP). `Assets/Bench/BenchRunner.cs` monta as mesmas cenas por
  código e mede; `Bobber.cs` e `Spawner.cs` são os scripts equivalentes; `Editor/BenchBuild.cs` configura o URP
  igual ao Motor e gera o player.
- `medir.ps1` (mede o Player pelos contadores: `--perf` e `--segundos`; `-Csv` guarda o CSV com CPU, GPU e memória)
  e `comparar.ps1` (roda as 7 cenas nos dois, alternando).
- `referencia/`: a **cena de referência** do Motor (abaixo) e a linha de base do teste de desempenho.

## Cena de referência e teste de desempenho

`referencia/CenaReferencia.cpp` monta ~5000 entidades: chão e 4000 objetos estáticos (500 com collider), 900 caixas
com corpo rígido em colunas, 50 cubos com o script `Bobber`, 1 luz direcional com sombra Média e 8 pontuais, câmera
com ouvinte, 10 fontes de som e uma interface pequena. É a única definição da cena:

- **`test_desempenho`** (ctest, rótulo `desempenho`) monta a cena sem janela e mede a CPU: o passo do jogo (1
  FixedUpdate + 1 Update, com física, scripts, áudio e UI) e o que o editor faz com a cena (montar, copiar, comparar,
  gravar e ler o JSON). De cada número fica a menor de 3 repetições, comparada com `referencia/base.txt`: mais de
  30% acima falha. Só mede em Release; no build Debug aparece como pulado.
- **`--gerar`** grava a mesma cena num projeto, para medir com GPU no Player (`--perf`) ou no editor (Estatísticas).

```bash
ctest --test-dir build-release -L desempenho --output-on-failure        # compara com a base
MOTOR_DESEMPENHO_GRAVAR=1 ctest --test-dir build-release -L desempenho  # grava a base (outra máquina, melhora medida)
build-release/bin/test_desempenho.exe tmp --gerar "C:/temp/bench/Motor Bench"   # Assets/Scenes/referencia.mscene
build-release/bin/Player.exe "C:/temp/bench/Motor Bench" --quadros 900 --perf referencia.csv  # com startupScene nela
```

O projeto precisa do script `Bobber` compilado (o "Motor Bench" do `gerar_motor.py` já tem).

### Linha de base (20/10/2026)

Máquina de referência (i5 de 4 núcleos, Intel HD 530), Release.

| `test_desempenho` (CPU, ms) | jogo_passo | cena_montar | cena_copiar | cena_comparar | cena_json | cena_ler |
|---|---|---|---|---|---|---|
| base.txt | 3,1 | 5,4 | 4,7 | 1,8 | **337** | **314** |

Player 1280×720, vsync desligado, 900 quadros com `--perf`, medianas depois de 4 s:

| Quadro | CPU | GPU | Números |
|---|---|---|---|
| **58,2 ms (17 FPS)** | Render 33,0; FixedUpdate 19,6 (Física 19,3); Update 1,0; Swap 3,4 | Renderer 56,3: Cena HDR 34,3, Sombras 15,3, Pós 6,9 | 7927 draws (3372 de sombra), 2,1 mi triângulos, 130 MB de vídeo (64 de sombras, 57 de pós) |

Leitura: a GPU é o limite (56 ms; as 1000 esferas de 2300 triângulos pesam na Cena HDR: LOD e instancing, Fases 24 e
27), as sombras custam 15 ms (Fase 23) e a CPU do render gasta 33 ms montando 7927 draws (Fase 24). A física mede 19 ms
por quadro porque o quadro lento pede 3 a 4 passos fixos (no `test_desempenho`, com 1 passo, o quadro inteiro do jogo
custa 3,1 ms). No editor, gravar a cena em JSON leva 337 ms, e a checagem de "não salvo" faz isso a cada 0,5 s (Fase 26).

## Cenas

| Cena | O que mede |
|---|---|
| `vazia` | custo fixo do pipeline (só câmera e luz) |
| `cubos1000` | 1000 cubos estáticos, sem sombra (desenho) |
| `cubos1000sombra` | os mesmos com sombra (4 cascatas de 2048, 150 m) |
| `cubos10000` | 10 000 cubos estáticos (custo por desenho na CPU) |
| `fisica1000` | 1000 corpos rígidos desmoronando numa pilha (Jolt contra PhysX) |
| `scripts1000` | 1000 cubos movidos por script a cada quadro (C++ contra C#) |
| `spawn200` | criar e destruir 200 objetos por quadro |

Configuração igual dos dois lados: janela 1280×720, vsync desligado, HDR com 16 bits por canal, MSAA 4x, bloom,
tone mapping ACES, FXAA, só a luz direcional. Na Unity, o build desliga o SSAO e as texturas de profundidade e de
cor opaca que o template URP liga (o Motor não faz essas passadas). Medição: médias de 1 s depois de 4 s de
aquecimento, mediana de 8 delas.

## Como rodar

```bash
# 1. Motor: build Release e o projeto de comparação (os scripts compilam ao abrir o projeto no editor)
cmake --build build-release -j8 --target Player MotorEditor
python bench/gerar_motor.py "C:/temp/bench"
build-release/bin/MotorEditor.exe "C:/temp/bench/Motor Bench" --no-settings   # espere compilar e feche
# 2. Unity: configura o URP, monta a cena e gera bench/unity/Build/MotorBench.exe
"C:/Program Files/Unity/Hub/Editor/6000.3.24f1/Editor/Unity.exe" -batchmode -projectPath bench/unity/MotorBench -executeMethod BenchBuild.MontarECompilar -logFile build.log
# 3. Compara (PowerShell); feche outros programas que usem a GPU
powershell -File bench/comparar.ps1 -Projeto "C:/temp/bench/Motor Bench"
```

## Resultados

### 20/10/2026 — Fase 22: `glDrawRangeElements`, lista de scripts e layout da UI

Mesma máquina e mesma build da Unity de 18/10; Motor 0.2.650 Release (branch `fase-22-medicao`), duas rodadas do
`comparar.ps1`. A máquina estava mais rápida nesta sessão (a Unity também: `vazia` 11,6 a 12,6 ms contra 13,5); o que
vale é a razão de cada par Motor/Unity medido em seguida.

| Cena | Motor | Unity | Motor em % da Unity | 18/10 |
|---|---|---|---|---|
| vazia | 11,4 a 12,6 ms | 11,6 a 12,6 ms | 100 a 102% | 110 a 113% |
| cubos1000 | 13,4 a 14,4 ms | 15,1 a 15,4 ms | 107 a 112% | 121 a 125% |
| cubos1000sombra | 16,5 ms | 18,1 a 18,4 ms | 110 a 112% | 124 a 126% |
| **cubos10000** | 33,4 a 33,7 ms | 33,1 a 33,5 ms | **98 a 100%** | 68 a 69% |
| **fisica1000** | 18,2 a 18,4 ms | 15,4 ms | **84 a 85%** | 76 a 81% |
| scripts1000 | 13,4 ms | 14,9 ms | 112% | 121% |
| spawn200 | 12,3 a 12,4 ms | 12,5 ms | 101% | 114 a 122% |

Todas as cenas acima da meta de 70%. O que mudou no Motor (achado com o Tracy, Fase 22):

- **`glDrawRangeElements`** em vez de `glDrawElements`: o driver da Intel varria os índices na CPU a cada draw.
  Medição alternada (o mesmo Player com e sem, um depois do outro): `cubos1000sombra` de 18,6 para 17,4 ms,
  `fisica1000` de 24,2 para 22,5 ms, `cubos10000` de 110 para 84 a 101 ms.
- **Lista de scripts no `SceneRuntime`:** os laços dos scripts percorriam as 10 000 entidades (~1,5 ms por laço, 2 por
  `Update` e 2 por passo fixo, numa cena sem script nenhum). O `FixedUpdate` caiu de 3,75 para 0,2 ms por passo.
- **Layout da UI só com a UI:** o `UiSystem` e o `UiRenderer` montavam e ordenavam a árvore inteira da cena a cada
  quadro (~7,7 ms cada numa cena sem UI). O `Update` caiu de 8,7 para 0,8 ms.
- `cubos10000` ficou estável: ~60 a ~110 ms entre rodadas iguais antes, 33,4 a 33,7 agora. Antes, o quadro lento
  pedia mais passos fixos, e cada passo percorria a cena de novo (bola de neve).

A comparação desta data feita antes das duas últimas mudanças (37 a 53% em `cubos10000`) foi descartada: o
`comparar.ps1` corrompia o `.mproject` (abaixo), e a cena oscilava demais. Um bisect contra o commit da medição de
18/10 (a94da94), com o projeto gerado por ele, mostrou o Player da época e o de hoje sem as mudanças praticamente
iguais (63 a 67 contra 67 a 77 ms, medido por `--quadros`): não houve regressão de 2× desde a 0.2.0.

Mudanças no processo:

- A Fase 21 tirou o FPS do título do Player, e o `medir.ps1` deixou de ler. O Player ganhou `--stats` (FPS no
  título) e o `comparar.ps1` o usa, com `--opcoes` num arquivo que não existe (opções padrão: 1280×720, MSAA 4x).
- O `comparar.ps1` reescrevia o `.mproject` com o `ConvertTo-Json` do PowerShell, que por padrão só vai até 2
  níveis: no formato 4 do projeto, as camadas de física e as ações de entrada viravam texto (`"@{index=0; ...}"`).
  Agora usa `-Depth 32`. Projetos já estragados: gere de novo com o `gerar_motor.py` e abra no editor.

### 18/10/2026 — antes da alpha 3

i5 de 4 núcleos, Intel HD 530; Motor 0.2.0 Release (OpenGL 4.5) contra Unity 6000.3.24f1 (URP 17.3, Direct3D 11,
Mono). Duas rodadas; a tabela mostra a faixa. Maior que 100% = o Motor foi mais rápido.

| Cena | Motor | Unity | Motor em % da Unity |
|---|---|---|---|
| vazia | 12,0 a 12,3 ms | 13,5 ms | 110 a 113% |
| cubos1000 | 13,9 ms | 16,9 a 17,3 ms | 121 a 125% |
| cubos1000sombra | 16,4 a 16,7 ms | 20,5 a 20,7 ms | 124 a 126% |
| **cubos10000** | 53,8 a 54,5 ms | 37,1 ms | **68 a 69%** |
| **fisica1000** | 23,0 a 23,5 ms | 17,9 a 18,6 ms | **76 a 81%** |
| scripts1000 | 14,0 ms | 16,9 ms | 121% |
| spawn200 | 12,9 a 13,0 ms | 14,7 a 15,9 ms | 114 a 122% |

Leitura:

- **Abaixo da meta:** `cubos10000` (68 a 69%). É o custo de CPU por desenho: o Motor faz um draw por objeto, com
  uniforms procurados por nome; a Unity agrupa pelo SRP Batcher. Alvo da Fase 24 (renderer em lote).
- **Perto do limite:** `fisica1000` (76 a 81%). Jolt e PhysX são parecidos; a diferença deve estar na
  sincronização entre a cena e a física (transformações e `WorldMatrix` sem cache a cada passo). Alvo da Fase 25.
- **Acima da Unity** nas cenas dominadas pela GPU: o pipeline do Motor é mais enxuto que o URP mesmo com o SSAO
  desligado. Nessa máquina a GPU é o gargalo, e por isso `scripts1000` e `spawn200` medem mais o desenho que os
  scripts.

Cuidados: a Unity está em Mono (o IL2CPP para player de Windows não está instalado) e em Direct3D 11, o Motor em
OpenGL; o GPU Resident Drawer da Unity está desligado (o padrão do template), e ligá-lo deve deixar a Unity mais
rápida em `cubos10000`. Durante as medições havia um editor do Motor aberto na máquina (ruído igual nos dois).
