# Motor

Engine 3D em C++20 + OpenGL 4.5, escrita do zero. Dependências externas mínimas:
**GLFW** (janela/input), **GLM** (matemática), **Dear ImGui** (UI do editor, branch docking),
**ImGuizmo** (gizmos de transform), **nlohmann/json** (cenas e projetos), **stb_image** (PNG/JPG),
**cgltf** (modelos glTF), **ufbx** (modelos FBX), **Jolt Physics** (física), **miniaudio** (som) e **cpp-httplib** (servidor MCP do
editor e servidor de versões), baixadas
automaticamente pelo CMake. O leitor de OBJ é próprio; as fontes da UI do jogo usam o `imstb_truetype.h` que já
vem com o Dear ImGui.
O carregador de OpenGL é próprio (`engine/src/renderer/GL.h`).

## Compilar (Windows, MSYS2 UCRT64)

```bash
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure   # testes (sem janela)
./build/bin/Sandbox.exe       # demo
./build/bin/MotorEditor.exe    # editor (reabre o último projeto)
./build/bin/MotorEditor.exe "C:/caminho/MeuJogo"   # abre um projeto específico
./build/bin/Player.exe "C:/caminho/MeuJogo"        # roda o jogo sem o editor (ver "Exportar o jogo")
./build/bin/MotorHub.exe       # Hub: projetos e versões instaladas (ver "Hub do Motor")
./build/bin/MotorServidor.exe  # servidor de versões na rede local (ver "Servidor de versões")
./build/bin/MotorSetup.exe     # instalador do Hub (ver "Instalar")
```

Executáveis e DLLs ficam juntos em `build/bin`: a engine é a `libMotor.dll` (com ImGui e GLFW dentro), e
o runtime do C++ (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) é copiado do toolchain
para lá. `build/bin/sdk/` tem o que o editor usa para compilar os scripts dos projetos (ver "Scripts").

### Medir com o Tracy (profiler por quadro)

Um build à parte, em Release, com a opção `MOTOR_TRACY`:

```bash
cmake -S . -B build-tracy -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DMOTOR_TRACY=ON -DMOTOR_BUILD_TESTS=OFF
cmake --build build-tracy -j8
./build-tracy/bin/MotorEditor.exe "C:/caminho/MeuJogo"   # ou Player.exe, Sandbox.exe
```

Abra o visualizador do Tracy **0.14.1** (`windows-0.14.1.zip` nos releases do Tracy; tem de ser a mesma versão do
cliente) e conecte em `127.0.0.1`. Ele só grava com o visualizador conectado, e só aceita conexões da própria
máquina. Aparecem o quadro dividido (eventos, update, render, ImGui, swap), cada sistema do jogo, os scripts, as
passadas do renderer na CPU e na GPU, e os gráficos de draws e triângulos. Zona nova no código:
`MOTOR_PROFILE_SCOPE("Nome")` de `motor/core/Profile.h` (sem o Tracy, não gera código). Decisão:
`docs/decisoes/2026-10-20-profiler-tracy.md`.

### Contadores por quadro (sem o Tracy)

Sempre ligados, em qualquer build: ms de CPU de cada parte do quadro (eventos, update, render, ImGui, swap, cada
sistema do jogo, os scripts), ms de GPU de cada passada do renderer (timer queries lidas ~4 quadros depois, sem a CPU
esperar), números (draws, draws de sombra, triângulos, recortados, transparentes) e a memória de vídeo por tipo de
recurso em MB (texturas, malhas, framebuffers do editor, pós-processamento, sombras, céu, buffers; estimativa pelo
formato e tamanho de cada objeto, registrado em `motor/renderer/GpuMemory.h`). Onde ver:

- **Viewport do editor:** botão **Estatísticas** na barra do viewport (lembrado entre aberturas): painel no canto
  com os mesmos números, suavizados para não piscarem.
- **Player:** `Player.exe "C:/caminho/MeuJogo" --perf medicao.csv` grava um quadro por linha ao fechar (com
  `--quadros N`, fecha sozinho). Colunas `quadro`, `tempo_s`, `quadro_ms`, `cpu <parte>`, `gpu <passada>` e os
  números; vírgula e ponto decimal. O log mostra o quadro mediano depois de 4 s.
- **Editor:** a ferramenta MCP `editor_status` traz o bloco `perf` com o último quadro.
- **No código:** `MOTOR_PROFILE_STAT("Nome")` (`motor/core/FrameStats.h`) mede a CPU e é também zona do Tracy;
  passada nova do renderer: `MOTOR_PROFILE_GPU("Nome")` (`renderer/GpuProfile.h`). `MOTOR_GPU_TIMERS=0` desliga as
  consultas de GPU.

## Estrutura

```
engine/            camadas, de baixo para cima (cada uma só usa as de baixo; conferido pelo test_layers):
  include/motor/   API pública
    core/          Window, Input, Log, Paths (UTF-8), ImGuiLayer
    renderer/      Renderer, Shader, Mesh (cópia na CPU), Skeleton/Skin, Texture2D, Framebuffer, Camera, Material
    project/       Project (pasta do projeto + arquivo .mproject)
    assets/        AssetLibrary (malhas/texturas/modelos), AssetDatabase (GUIDs em .meta), AssetLoaders (OBJ,
                   PNG/JPG), Model (glTF, cgltf), Animation (clipes e amostragem)
    scene/         Scene, componentes (Component.h: registro e campos), SceneSerializer (.mscene), Transform,
                   Prefab, Animator (animação), ModelImport (glTF -> prefab)
    app/           Application (game loop c/ fixed timestep)
    physics/       PhysicsSystem (Jolt Physics): corpos, personagem, eventos, raios; contorno dos colliders
    ui/            interface do jogo: componentes Ui*, layout com âncoras, fontes SDF, UiRenderer, UiSystem
    audio/         AudioSystem (miniaudio): toca as Fontes de áudio no Play, som 3D pelo Ouvinte
    ai/            NavMesh e NavSystem (Recast/Detour): navmesh, caminhos e agentes de navegação
  src/<camada>/    implementação + CMakeLists.txt da camada (+ renderer/GL.h: loader OpenGL privado)
  assets/shaders/  shaders GLSL da engine (skinned.vert: malhas animadas)
  assets/fonts/    fonte padrão da UI do jogo (Roboto Medium, Apache 2.0)
  assets/models/   Robo.glb, personagem animado de exemplo (gerado por gerar_robo.py)
docs/              guia e referência dos scripts (scripts.md, scripts-api.md) e exemplos compilados pelo ctest
editor/            MotorEditor: hierarquia, inspector, cena, viewport, projeto (docking), modo Play, scripts
  templates/Scripts/  scripts de exemplo copiados para os projetos (Girar, Jogador, MoverWASD, Seguir)
sandbox/           app de demonstração
player/            Player: o executável do jogo exportado (roda um projeto sem o editor)
common/            gravação segura de arquivos, comum à engine, ao Hub e à distribuição (sem dependências)
hub/               MotorHub: projetos e versões do Motor instaladas (não liga na libMotor.dll)
distrib/           núcleo da distribuição: manifesto de versões, SHA-256, download com retomada, plano de instalação
server/            MotorServidor: publica e serve versões do Motor e o Hub por HTTP
installer/         MotorSetup: instalador do Hub (por usuário, Hub embutido)
cmake/             MotorPackage.cmake: monta a pasta de instalação de uma versão (alvo motor_package)
tests/             testes ctest (projeto, assets, GUIDs, componentes, camadas, desfazer, scripts, Play, recarga, luzes, física,
                   materiais, prefabs, glTF e animação, UI, áudio, versão do projeto, hub, distribuição)
```

## Controles do Sandbox

| Ação | Tecla |
|---|---|
| Olhar | segurar botão direito + mouse |
| Mover | WASD, Q/E desce/sobe, Shift acelera, scroll ajusta velocidade |
| Wireframe | F1 |
| Sair | Esc |

## Editor

| Ação | Como |
|---|---|
| Voar pela cena | segurar botão direito no viewport + WASD/QE (Shift acelera) |
| Selecionar | clique no objeto no viewport ou na hierarquia (Esc limpa) |
| Selecionar vários | Ctrl+clique alterna (viewport e hierarquia); Shift+clique na hierarquia pega o intervalo; arrastar no vazio do viewport faz um retângulo (com Ctrl/Shift, soma); Ctrl+A seleciona tudo. Gizmo, Del, Ctrl+D e arrastar na hierarquia agem em todos; o Inspector mostra o último escolhido e repete nos outros o que mudar nele |
| Gizmo | Q nenhum / W mover / E rotacionar / R escalar; X alterna eixos Local/Global |
| Snap | botão "Snap" na barra do viewport, ou segurar Ctrl ao arrastar (padrão 0.5 m / 15° / 0.1). Mover com encaixe põe a posição na grade. A seta ao lado do Snap abre os passos (mover, girar, escalar) e a grade (visível, altura) |
| Grade | botão "Grade" (começa desligada): linhas no chão a cada 10 passos do mover (as finas, de cada passo, ligam em "Linhas finas" na seta do Snap), eixos X (vermelho) e Z (azul); objetos na frente tapam a grade. Encaixe e grade ficam salvos no editor |
| Reparentar | arrastar na hierarquia (soltar no vazio = raiz) |
| Buscar na hierarquia | Ctrl+F ou a caixa ao lado do "+ Criar": palavras do nome e `t:componente` (`t:luz`, `t:collider`, `t:Girar`), sem diferença de maiúsculas e acentos; os resultados aparecem numa lista com o caminho do pai, e "Selecionar todas" pega todos. Esc ou "x" limpa |
| Criar | botão "+ Criar", menu Entidade ou clique direito na hierarquia; as formas (Cubo, Esfera, Plano, Cilindro, Cápsula, Cone) já vêm com o Collider que bate com elas |
| Focar / Excluir / Duplicar | F / Del / Ctrl+D |
| Copiar / Recortar / Colar | Ctrl+C / Ctrl+X / Ctrl+V (também no menu Editar e no clique direito da hierarquia, com "Colar como filho"): os selecionados com os filhos, colados no mesmo lugar do mundo; vale colar em outra cena do projeto |
| Desfazer / Refazer | Ctrl+Z / Ctrl+Y (ou Ctrl+Shift+Z), também no menu Editar; segurar repete. Cobre também materiais (`.mmat`) e prefabs editados no Inspector (o arquivo volta junto). No Play, o que se muda em materiais e prefabs volta ao parar |
| Restaurar layout | Ver > Restaurar layout (o layout fica salvo em `imgui.ini`) |
| Novo / Abrir projeto | Arquivo > Novo projeto... / Abrir projeto... / Projetos recentes |
| Nova / Abrir cena | Arquivo > Nova cena (Ctrl+N) / Abrir cena... (Ctrl+O) |
| Salvar cena | Arquivo > Salvar cena (Ctrl+S) / Salvar cena como... (Ctrl+Shift+S) |
| Play / Parar | botão ▶ / ⏹ no topo do viewport, Ctrl+P ou menu Jogo |
| Pausa / Avançar um quadro | botões ⏸ / ⏭ (ou Ctrl+Shift+P) |
| Câmera do jogo ↔ do editor no Play | botão "Câmera: jogo/editor" ou F8 |
| Cena e Jogo lado a lado | Jogo > "Cena e Jogo lado a lado": a janela **Jogo** aparece à direita do Viewport e mostra a câmera do jogo (também fora do Play); no Play o jogo recebe teclado e mouse com ela em foco (clique nela) e o Viewport fica na câmera do editor, para mexer na cena jogando. Fechar a janela desliga; a escolha fica salva |

