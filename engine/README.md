# Engine — uso

A `libMotor.dll` (camadas em `engine/`), o `Player.exe` (`player/`) e o `Sandbox` (`sandbox/`). Estado e roteiro:
`docs/engine/estado.md` e `docs/engine/roteiro.md`. Compilar e testar: `README.md` da raiz.

## Medir com o Tracy (profiler por quadro)

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

## Contadores por quadro (sem o Tracy)

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

## Controles do Sandbox

| Ação | Tecla |
|---|---|
| Olhar | segurar botão direito + mouse |
| Mover | WASD, Q/E desce/sobe, Shift acelera, scroll ajusta velocidade |
| Wireframe | F1 |
| Sair | Esc |

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
  scripts trocam com `GetAnimator()->CrossFade("Andando", 0.2f)` (guia: [docs/scripts.md](../docs/scripts.md),
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
- Campo dentro de outra struct: `.FieldRef("cor", "Cor", [](../MeshRenderer& m) -> auto& { return m.material.color; })`.
- Valores iniciais ao adicionar pelo editor: `.OnAdd([](../T& c, AssetLibrary& assets) { ... })`.
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

**Guia completo: [docs/scripts.md](../docs/scripts.md)** (do primeiro script a um mini jogo, com receitas e
erros comuns) e **referência da API: [docs/scripts-api.md](../docs/scripts-api.md)**. Os exemplos do guia
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
  [guia de scripts](../docs/scripts.md) e os exemplos `docs/examples/Hud.cpp` e `MenuPrincipal.cpp`.
- **Fontes:** `.ttf` e `.otf` do projeto são assets (com GUID); o campo Fonte vazio usa a padrão, **Roboto
  Medium** (`engine/assets/fonts/`, Apache License 2.0, ver `LICENSE-Roboto.txt` na mesma pasta). Cada fonte
  vira um atlas **SDF** (campo de distância, gerado pelo `imstb_truetype.h` que vem com o Dear ImGui), nítido
  em qualquer tamanho; o atlas tem ASCII, Latin-1 (acentos) e algumas pontuações.
- **Desempenho:** todo o texto e as imagens vão num VBO dinâmico só, em uma chamada de desenho por troca de
  textura (retângulos sólidos usam o atlas da fonte: um HUD de texto e painéis é 1 chamada; a barra do
  viewport mostra "UI: N draws"). A geometria de cada elemento só é refeita quando o texto, o tamanho, a
  fonte, a cor ou o retângulo mudam; frame sem mudança não envia nada para a GPU.

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
  Música e Interface, campo **Grupo** da fonte). Ver a seção 14 do [guia de scripts](../docs/scripts.md) e o exemplo
  `docs/examples/SonsDoMenu.cpp` (som de clique de botão e música).
- **Clipes de uma cena** (para exportar o jogo): `motor::CollectAudioClips(scene)` (`motor/scene/AudioSource.h`)
  lista os clipes das Fontes de áudio; no `.mscene`, cada um é o campo `"clip"` do componente `"AudioSource"`,
  `{"guid": ..., "path": "Assets/..."}`.

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
