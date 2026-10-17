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
10. [Receitas](#10-receitas)
11. [Mini jogo: andar, coletar, pontuar](#11-mini-jogo-andar-coletar-pontuar)
12. [Interface do jogo (HUD e menus)](#12-interface-do-jogo-hud-e-menus)
13. [Recarga: o que é preservado](#13-recarga-o-que-é-preservado)
14. [Componente "desconhecido" (script não carregado)](#14-componente-desconhecido-script-não-carregado)
15. [Erros comuns](#15-erros-comuns)

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
e avisos (clique num erro para abrir o arquivo) e a saída completa do compilador.

**Onde aparece o `Log`.** As mensagens de `Log(...)` dos scripts saem na **janela de console** do editor
(a janela de texto que abre junto com ele), com a hora e o nome da entidade:
`[14:02:31] [info ] [Moeda] pontos: 1 de 3`.

**Play.** Os scripts só rodam no modo Play (botão ▶ no topo do viewport, Ctrl+P ou menu Jogo). Ao parar
(⏹), a cena volta **exatamente** como estava antes do Play: o que os scripts mudaram é descartado. O jogo
só recebe teclado e mouse com o viewport em foco (o Play já dá o foco a ele).

**Editor de código.** Qualquer editor serve. Para o autocompletar funcionar (VS Code, CLion...), aponte os
includes para `engine/include` e para o GLM (`build/_deps/glm-src`) da pasta do Motor; a compilação de
verdade é sempre a do editor.

## 2. O primeiro script

Vamos fazer uma entidade flutuar: subir e descer devagar, como uma boia.

1. Abra (ou crie, em **Arquivo > Novo projeto...**) um projeto.
2. Menu **Scripts > Novo script...**, nome `Flutuar`. O editor cria `Scripts/Flutuar.cpp` com um esqueleto.
   (O nome precisa ser um identificador C++: letras sem acento, dígitos e `_`, sem começar com dígito.)
3. Abra o arquivo (**Scripts > Abrir pasta de scripts**) e troque o conteúdo por este
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
| `void OnUpdate(float) override` | métodos `On...` que a engine chama no Play. Sempre com `override` (ver [Erros comuns](#15-erros-comuns)) |
| `GetTransform()`, `Time()`, `Log(...)` | a API do script (seções 5 e 6, e a [referência](scripts-api.md)) |
| `MOTOR_SCRIPT(Flutuar) { ... }` | o registro: roda quando a DLL é carregada |
| `Register<Flutuar>("Flutuar", "Flutuar")` | o 1º nome é o **identificador** gravado na cena; o 2º é o que o editor mostra |
| `.Field("altura", "Altura", &Flutuar::altura)` | campo no Inspector: chave no arquivo, rótulo, membro |

O identificador do `Register` é o que liga a cena ao script. Pode renomear a struct à vontade; se renomear o
identificador, as entidades que usavam o nome antigo ficam com um componente "desconhecido" (seção 14) até
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
| `std::shared_ptr<motor::Mesh>` | malha (Cubo, Esfera, Plano ou um `.obj` do projeto) |
| `std::shared_ptr<motor::Texture2D>` | textura (`.png`/`.jpg` do projeto) |
| `motor::ColliderShape`, `motor::LightType` | lista (os enums da engine que têm editor) |

Outros tipos (`double`, `unsigned`, enums seus...) não compilam: use um dos acima (ex.: `float` em vez de
`double`, `int` para uma escolha numerada).

O quarto argumento opcional de `Field` são as **opções** (`motor::FieldOptions`), escritas com
inicializadores designados **na ordem em que estão declaradas**: `color`, `slider`, `logarithmic`, `speed`,
`min`, `max`, `format`, `tooltip`.

| Opção | Efeito |
|---|---|
| `.color = true` | `vec3`/`vec4` como seletor de cor |
| `.slider = true` | `int`/`float` como slider entre `min` e `max` |
| `.logarithmic = true` | slider logarítmico (bom para valores de 0.01 a 100) |
| `.speed = 0.1f` | quanto o valor muda ao arrastar o mouse (padrão 0.05) |
| `.min = 0.0f, .max = 10.0f` | limites; `min == max` (o padrão) = sem limite |
| `.format = "%.1f m/s"` | formato `printf` do número mostrado |
| `.tooltip = "..."` | texto ao passar o mouse sobre o campo |

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
  salvo dele (volta ao padrão); trocar o rótulo (2º texto) é inofensivo.
- O valor padrão é o do inicializador do membro (`int vidas = 3;`). Ao adicionar o script pelo Inspector,
  é ele que aparece.
- Durante o Play o Inspector mostra os valores **ao vivo** (e você pode mexer neles); ao parar, voltam.
- A ordem dos `.Field` é a ordem no Inspector.

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
| `IsKeyDown(motor::Key::W)` | `true` enquanto a tecla está apertada |
| `WasKeyPressed(motor::Key::Space)` | `true` só no frame em que a tecla desceu |
| `IsMouseDown(motor::MouseButton::Left)` | `true` enquanto o botão está apertado (`Left`, `Right`, `Middle`) |
| `MouseDelta()` | quanto o mouse andou neste frame, em pixels (`glm::vec2`) |
| `Time().delta` | segundos desde o último `OnUpdate` (o mesmo `dt`) |
| `Time().fixedDelta` | o passo do `OnFixedUpdate` (1/60 s) |
| `Time().elapsed` | segundos de jogo desde o Play (sem as pausas), em `double` |
| `Time().frame` | quantos `OnUpdate` já rodaram |

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

**Outras entidades.** `FindEntity("Nome")` devolve a primeira entidade com esse nome, ou `nullptr`.
`GetScene().Find(id)` acha pelo número. Guarde o **id** (`motor::EntityId`), nunca o ponteiro: entidades
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
scripts:

| Componente (no editor) | Tipo C++ | Campos principais |
|---|---|---|
| Mesh Renderer | `motor::MeshRenderer` | `mesh`, `sharedMaterial` (asset `.mmat`) e `material` (o próprio): `color` (vec4), `albedo` (textura), `metallic`, `roughness`, `specular`, `emissive`... (PBR, ver a referência) |
| Câmera | `motor::CameraComponent` | `fov` (graus), `nearPlane`, `farPlane`, `main` |
| Luz | `motor::LightComponent` | `type` (`motor::LightType::Directional`/`Point`/`Spot`), `color`, `intensity`, `range`, `innerAngle`, `outerAngle`, `castShadows` |
| Collider | `motor::ColliderComponent` | `shape` (`motor::ColliderShape::Box`/`Sphere`/`Capsule`/`Mesh`), `size`, `radius`, `height`, `center`, `isTrigger`, `friction`, `restitution` |
| Rigidbody | `motor::RigidbodyComponent` | `mass`, `useGravity`, `isKinematic`, `linearDamping`, `angularDamping` |
| Controlador de personagem | `motor::CharacterControllerComponent` | `height`, `radius`, `maxSlope`, `stepHeight`, `mass` |

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

**Criar.** `CreateEntity("Nome")` cria uma entidade na raiz (ou `CreateEntity("Nome", idDoPai)` como filha).
Ela nasce vazia (sem malha): adicione os componentes. Os scripts dela começam no frame seguinte.
O [examples/Disparador.cpp](examples/Disparador.cpp) cria um projétil com a malha do próprio objeto:

```cpp
        // Use a referência só aqui; para lembrar da entidade depois, guarde o id (tiro.id).
        motor::Entity& tiro = CreateEntity("Projétil");
        tiro.transform.position = posicao + frente;
        tiro.transform.scale = glm::vec3(tamanho);
        if (const auto* meu = GetEntity().Get<motor::MeshRenderer>()) {
            tiro.Add<motor::MeshRenderer>() = *meu;  // copia malha e material
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
- No viewport, o botão **Colliders** mostra as formas (verde), triggers (azul) e personagens.

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
| Raio | `Raycast(origem, direção, distânciaMáxima, idIgnorado)`: o primeiro corpo atingido (triggers não contam) |

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

- Mudar o Transform de um corpo pelo script **teleporta** o corpo (inclusive o personagem). Para mover um
  objeto que deve empurrar os outros (porta, plataforma), use Rigidbody **Cinemático**.
- Entidade criada neste passo só ganha corpo no próximo passo fixo: as chamadas acima devolvem `false`/zero
  até lá.

## 8. Sistemas do jogo (`GetSystem`)

Partes da engine que rodam a cada passo do jogo (a física, a interface do jogo; depois, o áudio) são **sistemas**. Um
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

## 10. Receitas

Cada receita é um arquivo completo, pronto para copiar para `Scripts/`. Os modelos Jogador e Seguir também
vêm no editor: **Scripts > Adicionar exemplo**.

### Jogador

[editor/templates/Scripts/Jogador.cpp](../editor/templates/Scripts/Jogador.cpp): anda com WASD (Shift corre),
pula com Espaço e vira para onde anda. Na entidade: **Controlador de personagem** + **Jogador (física)**. O
chão precisa de Collider. W anda para -Z (a "frente" do mundo; a câmera do Seguir fica atrás, em +Z). Com um
modelo animado como filho, troca entre parado, andando e pulando ([Animação](#9-animação)).

```cpp
    void OnFixedUpdate(float) override {
        // A física é um sistema do jogo: existe só no Play (fora dele, nullptr).
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;

        using motor::Key;
        glm::vec3 dir{0.0f};
        if (IsKeyDown(Key::W)) dir.z -= 1.0f;
        if (IsKeyDown(Key::S)) dir.z += 1.0f;
        if (IsKeyDown(Key::A)) dir.x -= 1.0f;
        if (IsKeyDown(Key::D)) dir.x += 1.0f;
        const bool moving = glm::length(dir) > 1e-6f;
        if (moving) dir = glm::normalize(dir);  // na diagonal não anda mais rápido

        const bool running = IsKeyDown(Key::LeftShift) || IsKeyDown(Key::RightShift);
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
na tela, veja a [seção 12](#12-interface-do-jogo-hud-e-menus).

## 11. Mini jogo: andar, coletar, pontuar

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

## 12. Interface do jogo (HUD e menus)

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

Para pôr o HUD no mini jogo da seção anterior: copie `Hud.cpp` para `Scripts/`; crie **Entidade > Interface
(UI) > Texto** com o nome `Vida` (Âncora em cima à esquerda, Posição `40 40`) e outro, `Pontos` (em cima à
direita, Posição `-40 40`); um **Painel** chamado `FimDeJogo` com um Texto filho "Fim de jogo"; e uma entidade
vazia `HUD` com **Scripts > HUD (vida e pontos)**. Para o menu: uma cena `Menu` com dois **Botões** chamados
`Jogar` e `Creditos`, um Painel `PainelCreditos` e uma entidade `Menu` com **Scripts > Menu principal**
(Cena do jogo = o nome da cena do mini jogo). Abra a cena `Menu` e aperte **Play**.

## 13. Recarga: o que é preservado

Ao salvar um arquivo de script com o editor aberto:

- Os valores de **todos os campos registrados**, em todas as entidades, são guardados; a DLL antiga sai, a
  nova entra e os valores voltam. **Campo novo** fica com o valor padrão; **campo removido** é ignorado.
  Campos são casados pela **chave** (o 1º texto do `.Field`), não pelo nome do membro.
- Membros **não registrados** voltam ao padrão (mas eles só importam no Play, que recomeça do zero).
- O desfazer continua funcionando.
- **Durante o Play** a recarga espera: o jogo continua com a versão antiga, e a nova entra quando você parar.
- Com **erro de compilação** nada muda: os scripts da última compilação boa continuam carregados, e a barra
  de menus mostra "Scripts: N erro(s)".

## 14. Componente "desconhecido" (script não carregado)

Se uma cena usa um script que não está carregado (você apagou o arquivo, mudou o identificador do
`Register`, ou abriu a cena antes de os scripts compilarem), o componente **não some**: o Inspector o mostra
como "script não carregado", com os dados guardados. Ele é salvo de volta igual na cena e volta a funcionar
sozinho quando um script com aquele identificador for carregado de novo. Para se livrar dele de vez, remova o
componente no Inspector.

## 15. Erros comuns

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
