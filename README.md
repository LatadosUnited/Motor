# Motor

Engine 3D em C++20 + OpenGL 4.5, escrita do zero. Dependências externas mínimas:
**GLFW** (janela/input), **GLM** (matemática), **Dear ImGui** (UI do editor, branch docking),
**ImGuizmo** (gizmos de transform), **nlohmann/json** (cenas e projetos), **stb_image** (PNG/JPG),
**cgltf** (modelos glTF) e **Jolt Physics** (física), baixadas automaticamente pelo CMake. O leitor de OBJ é
próprio.
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
```

Executáveis e DLLs ficam juntos em `build/bin`: a engine é a `libMotor.dll` (com ImGui e GLFW dentro), e
o runtime do C++ (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) é copiado do toolchain
para lá. `build/bin/sdk/` tem o que o editor usa para compilar os scripts dos projetos (ver "Scripts").

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
  src/<camada>/    implementação + CMakeLists.txt da camada (+ renderer/GL.h: loader OpenGL privado)
  assets/shaders/  shaders GLSL da engine (skinned.vert: malhas animadas)
  assets/models/   Robo.glb, personagem animado de exemplo (gerado por gerar_robo.py)
docs/              guia e referência dos scripts (scripts.md, scripts-api.md) e exemplos compilados pelo ctest
editor/            MotorEditor: hierarquia, inspector, cena, viewport, projeto (docking), modo Play, scripts
  templates/Scripts/  scripts de exemplo copiados para os projetos (Girar, Jogador, MoverWASD, Seguir)
sandbox/           app de demonstração
tests/             testes ctest (projeto, assets, GUIDs, componentes, camadas, desfazer, scripts, Play, recarga, luzes, física,
                   materiais, prefabs, glTF e animação)
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
| Gizmo | Q nenhum / W mover / E rotacionar / R escalar; X alterna eixos Local/Global |
| Snap | botão "Snap" na barra do viewport, ou segurar Ctrl ao arrastar (0.5 m / 15° / 0.1) |
| Reparentar | arrastar na hierarquia (soltar no vazio = raiz) |
| Criar | botão "+ Criar", menu Entidade ou clique direito na hierarquia |
| Focar / Excluir / Duplicar | F / Del / Ctrl+D |
| Desfazer / Refazer | Ctrl+Z / Ctrl+Y (ou Ctrl+Shift+Z), também no menu Editar; segurar repete |
| Restaurar layout | Ver > Restaurar layout (o layout fica salvo em `imgui.ini`) |
| Novo / Abrir projeto | Arquivo > Novo projeto... / Abrir projeto... / Projetos recentes |
| Nova / Abrir cena | Arquivo > Nova cena (Ctrl+N) / Abrir cena... (Ctrl+O) |
| Salvar cena | Arquivo > Salvar cena (Ctrl+S) / Salvar cena como... (Ctrl+Shift+S) |
| Play / Parar | botão ▶ / ⏹ no topo do viewport, Ctrl+P ou menu Jogo |
| Pausa / Avançar um quadro | botões ⏸ / ⏭ (ou Ctrl+Shift+P) |
| Câmera do jogo ↔ do editor no Play | botão "Câmera: jogo/editor" ou F8 |

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
  MeuJogo.mproject     { "format": "motor-project", "version": 1, "name": "MeuJogo",
                         "startupScene": "Assets/Scenes/Principal.mscene" }
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
| Modelo | `.gltf` `.glb` | glTF 2.0 (via cgltf): nós, malhas, materiais, texturas, esqueleto e animações. Vira prefab (abaixo) |
| Material | `.mmat` | do Motor: cor, textura, especular e brilho (abaixo) |
| Prefab | `.mprefab` | do Motor: entidade com filhos (abaixo) |

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

Um material guarda **cor, textura, especular e brilho** num arquivo próprio, reaproveitado por vários
objetos: editar o material muda todos os que o usam.

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
{ "format": "motor-material", "version": 1, "color": [0.9, 0.15, 0.1, 1.0],
  "texture": { "guid": "07b1...c9d0", "path": "Assets/Texturas/pintura.png" }, "specular": 0.6, "shininess": 48 }
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
- Prefab dentro de prefab não é suportado: ao criar um prefab, vínculos com outros prefabs viram cópias.
- Mudanças nos materiais e prefabs não entram no desfazer (são arquivos, gravados na hora); a cena registra
  as alterações das instâncias normalmente.

### Modelos 3D (glTF) e animação

Copie um `.gltf` (com o `.bin` e as imagens ao lado) ou `.glb` para `Assets/`. O editor importa sozinho
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
  seção "Animação"). Fora do Play o modelo fica na pose de repouso. O skinning é feito no shader
  (`skinned.vert`, até 128 juntas, 4 por vértice).
- Exemplo: `engine/assets/models/Robo.glb` (1,93 m, olha para -Z, clipes Parado, Andando e Pulando). Com o
  modelo Jogador: entidade com Controlador de personagem + Jogador, o robô como filho na posição `0 -0.9 0`.

Limitações: sem morph targets, câmeras e luzes do glTF; animação só de juntas (nós sem pele não se movem);
malhas do modelo não aparecem no combo "Malha" do Inspector (use o prefab); o modelo alterado no disco é
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
  `MOTOR_FIELD_DRAWER(NomeDoTipo)` (`editor/src/FieldDrawers.h`). Nada mais muda na engine ou no editor.
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
- Componentes da engine hoje: **Mesh Renderer** (malha, textura, cor, especular, brilho) e **Câmera**
  (campo de visão, planos perto/longe, principal). A câmera olha para o -Z local da entidade; no Play vale
  a primeira câmera ativa marcada como principal (senão, a primeira ativa). Entidade > Câmera cria uma.
- **Luz**: tipo (direcional, pontual ou spot), cor, intensidade, alcance (pontual e spot) e ângulos interno/
  externo do cone (spot). Como a câmera, aponta para o -Z local: direcional e spot se giram com o gizmo de
  rotação; a posição só importa para pontual e spot. Várias por cena (até 32 acesas ao mesmo tempo).
  Entidade > Luz direcional / pontual / spot cria uma. Céu, neblina e **luz ambiente** ficam no painel Cena.
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

- Menu **Scripts**: Novo script, Adicionar exemplo (Girar, Jogador, Mover com WASD, Seguir, de
  `editor/templates/Scripts/`), Recompilar (F7), Abrir pasta. O painel **Scripts** mostra erros e avisos.
- Com erro de compilação, continuam carregados os scripts da última compilação boa. Durante o Play a recarga
  espera o Play parar. Script removido ou renomeado vira componente "script não carregado", com os dados
  guardados, até voltar.
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
- No viewport, o botão **Colliders** mostra as formas (verde), os triggers (azul) e os personagens.

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
- `Raycast(origem, direção, distância, ignorar)`: primeiro corpo atingido (entidade, ponto, normal,
  distância); triggers não contam. Rigidbody: `AddForce`, `AddImpulse`, `SetVelocity`, `GetVelocity`.
  Personagem: `MoveCharacter`, `Jump`, `IsGrounded`, `GetCharacterVelocity`. Entidade criada neste passo só
  ganha corpo no próximo `FixedUpdate`.
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

## Cenas (`.mscene`)

Cenas são salvas em JSON legível (`engine/src/scene/SceneSerializer.cpp`, via nlohmann/json):

```json
{
  "format": "motor-scene",
  "version": 5,
  "environment": { "skyColor": [0.53, 0.7, 0.88], "fogDensity": 0.012, "ambient": [0.12, 0.14, 0.18] },
  "entities": [
    {
      "id": 1, "name": "Chão", "active": true, "parent": 0,
      "transform": { "position": [0, 0, 0], "rotation": [x, y, z, w], "scale": [40, 1, 40] },
      "components": [
        { "type": "MeshRenderer", "mesh": "Plano", "texture": "Xadrez", "color": [1, 1, 1, 1], "specular": 0.1, "shininess": 32 }
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