**Desfazer** vale para tudo que muda a cena: transform (gizmo ou Inspector), criar, excluir, duplicar,
reparentar, renomear, trocar malha/textura/cor, luz e ambiente. Cada interação vira **um** passo: um
arraste inteiro do gizmo ou de um campo, ou uma edição de nome do clique até o Enter. A seleção volta
junto. Guarda até 100 passos por cena; abrir ou criar outra cena limpa o histórico. Operações de arquivo
do painel Projeto (renomear, mover, excluir arquivos) não entram no histórico. Dentro de um campo de
texto, Ctrl+Z desfaz o texto digitado.

O título da janela mostra a cena aberta e um `*` quando há alterações não salvas. Novo, Abrir e
fechar a janela perguntam antes de descartar alterações (Salvar / Não salvar / Cancelar).

### Modo Play

- **Play** guarda uma cópia da cena e liga o loop de jogo: os scripts recebem `OnStart`, `OnUpdate`
  (todo frame) e `OnFixedUpdate` (passo fixo de 1/60 s). Fora do Play nenhum script roda.
- O viewport passa a mostrar a cena pela **câmera principal** (componente Câmera; ver abaixo), sem gizmo
  nem contorno de seleção. Sem câmera na cena, mostra pela câmera do editor com um aviso. F8 (ou o botão
  "Câmera") alterna para a câmera livre do editor sem parar o jogo.
- O jogo só recebe teclado e mouse com o **viewport em foco** (o Play já dá o foco a ele) e sem campo de
  texto ativo. Nesse estado as teclas de atalho do editor (W/E/R, Del...) ficam com o jogo.
- Indicação: borda azul no viewport e "JOGANDO" na barra de menus (laranja/"EM PAUSA" na pausa).
- **Pausa** congela o jogo; "Avançar um quadro" roda um passo de cada vez.
- **Parar** chama `OnDestroy` e devolve a cena exatamente como estava: tudo que mudou no Play (pelos
  scripts ou pelo Inspector) é descartado. Desfazer fica desligado durante o Play, salvar pede para parar
  antes, e abrir/criar cena ou projeto e fechar o editor param o Play primeiro.

## Projetos

Um projeto é uma pasta:

```
MeuJogo/
  MeuJogo.mproject     { "format": "motor-project", "version": 2, "name": "MeuJogo",
                         "engineVersion": "0.2.0", "startupScene": "Assets/Scenes/Principal.mscene" }
  Assets/
    Scenes/
      Principal.mscene
  Scripts/             scripts do jogo em C++ (ver "Scripts")
    Girar.cpp
  .motor/              gerado pelo editor (compilação dos scripts); pode apagar, fica fora do git
  .gitignore
```

- **Novo projeto** cria essa estrutura (em Documentos, por padrão) com uma cena inicial: chão, um cubo que
  gira com o script de exemplo `Scripts/Girar.cpp` e a câmera do jogo.
- O editor lembra o último projeto e a última cena aberta em `%APPDATA%/Motor/editor.json`
  e os reabre ao iniciar. Sem projeto, abre a cena de exemplo.
- A primeira cena salva num projeto sem cena inicial vira a `startupScene`.
- `engineVersion` é a versão do Motor que salvou o projeto por último (o Hub abre cada projeto com a sua).
  Projetos do formato 1 (sem esse campo) abrem como "versão desconhecida"; abrir um projeto de outra versão
  no editor passa o projeto para a versão do editor (aviso no log). A versão do editor aparece no título, em
  **Ajuda > Sobre** e em `MotorEditor --version`.

### Linha de comando do editor

```
MotorEditor [pasta-do-projeto | arquivo.mproject] [opções]
  --project <pasta>             abre o projeto (o mesmo que passar a pasta sozinha)
  --create <pasta-pai> <nome>   cria o projeto (como Arquivo > Novo projeto) e abre
  --settings <arquivo>          usa outro arquivo de configuração no lugar de %APPDATA%/Motor/editor.json
  --no-settings                 não lê nem grava configuração nenhuma (agentes e testes automáticos)
  --mcp[=porta]                 liga o servidor MCP
  --version                     mostra a versão (e a da API de scripts) e sai
```

### Painel Projeto

Navegador da pasta `Assets`: árvore de pastas à esquerda, conteúdo à direita, em **lista** (com tipo e
tamanho) ou em **grade** (ícones grandes e miniaturas das imagens) — botão "Ver".

| Ação | Como |
|---|---|
| Entrar na pasta / abrir cena | duplo clique ou Enter (outros arquivos abrem no programa padrão do Windows) |
| Voltar uma pasta | botão ▲ ou Backspace (também dá para clicar na trilha `Assets > ...`) |
| Selecionar vários | Ctrl+clique alterna, Shift+clique pega o intervalo, Ctrl+A seleciona tudo |
| Nova pasta / nova cena | botão "+ Criar" ou clique direito no espaço vazio |
| Copiar / recortar / colar | Ctrl+C / Ctrl+X / Ctrl+V (cola na pasta aberta; nome repetido vira "nome 2") |
| Duplicar | Ctrl+D (cópia na mesma pasta) |
| Renomear | F2 ou clique direito (a extensão é mantida) |
| Mover | arrastar os itens para uma pasta (na lista, na árvore ou na trilha) |
| Excluir | Del ou clique direito: **vai para a Lixeira**, com confirmação |
| Buscar | Ctrl+F ou o campo "Buscar": procura pelo nome em todo o `Assets` (Esc limpa) |
| Filtrar | caixa ao lado da busca: Todos / Cenas / Texturas / Malhas |
| Tamanho dos ícones | Ver > Tamanho, ou Ctrl + roda do mouse no modo grade |
| Mostrar no Explorer | clique direito |

**Importar arquivos:** arraste arquivos ou pastas do Explorer para a janela do editor. Eles são
**copiados** para a pasta sob o mouse no painel (ou para a pasta aberta). Também funciona copiar no
Explorer (Ctrl+C) e colar no painel (Ctrl+V), e o contrário: o Ctrl+C do painel cola no Explorer.

Renomear, mover ou excluir a cena aberta (ou a pasta dela) atualiza o editor: o título e a
cena inicial do projeto acompanham. Se a cena aberta for excluída, ela continua na memória como
"Sem título" com alterações pendentes. Mudanças feitas fora do editor aparecem em até 1 s.

## Assets (texturas, malhas, modelos, materiais e prefabs)

Arquivos dentro do projeto podem ser usados direto nas cenas:

| Tipo | Extensões | Observações |
|---|---|---|
| Textura | `.png` `.jpg` `.jpeg` `.bmp` `.tga` | via stb_image; tratada como cor sRGB, com mipmaps |
| Malha | `.obj` | `v`, `vt`, `vn`, `f` (polígonos viram triângulos). Sem normais no arquivo, são calculadas. Materiais `.mtl` são ignorados |
| Modelo | `.gltf` `.glb` `.fbx` | glTF 2.0 (via cgltf) e FBX (via ufbx): nós, malhas, materiais, texturas, esqueleto e animações. Vira prefab (abaixo) |
| Material | `.mmat` | do Motor: cor, textura, especular e brilho (abaixo) |
| Prefab | `.mprefab` | do Motor: entidade com filhos (abaixo) |
| Fonte | `.ttf` `.otf` | fonte dos textos da UI do jogo (ver "Interface do jogo") |
| Som | `.wav` `.ogg` `.mp3` | clipe da Fonte de áudio (ver "Áudio"); via miniaudio |

Como usar:

- **Inspector:** os combos "Malha" e "Textura" listam os embutidos (Cubo, Xadrez...) e, abaixo de
  "Projeto", os arquivos do projeto. Também dá para arrastar o arquivo do painel Projeto para o combo.
- **Viewport:** arraste um `.obj` para criar uma entidade onde o mouse aponta (apoiada no chão ou no objeto
  embaixo); arraste uma textura sobre um objeto para aplicá-la.
- **Hierarquia:** solte um `.obj` no espaço vazio (entidade na raiz) ou sobre uma entidade (vira filho);
  uma textura sobre uma entidade é aplicada a ela.
- **Clique direito no painel Projeto:** "Adicionar à cena" (malha) e "Aplicar à entidade selecionada" (textura).

Cada arquivo é carregado uma vez e reaproveitado.

### IDs fixos (`.meta`)

Cada cena, textura e malha dentro de `Assets/` ganha ao lado um arquivo `.meta` com um **GUID**:

```
Assets/Modelos/carro.obj
Assets/Modelos/carro.obj.meta    { "format": "motor-meta", "version": 1, "guid": "9f2c...e41a" }
```

As cenas apontam para o GUID, não para o caminho. Por isso dá para **renomear e mover** arquivos e pastas
à vontade (pelo painel ou pelo Explorer) sem quebrar nenhuma cena, aberta ou salva.

