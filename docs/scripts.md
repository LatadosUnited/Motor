---
tipo: guia
atualizado: 2026-10-18
---

# Guia de scripts do Motor

Este guia ensina a programar o comportamento do seu jogo no Motor com scripts em C++. Ele começa do zero
(o primeiro script) e termina com um mini jogo completo: um personagem que anda, pega moedas e soma pontos.
Você só precisa saber C++ básico (structs, métodos, `if`, `float`). Não é preciso ler o código da engine.

- Referência de tudo que um script pode chamar: [scripts-api.md](scripts-api.md).
- Todos os exemplos deste guia estão completos em [`docs/examples/`](examples/) e nos modelos do editor
  em [`editor/templates/Scripts/`](../editor/templates/Scripts/). Um teste (`test_script_docs`) compila
  e roda todos eles a cada build: se o código daqui parar de funcionar, o teste falha.

Sumário:

1. [Antes de começar](#1-antes-de-começar)
2. [O primeiro script](#2-o-primeiro-script)
3. [Campos no Inspector](#3-campos-no-inspector)
4. [Ciclo de vida](#4-ciclo-de-vida)
5. [Entrada e tempo](#5-entrada-e-tempo)
6. [Achar, criar e destruir entidades](#6-achar-criar-e-destruir-entidades)
7. [Física](#7-física)
8. [Sistemas do jogo (`GetSystem`)](#8-sistemas-do-jogo-getsystem)
9. [Animação](#9-animação)
10. [Temporizadores, mensagens e sorteio](#10-temporizadores-mensagens-e-sorteio)
11. [Receitas](#11-receitas)
12. [Mini jogo: andar, coletar, pontuar](#12-mini-jogo-andar-coletar-pontuar)
13. [Interface do jogo (HUD e menus)](#13-interface-do-jogo-hud-e-menus)
14. [Som](#14-som)
15. [Navegação (IA)](#15-navegação-ia)
16. [Recarga: o que é preservado](#16-recarga-o-que-é-preservado)
17. [Componente "desconhecido" (script não carregado)](#17-componente-desconhecido-script-não-carregado)
18. [Erros comuns](#18-erros-comuns)

---

## 1. Antes de começar

**O que é um script.** No Motor, tudo na cena é uma **entidade** (um nome, um Transform com posição,
rotação e escala, e uma lista de **componentes**). Componentes dão comportamento à entidade: o Mesh Renderer
desenha uma malha, o Collider dá forma para a física, a Câmera mostra o jogo. Um **script** é um componente
que você escreve em C++: uma `struct` que herda de `motor::Script`.

**Onde ficam.** Cada projeto tem a pasta `Scripts/` (ao lado de `Assets/`). Todo `.cpp` dessa pasta (e das
subpastas) entra no jogo; não há lista de arquivos para manter. O editor compila a pasta numa DLL
(`GameScripts.dll`) e a carrega **sem fechar**:

- ao abrir o projeto;
- quando você salva, cria ou apaga um arquivo da pasta (o editor confere a cada ~1 s);
- pelo menu **Scripts > Recompilar scripts** (F7).

O painel **Scripts** (aba ao lado de Projeto; menu Scripts > Painel Scripts) mostra se compilou, os erros
e avisos (clique num erro para abrir o arquivo **na linha**), o tempo de cada arquivo e a saída completa do
compilador.

**Onde aparece o `Log`.** As mensagens de `Log(...)` dos scripts saem na **janela de console** do editor
(a janela de texto que abre junto com ele), com a hora e o nome da entidade:
`[14:02:31] [info ] [Moeda] pontos: 1 de 3`.

**Play.** Os scripts só rodam no modo Play (botão ▶ no topo do viewport, Ctrl+P ou menu Jogo). Ao parar
(⏹), a cena volta **exatamente** como estava antes do Play: o que os scripts mudaram é descartado. O jogo
só recebe teclado e mouse com o viewport em foco (o Play já dá o foco a ele).

**Editor de código.** Ao abrir o projeto, o editor gera a pasta `.vscode/` com o **VS Code** já
configurado para os scripts (instale a extensão **C/C++** da Microsoft, que o VS Code sugere ao abrir):

- autocompletar e erros na hora, com os headers do Motor e as mesmas opções da compilação (o
  `compile_commands.json` que o editor gera em `.motor/scripts/build/`);
- **Ctrl+Shift+B** compila (o editor aberto já compila sozinho ao salvar: a tarefa é para quando ele está
  fechado);
- **Executar > Motor: anexar ao editor (gdb)** depura os scripts com o editor aberto: pontos de parada no seu
  `.cpp`, Play no editor. Precisa do gdb do MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-gdb`).

Abra pelo menu **Scripts > Abrir no VS Code**. O duplo clique num arquivo do painel Scripts (ou num `.cpp`
do painel Projeto) e o clique num erro abrem o arquivo no VS Code, na linha. Os arquivos gerados começam
com `// Gerado pelo Motor Editor` e são refeitos quando o Motor muda; editou um e quer que fique como está?
Apague essa primeira linha. Sem o VS Code instalado, os arquivos abrem no programa padrão do Windows (aí,
para o autocompletar, aponte os includes para `engine/include` e para o GLM, `build/_deps/glm-src`, da
pasta do Motor).

## 2. O primeiro script

Vamos fazer uma entidade flutuar: subir e descer devagar, como uma boia.

1. Abra (ou crie, em **Arquivo > Novo projeto...**) um projeto.
2. Menu **Scripts > Novo script...** (ou **+ Criar > Novo script...** no painel Projeto), nome `Flutuar`,
   modelo **Vazio**. O editor cria `Scripts/Flutuar.cpp` e o abre no editor de código. (O nome precisa ser
   um identificador C++: letras sem acento, dígitos e `_`, sem começar com dígito.) Os outros modelos já vêm
   com um comportamento pronto: **Comportamento** (um campo e um `Log`), **Que segue** (segue uma entidade
   escolhida no Inspector), **Coletável** (gira e some quando o jogador encosta) e **Gerador** (cria cópias
   de um prefab de tempos em tempos).
3. Troque o conteúdo do arquivo por este
   ([examples/Flutuar.cpp](examples/Flutuar.cpp)):

```cpp
#include <motor/scene/ScriptModule.h>

#include <cmath>

struct Flutuar : motor::Script {
    float altura = 0.5f;      // metros acima e abaixo do ponto de partida
    float velocidade = 2.0f;  // quanto maior, mais rápido

    void OnStart() override {
        m_baseY = GetTransform().position.y;
        Log("começou a flutuar em y = {:.2f}", m_baseY);
    }

    void OnUpdate(float /*dt*/) override {
        const float t = static_cast<float>(Time().elapsed);  // segundos desde o Play
        GetTransform().position.y = m_baseY + altura * std::sin(t * velocidade);
    }

private:
    float m_baseY = 0.0f;  // não registrado: estado interno, não aparece no Inspector nem é salvo
};

MOTOR_SCRIPT(Flutuar) {
    registry.Register<Flutuar>("Flutuar", "Flutuar")
        .Field("altura", "Altura", &Flutuar::altura)
        .Field("velocidade", "Velocidade", &Flutuar::velocidade);
}
```

4. Salve. Em um ou dois segundos o painel Scripts mostra "compilado" (a primeira compilação demora mais).
5. Selecione uma entidade (ex.: crie um cubo em **Entidade > Cubo**). No Inspector, **Adicionar
   componente > Scripts > Flutuar**. Os campos **Altura** e **Velocidade** aparecem.
6. Aperte **Play**. O cubo flutua, e o console mostra `[Cubo] começou a flutuar em y = ...`.
   Pare com ⏹: o cubo volta para onde estava.

O que cada parte faz:

| Parte | Para quê |
|---|---|
| `#include <motor/scene/ScriptModule.h>` | traz tudo de que um script precisa (Script, Scene, Transform, componentes, GLM) |
| `struct Flutuar : motor::Script` | o script. Uma struct comum: campos com valor padrão e métodos |
| `void OnUpdate(float) override` | métodos `On...` que a engine chama no Play. Sempre com `override` (ver [Erros comuns](#18-erros-comuns)) |
| `GetTransform()`, `Time()`, `Log(...)` | a API do script (seções 5 e 6, e a [referência](scripts-api.md)) |
| `MOTOR_SCRIPT(Flutuar) { ... }` | o registro: roda quando a DLL é carregada |
| `Register<Flutuar>("Flutuar", "Flutuar")` | o 1º nome é o **identificador** gravado na cena; o 2º é o que o editor mostra |
| `.Field("altura", "Altura", &Flutuar::altura)` | campo no Inspector: chave no arquivo, rótulo, membro |

O identificador do `Register` é o que liga a cena ao script. Pode renomear a struct à vontade; se renomear o
identificador, as entidades que usavam o nome antigo ficam com um componente "desconhecido" (seção 17) até
o nome voltar.

## 3. Campos no Inspector

Cada `.Field(...)` vira um campo editável no Inspector. Campos registrados são **salvos na cena**, entram no
**desfazer** (Ctrl+Z), são **copiados** ao duplicar a entidade e **sobrevivem à recompilação**. Membros que
não são registrados são estado interno do script: começam com o valor padrão a cada Play e não são salvos.

Tipos de campo suportados:

| Tipo C++ | No Inspector |
|---|---|
| `bool` | caixa de marcar |
| `int`, `float` | número (arrastar ou digitar; slider com `.slider = true`) |
| `glm::vec2`, `glm::vec3`, `glm::vec4` | 2, 3 ou 4 números; `vec3`/`vec4` viram seletor de cor com `.color = true` |
| `std::string` | texto |
| `std::shared_ptr<motor::Mesh>` | malha (Cubo, Esfera, Plano, Cilindro, Cápsula, Cone ou um `.obj` do projeto) |
| `std::shared_ptr<motor::Texture2D>` | textura (`.png`/`.jpg` do projeto) |
| `motor::PrefabRef` | prefab (`.mprefab` do projeto, para `Instantiate`) |
| `motor::EntityRef` | outra entidade da cena: escolha na lista ou arraste da Hierarquia (seção 6) |
| `motor::ColliderShape`, `motor::LightType` | lista (os enums da engine que têm editor) |

Outros tipos (`double`, `unsigned`, enums seus...) não compilam: use um dos acima (ex.: `float` em vez de
`double`, `int` para uma escolha numerada).

O quarto argumento opcional de `Field` são as **opções** (`motor::FieldOptions`), escritas com
inicializadores designados **na ordem em que estão declaradas**: `color`, `slider`, `logarithmic`, `speed`,
`min`, `max`, `format`, `tooltip`, `formerKey`.

| Opção | Efeito |
|---|---|
| `.color = true` | `vec3`/`vec4` como seletor de cor |
| `.slider = true` | `int`/`float` como slider entre `min` e `max` |
| `.logarithmic = true` | slider logarítmico (bom para valores de 0.01 a 100) |
| `.speed = 0.1f` | quanto o valor muda ao arrastar o mouse (padrão 0.05) |
| `.min = 0.0f, .max = 10.0f` | limites; `min == max` (o padrão) = sem limite |
| `.format = "%.1f m/s"` | formato `printf` do número mostrado |
| `.tooltip = "..."` | texto ao passar o mouse sobre o campo |
| `.formerKey = "velocidade"` | nome antigo do campo: renomeou a chave? O valor salvo com o nome antigo continua valendo |

Exemplo com um campo de cada tipo ([examples/ExemploCampos.cpp](examples/ExemploCampos.cpp)):

```cpp
struct ExemploCampos : motor::Script {
    bool ligado = true;
    int vidas = 3;
    float velocidade = 2.5f;
    float volume = 0.8f;
    glm::vec2 tamanho{1.0f, 1.0f};
    glm::vec3 direcao{0.0f, 0.0f, -1.0f};
    glm::vec4 cor{1.0f, 0.8f, 0.2f, 1.0f};
    std::string alvo = "Jogador";
    std::shared_ptr<motor::Mesh> malha;         // vazio = nenhuma escolhida
    std::shared_ptr<motor::Texture2D> textura;  // vazio = nenhuma escolhida
// ...
MOTOR_SCRIPT(ExemploCampos) {
    registry.Register<ExemploCampos>("ExemploCampos", "Exemplo de campos")
        .Field("ligado", "Ligado", &ExemploCampos::ligado)
        .Field("vidas", "Vidas", &ExemploCampos::vidas, {.slider = true, .min = 0.0f, .max = 9.0f})
        .Field("velocidade", "Velocidade", &ExemploCampos::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 50.0f, .format = "%.1f m/s", .tooltip = "Metros por segundo"})
        .Field("volume", "Volume", &ExemploCampos::volume,
               {.slider = true, .logarithmic = true, .min = 0.01f, .max = 1.0f})
        .Field("tamanho", "Tamanho", &ExemploCampos::tamanho)
        .Field("direcao", "Direção", &ExemploCampos::direcao)
        .Field("cor", "Cor", &ExemploCampos::cor, {.color = true})
```

Regras práticas:

- A **chave** (1º texto) é o nome no arquivo da cena. Trocar a chave de um campo faz a cena perder o valor
  salvo dele (volta ao padrão, com um aviso no painel Scripts), a não ser que o `.Field` novo diga o nome
  antigo: `{.formerKey = "velocidade"}`. Trocar o rótulo (2º texto) é inofensivo.
- O valor padrão é o do inicializador do membro (`int vidas = 3;`). Ao adicionar o script pelo Inspector,
  é ele que aparece.
- Durante o Play o Inspector mostra os valores **ao vivo** (e você pode mexer neles); ao parar, voltam.
- A ordem dos `.Field` é a ordem no Inspector.

**Registro curto.** Sem o rótulo, `.Field("duracao", &Lampada::duracao)` mostra o nome do campo no
Inspector ("duracao" vira "Duracao"; "quemAperta", "Quem aperta"). E quando nenhum campo precisa de rótulo
nem de opções, o bloco inteiro cabe numa linha com `MOTOR_FIELDS(Struct, campo1, campo2, ...)`: registra o
script com o nome da struct e os campos na ordem. Os dois jeitos no
[examples/Interruptor.cpp](examples/Interruptor.cpp):

```cpp
MOTOR_SCRIPT(Lampada) {
    registry.Register<Lampada>("Lampada", "Lâmpada")
        .Field("corLigada", "Cor ligada", &Lampada::corLigada, {.color = true})
        .Field("duracao", &Lampada::duracao, {.speed = 0.1f, .min = 0.1f, .max = 60.0f, .format = "%.1f s"});
}

// Registro curto: o rótulo vem do nome do campo ("quemAperta" aparece como "Quem aperta").
MOTOR_FIELDS(Interruptor, lampada, quemAperta)
```

## 4. Ciclo de vida

A engine chama estes métodos (todos opcionais; escreva só os que usar):

| Método | Quando |
|---|---|
| `OnStart()` | uma vez, antes do primeiro `OnUpdate`. Entidade inativa só começa quando fica ativa; entidade criada durante o jogo começa no frame seguinte |
| `OnUpdate(float dt)` | todo frame. `dt` = segundos desde o frame anterior |
| `OnFixedUpdate(float dt)` | em passos fixos de 1/60 s (0, 1 ou vários por frame). Use para física |
| `OnCollisionEnter(const motor::Collision&)`, `OnCollisionExit(motor::EntityId)` | início e fim de um contato físico (seção 7) |
| `OnTriggerEnter(motor::EntityId)`, `OnTriggerExit(motor::EntityId)` | alguém entrou/saiu de um trigger (seção 7) |
| `OnDestroy()` | ao parar o Play, ou quando a entidade é destruída. Só para scripts que já tiveram `OnStart` |

Ordem dentro de um passo do jogo (a física é um "sistema", seção 8):

```text
Play:   OnStart dos sistemas (física) -> OnStart dos scripts
Frame:  [passo fixo, 0 ou mais vezes]
            OnStart dos scripts novos -> física avança -> OnCollision*/OnTrigger* -> OnFixedUpdate dos scripts
            -> destruições pedidas
        OnStart dos scripts novos -> OnUpdate dos scripts -> destruições pedidas
Parar:  OnDestroy dos scripts -> a física para -> a cena volta ao estado de antes do Play
```

Para ver na prática, ponha o [examples/CicloDeVida.cpp](examples/CicloDeVida.cpp) numa entidade e acompanhe
o console:

```cpp
    void OnStart() override {
        Log("OnStart: entidade {} começou (frame {})", GetId(), Time().frame);
    }

    void OnUpdate(float dt) override {
        // Todo frame. Para não encher o log, escreve uma vez a cada 60 frames.
        if (Time().frame % 60 == 0) {
            Log("OnUpdate: dt = {:.4f} s, {:.1f} s de jogo, {} passos fixos", dt, Time().elapsed, passosFixos);
        }
    }

    void OnFixedUpdate(float /*dt*/) override {
        ++passosFixos;  // 60 vezes por segundo, não importa o FPS
    }
```

Coisas que vale saber:

- O script que você vê no editor **não** é o que roda: o Play copia a cena, e os scripts da cópia recebem as
  chamadas. Por isso mudar algo no Play nunca estraga a cena salva.
- Na **pausa** nada é chamado; `Time().elapsed` não conta o tempo pausado.
- O `Log` aceita o formato do `std::format`: `Log("vidas = {}, posição = {:.2f}", vidas, x)`. Cada `{}`
  precisa de um valor.

## 5. Entrada e tempo

Dentro do script:

| Chamada | Devolve |
|---|---|
| `GetAxis("Horizontal")` | a ação do projeto de -1 a 1: A/D, setas, analógico esquerdo ou direcional (veja abaixo) |
| `IsActionDown("Correr")`, `WasActionPressed("Pular")` | a ação apertada (Shift ou o analógico apertado; Espaço ou o botão A) |
| `IsKeyDown(motor::Key::W)` | `true` enquanto a tecla está apertada |
| `WasKeyPressed(motor::Key::Space)` | `true` só no frame em que a tecla desceu |
| `IsMouseDown(motor::MouseButton::Left)` | `true` enquanto o botão está apertado (`Left`, `Right`, `Middle`) |
| `MouseDelta()` | quanto o mouse andou neste frame, em pixels (`glm::vec2`) |
| `SetCursorMode(motor::CursorMode::Locked)` | prende e esconde o cursor (câmera em primeira pessoa); `Hidden` só esconde, `Normal` devolve (menus). No editor, Esc solta e um clique no jogo prende de novo |
| `Time().delta` | segundos desde o último `OnUpdate` (o mesmo `dt`) |
| `Time().fixedDelta` | o passo do `OnFixedUpdate` (1/60 s) |
| `Time().elapsed` | segundos de jogo desde o Play (sem as pausas), em `double` |
| `Time().frame` | quantos `OnUpdate` já rodaram |

**Ações e controle.** Em vez de perguntar por uma tecla, pergunte pela ação: o mesmo script funciona no
teclado e no controle (gamepad), e quem joga pode trocar as teclas. As ações ficam no projeto, em **Arquivo >
Controles** (o padrão já traz `Horizontal`, `Vertical`, `Olhar X`, `Olhar Y`, `Pular`, `Correr`, `Interagir` e
`Pausa`, com teclado e controle). Na janela, **Adicionar (aperte)** liga a próxima tecla ou botão apertado, e
uma barra mostra o valor de cada ação na hora, para conferir sem dar Play. Uma entrada marcada como
**Negativo** empurra o eixo para o outro lado (a tecla A em `Horizontal`).

| Chamada | Devolve |
|---|---|
| `GetAxis("Vertical")` | -1 a 1: a soma das entradas (W = +1, S = -1, analógico para a frente = até +1) |
| `IsActionDown("Pular")`, `WasActionPressed`, `WasActionReleased` | apertada (valor acima de 0,5), apertou ou soltou neste frame |
| `IsGamepadConnected()` | há um controle (o primeiro conectado; Xbox, PlayStation e outros, no padrão Xbox) |
| `IsGamepadButtonDown(motor::GamepadButton::A)`, `WasGamepadButtonPressed` | um botão do controle direto: `A`, `B`, `X`, `Y`, `LeftBumper`, `Start`, `DpadUp`... |
| `GamepadAxisValue(motor::GamepadAxis::LeftX)` | analógico de -1 a 1 (Y para cima é positivo), com zona morta; gatilhos (`LeftTrigger`, `RightTrigger`) de 0 a 1 |

Ação com nome errado devolve zero e avisa uma vez no log. No editor, a ferramenta MCP `game_gamepad` simula um
controle (para testar sem ter um).

Teclas: `motor::Key::A` a `Z`, `D0` a `D9` (números), `Space`, `Enter`, `Escape`, `Tab`, `Backspace`,
`Up`/`Down`/`Left`/`Right` (setas), `LeftShift`, `LeftControl`, `LeftAlt` (e os `Right...`), `F1` a `F12`
e outras (lista completa na [referência](scripts-api.md#teclas)).

Quando o jogo não tem o foco (você está digitando num campo do editor, por exemplo), essas chamadas
devolvem "solto"/zero: o jogo não reage ao que você digita no editor.

Regras:

- **Multiplique por `dt`** tudo que acontece "por segundo" no `OnUpdate`: `posição += velocidade * dt`. Assim
  o jogo anda igual a 30 ou 300 FPS.
- **`WasKeyPressed` no `OnUpdate`**, não no `OnFixedUpdate`: o passo fixo pode rodar zero vezes num frame
  (e perder o aperto) ou duas (e contar duas vezes). Se a ação é de física (pular), guarde um pedido no
  `OnUpdate` e use no `OnFixedUpdate` (o modelo Jogador e a BolaFisica fazem assim).
- Para "no máximo uma vez a cada N segundos", compare com `Time().elapsed`.

O [examples/Disparador.cpp](examples/Disparador.cpp) junta as duas coisas: atira com F ou com o botão
esquerdo, no máximo um tiro a cada `intervalo` segundos:

```cpp
    void OnUpdate(float /*dt*/) override {
        const bool atirar = WasKeyPressed(motor::Key::F) || IsMouseDown(motor::MouseButton::Left);
        const double agora = Time().elapsed;
        if (!atirar || agora < m_proximoTiro) return;
        m_proximoTiro = agora + intervalo;
```

## 6. Achar, criar e destruir entidades

**A própria entidade.** `GetEntity()` (a entidade: `name`, `active`, `transform`, componentes), `GetId()`
(o número dela), `GetTransform()` (atalho para `GetEntity().transform`) e `GetScene()` (a cena inteira).

**O Transform é relativo ao pai.** `GetTransform().position` é a posição local. Numa entidade na raiz da
hierarquia, local = mundo. Para a posição no mundo de qualquer entidade use
`glm::vec3(GetScene().WorldMatrix(id)[3])`. A "frente" de entidades e câmeras é o **-Z** local.

**Outras entidades: pelo Inspector.** O jeito mais simples é um campo `motor::EntityRef`: no Inspector ele
vira uma lista com as entidades da cena (ou arraste a entidade da Hierarquia para o campo), e no código
`GetEntity(campo)` devolve a entidade, ou `nullptr` se ela não existe mais. Ele também serve onde se espera um
id: `Destroy(alvo)`, `Send(alvo, ...)`, `GetComponent<T>(alvo)`. Duplicar a entidade ou fazer um prefab dela
ajusta as referências que apontam para dentro da parte copiada (o inimigo duplicado aponta para a própria
arma, não para a do original). O interruptor do [examples/Interruptor.cpp](examples/Interruptor.cpp) liga a
lâmpada escolhida no Inspector:

```cpp
struct Interruptor : motor::Script {
    motor::EntityRef lampada;            // arraste a lâmpada da Hierarquia para este campo
    std::string quemAperta = "Jogador";  // nome de quem liga ao entrar na área

    void OnTriggerEnter(motor::EntityId outro) override {
        const motor::Entity* e = GetEntity(outro);
        if (e && e->name == quemAperta) Send(lampada, "ligar");
    }
};
```

**Outras entidades: pelo nome ou pelo id.** `FindEntity("Nome")` devolve a primeira entidade com esse nome,
ou `nullptr`. `GetEntity(id)` (ou `GetScene().Find(id)`) acha pelo número. Guarde o **id** (`motor::EntityId`), nunca o ponteiro: entidades
podem ser destruídas a qualquer momento, e `GetScene().Find(id)` devolve `nullptr` se a entidade não existe
mais. O modelo Seguir faz exatamente isso:

```cpp
    void OnStart() override {
        const motor::Entity* target = FindEntity(alvo);
        m_alvo = target ? target->id : motor::kNullEntity;
        if (!target) Log("Seguir: entidade \"{}\" não encontrada", alvo);
    }

    void OnUpdate(float dt) override {
        motor::Scene& scene = GetScene();
        if (!scene.Find(m_alvo)) return;  // alvo destruído
```

**Componentes.** Numa entidade: `Get<T>()` (ponteiro, ou `nullptr` se não tiver), `Has<T>()`, `Add<T>()`
(adiciona, ou devolve o que já existe) e `Remove<T>()`. Funciona com os componentes da engine e com os seus
scripts. Atalhos no script: `GetComponent<T>()` (desta entidade) e `GetComponent<T>(id)` (de outra, pelo id
ou por um `EntityRef`), que devolvem `nullptr` se não houver. A lâmpada guarda a cor original no começo:

```cpp
    void OnStart() override {
        if (auto* mr = GetComponent<motor::MeshRenderer>()) m_corDesligada = mr->material.color;
    }
```

Os componentes da engine:

| Componente (no editor) | Tipo C++ | Campos principais |
|---|---|---|
| Mesh Renderer | `motor::MeshRenderer` | `mesh`, `sharedMaterial` (asset `.mmat`) e `material` (o próprio): `color` (vec4), `albedo` (textura), `metallic`, `roughness`, `specular`, `emissive`, `blend` (`motor::BlendMode::Opaque`/`Cutout`/`Transparent`), `alphaCutoff`, `doubleSided`... (PBR, ver a referência) |
| Câmera | `motor::CameraComponent` | `fov` (graus), `nearPlane`, `farPlane`, `main` |
| Luz | `motor::LightComponent` | `type` (`motor::LightType::Directional`/`Point`/`Spot`), `color`, `intensity`, `range`, `innerAngle`, `outerAngle`, `castShadows` |
| Collider | `motor::ColliderComponent` | `shape` (`motor::ColliderShape::Box`/`Sphere`/`Capsule`/`Cylinder`/`Mesh`), `size`, `radius`, `height`, `center`, `isTrigger`, `friction`, `restitution`, `layer` |
| Rigidbody | `motor::RigidbodyComponent` | `mass`, `useGravity`, `isKinematic`, `linearDamping`, `angularDamping` |
| Controlador de personagem | `motor::CharacterControllerComponent` | `height`, `radius`, `maxSlope`, `stepHeight`, `mass`, `useGravity`, `layer` |

Exemplo: trocar a cor do próprio objeto (do [examples/ExemploCampos.cpp](examples/ExemploCampos.cpp)):

```cpp
        if (auto* renderer = GetEntity().Get<motor::MeshRenderer>()) {
            renderer->material.color = cor;
```

O Mesh Renderer tem dois materiais. `material` é o próprio do objeto e só vale quando o campo "Material"
está vazio. `sharedMaterial` é o asset `.mmat`: mudar `renderer->sharedMaterial->color` muda **todos** os
objetos que usam aquele material. Para mudar só este objeto, solte o asset antes
(`renderer->sharedMaterial = nullptr;`) e mexa em `material`. `renderer->Resolved()` devolve o que está
valendo.

**Criar.** Há dois jeitos de criar entidades durante o Play:

- `Instantiate(prefab, posicao)` (ou `Instantiate(prefab)`) instancia um prefab (`.mprefab`) escolhido num
  campo `motor::PrefabRef` do Inspector e devolve a entidade raiz (`motor::Entity*`, ou `nullptr` se o campo
  estiver vazio).
- `CreateEntity("Nome")` cria uma entidade na raiz (ou `CreateEntity("Nome", idDoPai)` como filha), vazia
  (sem malha), para montar os componentes pelo código.

Os scripts das entidades criadas começam no frame seguinte. O [examples/Disparador.cpp](examples/Disparador.cpp)
usa o prefab quando um foi escolhido no Inspector, ou cria um projétil com a malha do próprio objeto:

```cpp
        // Use o ponteiro/referência só aqui; para lembrar da entidade depois, guarde o id (tiro.id).
        motor::Entity* instanciado = !prefab.IsEmpty() ? Instantiate(prefab, posicao + frente) : nullptr;
        motor::Entity& tiro = instanciado ? *instanciado : CreateEntity("Projétil");
        if (!instanciado) {
            tiro.transform.position = posicao + frente;
            tiro.transform.scale = glm::vec3(tamanho);
            if (const auto* meu = GetEntity().Get<motor::MeshRenderer>()) {
                tiro.Add<motor::MeshRenderer>() = *meu;  // copia malha e material
            }
        }
        Projetil& projetil = tiro.Add<Projetil>();  // scripts também são componentes
        projetil.velocidade = frente * velocidade;
```

**Usar um script em outro.** Para o Disparador conhecer o `Projetil`, a struct fica num header
(`Scripts/Projetil.h`) incluído pelos dois arquivos; o `MOTOR_SCRIPT(Projetil)` fica num só `.cpp`
([examples/Projetil.cpp](examples/Projetil.cpp)). Com o header incluído, `entidade->Get<Projetil>()`
funciona como com qualquer componente.

**Destruir.** `Destroy(id)` destrói uma entidade (e os filhos); `DestroySelf()` destrói a própria. A
destruição acontece no **fim do passo**: até lá a entidade continua existindo, e o `OnDestroy` dos scripts
dela é chamado antes. O projétil ([examples/Projetil.h](examples/Projetil.h)) some sozinho:

```cpp
struct Projetil : motor::Script {
    glm::vec3 velocidade{0.0f, 0.0f, -10.0f};  // metros por segundo, no mundo
    float vida = 2.0f;                         // segundos até sumir

    void OnUpdate(float dt) override {
        GetTransform().position += velocidade * dt;
        vida -= dt;
        if (vida <= 0.0f) DestroySelf();  // a entidade some no fim do frame (OnDestroy antes)
    }
};
```

Não remova o componente do próprio script durante o Play (`Remove<EsteScript>()`): destrua a entidade ou use
um campo `bool` para desligar o comportamento.

## 7. Física

A física (Jolt Physics) é montada com componentes no editor; o script só reage e dá ordens.

| Componentes na entidade | Resultado |
|---|---|
| **Collider** | corpo estático: chão, paredes, obstáculos |
| **Collider** + **Rigidbody** | corpo que cai, rola e é empurrado. Com **Cinemático**: não cai, segue o Transform (que o script mexe) e empurra os outros |
| **Collider** com **Trigger** marcado | área invisível que só avisa quem entra e sai; não empurra |
| **Controlador de personagem** (sem Collider) | o jogador ou um inimigo: anda pelo script, sobe degraus e rampas, não atravessa o chão |

- As medidas do Collider são locais e multiplicadas pela escala da entidade: um Cubo de escala `4 x 1 x 4`
  com um Collider caixa padrão (`1 x 1 x 1`) já tem a forma certa. O Controlador de personagem **não** usa a
  escala: a altura e o raio são os do componente, e a origem da entidade é o centro da cápsula.
- O Plano embutido não tem espessura: para usá-lo como chão, Collider caixa com tamanho `1 x 0.1 x 1` e
  centro `0 -0.05 0`. Ou use um Cubo achatado.
- No viewport, o botão **Colliders** mostra as formas (verde), triggers (azul) e personagens: só do objeto
  selecionado (padrão), de todos ou de nenhum; um clique alterna.

**Eventos.** Os dois lados recebem. Colisão precisa de Collider nas duas entidades (ou personagem) e de um
Rigidbody ou personagem em pelo menos uma; um personagem não gera eventos com o chão estático (para saber se
está no chão, `IsGrounded`).

```cpp
    void OnCollisionEnter(const motor::Collision& c) override {
        if (c.relativeSpeed >= pancadaForte) {
            Log("pancada em {} a {:.1f} m/s (velocidade agora: {:.1f} m/s)", c.other, c.relativeSpeed,
                glm::length(GetSystem<motor::PhysicsSystem>()->GetVelocity(GetId())));
        }
    }
```

`motor::Collision` traz `other` (a outra entidade), `point` (ponto de contato), `normal` (aponta do outro
para este) e `relativeSpeed` (m/s). `OnCollisionExit`, `OnTriggerEnter` e `OnTriggerExit` recebem só o id da
outra entidade. Para saber **quem** entrou, olhe a entidade (o nome, ou se tem um componente):

```cpp
    bool EhJogador(motor::EntityId id) {
        const motor::Entity* entidade = GetScene().Find(id);
        return entidade && entidade->Has<motor::CharacterControllerComponent>();
    }
```

**Dar ordens à física.** Pegue o sistema de física com `GetSystem<motor::PhysicsSystem>()` (seção 8;
inclua `<motor/physics/PhysicsSystem.h>`) e chame, de preferência no `OnFixedUpdate`:

| Para | Chamada |
|---|---|
| Rigidbody: empurrar aos poucos | `AddForce(id, força)` (newtons, só no próximo passo: chame a cada passo) |
| Rigidbody: empurrão instantâneo | `AddImpulse(id, impulso)` |
| Rigidbody: velocidade | `SetVelocity(id, v)`, `GetVelocity(id)` |
| Personagem: andar | `MoveCharacter(id, velocidade)` (m/s; vale até a próxima chamada; zero para parar) |
| Personagem: pular | `Jump(id, velocidadeParaCima)` (só se estiver no chão) |
| Personagem: estado | `IsGrounded(id)`, `GetCharacterVelocity(id)` |
| Personagem: lançar | `SetCharacterVelocity(id, v)`: a velocidade inteira do próximo passo (trampolim, empurrão); sem gravidade: campo **Usa gravidade** do Controlador |
| Área | `OverlapSphere(centro, raio)`, `OverlapBox(centro, metadeDoTamanho)`: quem está ali (triggers também; útil para coletas e explosões) |
| Raio grosso | `SphereCast(origem, raio, direção, distânciaMáxima)`: como o `Raycast`, com uma esfera (não passa por frestas menores que ela) |
| Raio | `Raycast(origem, direção, distânciaMáxima, idIgnorado, máscara)`: o primeiro corpo atingido (triggers não contam; `máscara` = camadas que o raio vê, `LayerMask({"Parede"})`) |

A bola do [examples/BolaFisica.cpp](examples/BolaFisica.cpp) usa força, raio e impulso:

```cpp
        fisica->AddForce(GetId(), empurrao);  // vale só para o próximo passo: chame de novo a cada passo

        if (querPular) {
            querPular = false;
            const glm::vec3 centro = GetTransform().position;  // a bola está na raiz da hierarquia
            const float raio = 0.5f * GetTransform().scale.y;
            if (fisica->Raycast(centro, glm::vec3(0.0f, -1.0f, 0.0f), raio + 0.1f, GetId())) {
                fisica->AddImpulse(GetId(), glm::vec3(0.0f, impulso, 0.0f));
            }
        }
```

O `Raycast` devolve um `std::optional<motor::PhysicsRaycastHit>`: teste com `if (hit)` e leia
`hit->entity`, `hit->point`, `hit->normal` e `hit->distance`.

**Camadas de colisão.** Em **Arquivo > Camadas de colisão** o projeto dá nome a até 16 camadas e marca quem
se choca com quem (vale para todas as cenas, a partir do próximo Play). Cada Collider e Controlador de
personagem escolhe a sua no campo **Camada** do Inspector. Num raio, a máscara diz o que ele vê: a câmera
orbital que não deve bater no próprio jogador olha só as paredes, com
`fisica->Raycast(alvo, paraTras, distancia, GetId(), fisica->LayerMask({"Parede"}))`.

- Mudar o Transform de um corpo pelo script **teleporta** o corpo (inclusive o personagem). Para mover um
  objeto que deve empurrar os outros (porta, plataforma), use Rigidbody **Cinemático**.
- Entidade criada neste passo só ganha corpo no próximo passo fixo: as chamadas acima devolvem `false`/zero
  até lá.

## 8. Sistemas do jogo (`GetSystem`)

Partes da engine que rodam a cada passo do jogo (a física, a interface do jogo, o áudio, a navegação) são **sistemas**. Um
script acha o sistema com `GetSystem<Tipo>()`:

```cpp
    void OnFixedUpdate(float) override {
        // A física é um sistema do jogo: existe só no Play (fora dele, nullptr).
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;
```

- Devolve `nullptr` fora do Play ou se o sistema não existe. Sempre teste.
- Os sistemas já existem no `OnStart` e ainda existem no `OnDestroy`.
- Cada Play cria sistemas novos: não guarde o ponteiro de um Play para outro (pegar a cada uso é barato).
- Sistemas são da engine: um script usa, não cria. (Criar um sistema é trabalho de engine, descrito em
  `engine/include/motor/scene/RuntimeSystem.h`.)

## 9. Animação

Personagens animados vêm de modelos **glTF** (`.gltf` ou `.glb`, o formato que o Blender e a maioria dos
programas exportam) com esqueleto e clipes de animação. A engine traz um de exemplo:
`engine/assets/models/Robo.glb`, com os clipes `Parado`, `Andando` e `Pulando`.

1. Copie o `.glb` para a pasta `Assets/` do projeto. O editor importa sozinho: aparecem `Robo.mprefab` (o
   modelo pronto para a cena) e a pasta `Robo_materiais/` (materiais e texturas do modelo).
2. Arraste o `.glb` (ou o `.mprefab`) para a cena. A raiz do modelo tem o componente **Animator**: Modelo,
   Clipe inicial (toca sozinho no Play), Repetir e Velocidade.
3. Jogador animado: na entidade com **Controlador de personagem** e **Jogador (física)**, arraste o modelo
   para dentro dela na Hierarquia (vira filho) e ponha a posição do modelo em `0 -0.9 0` (a origem do
   controlador é o meio da cápsula; a do robô, os pés). Tire o Mesh Renderer da entidade do jogador.
4. Primeira pessoa: para a câmera não ver o corpo por dentro, mude a **Sombra** dos Mesh Renderers do modelo
   para **Só sombra**. O corpo some da imagem e continua fazendo sombra, e o Animator segue tocando. Desativar a
   entidade também esconde o corpo, mas tira a sombra e para a animação. No script:
   `mr->shadows = motor::ShadowCasting::ShadowsOnly`.

No script, `GetAnimator()` acha o Animator da própria entidade ou do primeiro filho que tiver um. O modelo
Jogador troca de animação conforme anda ou pula:

```cpp
        // Animação: CrossFade com o clipe que já está tocando não faz nada, então dá para pedir a cada passo.
        if (auto* anim = GetAnimator()) {
            const bool noAr = pulou || !fisica->IsGrounded(GetId());
            const std::string& clipe = noAr ? animPulando : (moving ? animAndando : animParado);
            if (!clipe.empty()) anim->CrossFade(clipe, transicao, !noAr);  // o pulo não repete: segura a pose
            anim->speed = moving && running && !noAr ? corrida : 1.0f;
        }
```

- `CrossFade(nome, segundos, repetir)` troca misturando com o clipe atual (sem "tranco"); `Play(nome)` troca
  na hora. Pedir o clipe que já está tocando continua de onde está: dá para chamar a cada passo.
- Clipe sem repetir (`repetir = false`) para no último quadro; `IsFinished()` diz quando chegou lá.
- `speed` acelera (`2`), desacelera (`0.5`) ou congela (`0`) a animação.
- A animação avança a cada frame depois do `OnUpdate` dos scripts (o pedido feito no frame já vale nele).
  Fora do Play o modelo fica na pose de repouso.
- Nomes dos clipes: passe o mouse sobre o campo Modelo do Animator, ou `ClipNames()`.

## 10. Temporizadores, mensagens e sorteio

**Temporizadores.** `After(segundos, função)` chama a função uma vez, daqui a tantos segundos; `Every(segundos,
função)` chama a cada tantos segundos. A função costuma ser uma lambda que captura `this` (`[this] { ... }`)
para usar os membros do script. Os dois devolvem um número para `CancelTimer(numero)`; `CancelTimers()`
cancela todos os do script. Eles contam o tempo do jogo (param na pausa), rodam antes do `OnUpdate` daquele
frame e somem sozinhos quando o script ou a entidade somem: não há o que limpar no `OnDestroy`.

**Mensagens.** `Send(entidade, "texto")` chama `OnMessage` em todos os scripts daquela entidade, na hora;
`Broadcast("texto")` chama em todas as entidades ativas. Um número pode ir junto: `Send(alvo, "dano", 10.0f)`.
Quem recebe olha `mensagem.text`, `mensagem.value` e `mensagem.from` (quem mandou). Assim um script avisa
outro sem conhecer a struct dele (sem header compartilhado, ao contrário do `Placar` da seção 11). A lâmpada
do interruptor acende ao receber "ligar" e apaga sozinha com um `After`:

```cpp
    void OnMessage(const motor::Message& mensagem) override {
        if (mensagem.text != "ligar") return;
        Acender(true);
        CancelTimers();  // ligou de novo antes de apagar: recomeça a contagem
        After(duracao, [this] { Acender(false); });
    }
```

**Sorteio.** `Random(min, max)` sorteia um `float` entre os dois; `RandomInt(min, max)` um inteiro de `min`
a `max` (os dois inclusive: `RandomInt(1, 6)` é um dado); `Chance(0.1f)` é verdadeiro 10% das vezes. Cada Play
sorteia diferente; `SetRandomSeed(42)` repete sempre a mesma sequência (bom para testar). A chuva do
[examples/Chuva.cpp](examples/Chuva.cpp) junta os três: cria uma gota a cada `intervalo`, num lugar sorteado,
e cada gota some sozinha depois de `vida` segundos:

```cpp
    void OnStart() override {
        Every(intervalo, [this] { CriarGota(); });
    }

    void CriarGota() {
        const glm::vec3 centro = GetTransform().position;
        motor::Entity& gota = CreateEntity("Gota");
        gota.transform.position = centro + glm::vec3(Random(-raio, raio), 0.0f, Random(-raio, raio));
        const float tamanho = Chance(chanceGrande) ? 0.6f : 0.1f * static_cast<float>(RandomInt(1, 3));
        gota.transform.scale = glm::vec3(tamanho);
        ++criadas;

        // Guarde o id (não o ponteiro): o temporizador roda depois, e a gota pode já ter sido destruída.
        const motor::EntityId id = gota.id;
        After(vida, [this, id] { Destroy(id); });
    }
```

O modelo **Gerador** (Scripts > Novo script...) faz o mesmo com um prefab escolhido no Inspector.

## 11. Receitas

Cada receita é um arquivo completo, pronto para copiar para `Scripts/`. Os modelos Jogador e Seguir também
vêm no editor: **Scripts > Adicionar exemplo**.

### Jogador

[editor/templates/Scripts/Jogador.cpp](../editor/templates/Scripts/Jogador.cpp): anda com WASD ou o analógico
(Shift ou o analógico apertado corre), pula com Espaço ou o botão A e vira para onde anda (ações Horizontal,
Vertical, Pular e Correr). Na entidade: **Controlador de personagem** + **Jogador (física)**. O
chão precisa de Collider. W anda para -Z (a "frente" do mundo; a câmera do Seguir fica atrás, em +Z). Com um
modelo animado como filho, troca entre parado, andando e pulando ([Animação](#9-animação)).

```cpp
    void OnFixedUpdate(float) override {
        // A física é um sistema do jogo: existe só no Play (fora dele, nullptr).
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;

        // Vertical para cima (W, analógico para a frente) = -Z.
        glm::vec3 dir{GetAxis("Horizontal"), 0.0f, -GetAxis("Vertical")};
        const bool moving = glm::length(dir) > 1e-3f;
        // Na diagonal do teclado não anda mais rápido; o analógico meio inclinado anda mais devagar.
        if (glm::length(dir) > 1.0f) dir = glm::normalize(dir);

        const bool running = IsActionDown("Correr");
        // MoveCharacter vale até a próxima chamada: chamar a cada passo (com zero, para).
        fisica->MoveCharacter(GetId(), dir * velocidade * (running ? corrida : 1.0f));
```

Sem física, o modelo [MoverWASD.cpp](../editor/templates/Scripts/MoverWASD.cpp) mexe direto no Transform (atravessa
paredes). E o [Girar.cpp](../editor/templates/Scripts/Girar.cpp) gira um objeto sem parar.

### Câmera que segue

[editor/templates/Scripts/Seguir.cpp](../editor/templates/Scripts/Seguir.cpp): numa entidade com
**Câmera** (na raiz da hierarquia), segue a entidade de nome **Alvo** mantendo o **Deslocamento** e olhando
para ela. A suavidade usa uma aproximação exponencial, que fica igual em qualquer FPS:

```cpp
        const glm::vec3 target{scene.WorldMatrix(m_alvo)[3]};
        motor::Transform& t = GetTransform();
        const glm::vec3 desired = target + deslocamento;
        // Aproximação exponencial: independe do FPS.
        const float k = suavidade > 0.0f ? 1.0f - std::exp(-suavidade * dt) : 1.0f;
        t.position += (desired - t.position) * k;
```

### Porta com trigger

[examples/Porta.cpp](examples/Porta.cpp): a porta sobe quando o jogador entra numa área e desce quando ele
sai. Monte duas entidades:

- **Porta**: Cubo com a forma da porta, **Collider** e **Rigidbody** com **Cinemático** marcado.
- **Sensor**: entidade vazia na frente da porta com **Collider** (marque **Trigger** e aumente o tamanho, ex.
  `3 x 2 x 3`) e o script **Porta com sensor** (campo Porta = `Porta`).

O sensor conta quem está dentro e a porta anda até a altura certa, sem passar dela:

```cpp
    void OnTriggerEnter(motor::EntityId outro) override {
        if (EhJogador(outro)) ++m_dentro;
    }

    void OnTriggerExit(motor::EntityId outro) override {
        if (EhJogador(outro) && m_dentro > 0) --m_dentro;
    }

    void OnUpdate(float dt) override {
        motor::Entity* entidade = GetScene().Find(m_porta);  // pode ter sido destruída
        if (!entidade) return;
```

### Inimigo que persegue

[examples/Inimigo.cpp](examples/Inimigo.cpp): na entidade do inimigo, **Controlador de personagem** + **Inimigo
(persegue)**. Ele anda até o **Alvo** quando o enxerga (um raio sem nada no meio) e, se chegar a menos de
**Alcance**, manda o alvo para o **Recomeço**:

```cpp
        // Enxerga se o primeiro corpo na direção do alvo é o próprio alvo (o raio ignora o inimigo).
        const auto hit = fisica->Raycast(eu, ele - eu, visao, GetId());
        if (!hit || hit->entity != m_alvo) {
            fisica->MoveCharacter(GetId(), glm::vec3(0.0f));  // parado até ver de novo
            return;
        }

        direcao /= distancia;
        fisica->MoveCharacter(GetId(), direcao * velocidade);
```

Esconda-se atrás de uma parede (Cubo com Collider) e ele para.

### Coletável com pontuação

Três arquivos: [examples/Placar.h](examples/Placar.h) (o placar, usado pelos outros scripts),
[examples/Placar.cpp](examples/Placar.cpp) (o registro dele) e [examples/Moeda.cpp](examples/Moeda.cpp).
O placar guarda os pontos e avisa quando acabam as moedas:

```cpp
struct Placar : motor::Script {
    int pontos = 0;
    std::string mensagemFinal = "Você pegou todas as moedas!";

    int total = 0;  // pontos possíveis: cada moeda soma o seu valor aqui no OnStart dela

    void Somar(int valor) {
        pontos += valor;
        Log("pontos: {} de {}", pontos, total);
        if (total > 0 && pontos >= total) Log("{}", mensagemFinal);
    }
};

// Acha o placar da cena (a entidade "Placar"), ou nullptr. Use de dentro de outro script:
//   if (Placar* placar = AcharPlacar(GetScene())) placar->Somar(1);
inline Placar* AcharPlacar(motor::Scene& scene) {
    motor::Entity* entidade = scene.FindByName("Placar");
    return entidade ? entidade->Get<Placar>() : nullptr;
}
```

A moeda gira e, quando o jogador encosta, soma e some:

```cpp
    void OnTriggerEnter(motor::EntityId outro) override {
        if (m_coletada || !EhJogador(outro)) return;
        m_coletada = true;  // DestroySelf só acontece no fim do passo: não conta duas vezes
        if (Placar* placar = AcharPlacar(GetScene())) placar->Somar(valor);
        DestroySelf();
    }
```

Os pontos aparecem no console e no Inspector (selecione a entidade Placar durante o Play); para mostrá-los
na tela, veja a [seção 13](#13-interface-do-jogo-hud-e-menus).

## 12. Mini jogo: andar, coletar, pontuar

Juntando as receitas. Copie para a pasta `Scripts/` do projeto: `Placar.h`, `Placar.cpp` e `Moeda.cpp` (de
`docs/examples/`), e adicione os modelos **Jogador** e **Seguir** em **Scripts > Adicionar exemplo**. Espere
o painel Scripts mostrar que compilou. Depois, numa cena nova (**Arquivo > Nova cena**):

1. **Chão**: **Entidade > Cubo**, nome `Chão`, posição `0 -0.5 0`, escala `20 1 20`. **Adicionar componente
   > Collider** (caixa, tamanho padrão).
2. **Jogador**: **Entidade > Cubo**, nome `Jogador` (exatamente assim: é o alvo padrão do Seguir), posição
   `0 1 0`, escala `0.6 1.8 0.6`. Adicione **Controlador de personagem** e **Scripts > Jogador (física)**.
3. **Moedas**: **Entidade > Esfera**, nome `Moeda`, posição `3 1 0`, escala `0.5 0.5 0.5`; no Mesh Renderer,
   cor amarela. Adicione **Collider** (marque **Trigger**) e **Scripts > Moeda (coletável)**. Duplique
   (Ctrl+D) e espalhe umas cinco moedas pelo chão (sempre em `y = 1`).
4. **Placar**: **Entidade > Entidade vazia**, nome `Placar`, com **Scripts > Placar**.
5. **Câmera**: se a cena não tem, **Entidade > Câmera**. Adicione **Scripts > Seguir** (Alvo = `Jogador`).
6. Se a cena está escura, **Entidade > Luz direcional**.
7. Salve (Ctrl+S) e aperte **Play**. Ande com WASD, pule com Espaço. A cada moeda o console mostra
   `[Placar] pontos: N de 5`, e no fim, `Você pegou todas as moedas!`.

Quer mais? Uma porta que só abre com todas as moedas (o script da porta acha o `Placar` e compara `pontos`
com `total`), um inimigo de [examples/Inimigo.cpp](examples/Inimigo.cpp) com `Recomeço` = `0 1 0`, moedas que
flutuam com o [examples/Flutuar.cpp](examples/Flutuar.cpp) (Flutuar e Moeda juntos na mesma entidade), ou um
tiro com o [examples/Disparador.cpp](examples/Disparador.cpp).

## 13. Interface do jogo (HUD e menus)

Texto, imagens e botões na tela são **componentes de UI** em entidades comuns, desenhados por cima da cena:

| Componente | Para quê |
|---|---|
| **UI: Retângulo** | onde fica na tela: **Âncora** (canto, borda, centro ou **Esticar**), **Posição** (pixels a partir da âncora; x para a direita, y para baixo), **Tamanho**, **Ordem** (maior desenha por cima) e **Visível** |
| **UI: Texto** | o texto, a **Fonte** (um `.ttf`/`.otf` do projeto; vazio = Roboto), o tamanho, a cor e o alinhamento |
| **UI: Imagem** | um retângulo com cor e, se quiser, uma textura |
| **UI: Botão** | fundo com cor normal, com o mouse em cima e pressionado; o clique é lido pelo script |

Crie com **Entidade > Interface (UI)** (Texto, Imagem, Botão, Painel). As medidas são pixels de uma tela de
**1080 de altura**: numa janela menor (o viewport do editor) tudo encolhe junto, então o HUD fica no mesmo
lugar em qualquer tamanho. Um elemento filho de outro se posiciona dentro do retângulo do pai (um painel com
botões); esconder o pai esconde os filhos. No viewport, com o botão **UI** da barra ligado, um clique seleciona
o elemento e arrastar o move; os campos também ficam no Inspector, com desfazer, como qualquer componente.

Os scripts falam com a UI pelo sistema `motor::UiSystem` (`#include <motor/ui/UiSystem.h>`), achando os
elementos pelo **nome da entidade**. O HUD do mini jogo mostra a vida e os pontos
([examples/Hud.cpp](examples/Hud.cpp)):

```cpp
        // Mudar o texto todo frame é barato: se ficou igual, a geometria do texto não é refeita.
        const Placar* placar = AcharPlacar(GetScene());
        ui->SetText("Vida", std::format("Vida: {}", vida));
        ui->SetText("Pontos", std::format("Pontos: {}", placar ? placar->pontos : 0));
```

E um menu com o botão **Jogar** troca de cena ([examples/MenuPrincipal.cpp](examples/MenuPrincipal.cpp)):

```cpp
        if (ui->WasClicked("Jogar")) {
            Log("indo para {}", cenaDoJogo);
            LoadScene(cenaDoJogo);  // no fim deste passo: o menu para e a cena do jogo começa
        }
        if (ui->WasClicked("Creditos")) ui->SetVisible("PainelCreditos", !ui->IsVisible("PainelCreditos"));
```

- `SetText(nome, texto)`, `SetVisible(nome, visível)`, `IsVisible(nome)` e `WasClicked(nome)` também
  aceitam o id da entidade. Todos devolvem `false` se a entidade não existe (ou não tem o componente).
- `WasClicked` fica verdadeiro durante **um** frame depois do clique (apertar e soltar em cima do botão):
  leia no `OnUpdate`. O elemento de cima ganha o clique; texto sozinho não bloqueia.
- `LoadScene("Fase1")` acha a cena pelo nome (ou pelo caminho, `"Assets/Cenas/Fase1.mscene"`). A cena atual
  para (`OnDestroy` de todos) e a nova começa do zero. No editor, **Parar** volta à cena de antes do Play.
  No jogo exportado, enquanto a cena nova carrega aparece a **tela de carregamento** (Arquivo > Configurações
  do jogo: uma cena sua com UI, ou a padrão "Carregando..."). Ela é desenhada parada: scripts não rodam nela.

**Menu de opções** (tela cheia, resolução, vsync, sombras e sair): `GetOptions()` devolve as opções atuais do
jogador (`motor::GameOptions`), `ApplyOptions(o)` aplica na hora e `QuitGame()` fecha o jogo
([examples/MenuOpcoes.cpp](examples/MenuOpcoes.cpp)):

```cpp
        } else if (ui->WasClicked("Vsync")) {
            o.vsync = !o.vsync;
        } else if (ui->WasClicked("Sombras")) {
            o.shadows = static_cast<motor::ShadowSetting>((static_cast<int>(o.shadows) + 1) % 4);  // desligadas..altas
        } else if (ui->WasClicked("Sair")) {
            QuitGame();
            mudou = false;
        } else {
            mudou = false;
        }
        if (mudou) {
            ApplyOptions(o);
            Mostrar();
        }
```

- No jogo exportado as opções ficam gravadas em `%APPDATA%\<Jogo>\opcoes.json` e o jogo abre com elas da
  próxima vez. Na primeira vez valem as de **Arquivo > Configurações do jogo**.
- No Play do editor, cada Play começa nas opções do projeto e nada é gravado; só o MSAA e as sombras mudam de
  verdade (tela cheia, resolução e vsync são do jogo exportado). `QuitGame` para o Play.

Para pôr o HUD no mini jogo da seção anterior: copie `Hud.cpp` para `Scripts/`; crie **Entidade > Interface
(UI) > Texto** com o nome `Vida` (Âncora em cima à esquerda, Posição `40 40`) e outro, `Pontos` (em cima à
direita, Posição `-40 40`); um **Painel** chamado `FimDeJogo` com um Texto filho "Fim de jogo"; e uma entidade
vazia `HUD` com **Scripts > HUD (vida e pontos)**. Para o menu: uma cena `Menu` com dois **Botões** chamados
`Jogar` e `Creditos`, um Painel `PainelCreditos` e uma entidade `Menu` com **Scripts > Menu principal**
(Cena do jogo = o nome da cena do mini jogo). Abra a cena `Menu` e aperte **Play**.

### Salvar o progresso

`SaveSlot()` é um arquivo de chave/valor (o slot "progresso"; dê outro nome para ter vários, como
`SaveSlot("slot2")` ou `SaveSlot("config")`). Leia com `GetInt`, `GetFloat`, `GetBool`, `GetString` e `GetVec3`
(com o valor de quando ainda não existe), mude com os `Set...` e grave com `Save()`. Um menu com **Continuar**
(só aparece se houver save) e **Novo jogo** ([examples/MenuContinuar.cpp](examples/MenuContinuar.cpp)):

```cpp
        if (ui->WasClicked("Continuar")) {
            LoadScene(SaveSlot().GetString("fase", primeiraFase));
        } else if (ui->WasClicked("NovoJogo")) {
            DeleteSave();  // começa do zero
            LoadScene(primeiraFase);
        }
```

E na cena de cada fase, o jogador grava onde está e volta para lá ao continuar:

```cpp
        motor::SaveData& save = SaveSlot();
        if (save.GetString("fase") == fase && save.Has("posicao")) {
            GetTransform().position = save.GetVec3("posicao");  // continuar: de onde parou
        }
        save.SetString("fase", fase);
        save.SetInt("vezes", save.GetInt("vezes", 0) + 1);
        save.Save();
```

- No jogo exportado os saves ficam em `%APPDATA%\<Jogo>\saves\` (um `.json` por slot); no Play do editor,
  em `<projeto>/.motor/saves/`, separados: **Jogo > Apagar saves do Play** volta a ser um jogador novo.
- O que mudou e não foi gravado com `Save()` é gravado ao fechar o jogo e ao parar o Play. Gravar a cada
  mudança pequena (posição todo frame) é desperdício: grave em momentos certos (fase, checkpoint, a cada
  alguns segundos).
- Para arquivos próprios (uma foto, um replay), `SaveFolder()` é a pasta dos saves.

## 14. Som

Arquivos `.wav`, `.ogg` e `.mp3` dentro de `Assets/` são **clipes de som** (aparecem no painel Projeto, com
GUID no `.meta` como os outros assets). Um som toca numa entidade com o componente **Fonte de áudio**:

| Campo | Para quê |
|---|---|
| **Clipe** | o arquivo de som (escolha na lista ou arraste do painel Projeto) |
| **Volume**, **Tom** | volume (0 a 1) e tom (2 = uma oitava acima, e mais rápido) |
| **Repetir** | toca em loop (música, motor, vento) |
| **Tocar ao iniciar** | começa sozinho no Play; desligado, só toca quando um script chamar `Play` |
| **Som 3D** | o volume cai com a distância e o som vem do lado certo; desligado para música e sons da UI |
| **Distância mínima/máxima** | som 3D: volume cheio até a mínima; a partir da máxima, para de cair |

Quem ouve os sons 3D é o **Ouvinte de áudio**: ponha na câmera (sem nenhum, vale a câmera principal). O
**Volume geral** dele multiplica todos os sons. O som só toca no **Play**; **Parar** silencia tudo, e a
**Pausa** pausa os sons junto com o jogo.

Os scripts tocam e param os sons pelo sistema `motor::AudioSystem` (`#include <motor/audio/AudioSystem.h>`),
achando a fonte pelo **nome da entidade**. Um clique de botão da UI e uma música que liga e desliga com a
tecla M ([examples/SonsDoMenu.cpp](examples/SonsDoMenu.cpp)):

```cpp
        if (auto* ui = GetSystem<motor::UiSystem>(); ui && ui->WasClicked(botao)) {
            audio->Play(somDoClique);  // recomeça do início se ainda estava tocando
        }
        if (WasKeyPressed(motor::Key::M)) {
            if (audio->IsPlaying(musica)) {
                audio->Stop(musica);
            } else {
                audio->Play(musica);
            }
        }
```

- `Play(nome)`, `Stop(nome)`, `SetVolume(nome, volume)` e `IsPlaying(nome)` também aceitam o id da entidade e
  devolvem `false` se ela não existe ou não tem Fonte de áudio. `Play` também devolve `false` sem clipe ou se
  o arquivo não abriu (com aviso no console).
- `PlayOneShot(nome)` toca **outra voz** da mesma fonte, sem cortar a que já toca: passos e coletas seguidas
  não se cortam (com outro clipe: `PlayOneShot(id, clipe)`, de um campo `motor::AudioClip` do script).
- Cada fonte tem um **Grupo** (Efeitos, Música, Interface): `SetGroupVolume(motor::AudioGroup::Music, 0.5f)`
  muda todos os sons do grupo de uma vez e vale nas próximas cenas (o menu de opções do jogo).
- Mudar os campos da Fonte de áudio (volume, tom, a posição da entidade) vale na hora, mesmo tocando.
- Sem placa de som (ou sem saída), o jogo roda igual, só sem som.

Para montar: crie uma entidade `SomClique` com **Fonte de áudio** (Clipe = o som do clique, Som 3D e Tocar ao
iniciar desligados), outra `Musica` (Repetir ligado, Som 3D desligado), um **Botão** chamado `Jogar` e uma
entidade vazia com **Scripts > Sons do menu**.

## 15. Navegação (IA)

Inimigos e NPCs que andam sozinhos pelo cenário, dando a volta nas paredes, usam um **navmesh**: a malha das
partes do chão por onde um personagem de um certo tamanho consegue andar. Ele é feito a partir dos **Colliders
parados** da cena (sem Rigidbody nem Trigger): o chão, as paredes, as rampas. Para ligar:

1. Crie uma entidade vazia (ex.: `Navegação`) com o componente **Superfície de navegação**. Os campos dizem o
   tamanho do agente: **Raio** (distância que ele mantém das paredes), **Altura** (por baixo de algo mais baixo
   ele não passa), **Altura do degrau** e **Inclinação máxima** (a rampa mais íngreme que ele sobe).
2. Clique em **Gerar**, ao lado do campo **Navmesh**: o navmesh é gravado em `Assets/Navegacao/Navmesh.mnav`
   e aparece em azul no viewport (botão **Navmesh** da barra). Mudou o cenário? Gere de novo; no Play, o
   console avisa quando o navmesh ficou velho. Sem gerar, o navmesh é feito no começo de cada Play.
3. Na entidade que vai andar, ponha o componente **Agente de navegação**: **Velocidade**, **Aceleração**,
   **Distância de parada** e **Altura da base** (quanto a origem da entidade fica acima do chão: 1 numa cápsula
   de 2 m com a origem no meio). Com o campo **Alvo** preenchido, o agente segue aquela entidade sem script
   nenhum.

Para decidir **para onde** ir, o script usa o sistema `motor::NavSystem` (`#include <motor/ai/NavSystem.h>`).
Um guarda que patrulha entre dois pontos e persegue o jogador quando o vê
([examples/Guarda.cpp](examples/Guarda.cpp)):

```cpp
        // Vê o alvo se ele está perto e o caminho reto até ele, pelo navmesh, não bate em parede.
        if (GetEntity(alvo)) {
            const glm::vec3 ele = Posicao(alvo);
            const bool perto = glm::distance(eu, ele) < visao;
            if (perto && !nav->Raycast(eu, ele)) {
                // Refaz o caminho só quando o alvo andou: pedir a cada passo refaria à toa.
                if (!m_perseguindo || glm::distance(ele, m_ultimoVisto) > 0.5f) {
                    nav->SetDestination(GetId(), ele);
                    m_ultimoVisto = ele;
                }
                m_perseguindo = true;
                return;
            }
        }

        // Patrulha: chegou num ponto (ou acabou de perder o alvo de vista), vai para o outro.
        if (m_perseguindo || nav->HasArrived(GetId()) || !nav->HasPath(GetId())) {
```

- `SetDestination(id, ponto)` manda o agente andar até lá; devolve `false` se o ponto está longe do navmesh.
  Destino numa parte do chão que não se liga à do agente (outra ilha): ele vai até onde dá (`IsPathPartial`).
- `HasArrived(id)` fica `true` quando ele chega (até o próximo destino); `HasPath(id)`, enquanto anda.
  `RemainingDistance(id)` diz quanto falta pelo caminho, e `Stop(id)` o faz frear e esquecer o destino.
- `Raycast(de, ate)` anda em linha reta pelo navmesh: `nullopt` se chega sem bater em parede ou borda. É um
  jeito barato de "ver" (para ver por cima de uma mesa baixa, use o `Raycast` da física).
- `FindPath(de, ate, cantos)` só calcula o caminho, sem mover ninguém (ex.: para saber se dá para chegar);
  `SamplePosition(ponto)` acha o ponto do navmesh mais perto, e `Warp(id, ponto)` teleporta o agente.
Para montar: o chão e as paredes com Collider, a **Superfície de navegação** (e **Gerar**), duas entidades
vazias `PontoA` e `PontoB`, e o guarda com **Agente de navegação** e **Scripts > Guarda**, com Ponto A, Ponto B
e Alvo escolhidos no Inspector. No Play, ligue **Cena e Jogo lado a lado** (menu Jogo) para ver no Viewport o
caminho do guarda em amarelo.

### Multidão, pulos e lama

Vários agentes andam juntos como uma multidão: desviam uns dos outros e não se atravessam, sem script nenhum. O
**Raio** do agente é o tamanho com que os outros o veem. Muitos indo ao mesmo ponto param em volta dele, e cada um
conta como chegado (`HasArrived`) quando não consegue mais avançar; para não se espremerem, mande cada um para um
lugar. Um pastor que leva o rebanho (as ovelhas são filhas dele) até o curral, e com chuva desvia da lama
([examples/Pastor.cpp](examples/Pastor.cpp)):

```cpp
        // O custo vale na hora, sem gerar o navmesh de novo: as ovelhas que ainda vão pedir caminho já veem a lama
        // nova. As que estão andando continuam no caminho que têm; Recolher() refaz o de todas.
        if (auto* area = GetComponent<motor::NavMeshArea>(lama)) {
            const float custo = chovendo ? custoComChuva : m_custoSeco;
            if (area->cost != custo) {
                area->cost = custo;
                Recolher();
            }
        }
```

```cpp
        const glm::vec3 centro = glm::vec3(GetScene().WorldMatrix(curral)[3]);
        const float n = static_cast<float>(m_ovelhas.size());
        const float raio = n > 1.0f ? espaco * n / glm::two_pi<float>() : 0.0f;  // a volta cabe todas
        for (size_t i = 0; i < m_ovelhas.size(); ++i) {
            const float angulo = glm::two_pi<float>() * static_cast<float>(i) / std::max(n, 1.0f);
            nav->SetDestination(m_ovelhas[i], centro + raio * glm::vec3(std::cos(angulo), 0.0f, std::sin(angulo)));
        }
```

- **Água, lama, grama alta:** a **Área de navegação** (`NavMeshArea`) é uma caixa que marca o chão dentro dela com
  uma área (1 a 15) e um **Custo**: com custo 3, andar 1 m ali conta como 3 m, e o agente dá a volta quando a volta
  sai mais curta. O custo vale na hora (como a chuva do pastor); mudar a caixa pede gerar o navmesh de novo. No
  agente, **Áreas por onde anda** desmarca as que ele evita de todo (o cavalo não entra na água), e
  `FindPath(de, ate, cantos, areas)` aceita a mesma máscara (um bit por área).
- **Pulos e escadas:** a **Ligação de navegação** (`NavMeshLink`) liga dois pontos do navmesh que não se alcançam
  andando (o vão entre duas plataformas, o alto de um muro, a cerca do curral). As pontas são relativas à entidade e
  precisam cair perto do navmesh; gere o navmesh de novo depois de pôr ou mudar uma. O agente vai em arco (**Altura
  do arco**) ou reto (0, escada), na velocidade dele, e `IsOnLink(id)` diz quando ele está no meio do pulo.
- **Corpo físico:** sem física, o agente mexe no Transform direto. Com um **Controlador de personagem** na mesma
  entidade ele anda pela física: empurra caixas, cai e não atravessa paredes, e volta para o navmesh se for empurrado
  para fora dele. Não chame `MoveCharacter` nele (o `NavSystem` manda a cada passo); Rigidbody num agente não
  funciona.
- **Animação:** a **Animação do agente** (`NavMeshAgentAnimation`), na entidade do agente, troca os clipes do
  Animator dele (ou do modelo filho) pela velocidade: parado, andando, correndo e pulando nas ligações, com mistura.
  Com **Acompanhar a velocidade**, o clipe toca mais rápido quanto mais rápido o agente anda (os pés não escorregam).
  Um script que chame `CrossFade` no mesmo passo, depois do `NavSystem`, vence.
- `GetNeighbors(id)` diz de quem o agente está desviando agora (os mais perto, até 6), e no viewport, com o botão
  **Navmesh**, aparecem o raio de cada agente, as ligações, as áreas e, no Play, as linhas até os vizinhos.

Para montar: a lama com **Área de navegação** (Área 2, Custo 1,5), uma entidade vazia `Curral`, e o pastor (vazio,
com **Scripts > Pastor**, Curral e Lama escolhidos) com as ovelhas como filhas, cada uma com **Agente de
navegação**. Gere o navmesh e dê Play; marque **Chovendo** no Inspector do pastor para ver o rebanho mudar de rumo.

## 16. Recarga: o que é preservado

Ao salvar um arquivo de script com o editor aberto:

- Os valores de **todos os campos registrados**, em todas as entidades, são guardados; a DLL antiga sai, a
  nova entra e os valores voltam. **Campo novo** fica com o valor padrão. Campos são casados pela **chave**
  (o 1º texto do `.Field`), não pelo nome do membro.
- **Campo removido, renomeado ou com o tipo trocado** (um `int` que virou `std::string`): o valor salvo não
  cabe mais e o campo fica com o padrão. O painel Scripts mostra um aviso com o valor perdido, para você
  copiar se precisar. Renomeou? Ponha `{.formerKey = "nomeAntigo"}` no `.Field` e o valor volta.
- Membros **não registrados** voltam ao padrão.
- O desfazer continua funcionando.
- **Durante o Play** a recarga também acontece, e o jogo continua de onde estava: cada script é recriado com
  os valores **do jogo** nos campos registrados, os membros não registrados voltam ao padrão, os
  temporizadores são cancelados e o `OnStart` roda de novo (sem `OnDestroy` antes). Escreva o `OnStart` para
  que rodar de novo não estrague nada: achar entidades e ligar temporizadores, sim; criar dez inimigos, só
  se ainda não existirem. Ao parar o Play, a cena volta à de antes, já com os scripts novos.
- Com **erro de compilação** nada muda: os scripts da última compilação boa continuam carregados, e a barra
  de menus mostra "Scripts: N erro(s)".

## 17. Componente "desconhecido" (script não carregado)

Se uma cena usa um script que não está carregado (você apagou o arquivo, mudou o identificador do
`Register`, ou abriu a cena antes de os scripts compilarem), o componente **não some**: o Inspector o mostra
como "script não carregado", com os dados guardados. Ele é salvo de volta igual na cena e volta a funcionar
sozinho quando um script com aquele identificador for carregado de novo. Para se livrar dele de vez, remova o
componente no Inspector.

## 18. Erros comuns

O painel Scripts lista as mensagens do compilador (GCC, em inglês). As mais comuns:

| Mensagem | Causa e solução |
|---|---|
| `'void A::OnUpdate()' marked 'override', but does not override` | assinatura errada: é `OnUpdate(float dt)`, `OnTriggerEnter(motor::EntityId)`, `OnCollisionEnter(const motor::Collision&)`. Copie da [referência](scripts-api.md) |
| `warning: 'virtual void motor::Script::OnUpdate(float)' was hidden` | o mesmo erro sem `override`: compila, mas o seu método **nunca é chamado**. Sempre escreva `override` |
| `static assertion failed: tipo de campo não suportado` | `.Field` com um tipo que o Inspector não conhece (`double`, `unsigned`...). Use os tipos da [seção 3](#3-campos-no-inspector) |
| `'x' is not a member of 'F'` (no `MOTOR_SCRIPT`) | o `.Field` aponta para um membro que não existe (nome errado ou apagado) |
| `call to consteval function 'std::basic_format_string<...>' is not a constant expression` | o texto do `Log` tem mais `{}` do que valores (ou um `{` solto: escreva `{{`) |
| `'Key' has not been declared` | falta o `motor::`: `motor::Key::W` (ou `using motor::Key;` dentro do método) |
| `'PhysicsSystem' is not a member of 'motor'` | falta `#include <motor/physics/PhysicsSystem.h>` |
| `'AudioSystem' is not a member of 'motor'` | falta `#include <motor/audio/AudioSystem.h>` |
| `expected ';' after struct definition` | falta o `;` depois do `}` da struct |
| `undefined reference to ...` | um método foi declarado e não tem corpo, ou um `.cpp` de que ele depende está fora da pasta `Scripts/` |
| `redefinition of 'void MotorRegisterScript_X(...)'` | dois `MOTOR_SCRIPT(X)` com o mesmo nome no mesmo arquivo; cada script é registrado uma vez |

Erros que só aparecem rodando:

- **O editor fecha com `componente não registrado no ComponentRegistry`**: `Add<T>()`/`Get<T>()`/`Has<T>()`
  com uma struct que não tem `MOTOR_SCRIPT` (ou cujo registro foi recusado, ver abaixo).
- **Script não aparece em "Adicionar componente"**: falta o `MOTOR_SCRIPT`, a compilação falhou (veja o
  painel Scripts) ou o arquivo não está dentro de `Scripts/`.
- **Script não faz nada**: está fora do Play, a entidade está inativa, ou o método está sem `override` (aviso
  acima). Ponha um `Log` no `OnStart` para conferir.
- **O editor fechou de repente no Play**: um ponteiro inválido no script (ex.: guardou um `Entity*` e a
  entidade foi destruída). Guarde ids e confira com `GetScene().Find(id)`.
- **`Scripts: já existe um componente chamado 'X'`** no console: dois scripts usam o mesmo identificador no
  `Register`, ou um script usa o nome de um componente da engine (`Camera`, `Collider`, `Light`,
  `MeshRenderer`, `Rigidbody`, `CharacterController`). O script repetido é ignorado: troque o identificador.
