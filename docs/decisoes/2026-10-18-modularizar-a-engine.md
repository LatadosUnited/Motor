---
tipo: decisao
data: 2026-10-18
status: proposta
fases: [24, 25]
---

# Modularizar a engine

## Contexto

O pedido do usuário (18/10/2026) foi estudar como modularizar a engine. Ela já está dividida em camadas
(`core → renderer → project → assets → scene → app`, mais `physics`, `ui` e `audio` acima de `scene`): cada
camada é uma biblioteca de objetos própria (`motor_<camada>`), e o `test_layers` confere os `#include`. Os
componentes (`MOTOR_COMPONENT_MODULE`), os sistemas do jogo (`MOTOR_RUNTIME_SYSTEM`) e os desenhadores do
Inspector (`MOTOR_FIELD_DRAWER`) se registram sozinhos, sem lista central. Hub, distribuição, servidor e
instalador já ficam fora da `libMotor.dll` (ver [Hub fora da engine](2026-10-17-hub-fora-da-engine.md)).

Essa base é boa. A análise do código achou seis pontos em que as camadas, na prática, ainda dependem umas das
outras:

1. **O `test_layers` usa uma ordem linear, não um grafo.** `MOTOR_LAYERS` é uma lista, e o teste só proíbe
   incluir camadas que vêm depois na lista. Como `physics`, `ui` e `audio` foram postas no fim, `physics`
   pode incluir `app`, `ui` pode incluir `physics` e `audio` pode incluir as duas, e o teste passa. As
   dependências de verdade (`DEPENDS scene`, nos `CMakeLists.txt`) não são conferidas.
2. **A cena depende do renderer.** `Scene.h` inclui `Renderer.h`, `Mesh.h` e `Material.h`; `Scene::Render`,
   `SubmitTo`, `CollectLights` e `Raycast` (nos triângulos da `Mesh`) ficam na cena, e `Environment` (dado
   da cena) mora em `renderer/`. Não existe cena sem o renderer de OpenGL.
3. **Os assets dependem do renderer.** O `AssetLibrary` cria `Mesh` e `Texture` de GPU direto, e os headers
   de `assets/` incluem `renderer/` (`Mesh`, `Material`, `Environment`, `Skeleton`). Carregar ou importar
   um asset sem placa de vídeo só funciona porque as funções do GL são carregadas sob demanda.
4. **Os componentes da física e do áudio ficam em `scene/`.** `ColliderComponent`, `RigidbodyComponent`,
   `CharacterControllerComponent`, `AudioSource` e `AudioListener` estão na camada da cena e em
   `Components.h`. Os dados ficam na cena, e só os sistemas ficam nas camadas de cima. Tirar a física de uma
   build deixaria os componentes dela no Inspector, sem efeito nenhum.
5. **O `core` não é só núcleo.** Além de `Log`, `Paths` e `Version`, ele tem `Window`, `Input` e
   `ImGuiLayer`, e por isso traz o GLFW e o ImGui. Uma ferramenta sem janela (servidor, conversor de assets,
   teste) não consegue usar só o log e os caminhos.
6. **Uma DLL só, com tudo.** A `libMotor.dll` sempre leva todas as camadas, a Jolt e a miniaudio, e exporta
   todos os símbolos (`--export-all-symbols`). `Motor.h` inclui tudo. Não dá para montar uma build sem
   física, nem um projeto escolher os módulos que usa.

## Decisão

Proposta: modularizar **aos poucos, dentro da mesma DLL**, corrigindo primeiro as dependências (pontos 1 a 5).
DLLs separadas por módulo só entram se surgir uma necessidade concreta (módulos escolhidos por projeto,
plugins de terceiros). Grafo final proposto:

```
core (log, caminhos, versão)
 ├── platform (Window, Input, ImGuiLayer)        ← sai do core
 ├── project
 ├── assets (dados na CPU: malhas, imagens, esqueletos, materiais)
 │    └── scene (entidades, componentes, runtime, serialização; sem GPU)
 │         ├── physics (+ os seus componentes)
 │         ├── audio   (+ os seus componentes)
 │         └── graphics (SceneRenderer, envio dos assets para a GPU, MeshRenderer, Luz, Câmera) ← renderer
 │              └── ui (precisa do Renderer2D)
 └── renderer (OpenGL; não sabe o que é cena nem asset de arquivo)
app ← platform, graphics
```

### Passos, do mais barato ao mais caro