- O painel Projeto esconde os `.meta` e os leva junto ao renomear, mover ou excluir.
- O editor cria os `.meta` que faltam ao abrir o projeto e a cada 2 s (arquivos novos, importados ou
  copiados pelo Explorer).
- Arquivo copiado **junto com** o `.meta` (no Explorer, por exemplo) fica com GUID repetido: o original
  mantém o seu e a cópia recebe um novo.
- **Pelo Explorer, mova o `.meta` junto com o arquivo.** Sem ele, o arquivo ganha um GUID novo e as cenas
  só o encontram se ele continuar no caminho antigo (plano B, com aviso no log).
- Versione os `.meta` junto com os assets (no git, por exemplo).

### Materiais (`.mmat`)

Um material PBR (metal/rugosidade, o mesmo modelo do glTF) guarda **cor, textura, metal, rugosidade,
reflexo, mapas de normal, metal/rugosidade e oclusão, e emissão** num arquivo próprio, reaproveitado por
vários objetos: editar o material muda todos os que o usam. Materiais da versão 1 (Blinn-Phong, até a Fase 6)
continuam abrindo: o "brilho" vira rugosidade. Os mapas de dados (normal, metal/rugosidade, oclusão) são
lidos sem a conversão sRGB; o de normal é em espaço tangente com +Y para cima (OpenGL/glTF).

**Alfa** (Fase 18, `.mmat` versão 3): **Opaco** ignora o alfa da cor e da textura; **Recorte** some onde o
alfa fica abaixo do **Limite do recorte** (folhas, grades; a sombra também é recortada); **Transparente**
mistura com o que está atrás (vidro, água simples): desenhado depois dos opacos e do céu, de trás para frente
pelo centro, sem gravar profundidade e sem sombra. **Duas faces** mostra as faces de trás (planos de folha,
grade, vidro fino). O importador glTF lê `alphaMode`, `alphaCutoff` e `doubleSided`.

- **Criar:** painel Projeto > "+ Criar" > "Novo material", ou no Inspector de um Mesh Renderer o botão
  "Salvar como asset" (grava o material daquele objeto em `Assets/Materiais` e passa a usá-lo).
- **Usar:** campo "Material" do Mesh Renderer (combo ou arrastando o `.mmat`), ou arraste o `.mmat` sobre
  um objeto no viewport ou na Hierarquia. "(próprio)" volta ao material só daquele objeto.
- **Editar:** clique no `.mmat` no painel Projeto (aparece no Inspector) ou edite os campos dele no Mesh
  Renderer de qualquer objeto que o usa. O arquivo é gravado quando você solta o controle; arquivos
  editados fora do editor são relidos (a cada 2 s).
- O Mesh Renderer continua tendo o material próprio (cor e textura direto no componente, como nas cenas
  antigas): ele vale quando o campo "Material" está vazio.
- Nos scripts: `renderer->sharedMaterial->color` muda o asset (todos os objetos); `renderer->material` é o
  material próprio, usado só sem asset. `Resolved()` devolve o que está valendo.

```json
{ "format": "motor-material", "version": 2, "color": [0.9, 0.15, 0.1, 1.0],
  "texture": { "guid": "07b1...c9d0", "path": "Assets/Texturas/pintura.png" },
  "metallic": 0.0, "roughness": 0.4, "specular": 0.5,
  "normalMap": { "guid": "5c2e...", "path": "Assets/Texturas/pintura_normal.png" }, "normalStrength": 1.0,
  "metallicRoughnessMap": null, "occlusionMap": null, "occlusionStrength": 1.0,
  "emissive": [0.0, 0.0, 0.0], "emissiveIntensity": 1.0, "emissiveMap": null }
```

### Prefabs (`.mprefab`)

Um prefab é uma entidade com filhos salva como asset. As cópias na cena (**instâncias**) seguem o prefab:
mudar a cor de uma peça no prefab muda todas as instâncias.

- **Criar:** clique direito numa entidade da Hierarquia > "Criar prefab" (vai para `Assets/Prefabs`), ou
  arraste a entidade para uma pasta do painel Projeto. A entidade vira a primeira instância.
- **Instanciar:** arraste o `.mprefab` para o viewport (onde o mouse aponta) ou para a Hierarquia, ou
  clique direito > "Adicionar à cena". Instâncias aparecem em azul na Hierarquia.
- **Editar o prefab:** clique no `.mprefab` no painel Projeto (ou "Abrir" no componente Prefab de uma
  instância). O Inspector mostra as entidades do prefab; as mudanças valem na hora para todas as instâncias
  e o arquivo é gravado ao soltar o controle.
- **Overrides:** o que você muda numa instância (um campo, o nome, um componente adicionado ou removido,
  uma entidade apagada) fica só nela e deixa de seguir o prefab; esses campos aparecem em azul no Inspector
  e a lista fica no componente Prefab. Posição e rotação da raiz são sempre de cada instância.
- **Aplicar / Reverter / Desvincular** (componente Prefab ou clique direito na Hierarquia): grava a instância
  no prefab (entidades novas chegam às outras instâncias), descarta as alterações dela, ou a solta do prefab.
- **Prefab dentro de prefab:** criar ou aplicar um prefab que tem instâncias de outro (ex.: uma Janela dentro da
  Casa) deixa a Janela aninhada na Casa. Mudar o prefab Janela muda as janelas de todas as casas, menos o que a
  Casa mudou nelas; o Inspector mostra "Do prefab aninhado: Janela". Desvincular a casa na cena faz as janelas
  voltarem a ser instâncias da Janela. Um prefab dentro dele mesmo é recusado ao aplicar.
- Editar um material ou um prefab no Inspector entra no desfazer (o arquivo volta junto); "Aplicar ao prefab" e
  criar prefab ou material, não. A cena registra as alterações das instâncias normalmente.

### Modelos 3D (glTF e FBX) e animação

Copie um `.gltf` (com o `.bin` e as imagens ao lado), `.glb` ou `.fbx` para `Assets/`. O editor importa sozinho
(e de novo quando o arquivo muda; clique direito > "Importar de novo" força):

```
Assets/Personagens/Robo.glb
Assets/Personagens/Robo.mprefab          o modelo pronto para a cena (prefab)
Assets/Personagens/Robo_materiais/       um .mmat por material do glTF e as texturas embutidas no arquivo
```

- Arraste o `.glb` (ou o `.mprefab`) para o viewport ou a Hierarquia, ou clique direito > "Adicionar à cena".
  As entidades seguem os nós do glTF; os ossos não viram entidades (o esqueleto fica no Animator).
- As malhas do arquivo aparecem nas cenas como `{ "guid": "<guid do .glb>#Corpo", "path": "Assets/.../Robo.glb#Corpo" }`;
  os clipes, do mesmo jeito (`#Andando`).
- Materiais PBR do glTF viram os materiais do Motor: cor base (convertida para sRGB), textura de cor; a
  rugosidade vira especular e brilho. Mapas de normal, metal e emissão são ignorados (Fase 7).
- **Animação esquelética:** o prefab de um modelo com esqueleto ou clipes vem com o componente **Animator**
  (Modelo, Clipe inicial, Tocar ao iniciar, Repetir, Velocidade). No Play, o clipe inicial toca sozinho;
  scripts trocam com `GetAnimator()->CrossFade("Andando", 0.2f)` (guia: [docs/scripts.md](docs/scripts.md),
  seção "Animação"). Fora do Play o modelo fica na pose de repouso; para ver um clipe sem dar Play, use
  **Pré-visualizar** (escolha o clipe e Tocar) logo abaixo do Animator no Inspector. O skinning é feito no shader
  (`skinned.vert`, até 128 juntas, 4 por vértice).
- Exemplo: `engine/assets/models/Robo.glb` (1,93 m, olha para -Z, clipes Parado, Andando e Pulando). Com o
  modelo Jogador: entidade com Controlador de personagem + Jogador, o robô como filho na posição `0 -0.9 0`.

**FBX** (Mixamo, Character Creator, Blender, Maya, 3ds Max), lido com a ufbx e importado igual ao glTF:

- Convertido na importação para Y para cima e metros (o Mixamo grava em centímetros), sem pivôs nem
  "transformações de geometria". Polígonos viram triângulos; cada material de uma malha vira uma malha.
- Texturas embutidas no `.fbx` ou ao lado dele (o caminho relativo, o absoluto gravado, ou o nome do arquivo na
  pasta do `.fbx` ou em `textures/`). Texturas fora da pasta `Assets/` não vão para o jogo exportado.
- Animações: cada pilha do arquivo vira um clipe (amostrada em quadros); pilhas vazias saem. Sem o "Armature|"
  do Blender no nome. Arquivos do Mixamo trazem a animação como "mixamo.com": o único clipe do arquivo ganha o
  nome do arquivo depois do `@`, como na Unity (`X Bot@Walking.fbx` -> clipe `Walking`).
- Clipes de outro arquivo com o mesmo esqueleto (as animações do Mixamo "Without Skin") servem no Animator pelas
  versões de `Play`/`CrossFade` que recebem um `AnimationClip`: as juntas são achadas pelo nome.

Limitações: sem morph targets, câmeras e luzes do glTF (nem do FBX); animação só de juntas (nós sem pele não se movem);
as malhas do modelo também aparecem no combo "Malha" do Inspector, em "Modelos" (abra o modelo na lista); o modelo alterado no disco é
relido, mas cenas abertas só pegam as malhas novas ao reabrir.

## Componentes

Toda entidade tem nome, ativa/inativa, pai e **Transform**. O resto vem de **componentes**: structs comuns,
registradas uma vez com a lista dos seus campos. A partir disso a engine e o editor fazem sozinhos o resto:
o componente aparece em "Adicionar componente" no Inspector, com os campos editáveis, é salvo e carregado
na cena, copiado ao duplicar a entidade e entra no desfazer.

```cpp
#include <motor/Motor.h>

struct Girar {
    float velocidade = 90.0f;          // graus por segundo
    glm::vec3 eixo{0.0f, 1.0f, 0.0f};
    bool ligado = true;
};

// Uma vez, na inicialização (os da engine: MOTOR_COMPONENT_MODULE, um .cpp por componente):
motor::ComponentRegistry::Instance().Register<Girar>("Girar", "Girar")
    .Field("velocidade", "Velocidade", &Girar::velocidade, {.slider = true, .min = 0.0f, .max = 720.0f})
    .Field("eixo", "Eixo", &Girar::eixo)
    .Field("ligado", "Ligado", &Girar::ligado);

// Uso:
entity.Add<Girar>().velocidade = 180.0f;
if (Girar* g = entity.Get<Girar>()) g->ligado = false;
entity.Remove<Girar>();
```

