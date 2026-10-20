# Comparação de desempenho com a Unity

Meta da alpha 3: o Motor com **pelo menos 70% do desempenho da Unity** nas mesmas cenas, na mesma máquina
(`docs/roadmap/alpha-3.md`, Fase 22). Esta pasta tem as duas metades da comparação:

- `gerar_motor.py` + `motor/Scripts/`: gera o projeto "Motor Bench" com as 7 cenas.
- `unity/MotorBench/`: projeto gêmeo na Unity 6.3 (URP). `Assets/Bench/BenchRunner.cs` monta as mesmas cenas por
  código e mede; `Bobber.cs` e `Spawner.cs` são os scripts equivalentes; `Editor/BenchBuild.cs` configura o URP
  igual ao Motor e gera o player.
- `medir.ps1` (mede o Motor pelo título da janela) e `comparar.ps1` (roda as 7 cenas nos dois, alternando).

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

### 20/10/2026 — Fase 22, com o `glDrawRangeElements`

Mesma máquina e mesma build da Unity de 18/10; Motor 0.2.650 Release (branch `fase-22-medicao`). Quatro medições por
cena: as duas rodadas do `comparar.ps1` e duas da medição alternada abaixo. A tabela mostra a faixa; a porcentagem é
de cada par Motor/Unity medido em seguida.

| Cena | Motor | Unity | Motor em % da Unity | 18/10 |
|---|---|---|---|---|
| vazia | 12,2 a 12,6 ms | 13,6 a 15,2 ms | 108 a 124% | 110 a 113% |
| cubos1000 | 14,3 a 14,5 ms | 16,9 a 17,7 ms | 117 a 124% | 121 a 125% |
| cubos1000sombra | 17,3 a 17,5 ms | 20,9 a 22,8 ms | 121 a 131% | 124 a 126% |
| **cubos10000** | 84 a 113 ms | 37,1 a 59,6 ms | **37 a 53%** | 68 a 69% |
| **fisica1000** | 22,3 a 23,2 ms | 17,7 a 17,9 ms | **77 a 80%** | 76 a 81% |
| scripts1000 | 14,3 a 14,4 ms | 16,9 a 17,0 ms | 118 a 119% | 121% |
| spawn200 | 13,3 a 14,9 ms | 14,8 a 15,6 ms | 105 a 114% | 114 a 122% |

**Efeito do `glDrawRangeElements`** (medição alternada: o mesmo Player Release com e sem a correção, um depois do
outro, duas vezes por cena): neutro ou melhor em todas. `cubos1000sombra` de 18,6 para 17,4 ms; `fisica1000` de 24,2
para 22,5 ms; `cubos10000` de 110 a 111 para 84 a 101 ms; nas outras, até 0,5 ms (dentro do ruído).

Leitura:

- **`cubos10000` piorou desde 18/10, e não por causa da correção:** sem ela o Motor mede 110 ms (contra 54 ms em
  18/10). O Tracy mostra o porquê nessa cena, que não tem script nenhum: além dos ~30 ms de CPU dos 10 000 draws
  (Fase 24), o `SceneRuntime` gasta ~19 ms por quadro percorrendo as 10 000 entidades atrás de scripts
  (`ForEachScript` e `StartPending`, ~1,3 a 1,6 ms cada, 2 vezes por `Update` e 2 por passo fixo). E, como o passo
  fixo é de 60 Hz, quadro lento = mais passos por quadro = mais voltas pela cena: a cena oscila entre ~60 ms e
  ~110 ms de uma rodada para outra. Que mudança desde a 0.2.0 dobrou esse custo não foi isolada (sem bisect).
- `fisica1000` igual (77 a 80%); as cenas dominadas pela GPU continuam acima da Unity.
- A Unity também variou: `cubos10000` mediu 59,6 ms numa das quatro rodadas (37 nas outras).

Mudança no processo: a Fase 21 tirou o FPS do título do Player, e o `medir.ps1` deixou de ler. O Player ganhou
`--stats` (FPS no título) e o `comparar.ps1` o usa, com `--opcoes` num arquivo que não existe (opções padrão:
1280×720 em janela, MSAA 4x).

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
