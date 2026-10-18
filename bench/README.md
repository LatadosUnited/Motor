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