- Tipos de campo: `bool`, `int`, `float`, `glm::vec2/3/4`, `std::string`, malha e textura
  (`std::shared_ptr<Mesh>` / `std::shared_ptr<Texture2D>`, com o seletor de assets do projeto).
- Tipo de campo novo (enum, referência a outro tipo de asset...): especialize `motor::FieldTraits<T>`
  (comparação e JSON do valor, ver `Component.h`) e, para editá-lo no Inspector, registre um desenhador com
  `MOTOR_FIELD_DRAWER(NomeDoTipo)` (`editor/src/drawers/FieldDrawers.h`). Nada mais muda na engine ou no editor.
- Componentes da engine ficam um por arquivo (`engine/include/motor/scene/<Nome>.h` + `src/scene/<Nome>.cpp`)
  e se registram com `MOTOR_COMPONENT_MODULE(Nome) { registry.Register<...>(...); }`. A ordem no menu
  "Adicionar componente" é a ordem alfabética do nome do módulo.
- Opções de exibição (`FieldOptions`): `color` (vec3/vec4 como cor), `slider` + `min`/`max`, `logarithmic`,
  `speed`, `format`, `tooltip`.
- Campo dentro de outra struct: `.FieldRef("cor", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; })`.
- Valores iniciais ao adicionar pelo editor: `.OnAdd([](T& c, AssetLibrary& assets) { ... })`.
- Um componente de cada tipo por entidade. O nome registrado ("Girar") é o que vai no arquivo: não mude
  depois de ter cenas salvas. Campos que não estão registrados não são salvos nem comparados (bom para
  estado interno).
- No Inspector, o X do cabeçalho remove o componente; clique direito no cabeçalho tem "Redefinir valores".
- Componentes da engine hoje: **Mesh Renderer** (malha e material: textura, cor, metal, rugosidade,
  reflexo, mapas e emissão) e **Câmera**
  (campo de visão, planos perto/longe, principal). A câmera olha para o -Z local da entidade; no Play vale
  a primeira câmera ativa marcada como principal (senão, a primeira ativa). Entidade > Câmera cria uma.
- **Luz**: tipo (direcional, pontual ou spot), cor, intensidade, alcance (pontual e spot) e ângulos interno/
  externo do cone (spot). Como a câmera, aponta para o -Z local: direcional e spot se giram com o gizmo de
  rotação; a posição só importa para pontual e spot. Várias por cena (até 32 acesas ao mesmo tempo).
  **Projeta sombra** e **qualidade da sombra** (baixa/média/alta): a primeira direcional com sombra usa
  4 cascatas (1024/2048/4096 por cascata); pontuais e spots usam um cubo (256/512/1024 por face), até 4 ao
  mesmo tempo (as mais perto da câmera que iluminam algo visível). Funciona com malhas animadas.
  Entidade > Luz direcional / pontual / spot cria uma. Céu, neblina e **luz ambiente** ficam no painel Cena.

### Renderização (Fase 7)

O renderer desenha em passadas no `EndScene` (os `Submit*` só guardam o pedido):
mapas de sombra → cena em **HDR** (RGBA16F, MSAA 4x) com o céu ao fundo → **bloom** → **tone mapping**
(ACES, Reinhard ou nenhum) e gama → **FXAA** → contornos do editor por cima.

- **PBR** de metal/rugosidade (Cook-Torrance com GGX), mapas de normal sem tangentes nos vértices (base pelas
  derivadas da tela, vale também com pele), oclusão e emissão (que o bloom faz brilhar).
- **Céu** (painel Cena > Céu): uma cor (degradê do zênite ao chão, como antes) ou uma imagem **HDR
  equirretangular** (`.hdr` do projeto, asset "Céu HDR"), com brilho e rotação. O céu também **ilumina a
  cena**: luz difusa em harmônicos esféricos e reflexos de um cubemap pré-filtrado por rugosidade, com a
  tabela da BRDF; "Luz do céu" ajusta quanto. A neblina usa a cor do horizonte.
- **Pós-processamento** (painel Cena): exposição, tone mapping, bloom (intensidade e limiar) e FXAA. A
  distância das sombras da luz direcional também fica lá. Tudo entra no desfazer e na cena (versão 6).
- O viewport mostra os contadores: FPS, draws (e quantos foram para as sombras, em quantos passes), triângulos.
- Base para efeitos de ambiente (água, partículas): `Renderer::SceneDepthTexture()` (profundidade da cena),
  `SceneColorTexture()` (luz HDR antes do pós), `SkyCubemap()` e `ReflectionCubemap()` (céu e reflexo).
- Código: `engine/src/renderer/` (`Renderer`, `Shadows`, `Sky`, `PostProcess`) e `engine/assets/shaders/`
  (`#include` e variantes por `#define` no `Shader::FromFiles`). A matemática sem GPU (`ShadowMath.h`,
  `Pbr.h`) tem teste: `tests/test_render.cpp`.
- No viewport, luzes e câmeras aparecem como **ícones** clicáveis (selecionam a entidade e mostram o
  gizmo). Selecionada, a luz mostra o alcance (pontual), o cone (spot) ou a direção (direcional), e a
  câmera, a pirâmide de visão.

## Scripts (C++)

**Guia completo: [docs/scripts.md](docs/scripts.md)** (do primeiro script a um mini jogo, com receitas e
erros comuns) e **referência da API: [docs/scripts-api.md](docs/scripts-api.md)**. Os exemplos do guia
ficam em `docs/examples/` e são compilados e executados pelo teste `test_script_docs`.

