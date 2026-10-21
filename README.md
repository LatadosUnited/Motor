# Motor

Engine 3D em C++20 + OpenGL 4.5, escrita do zero. Dependências externas mínimas:
**GLFW** (janela/input), **GLM** (matemática), **Dear ImGui** (UI do editor, branch docking),
**ImGuizmo** (gizmos de transform), **nlohmann/json** (cenas e projetos), **stb_image** (PNG/JPG),
**cgltf** (modelos glTF), **ufbx** (modelos FBX), **Jolt Physics** (física), **miniaudio** (som) e **cpp-httplib** (servidor MCP do
editor e servidor de versões), baixadas
automaticamente pelo CMake. O leitor de OBJ é próprio; as fontes da UI do jogo usam o `imstb_truetype.h` que já
vem com o Dear ImGui.
O carregador de OpenGL é próprio (`engine/src/renderer/GL.h`).

## Módulos

O Motor é dividido em quatro módulos, cada um com versão, estado e roteiro próprios ([`docs/visao.md`](docs/visao.md)):

| Módulo | Pastas | Uso | Estado e roteiro |
|---|---|---|---|
| **Engine** (0.2.650) | `engine/`, `player/`, `sandbox/` | [`engine/README.md`](engine/README.md) | [`docs/engine/`](docs/engine/estado.md) |
| **Editor** (0.2.650) | `editor/` | [`editor/README.md`](editor/README.md) | [`docs/editor/`](docs/editor/estado.md) |
| **Hub** (0.2.0) | `hub/` | [`hub/README.md`](hub/README.md) | [`docs/hub/`](docs/hub/estado.md) |
| **Instalador** (0.2.0 / servidor 0.1.0) | `distrib/`, `server/`, `installer/` | [`installer/README.md`](installer/README.md) | [`docs/instalador/`](docs/instalador/estado.md) |

Scripts dos jogos: [guia](docs/scripts.md) e [referência da API](docs/scripts-api.md). Desempenho e comparação
com a Unity: [`bench/README.md`](bench/README.md). Toda a documentação: [`docs/inicio.md`](docs/inicio.md).

## Compilar (Windows, MSYS2 UCRT64)

```bash
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure   # testes (sem janela)
./build/bin/Sandbox.exe       # demo
./build/bin/MotorEditor.exe    # editor (reabre o último projeto)
./build/bin/MotorEditor.exe "C:/caminho/MeuJogo"   # abre um projeto específico
./build/bin/Player.exe "C:/caminho/MeuJogo"        # roda o jogo sem o editor (ver `editor/README.md`, "Exportar o jogo")
./build/bin/MotorHub.exe       # Hub: projetos e versões instaladas (ver `hub/README.md`)
./build/bin/MotorServidor.exe  # servidor de versões na rede local (ver `installer/README.md`)
./build/bin/MotorSetup.exe     # instalador do Hub (ver `installer/README.md`)
```

Executáveis e DLLs ficam juntos em `build/bin`: a engine é a `libMotor.dll` (com ImGui e GLFW dentro), e
o runtime do C++ (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) é copiado do toolchain
para lá. `build/bin/sdk/` tem o que o editor usa para compilar os scripts dos projetos (ver `engine/README.md`, "Scripts").

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
  src/<camada>/    implementação + CMakeLists.txt da camada (+ renderer/GL.h: loader OpenGL privado)
  assets/shaders/  shaders GLSL da engine (skinned.vert: malhas animadas)
  assets/fonts/    fonte padrão da UI do jogo (Roboto Medium, Apache 2.0)
  assets/models/   Robo.glb, personagem animado de exemplo (gerado por gerar_robo.py)
docs/              documentação (vault do Obsidian): visão, estado e roteiro de cada módulo, guia e referência dos
                   scripts (scripts.md, scripts-api.md) e exemplos compilados pelo ctest; arquivo/ com o roteiro antigo
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

Para quem trabalha no código (pessoas e agentes): [`CLAUDE.md`](CLAUDE.md) e o `CLAUDE.md` de cada módulo.
