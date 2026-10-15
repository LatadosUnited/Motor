# Motor

Engine 3D em C++20 + OpenGL 4.5, escrita do zero. Dependências externas mínimas:
**GLFW** (janela/input), **GLM** (matemática), **Dear ImGui** (UI do editor, branch docking),
**ImGuizmo** (gizmos de transform), **nlohmann/json** (cenas e projetos) e **stb_image** (PNG/JPG),
baixadas automaticamente pelo CMake. O leitor de OBJ é próprio.
O carregador de OpenGL é próprio (`engine/src/renderer/GL.h`).

## Compilar (Windows, MSYS2 UCRT64)

```bash
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure   # testes (sem janela)
./build/sandbox/Sandbox.exe       # demo
./build/editor/MotorEditor.exe    # editor (reabre o último projeto)
./build/editor/MotorEditor.exe "C:/caminho/MeuJogo"   # abre um projeto específico
```

## Estrutura

```
engine/
  include/motor/   API pública
    core/          Application (game loop c/ fixed timestep), Window, Input, Log, ImGuiLayer
    renderer/      Renderer, Shader, Mesh, Texture2D, Framebuffer, Camera, Material
    assets/        AssetLibrary (malhas/texturas), AssetDatabase (GUIDs em .meta), AssetLoaders (OBJ, PNG/JPG)
    project/       Project (pasta do projeto + arquivo .mproject)
    scene/         Scene, componentes (Component.h: registro e campos), SceneSerializer (.mscene), Transform
  src/             implementação (+ GL.h: loader OpenGL privado)
  assets/shaders/  shaders GLSL da engine
editor/            MotorEditor: hierarquia, inspector, cena, viewport, projeto (docking), modo Play
scripts/           scripts do jogo em C++ (biblioteca MotorScripts, compilada junto com o editor)
sandbox/           app de demonstração
tests/             testes ctest (projeto, assets, GUIDs, componentes, desfazer, scripts, Play)
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
```

- **Novo projeto** cria essa estrutura (em Documentos, por padrão) com uma cena inicial (chão + cubo).
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

## Assets (texturas e malhas)

Arquivos dentro do projeto podem ser usados direto nas cenas:

| Tipo | Extensões | Observações |
|---|---|---|
| Textura | `.png` `.jpg` `.jpeg` `.bmp` `.tga` | via stb_image; tratada como cor sRGB, com mipmaps |
| Malha | `.obj` | `v`, `vt`, `vn`, `f` (polígonos viram triângulos). Sem normais no arquivo, são calculadas. Materiais `.mtl` são ignorados |

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

// Uma vez, na inicialização (os da engine ficam em engine/src/scene/Component.cpp):
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

## Scripts (C++)

Um script é um componente que herda de `motor::Script`: os campos registrados aparecem no Inspector (em
"Adicionar componente" > Scripts), são salvos na cena e entram no desfazer como qualquer componente; os
métodos `On*` só rodam no Play.

```cpp
// scripts/include/scripts/Scripts.h
struct Girar : motor::Script {
    glm::vec3 eixo{0.0f, 1.0f, 0.0f};
    float velocidade = 90.0f;  // graus por segundo

    void OnUpdate(float dt) override {
        GetTransform().rotation *= glm::angleAxis(glm::radians(velocidade * dt), eixo);
    }
};

// scripts/src/Scripts.cpp, em RegisterAll():
registry.Register<Girar>("Girar", "Girar")
    .Field("eixo", "Eixo", &Girar::eixo)
    .Field("velocidade", "Velocidade", &Girar::velocidade);
```

| O que | API (dentro do script) |
|---|---|
| Ciclo de vida | `OnStart()`, `OnUpdate(dt)`, `OnFixedUpdate(dt)`, `OnDestroy()` (virtuais, todas opcionais) |
| Dono | `GetEntity()`, `GetId()`, `GetTransform()`, `GetScene()` |
| Cena | `FindEntity("nome")`, `CreateEntity("nome", pai)`, `Destroy(id)`, `DestroySelf()` |
| Tempo | `Time().delta`, `Time().fixedDelta`, `Time().elapsed` (s desde o Play, sem pausas), `Time().frame` |
| Entrada | `IsKeyDown(Key::W)`, `WasKeyPressed(...)`, `IsMouseDown(...)`, `MouseDelta()` |
| Log | `Log("vidas = {}", vidas)` — sai no log com o nome da entidade |

- `OnStart` roda antes do primeiro `OnUpdate`; entidades inativas só iniciam quando ficam ativas, e
  entidades criadas durante um quadro começam no seguinte.
- `Destroy` é adiado para o fim do passo e leva os filhos junto (todos recebem `OnDestroy`).
- A entrada dos scripts é falsa/zero quando o jogo não tem o foco; `motor::Input` direto não tem esse filtro.
- Campos não registrados são estado interno (não salvos, não comparados). Copiar um script (Play, duplicar)
  copia os campos, mas a cópia começa "não iniciada".
- Não remova o componente de um script de dentro dele mesmo durante o Play; use `Destroy` na entidade.

Scripts de exemplo (`scripts/src/`): **Girar**, **Mover com WASD** (plano XZ, Shift corre, vira para onde
anda) e **Seguir** (segue uma entidade pelo nome mantendo um deslocamento — bom para a câmera). A cena de
exemplo do editor (sem projeto aberto) já usa os três: dê Play e mova a esfera "Jogador" com WASD.

Para criar um script: declare a struct em `Scripts.h`, implemente num `.cpp` em `scripts/src/`, registre em
`RegisterAll` e adicione o arquivo em `scripts/CMakeLists.txt`. Os scripts são compilados junto com o editor
(é preciso recompilar e reabrir o editor); recarregar por DLL sem fechar o editor é o próximo passo (ROADMAP).

## Cenas (`.mscene`)

Cenas são salvas em JSON legível (`engine/src/scene/SceneSerializer.cpp`, via nlohmann/json):

```json
{
  "format": "motor-scene",
  "version": 3,
  "environment": { "skyColor": [0.53, 0.7, 0.88], "fogDensity": 0.012 },
  "light": { "direction": [-0.5, -1.0, -0.35], "color": [1.0, 0.97, 0.9], "intensity": 1.0, "ambient": [...] },
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
          "texture": { "guid": "07b1...c9d0", "path": "Assets/Texturas/pintura.png" },
          ...
        }
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
  componente de tipo desconhecido é ignorado com aviso no log.
- Cenas antigas continuam abrindo: versão 1 (assets por caminho) e 2 (`"meshRenderer"` fixo na entidade).
  Ao salvar, viram versão 3.
- Os ids são reatribuídos ao carregar; campos ausentes usam o valor padrão.
- O salvamento escreve em `arquivo.tmp` e depois substitui o original, para não corromper a cena se falhar no meio.
- Os diálogos de arquivo são nativos do Windows; em outras plataformas ainda não estão implementados.

## Notas

- Shaders são lidos de `engine/assets` via caminho absoluto definido no CMake (`MOTOR_ENGINE_ASSETS`) — prático em desenvolvimento; trocar por um sistema de assets depois.
- Iluminação em espaço linear; texturas são `SRGB8_ALPHA8` e o shader aplica gama 2.2 na saída
  (não usamos `GL_FRAMEBUFFER_SRGB` para a imagem do viewport e o ImGui ficarem consistentes).
- Em Debug, o contexto OpenGL é criado com debug output e os erros do driver vão para o log.