Os scripts são de cada projeto: ficam em `MeuJogo/Scripts/` e o editor os compila numa DLL que carrega e
**recarrega sem fechar** (ao abrir o projeto, ao salvar/criar/apagar um arquivo da pasta e com F7). Um script
é um componente que herda de `motor::Script`: os campos registrados aparecem no Inspector ("Adicionar
componente" > Scripts), são salvos na cena e entram no desfazer; os métodos `On*` só rodam no Play.

```cpp
// MeuJogo/Scripts/Girar.cpp
#include <motor/scene/ScriptModule.h>

struct Girar : motor::Script {
    float velocidade = 90.0f;  // graus por segundo

    void OnUpdate(float dt) override {
        GetTransform().Rotate({0.0f, 1.0f, 0.0f}, glm::radians(velocidade * dt));
    }
};

MOTOR_SCRIPT(Girar) {
    registry.Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);
}
```

- Menu **Scripts**: Novo script (modelos Vazio, Comportamento, Que segue, Coletável e Gerador; também em
  **+ Criar** no painel Projeto e pela ferramenta MCP `script_create`), Adicionar exemplo (Girar, Jogador,
  Mover (WASD ou controle), Seguir, de `editor/templates/Scripts/`), Recompilar (F7), Abrir no VS Code, Abrir pasta. O
  painel **Scripts** mostra erros e avisos; clicar num erro abre o arquivo na linha.
- **VS Code configurado**: ao abrir o projeto o editor gera `.vscode/` (IntelliSense pelo `compile_commands.json`
  da compilação dos scripts, tarefa de compilar e "anexar ao editor" com o gdb). Só reescreve os arquivos que
  ainda começam com `// Gerado pelo Motor Editor`.
- **Recarga também no Play**: os campos registrados ficam com os valores do jogo, o resto volta ao padrão e o
  `OnStart` roda de novo. Campo removido ou com o tipo trocado vira aviso no painel com o valor perdido;
  `FieldOptions::formerKey` lê o valor de um campo renomeado. Com erro de compilação, continuam carregados os
  scripts da última compilação boa. Script removido ou renomeado vira componente "script não carregado", com
  os dados guardados, até voltar.
- API (Fase 17): campos `motor::EntityRef` (outra entidade, escolhida no Inspector), `GetComponent<T>()`,
  temporizadores (`After`, `Every`), mensagens (`Send`, `Broadcast`, `OnMessage`), sorteio (`Random`,
  `RandomInt`, `Chance`) e o registro curto `MOTOR_FIELDS(Jogador, velocidade, pulo)`.
- Sistemas do jogo (física, áudio, HUD) são `motor::RuntimeSystem` registrados pela engine com
  `MOTOR_RUNTIME_SYSTEM` (ordem e regras em `engine/include/motor/scene/RuntimeSystem.h`, exemplo em
  `tests/test_runtime_systems.cpp`); os scripts os acham com `GetSystem<T>()`.

Como funciona: o editor gera `MeuJogo/.motor/scripts/cmake/CMakeLists.txt` (uma `GameScripts.dll` com
todos os `.cpp` de `Scripts/` mais um ponto de entrada que exporta `MotorRegisterScripts`), que inclui
`build/bin/sdk/MotorSDK.cmake` (alvo importado `Motor::Motor`: headers e `libMotor.dll`) e usa o mesmo
CMake, compilador e make com que o Motor foi compilado (`build/bin/sdk/MotorSDK.json`). O editor carrega
uma **cópia** da DLL (em `.motor/scripts/loaded/`), para o linker poder reescrever a original na próxima
compilação. A engine é uma DLL compartilhada e o runtime do C++ é dinâmico: o editor e a DLL de scripts
usam o mesmo registro de componentes, o mesmo heap, as mesmas exceções/RTTI, o mesmo `Input` e o mesmo log.

### Velocidade da compilação dos scripts

- **Cabeçalho pré-compilado** (`build/bin/sdk/MotorScriptPch.h.gch`, alvo `motor_sdk_pch`), refeito quando um
  header da engine muda e incluído em todo script.
- **Headers leves**: `Script.h` não puxa `<format>` (o `Log("x = {:.2f}", x)` formata dentro da `libMotor.dll`),
  `Scene.h` não puxa o renderer, e registrar campos não instancia `std::function` por campo.
- **Configura uma vez só**: o CMakeLists gerado lista os `.cpp` (sem `GLOB ... CONFIGURE_DEPENDS`) e só muda
  quando entra ou sai arquivo; mudar a versão do Motor ou o toolchain apaga o cache e reconfigura limpo.
- **Ninja e lld**: se `ninja.exe` e `ld.lld.exe` estiverem ao lado do compilador (MSYS2 UCRT64:
  `pacman -S mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-lld`), são usados; senão, make e `ld`.
- O painel **Scripts** e a ferramenta MCP `scripts_status` mostram o tempo de cada etapa e de cada arquivo.
- **Vigia imediata**: o editor recebe do Windows o aviso de arquivo salvo na pasta `Scripts/` (antes conferia a
  cada 0,75 s).
- **Editor mais leve enquanto compila**: fora do Play, o editor desenha a 10 FPS em segundo plano e a 20 FPS
  durante a compilação. A 60 FPS ele ocupava ~75% de um núcleo e, com o vídeo integrado dividindo a energia,
  a compilação de um script levava o triplo (1,6 s em vez de 0,54 s).

Medido com `test_script_project --bench` (Debug, GCC 15, i5-6400, scripts do modelo "Novo script"; o
"antes" é o início da Fase 17):

| Caso | Antes | Depois |
|---|---|---|
| 1 script, do zero | 8,42 s | 2,63 s |
| 10 scripts, do zero | 22,10 s | 4,30 s |
| 10 scripts, 1 arquivo editado (compilar → recarregado) | 9,19 s | 1,00 s |
| 50 scripts, do zero | 90,72 s | 11,47 s |

Etapas do caso "1 arquivo editado": compilar 0,59 s, ligar 0,23 s, recarregar 0,02 s. Dentro do editor aberto
(do salvar o arquivo até a recarga, medido pelo MCP com 10 scripts): 1,1 a 1,5 s; antes destes ajustes, ~2,7 s
mais a espera da vigia.

Limites atuais: os scripts precisam do Motor compilado na mesma máquina (o SDK aponta para a pasta de
build); um erro grave dentro de um script (ponteiro inválido) derruba o editor; sem projeto aberto não há
scripts.

## Física

A física usa o [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (v5.6.0, MIT): multithread e com SIMD.
Ela é um sistema do jogo (`motor::PhysicsSystem`, `engine/include/motor/physics/PhysicsSystem.h`) que roda
no `FixedUpdate`, **antes** dos scripts, só no Play. Tudo é montado por componentes:

| Componentes | Resultado |
|---|---|
| **Collider** | corpo estático (chão, paredes). Formas: caixa, esfera, cápsula (de pé no Y) e malha |
| **Collider** + **Rigidbody** | corpo que cai, empilha e é empurrado. Cinemático: segue o Transform e empurra os outros |
| **Collider** com **Trigger** | só detecta quem entra e sai (`OnTriggerEnter`/`OnTriggerExit`), não empurra |
| **Controlador de personagem** | cápsula para o jogador: anda, sobe degraus e rampas, não atravessa o chão |

- Medidas do Collider são locais: a escala da entidade multiplica todas (o cubo embutido tem 1 x 1 x 1, então
  um Collider caixa padrão já casa com ele). **Centro** desloca a forma; **Atrito** e **Elasticidade** por
  collider. A origem do personagem é o centro da cápsula.
- **Malha**: usa os triângulos da malha do Mesh Renderer da entidade (qualquer malha: OBJ, glTF ou as
  embutidas, como o Plano do chão), que a `Mesh` guarda na CPU. Vira malha estática de verdade (ou o casco
  convexo, se tiver Rigidbody dinâmico). Malhas com pele usam a pose de repouso.
- Mexer no Transform de um corpo (script ou editor) durante o Play o teleporta; mudar um componente refaz o
  corpo; desativar ou excluir a entidade tira o corpo. Corpos parados "dormem" e quase não custam.
- No viewport, o botão **Colliders** mostra as formas (verde), os triggers (azul) e os personagens. Um clique
  alterna **Selecionado** (padrão: só o objeto selecionado), **Todos** e **Nenhum**; a escolha fica salva no editor.

Scripts recebem os contatos (depois do passo da física, antes do próprio `OnFixedUpdate`) e usam o sistema:

```cpp
#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/ScriptModule.h>

struct Jogador : motor::Script {
    void OnFixedUpdate(float) override {
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;
        fisica->MoveCharacter(GetId(), {3.0f, 0.0f, 0.0f});           // m/s; vale até a próxima chamada
        if (IsKeyDown(motor::Key::Space)) fisica->Jump(GetId(), 5.0f);  // só no chão
        if (auto hit = fisica->Raycast(GetTransform().position, {0, -1, 0}, 10.0f, GetId())) {
            Log("chão a {:.2f} m", hit->distance);
        }
    }
    void OnCollisionEnter(const motor::Collision& c) override { Log("bateu em {} a {:.1f} m/s", c.other, c.relativeSpeed); }
    void OnTriggerEnter(motor::EntityId outro) override { Log("entrou {}", outro); }
};
```

- Colisão: `OnCollisionEnter(const Collision&)` (outra entidade, ponto, normal apontando do outro para este,
  velocidade de aproximação) e `OnCollisionExit(EntityId)`. Trigger: `OnTriggerEnter/Exit(EntityId)`. Os dois
  lados recebem. Um par de entidades entra uma vez e sai quando não sobra nenhum contato entre elas.
- `Raycast(origem, direção, distância, ignorar, máscara)`: primeiro corpo atingido (entidade, ponto, normal,
  distância); triggers não contam; `máscara` = camadas que o raio vê (`LayerMask({"Parede"})`). Rigidbody: `AddForce`, `AddImpulse`, `SetVelocity`, `GetVelocity`.
  Personagem: `MoveCharacter`, `Jump`, `IsGrounded`, `GetCharacterVelocity`, `SetCharacterVelocity` e o campo
  `useGravity`. Área: `OverlapSphere`, `OverlapBox`, `SphereCast` (com máscara de camadas). Entidade criada neste passo só
  ganha corpo no próximo `FixedUpdate`.
- **Camadas de colisão** (Fase 18): até 16 camadas com nome e a tabela de quem se choca com quem, no projeto
  (`.mproject`, formato 3; **Arquivo > Camadas de colisão** ou a ferramenta MCP `physics_layers`). Collider e
  Controlador de personagem escolhem a camada no campo **Camada**. O editor e o Player copiam a tabela para
  `PhysicsSettings::Global().layers` ao abrir o projeto; corpos parados nunca se chocam entre si.
- O personagem gera eventos com corpos dinâmicos e triggers (não com o chão estático: para saber se está no
  chão, `IsGrounded`). O modelo **Scripts > Adicionar exemplo > Jogador** anda com WASD e pula com Espaço.
- Configuração global (`motor::PhysicsSettings::Global()`, lida a cada Play): `workerThreads` (threads do job
  system do Jolt; -1 = núcleos - 1, 0 = só a thread do jogo), `gravity`, `maxSubstep` e `meshData` (de onde
  vêm os triângulos do collider de malha; o editor liga na AssetLibrary).
- **SIMD**: o Jolt é compilado com SSE4.2 como base, sem AVX/AVX2. O jogo roda em qualquer x86-64 desde ~2008
  (Nehalem) / 2011 (Bulldozer); com AVX2 ele fecharia com "instrução ilegal" em CPUs sem AVX2. No Debug o
  Jolt é compilado otimizado (`-O2`), senão o Play fica lento demais.

Desempenho (`tests/test_physics_bench.cpp`, Release, i5-6400 de 4 núcleos, caixas em colunas de 10 caindo e
empilhando, média de 600 passos de 1/60 s do `FixedUpdate` inteiro):

| Caixas | 0 threads extras | 3 threads extras |
|---|---|---|
| 500 | 2.1 ms | 0.9 ms |
| 1000 | 3.6 ms | 2.0 ms |
| 2000 | 6.5 ms | 2.7 ms |
| 4000 | 14.3 ms | 6.7 ms |

## Interface do jogo (HUD)

Texto, imagens e botões na tela durante o jogo, desenhados por cima da cena e independentes do ImGui do
editor (camada `engine/.../ui/`). São componentes comuns, então aparecem no Inspector, vão para a cena e têm
desfazer:

| Componente | Campos |
|---|---|
| **UI: Retângulo** (`UiRect`) | Âncora (9 pontos ou **Esticar**), Posição, Tamanho, Ordem, Visível |
| **UI: Texto** (`UiText`) | Texto (UTF-8, `\n` quebra a linha), Fonte, Tamanho da fonte, Cor, Alinhamento |
| **UI: Imagem** (`UiImage`) | Textura (vazia = cor sólida), Cor |
| **UI: Botão** (`UiButton`) | Cor normal, com o mouse em cima e pressionado; Clicável |

- **Criar:** Entidade > Interface (UI) > Texto, Imagem, Botão ou Painel (também no "+ Criar" da Hierarquia).
- **Unidades:** pixels de uma tela de 1080 de altura, escalados pela altura real (o HUD fica igual no
  viewport do editor e em tela cheia). Filho de um elemento de UI se posiciona dentro do pai; esconder o pai
  esconde os filhos. Entre irmãos, maior **Ordem** desenha por cima e recebe o clique primeiro.
- **No editor:** a UI aparece por cima do viewport (botão **UI** da barra liga/desliga). Fora da visão do
  jogo, os elementos têm contorno; um clique seleciona e arrastar move (um arraste = um passo de desfazer).
  Na visão do jogo (Play), o mouse vai para os botões do jogo.
- **Scripts:** `GetSystem<motor::UiSystem>()` com `SetText`, `SetVisible`, `IsVisible`, `WasClicked` (pelo
  nome da entidade ou pelo id) e `Script::LoadScene("Fase1")` para trocar de cena. Ver a seção 13 do
  [guia de scripts](docs/scripts.md) e os exemplos `docs/examples/Hud.cpp` e `MenuPrincipal.cpp`.
- **Fontes:** `.ttf` e `.otf` do projeto são assets (com GUID); o campo Fonte vazio usa a padrão, **Roboto
  Medium** (`engine/assets/fonts/`, Apache License 2.0, ver `LICENSE-Roboto.txt` na mesma pasta). Cada fonte
  vira um atlas **SDF** (campo de distância, gerado pelo `imstb_truetype.h` que vem com o Dear ImGui), nítido
  em qualquer tamanho; o atlas tem ASCII, Latin-1 (acentos) e algumas pontuações.
- **Desempenho:** todo o texto e as imagens vão num VBO dinâmico só, em uma chamada de desenho por troca de
  textura (retângulos sólidos usam o atlas da fonte: um HUD de texto e painéis é 1 chamada; a barra do
  viewport mostra "UI: N draws"). A geometria de cada elemento só é refeita quando o texto, o tamanho, a
  fonte, a cor ou o retângulo mudam; frame sem mudança não envia nada para a GPU.

## Exportar o jogo

**Arquivo > Exportar jogo...** monta uma pasta que roda o jogo sem o editor (e sem o MSYS2): escolha a cena
inicial (a do projeto vem marcada; a caixa "Gravar como cena inicial" grava a escolha no `.mproject`, no
campo `startupScene` que já existia) e a pasta de saída (padrão: `<projeto>/Exportado`). Depois de exportar,
os botões **Abrir pasta** e **Rodar o jogo** levam ao resultado.

```
Exportado/
  Jogo Ação.exe          o Player com o nome do projeto
  libMotor.dll           a engine (e libstdc++-6, libgcc_s_seh-1, libwinpthread-1: runtime do GCC)
  engine/                shaders e fontes da engine
  Dados/                 o projeto, só com o que o jogo usa
    Jogo Ação.mproject   com a cena inicial
    GameScripts.dll      os scripts compilados
    Assets/...           assets usados, com os .meta (os mesmos GUIDs do projeto)
  motor-export.json      marca de exportação
  Jogo Ação.log          criado ao rodar: o log do jogo
```

- **O que entra de `Assets/`:** todas as cenas (a troca de cena dos scripts é por nome, e o nome pode ser
  montado no código); tudo o que elas citam, seguindo as referências (malhas, texturas, materiais, prefabs,
  modelos glTF com os `.bin` e as imagens deles, fontes da UI, céus HDR, e qualquer componente novo que guarde
  um `{guid, path}`); a pasta **`Assets/Recursos/`** inteira (para arquivos que o jogo abre por conta própria);
  e caminhos `"Assets/..."` escritos entre aspas nos fontes de `Scripts/`. O resto fica de fora. Referências
  quebradas (GUID sem arquivo) aparecem como aviso e o jogo é exportado mesmo assim.
- **Sempre em Release** (Fase 21): o jogo leva o `Player.exe` e a `libMotor.dll` de Release e a DLL de
  scripts é **recompilada em Release** ao exportar (em `.motor/scripts/build-release`, separada da do editor; a
  janela mostra "Compilando os scripts em Release..."). O Motor em Release vem da pasta do editor (versão
  instalada de Release), de `release/` ao lado dela (o `motor_package` de uma versão de Debug copia um para lá)
  ou de `build-release/bin` no código-fonte (`MOTOR_RELEASE_BIN_DIR` no CMake). Sem nenhum, o jogo sai com o
  Motor do editor e um aviso explica como ter o de Release:

  ```bash
  cmake -S . -B build-release -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DMOTOR_BUILD_TESTS=OFF
  cmake --build build-release -j8 --target Player
  ```
- **Scripts:** um erro de compilação em Release aparece na janela (e no log) e cancela a exportação. Sem Motor
  em Release, a DLL do editor precisa estar compilada (a exportação recusa durante a compilação ou com erros).
  Os fontes não vão, só a `GameScripts.dll`.
- **Exportar de novo** na mesma pasta substitui a exportação anterior (só o que ela tinha criado, listado na
  marca); uma pasta que não esteja vazia nem seja uma exportação é recusada.
- **Player** (`build/bin/Player.exe`, pasta `player/`): o executável do jogo, compilado junto com o editor.
  Abre `Dados/` ao lado dele, carrega a DLL de scripts e roda a cena inicial como o Play do editor (mesmo
  `SceneRuntime`, física, HUD e câmera principal). Para testar um projeto sem exportar:
  `Player.exe "C:/caminho/MeuJogo"`. `--quadros N` fecha sozinho depois de N quadros (testes automáticos);
  `--janela` abre em janela mesmo com tela cheia nas opções; `--opcoes "<arquivo>"` usa outro arquivo de opções.
  O título da janela é só o nome do jogo (sem FPS); `--stats` acrescenta FPS, ms e draws (medição, `bench/`), e
  `--perf "<arquivo.csv>"` grava os contadores de cada quadro (ver "Contadores por quadro"); `--segundos S` fecha
  sozinho depois de S segundos (e grava o `--perf`).
- **Identidade do jogo** (Fase 21, **Arquivo > Configurações do jogo** ou MCP `game_settings`): nome do `.exe`
  (vazio = o do projeto), versão (`1.0.0`, vai para as propriedades do `.exe` e para `Script::GameVersion`) e
  ícone (`.ico` com vários tamanhos ou `.png` de até 256x256, de `Assets/`). A exportação grava o ícone e a versão
  como recursos do Windows no `.exe` copiado; o ícone aparece no Explorer, na janela e na barra de tarefas (a
  GLFW usa o recurso `GLFW_ICON`). Ícone que não serve faz a exportação ser recusada antes de mexer na pasta.
- **Tela de carregamento** (Fase 21): o jogo exportado desenha uma cena parada enquanto abre e a cada
  `LoadScene` (o carregamento trava o jogo até terminar). Sem configurar, fundo escuro e "Carregando..."; com
  **Configurações do jogo > Tela de carregamento** (MCP `game_settings`, `loading_scene`), uma cena do projeto
  feita no editor com UI (imagem, texto) e, se quiser, câmera e objetos. Ela é só desenhada: scripts e física
  não rodam nela. No Play do editor a troca de cena não mostra a tela.
- **Controle e ações** (Fase 21): os scripts perguntam por ações (`GetAxis("Horizontal")`,
  `WasActionPressed("Pular")`) do mapa do projeto (**Arquivo > Controles**, `.mproject` em `"input"`, MCP
  `input_actions`), que liga teclas, mouse e o controle (gamepad, padrão Xbox, pela GLFW). O padrão traz
  Horizontal, Vertical, Olhar X/Y, Pular, Correr, Interagir e Pausa. Controle direto: `IsGamepadButtonDown`,
  `GamepadAxisValue`. Os modelos Jogador e MoverWASD andam com o teclado ou o analógico. O MCP `game_gamepad`
  simula um controle no Play.
- **Progresso salvo** (Fase 21): os scripts guardam chave/valor em "slots" com `SaveSlot()` (`motor::SaveData`:
  `GetInt`/`SetInt`, `GetString`, `GetVec3`... e `Save()`), `HasSave`, `DeleteSave` e `SaveFolder`. No jogo
  exportado ficam em `%APPDATA%\<Jogo>\saves\<slot>.json`; no Play do editor, em `<projeto>/.motor/saves/`
  (**Jogo > Apagar saves do Play**, MCP `game_saves`). O que não foi gravado é gravado ao fechar o jogo e ao
  parar o Play; arquivo estragado vira `.corrompido` e o jogo segue sem save.
- **Opções do jogador** (Fase 21): tela cheia, resolução, vsync, MSAA e sombras. As iniciais ficam no projeto
  (**Arquivo > Configurações do jogo**, `.mproject` formato 4, ou a ferramenta MCP `game_settings`); depois valem
  as do jogador, gravadas em `%APPDATA%\<Jogo>\opcoes.json`. Os scripts montam o menu com `GetOptions`,
  `ApplyOptions`, `Resolutions` e `QuitGame` (guia, seção "Interface do jogo"). No Play do editor só o MSAA e as
  sombras valem, e nada é gravado.
  Os shaders são procurados em `engine/` ao lado do executável e, se não houver, na pasta do código-fonte.
- Pelo servidor MCP: ferramenta `game_export` (cena, pasta, `set_startup`). Testes: `tests/test_export.cpp`.

## Áudio

Sons tocam durante o Play pela camada `engine/.../audio/` (miniaudio, com o stb_vorbis para `.ogg`). São
componentes comuns (Inspector, cena, desfazer):

| Componente | Campos |
|---|---|
| **Fonte de áudio** (`AudioSource`) | Clipe (`.wav`/`.ogg`/`.mp3` do projeto, por GUID), Volume, Tom, Repetir, Tocar ao iniciar, Som 3D, Distância mínima e máxima |
| **Ouvinte de áudio** (`AudioListener`) | Volume geral. Ponha na câmera; sem nenhum, ouve a câmera principal |

- **Som 3D:** o volume cai com a distância até o ouvinte (modelo inverso: cheio até a distância mínima,
  constante depois da máxima) e vem do lado certo (esquerda/direita pela direção do ouvinte). Desligado (2D),
  para música e sons da UI, a posição não importa.
- **Play:** o som só toca no Play. **Parar** silencia tudo; **Pausa** pausa os sons e **Continuar** retoma.
  O dispositivo de som só abre quando o primeiro som toca; sem placa de som, o jogo roda igual, sem som.
- **Arquivos:** até 2 MB são decodificados inteiros numa thread ao iniciar o Play ou a cena (o primeiro toque
  não trava; um toque antes do fim espera só o seu clipe); maiores (música) são lidos aos poucos durante o jogo. Arquivo que não abre vira aviso no console.
- **Scripts:** `GetSystem<motor::AudioSystem>()` com `Play`, `Stop`, `SetVolume` e `IsPlaying` (pelo nome da
  entidade ou pelo id), `PlayOneShot` (outra voz da mesma fonte, até 8) e `SetGroupVolume` (grupos Efeitos,
  Música e Interface, campo **Grupo** da fonte). Ver a seção 14 do [guia de scripts](docs/scripts.md) e o exemplo
  `docs/examples/SonsDoMenu.cpp` (som de clique de botão e música).
- **Clipes de uma cena** (para exportar o jogo): `motor::CollectAudioClips(scene)` (`motor/scene/AudioSource.h`)
  lista os clipes das Fontes de áudio; no `.mscene`, cada um é o campo `"clip"` do componente `"AudioSource"`,
  `{"guid": ..., "path": "Assets/..."}`.

## Navegação (IA)

Inimigos e NPCs andam sozinhos pelo cenário com a camada `engine/.../ai/` (Recast/Detour, Fase 34). Dois
componentes comuns (Inspector, cena, desfazer):

| Componente | Campos |
|---|---|
| **Superfície de navegação** (`NavMeshSurface`) | Navmesh (arquivo `.mnav` e o botão Gerar), Raio, Altura, Altura do degrau e Inclinação máxima do agente; Tamanho e Altura da célula; Área mínima. Uma por cena: liga o navmesh |
| **Agente de navegação** (`NavMeshAgent`) | Velocidade, Aceleração, Velocidade de giro, Distância de parada, Altura da base, Virar para onde anda, Alvo (entidade a seguir), Refazer caminho a cada |

- **De onde vem o navmesh:** dos **Colliders estáticos** das entidades ativas (sem Rigidbody, sem trigger, sem
  agente, sem Controlador de personagem). Caixa, esfera, cilindro e malha entram como são; a cápsula entra como
  cilindro. Só a face virada para cima de cada triângulo vira chão.
- **Gerar no editor:** o botão **Gerar**, ao lado do campo **Navmesh** da Superfície, gera o navmesh da cena e grava
  num arquivo `.mnav` (em `Assets/Navegacao`, ou no arquivo já escolhido), com GUID como os outros assets; o jogo
  exportado leva o arquivo. No Play o navmesh vem dele; se o cenário ou a Superfície mudaram depois de gerar, o log
  avisa que está desatualizado (gere de novo). Sem arquivo, o navmesh é gerado no começo de cada Play. O log diz
  quantos polígonos saíram e quanto tempo levou, ou por que não saiu nenhum.
- **Agente:** anda pelo caminho até o destino, acelerando e freando a tempo de parar, vira em Y para onde anda (a
  frente é o -Z) e mantém os pés no navmesh (a origem fica **Altura da base** acima). Com **Alvo**, segue a
  entidade e refaz o caminho quando ela anda. Ele mexe no Transform direto: por enquanto, use em entidades sem
  física.
- **Scripts:** `GetSystem<motor::NavSystem>()` com `SetDestination`, `Stop`, `HasArrived`, `RemainingDistance`,
  `Warp`, `FindPath`, `SamplePosition`, `Raycast` e `Rebuild` (cenário mudou). Ver a referência em
  [docs/scripts-api.md](docs/scripts-api.md).
- **No viewport:** o botão **Navmesh** da barra mostra o navmesh em azul translúcido (fora do Play, o `.mnav` da
  Superfície, atualizado ao gerar de novo; no Play, o que o jogo está usando) e, no Play com a câmera do editor, o
  caminho de cada agente em amarelo.
- **Ainda não:** multidão, obstáculos que se mexem (roteiro em
  `docs/roadmap/alpha-6.md`).

## Servidor MCP (agentes no editor)

O editor tem um servidor [MCP](https://modelcontextprotocol.io) (Model Context Protocol): um agente como o
Claude Code vê e mexe no editor pelas mesmas operações da interface, sem simular mouse nem teclado do sistema.

- **Ligar:** menu **Ver > Servidor MCP**. Fica ligado nas próximas vezes que o editor abrir. Outra opção é a
  linha de comando `MotorEditor <projeto> --mcp` (ou `--mcp=<porta>`). A porta padrão é **47800**, e enquanto
  o servidor estiver ligado aparece "MCP" na barra de menus.
- **Conectar o Claude Code** (uma vez):

  ```bash
  claude mcp add --transport http motor http://127.0.0.1:47800/mcp
  ```

- **Ferramentas:**

  | Grupo | Ferramentas |
  |---|---|
  | Situação | `editor_status`, `scene_tree`, `entity_get` (no Play, também o estado do jogo: clipe do Animator, no chão, velocidade), `component_types`, `log_read`, `events_read` (o que mudou no editor: seleção, cena, Play, scripts), `docs_read` (documentação de scripts desta versão; o `editor_status` mostra em `script_reference` as pastas da documentação e dos headers do SDK) |
  | Cena | `entity_create`, `entity_update`, `entity_delete`, `entity_duplicate`, `entity_copy`, `entity_cut`, `entity_paste`, `select`, `scene_search`, `component_add`, `component_remove`, `component_set`, `scene_environment`, `undo`, `redo` |
  | Arquivos | `project_create`, `scene_save`, `scene_open`, `scene_new`, `assets_list`, `prefab_instantiate`, `prefab_create`, `asset_apply` |
  | Jogo | `play` (play/pause/resume/step/stop; `wait` responde quando valeu), `game_key` (teclas só para o jogo), `ui_click` (botões do HUD), `audio_status` (sons tocando, ouvinte, nível da saída) |
  | Scripts | `scripts_build` (`wait` responde quando a compilação termina), `scripts_status` (erros com arquivo e linha) |
  | Exportar | `game_export` (pasta com o .exe do jogo, ver "Exportar o jogo") |
  | Ver | `viewport_capture` (PNG da cena e do HUD, desenhado na hora; vistas scene, game e editor), `view_focus`, `view_camera` (mover e virar a câmera do editor), `view_side_by_side` |

  As mudanças viram passos de **desfazer**, como as feitas na interface. Durante o Play, a cena é a do jogo:
  o que mudar é descartado ao parar. O servidor atende também com a janela do editor **minimizada** ou sem
  foco, e o `viewport_capture` desenha a cena na hora num framebuffer próprio, então a imagem é a do momento
  do pedido. `play` e `scripts_build` com `wait: true` só respondem quando o pedido valeu (o servidor confere a
  cada quadro, sem travar os outros pedidos), o que dispensa esperar com `sleep` em testes automáticos.
- **Segurança:** o servidor só escuta em `127.0.0.1` e recusa pedidos vindos de páginas da web (cabeçalhos
  `Origin`/`Host` de fora). Ele não tem senha: enquanto estiver ligado, qualquer programa deste computador pode
  usá-lo. Desligue quando não estiver usando.
- **Como funciona:** o protocolo JSON-RPC fica em `editor/src/mcp/McpProtocol`, o HTTP em `McpServer` e as
  ferramentas em `McpSceneTools` e `McpController`. Os pedidos chegam numa thread do HTTP e são atendidos na
  thread principal, uma vez por quadro. A captura sai em PNG com um compressor próprio (`ImageEncode`).
  Testes: `tests/test_mcp.cpp`.

## Hub do Motor

`MotorHub.exe` é um programa separado do editor, como o Unity Hub: guarda a lista de projetos e as versões do
Motor instaladas, e abre cada projeto com o editor da versão em que ele foi feito. Ele não liga na
`libMotor.dll` (GLFW, ImGui e nlohmann_json estáticos, runtime do GCC estático): roda sozinho e funciona com
qualquer versão do editor, inclusive as futuras.

- **Projetos:** cartões com nome, pasta, versão do Motor e última abertura; **Abrir** (ou duplo clique) usa a
  versão do projeto. Versão ausente ou desconhecida: o hub pergunta com qual versão abrir e avisa que o
  editor vai passar o projeto para ela (faça uma cópia de segurança antes). No menu **...**: abrir com outra
  versão, mostrar no Explorer, remover da lista (a pasta fica). **Adicionar pasta** põe um projeto existente
  na lista; a busca filtra por nome ou pasta.
- **Novo projeto:** nome, pasta (padrão `Documentos\Motor`) e versão; o hub chama
  `MotorEditor.exe --create <pasta> <nome>` daquela versão.
- **Versões:** instaladas em `%LOCALAPPDATA%\Motor\Versões\<versão>\`. **Adicionar de uma pasta** copia uma
  pasta montada (abaixo) para lá; **Tornar padrão** escolhe a versão dos projetos novos; **Remover** manda a
  pasta para a Lixeira.
- **Baixar versões:** em **Disponíveis para baixar**, as versões publicadas nos servidores da lista
  **Servidores** (padrão `http://127.0.0.1:47900`, o `MotorServidor` desta máquina). **Baixar** mostra o
  progresso e pode ser cancelado; o que já veio fica guardado e o botão vira **Retomar**. Cada arquivo é
  conferido pelo SHA-256 do manifesto, e a versão só aparece instalada quando tudo confere (é baixada em
  `Versões\.baixando\<versão>\` e renomeada no fim). Um Hub mais novo publicado num servidor aparece no topo,
  com o link do instalador.
- **Configuração:** `%LOCALAPPDATA%\Motor\hub.json` (`{"format": "motor-hub", "version": 2, ...}`, com a lista
  `servers`; o formato 1 é lido com o servidor local na lista), separada da do editor.

**Montar a pasta de instalação de uma versão** (a do código atual):

```bash
cmake --build build --target motor_package     # build/package/Motor-0.2.0/
```

A pasta é autossuficiente: `MotorEditor.exe`, `Player.exe`, `libMotor.dll` e o runtime do GCC, `engine/`
(shaders e fontes), `templates/`, `sdk/` (headers e biblioteca de importação para compilar os scripts dos
projetos, com caminhos relativos) e `motor-version.json` (`{"version": "0.2.0", "scriptApiVersion": 10, ...}`),
que o hub lê. Outra pasta de saída: `-DMOTOR_PACKAGE_DIR=...` no configure. Montada de um build que não é de
Release, ela leva também `release/` (Player, DLLs e SDK de um build de Release, de `MOTOR_RELEASE_BIN_DIR`) para
os jogos exportados saírem em Release; sem esse build, o `motor_package` avisa.
**O editor instalado continua precisando do MSYS2 (UCRT64) e do CMake** nos mesmos caminhos do build que montou
a pasta (estão em `sdk/MotorSDK.json`) para compilar os scripts dos projetos; o jogo exportado não precisa.

**Linha de comando** (as mesmas ações dos botões, sem abrir a janela; para agentes e testes):

```bash
MotorHub --add-version build/package/Motor-0.2.0 --list
MotorHub --create "C:/Jogos" "Jogo Ação"                 # com a versão padrão (ou --engine 0.2.0)
MotorHub --open "C:/Jogos/Jogo Ação"                     # com a versão do projeto
MotorHub --open "C:/Jogos/Antigo" --engine 0.2.0         # outra versão (o editor atualiza o engineVersion)
MotorHub --data "C:/temp/Hub Teste" ... -- --no-settings --mcp   # outra pasta de dados; depois de "--": argumentos para o editor
```

Também: `--remove-version`, `--set-default`, `--add-project`, `--remove-project`, `--aba versões` (abre a janela
nessa aba), `--version`, `--disponiveis [--servidor URL]` (versões publicadas nos servidores) e
`--baixar <versão|mais-recente> [--servidor URL]` (baixa, confere e instala; retoma um download interrompido).
O hub instalado pelo `MotorSetup` com outra pasta de dados a lê do `instalacao.json` ao lado do `MotorHub.exe`. Código em `hub/`: `HubCore` (lógica sem janela, `tests/test_hub.cpp`),
`HubPlatform` (Windows: processos, Explorer, Lixeira), `HubApp` (janela e ações), `main.cpp` (linha de comando).

## Instalar

Para usar o Motor sem compilá-lo: o **instalador do Hub** (`MotorSetup.exe`, um arquivo só) instala o Hub para o
seu usuário, sem pedir administrador, e pode já instalar uma versão do Motor.

1. Baixe o instalador na página do servidor de versões (`http://<servidor>:47900/`, abaixo) ou use o
   `build/bin/MotorSetup.exe` de um build.
2. Rode e confira: pasta do Hub (padrão `%LOCALAPPDATA%\Programs\Motor Hub\`), pasta de dados (padrão
   `%LOCALAPPDATA%\Motor`: versões do Motor e `hub.json`), atalho no menu Iniciar e, se quiser, na área de
   trabalho. Em **Versão do Motor**, escolha baixar a mais recente de um servidor (botão **Verificar** mostra o
   que ele tem) ou instalar de uma pasta montada (`motor_package`), ou deixe para depois.
3. **Instalar**. No fim, **Abrir o Hub agora**.

O instalador leva o `MotorHub.exe` dentro dele (funciona sem servidor), registra a desinstalação em
**Configurações > Aplicativos > Aplicativos instalados** ("Motor Hub") e grava `instalacao.json` na pasta do Hub
com o que criou. Ele avisa se faltam o **MSYS2 (UCRT64)** e o **CMake**, que o editor usa para compilar os
scripts dos projetos (<https://www.msys2.org/>; no terminal UCRT64:
`pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake make`), mas não os instala.

**Desinstalar:** pelo Aplicativos instalados ou `MotorSetup.exe --desinstalar`. A pasta do Hub vai para a
Lixeira e os atalhos e o registro saem; a pergunta "também as versões do Motor e o hub.json" decide se
`Versões\` e `hub.json` da pasta de dados vão junto. Projetos nunca são tocados. Rodando de dentro da pasta do
Hub, o desinstalador continua numa cópia em `%TEMP%` (o Windows não move a pasta de um programa aberto), que se
apaga sozinha no fim.

Sem janela (testes e agentes; o resultado sai no console):

```bash
MotorSetup --silencioso --pasta "C:/temp/Hub" --dados "C:/temp/Dados" --servidor http://127.0.0.1:47900 --sem-atalhos --sem-registro
MotorSetup --silencioso --versao-local build/package/Motor-0.2.0 ...   # versão de uma pasta montada
MotorSetup --desinstalar --silencioso --pasta "C:/temp/Hub" [--apagar-dados]
```

Também: `--area-de-trabalho`, `--abrir-hub`, e para testes `--atalhos-em <pasta>` (atalhos numa pasta qualquer)
e `--chave-registro <nome>` (outra chave em `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall`).
Código em `installer/` (`Setup`: instalar e desinstalar; `SetupApp`: janela) sobre o plano de
`distrib/InstallPlan` (testado em `tests/test_distribuicao.cpp`).

## Servidor de versões

`MotorServidor.exe` distribui as versões do Motor e o Hub numa rede local, por HTTP (sem internet, sem
bibliotecas novas: cpp-httplib e o SHA-256 do Windows). Ele serve uma **pasta de publicações**: a de
`--pasta <dir>`; sem ela, a pasta **`Publicacoes` ao lado do `MotorServidor.exe`**, se existir (instalação
portátil: copie o `.exe` para `C:\MotorServidor`, publique em `C:\MotorServidor\Publicacoes` e os dois cliques
no `.exe` já servem essa pasta); senão `%LOCALAPPDATA%\Motor\Servidor`:

```bash
# 1. Monte a versão (de preferência Release: ~25 MB em vez de ~200 MB) e publique
cmake -S . -B build-release -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j8 --target motor_package MotorHub MotorSetup MotorServidor
build-release/bin/MotorServidor --pasta C:/MotorServidor --publicar build-release/package/Motor-0.2.0
build-release/bin/MotorServidor --pasta C:/MotorServidor --publicar-hub build-release/bin/MotorHub.exe
# 2. Sirva (porta 47900; sem --local, aceita a rede local e o Firewall do Windows pode pedir permissão)
build-release/bin/MotorServidor --pasta C:/MotorServidor
```

- `--publicar <pasta montada> [--canal <nome>]` copia a versão para `versoes/<versão>/` (via `.tmp`, renomeada
  no fim), calcula o tamanho e o SHA-256 de cada arquivo e refaz o `manifesto.json`. Versão já publicada:
  `--remover <versão>` antes (a pasta vai para a Lixeira).
- `--publicar-hub <MotorHub.exe>` publica o Hub (a versão vem do recurso VERSIONINFO do .exe) e o
  `MotorSetup.exe` que estiver ao lado (ou `--setup <arquivo>`), para baixar pela página.
- `--listar`, `--regenerar` (refaz o manifesto a partir das pastas), `--porta <n>`, `--local` (só 127.0.0.1).

Rotas: `GET /` (página com as versões e o link do instalador), `GET /api/versoes` (manifesto
`{"format": "motor-releases", "version": 1, "releases": [{"version", "channel", "scriptApiVersion",
"buildType", "date", "size", "files": [{"path", "size", "sha256"}]}], "hub": {...}}`),
`GET /arquivos/<versão>/<caminho>` (aceita `Range`, para retomar), `GET /hub/latest` e `GET /hub/MotorSetup.exe`.
Os arquivos vão um a um, sem compressão, e só o que está no manifesto é servido. Nos outros computadores, ponha
`http://<IP do servidor>:47900` em **Servidores** no Hub ou no instalador. O servidor relê o manifesto quando
ele muda: dá para publicar com ele no ar.

## Cenas (`.mscene`)

Cenas são salvas em JSON legível (`engine/src/scene/SceneSerializer.cpp`, via nlohmann/json):

```json
{
  "format": "motor-scene",
  "version": 6,
  "environment": { "skyColor": [0.53, 0.7, 0.88], "fogDensity": 0.012, "ambient": [0.12, 0.14, 0.18],
                   "sky": "Color", "skyTexture": null, "skyIntensity": 1.0, "skyRotation": 0.0, "skyLight": 1.0,
                   "shadowDistance": 60.0, "exposure": 1.0, "toneMapper": "ACES", "bloom": true,
                   "bloomIntensity": 0.5, "bloomThreshold": 1.0, "fxaa": true },
  "entities": [
    {
      "id": 1, "name": "Chão", "active": true, "parent": 0,
      "transform": { "position": [0, 0, 0], "rotation": [x, y, z, w], "scale": [40, 1, 40] },
      "components": [
        { "type": "MeshRenderer", "mesh": "Plano", "texture": "Xadrez", "color": [1, 1, 1, 1], "metallic": 0.0, "roughness": 0.5, "specular": 0.1, ... }
      ]
    },
    {
      "id": 2, "name": "Carro", "active": true, "parent": 0,
      "transform": { ... },
      "components": [
        {
          "type": "MeshRenderer",
          "mesh": { "guid": "9f2c...e41a", "path": "Assets/Modelos/carro.obj" },
          "material": { "guid": "5d0e...a7b3", "path": "Assets/Materiais/Pintura.mmat" },
          ...
        }
      ]
    },
    {
      "id": 3, "name": "Lâmpada", "active": true, "parent": 0,
      "transform": { ... },
      "components": [
        { "type": "Light", "kind": "Point", "color": [1, 0.8, 0.5], "intensity": 2, "range": 6, "innerAngle": 25, "outerAngle": 35 }
      ]
    }
  ]
}
```

- Transform é local ao pai (`parent` = `id` de outra entidade, 0 = raiz); rotação é um quaternion `[x, y, z, w]`.
- Malhas e texturas embutidas são referenciadas pelo **nome** (`"Cubo"`); as de arquivo, pelo **GUID** do
  `.meta`, com o caminho junto só para leitura e como plano B. Asset não encontrado ou com erro gera aviso
  no log e o componente fica sem ele (a cena carrega mesmo assim).
- Componentes vão em `"components"`, cada um com `"type"` e os seus campos. Campo ausente = valor padrão;
  componente de tipo desconhecido (script removido ou ainda não compilado) é **mantido** como está e
  gravado de volta igual (aviso no log).
- Cenas antigas continuam abrindo: versão 1 (assets por caminho), 2 (`"meshRenderer"` fixo na entidade) e
  1 a 3 (luz direcional fixa da cena, `"light"`: vira uma entidade "Luz direcional" com o componente Luz, e
  o ambiente vai para `"environment"`), 4 (sem asset de material: a cor e a textura do Mesh Renderer viram o
  material próprio dele, nada se perde). Ao salvar, viram versão 5.
- Instâncias de prefab têm o componente `PrefabInstance`: `{ "guid", "path", "source", "overrides" }`. Ao
  abrir a cena, elas são atualizadas pelo prefab atual (menos os overrides).
- Os ids são reatribuídos ao carregar; campos ausentes usam o valor padrão.
- O salvamento escreve em `arquivo.tmp` e depois substitui o original, para não corromper a cena se falhar no meio.
- Os diálogos de arquivo são nativos do Windows; em outras plataformas ainda não estão implementados.

## Notas

- Shaders são lidos de `engine/assets` via caminho absoluto definido no CMake (`MOTOR_ENGINE_ASSETS`) — prático em desenvolvimento; trocar por um sistema de assets depois.
- Iluminação em espaço linear; texturas são `SRGB8_ALPHA8` e o shader aplica gama 2.2 na saída
  (não usamos `GL_FRAMEBUFFER_SRGB` para a imagem do viewport e o ImGui ficarem consistentes).
- Em Debug, o contexto OpenGL é criado com debug output e os erros do driver vão para o log.