1. **`test_layers` com grafo.** O CMake grava as `DEPENDS` de cada camada (por exemplo
   `physics:scene;ui:scene`), e o teste aceita só as camadas alcançáveis a partir delas. Opções
   `MOTOR_WITH_PHYSICS`, `MOTOR_WITH_AUDIO` e `MOTOR_WITH_UI` (ligadas por padrão) tiram a camada da DLL. O
   registro automático já faz o resto, e uma build sem física serve de teste. Não muda a API.
2. **Separar `platform` do `core`.** `Window`, `Input` e `ImGuiLayer` vão para `motor/platform/`, e os
   headers antigos de `motor/core/` continuam lá só repassando para os novos, para não quebrar os scripts.
3. **Componentes no seu módulo.** Os componentes da física e do áudio vão para `physics/` e `audio/`, cada
   módulo com o seu `Components.h`. As cenas continuam abrindo, porque componente desconhecido já é
   preservado (ver `test_hotreload`). É preciso subir o `kScriptApiVersion` e manter os headers antigos
   repassando por uma versão.
4. **Cena sem renderer.** `Render`, `SubmitTo` e `CollectLights` saem de `Scene` para um `SceneRenderer`
   numa camada `graphics` (acima de `scene` e de `renderer`). `MeshRenderer`, Luz e Câmera vão junto. O
   `Raycast` passa a usar a cópia da malha na CPU, e `Environment` vira dado da cena. **Fazer junto com a
   [Fase 24](../arquivo/roadmap/alpha-3.md) (renderer em lote)**, que já vai mudar a montagem dos desenhos.
5. **Assets na CPU separados da GPU.** `assets/` passa a guardar só os dados (malha, imagem, esqueleto), e
   o envio para a GPU (com cache) fica em `graphics`. Os campos `FieldType::Mesh` e `FieldType::Texture` de
   `Component.h` passam a apontar para os tipos de asset na CPU (ou viram `FieldTraits<T>` em `graphics`),
   tirando de vez os tipos do `renderer` de dentro de `scene`. Assim `assets` e `scene` não dependem mais de
   `renderer`, e importar ou exportar um jogo não precisa de OpenGL. **Fazer junto com a
   [Fase 25](../arquivo/roadmap/alpha-3.md)** (dados contínuos), que reorganiza a cena de qualquer jeito.

Cada passo é uma tarefa `refactor/<assunto>`, com build sem avisos e `ctest` inteiro. Os passos 1 e 2 cabem
na alpha 2. Os passos 4 e 5 só compensam junto com as fases da alpha 3 que já mexem nos mesmos arquivos;
feitos antes, seriam refeitos.

## Alternativas

- **Uma DLL por módulo** (`MotorCore.dll`, `MotorPhysics.dll`...): deixa um projeto carregar só os
  módulos que usa e abre caminho para plugins. Custa mais DLLs no pacote e no SDK dos scripts, além de
  cuidado com a ordem de inicialização entre DLLs (os registros têm de ficar no `core`) e com as exportações
  de cada uma. Fica para depois do passo 5, se aparecer a necessidade. Catálogo dos módulos e regras da
  divisão: [Módulos da engine em DLLs separadas](2026-10-18-modulos-em-dlls.md).
- **Módulos do C++20** (`import motor.scene;`): não resolve nenhum dos pontos acima (que são de
  dependência, não de compilação). O suporte junto com MinGW, DLL, cabeçalho pré-compilado e a compilação
  dos scripts pelo editor (Fase 29) ainda é arriscado. Pode ser revisto na alpha 4, pelo tempo de compilação.
- **Deixar como está:** funciona hoje, mas cada fase de desempenho (24, 25) e cada recurso novo aumentaria o
  acoplamento entre cena, assets e renderer, e uma build sem física ou sem GPU continuaria impossível.

## Consequências

- Mais fácil: testar cena e assets sem GPU de verdade, fazer builds menores (sem física ou sem áudio),
  trabalhar em paralelo em módulos sem conflito e, mais tarde, separar DLLs ou aceitar plugins.
- Mais difícil: os passos 3 e 4 mudam headers públicos usados pelos scripts e pelo editor. Isso pede
  `kScriptApiVersion` novo, headers de repasse por uma versão e notas em `docs/scripts-api.md`.
- A seção Estrutura do `CLAUDE.md` e a tabela de áreas mudam a cada passo (camadas `platform` e `graphics`
  novas).
- Rever esta decisão ao planejar as Fases 24 e 25 e, de novo, se um projeto precisar escolher os seus
  módulos.
