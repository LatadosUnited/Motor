# Roteiro do Motor — Alpha 2

| Alpha | Foco | Roteiro |
|---|---|---|
| 1 | Base da engine, editor, jogo exportado, Hub e instalador (Fases 0 a 15) | ✅ [alpha-1.md](alpha-1.md) |
| **2** | **Funcionalidades e pendências** (Fases 16 a 21) | **este arquivo** |
| 3 | Desempenho, e depois cenários abertos e renderização 2 (Fases 22 a 28) | rascunho: [alpha-3.md](alpha-3.md) |
| 4 | Ferramentas: compilador integrado, distribuição, scripts avançados (Fases 29 a 31) | rascunho: [alpha-4.md](alpha-4.md) |

A numeração das fases continua de uma alpha para a outra. Cada fase termina com algo que dá para usar e
testar no editor e tem um "Pronto quando". Cada pendência diz de onde veio (fase da alpha 1, anotação ou a
análise do código de 17/10/2026).

**A alpha 2 está pronta quando:** uma pessoa que não conhece o código do Motor instala tudo pelo `MotorSetup`,
faz no editor um jogo 3D pequeno que salva o progresso, aceita controle e usa objetos transparentes; um erro
num script não fecha o editor nem perde trabalho; o jogo é exportado em Release e roda em outro computador da
rede.

## Onde estamos (17/10/2026)

Pronto na alpha 1: núcleo, renderização PBR com sombras, céu HDR e pós-processamento, cena e componentes,
Play, scripts C++ com recarga, luzes, física (Jolt), materiais e prefabs, glTF com animação, HUD, áudio,
exportar o jogo (Player), documentação de scripts, servidor MCP do editor, Hub de versões, instalador e
servidor de versões. Main sem pendências de merge, exceto o branch `fix/caminho-longo-objetos` (Fase 16).

## Prioridades

| # | Fase | Por quê | Pode rodar junto com |
|---|---|---|---|
| 1 | Fase 16 — Robustez e correções | hoje um erro de script fecha o editor e o trabalho não salvo se perde; contornos conhecidos | 17 (áreas diferentes) |
| 2 | Fase 17 — Scripts mais rápidos e fáceis | compilações de 10 a 124 s medidas num projeto real; menos código repetido | 16, 18 |
| 3 | Fase 18 — Funcionalidades de jogo | física, áudio, cursor, formas e transparência que jogos comuns pedem | 17 |
| 4 | Fase 19 — Validação de uso real | o editor quase só foi testado por agentes; a lista de defeitos decide o resto | — |
| 5 | Fase 20 — Editor produtivo | multi-seleção, copiar/colar, snapping, Cena e Jogo lado a lado, prefab dentro de prefab | 21 |
| 6 | Fase 21 — Jogo entregue | opções do jogador, ícone, salvar progresso, controle, exportação Release | 20 |

A 16 vem primeiro porque quem começa a programar agora perde trabalho a cada erro de script. A 17 e a 18
vêm antes da validação porque a validação é fazer um jogo, e é nelas que esse jogo vai esbarrar.

---

## Fase 16 — Robustez e correções

**Erro de script não derruba o editor** *(análise de 17/10/2026, item 1: o maior risco no dia a dia)*:

- `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy` e os eventos de física rodam sem proteção
  (`SceneRuntime::Update` chama `s.OnUpdate(dt)` direto). Uma exceção sobe até o `main.cpp` e o editor fecha;
  `Get<T>()` de um tipo sem `MOTOR_SCRIPT`, por exemplo, lança. Fazer: `try/catch` em volta de cada chamada
  de script e de sistema; no erro, o Play pausa, o console mostra script, entidade e mensagem, e dá para parar
  o Play normalmente. No Player, o erro vai para o log e o jogo segue sem aquele script.
- **Salvamento automático:** o estado de antes do Play só existe na memória (`PlayMode`). Gravar a cena aberta
  numa cópia de recuperação (`<projeto>/.motor/recuperacao/`) ao dar Play e a cada N minutos com alterações;
  ao abrir o editor depois de uma queda, oferecer recuperar.
- Limite conhecido: um ponteiro inválido no script ainda derruba o processo (a DLL roda no mesmo processo).
  Isolar os scripts em outro processo fica para a alpha 4 (Fase 31).

**Correções:**

- **Teleporte do personagem guarda a velocidade.** `SyncCharacters` (`engine/src/physics/PhysicsSystem.cpp`,
  "teleporte pelo script ou pelo editor") só chama `SetPosition`: o personagem reaparece com a velocidade da
  queda. Um agente contornou mudando a massa do controlador de 70 para 70,001 (o Motor recria o corpo parado).
  Zerar a velocidade (a do `CharacterVirtual` e a guardada no registro) no teleporte. Teste em
  `tests/test_physics.cpp`. *(Agente fazendo um jogo, 17/10/2026.)*
- **Caminho longo na compilação dos scripts:** revisar e mesclar o branch `fix/caminho-longo-objetos`.
- **Pasta vazia vira projeto:** abrir o editor numa pasta vazia oferece criar o projeto ali. *(Anotado para a
  alpha 2.)*
- **Scripts instanciam prefabs:** `Instantiate(prefab, posição)` com campo `PrefabRef` no Inspector, exemplo
  no guia. *(Limitação da Fase 6.)*
- **Documentação junto da versão instalada:** `docs/` no `motor_package`; Ajuda > Guia de scripts.
- **Câmera do jogo com roll.** *(Limitação da Fase 2.)*
- **Editor avisa antes de abrir projeto de versão mais nova** que a dele. *(Depois da Fase 14.)*
- **Valores inválidos pelo MCP:** `component_set` com valor de tipo errado responde com erro e o formato
  esperado, em vez de ignorar.

**Pronto quando:** um script que lança exceção ou chama `Get<T>()` errado pausa o Play com a mensagem no
console e o editor continua aberto; matar o editor no meio do trabalho e reabrir oferece a recuperação; cada
correção tem teste ou verificação pelo MCP.

## Fase 17 — Scripts mais rápidos e fáceis

Hoje a DLL de scripts é gerada por um CMakeLists (`ScriptProject.cpp`, `GenerateCMakeLists`) com "MSYS
Makefiles", em Debug, sem cabeçalho pré-compilado; cada `.cpp` inclui `ScriptModule.h`, que puxa a cena, os
componentes, `<format>` e a GLM. Medido: 14 a 45 s na primeira compilação de projetos de teste e **10, 88 e
124 s** num projeto de jogo real *(análise de 17/10/2026, item 6)*; ~1,4 s para mudar um arquivo pequeno. O
compilador continua o do MSYS2 (integrar o compilador é a alpha 4).

**Velocidade** (medir antes e depois de cada passo com um projeto de referência de 1, 10 e 50 scripts; os
números entram no README):

- **Medição:** o painel Scripts e o `scripts_status` do MCP mostram o tempo de cada etapa (configurar,
  compilar cada arquivo, ligar, recarregar).
- **Cabeçalho pré-compilado** com o `ScriptModule.h` e a GLM, gerado uma vez por versão do Motor no SDK.
- **Headers mais leves:** tirar `<format>` e o que não for preciso dos headers públicos que os scripts incluem
  (`Script.h`, `Scene.h`, `Component.h`), declarar adiante o que der; um teste mede o tempo de compilar um
  script vazio para não piorar sem perceber.
- **Configurar uma vez só:** o CMake só reconfigura quando entra ou sai arquivo ou muda a versão do Motor.
- **Ligação mais rápida:** comparar `ld` com `lld` (`-fuse-ld=lld`, já vem no MSYS2) e "MSYS Makefiles" com
  Ninja. Ferramenta nova só com aprovação do usuário.

**Facilidade:**

- **Novo script pelo editor:** painel Projeto > Criar > Script, com nome e modelo (vazio, comportamento, que
  segue, coletável...), já com `MOTOR_SCRIPT` e aberto no editor de código.
- **Editor de código configurado:** `compile_commands.json` e `.vscode/` (IntelliSense com os headers do Motor,
  tarefa de compilar, depurar com gdb anexando ao editor) gerados ao criar ou abrir o projeto; Abrir no VS
  Code pelo menu Scripts e pelo duplo clique num `.cpp`.
- **Erros clicáveis:** cada erro do painel Scripts abre o arquivo na linha.
- **Menos código repetido:** registrar campos com menos texto (ex.: `MOTOR_FIELDS(Jogador, velocidade, pulo)`
  ao lado do `Field(...)` completo), referências a entidades como campos do Inspector (`EntityRef`, arrastar da
  Hierarquia) no lugar de buscar pelo nome, `GetComponent<T>()` em outras entidades.
- **API que falta em jogos comuns:** temporizadores e "chamar daqui a N segundos", mensagens entre scripts,
  números aleatórios. Cada um com exemplo no guia, compilado pelo `test_script_docs`.
- **Recarga durante o Play** *(análise, item 6: hoje a recarga espera o Play parar)*: recarregar a DLL com o
  jogo rodando, preservando o estado dos componentes como a recarga fora do Play já faz.
- **Recarga que avisa:** campo renomeado ou com tipo trocado vira aviso no painel em vez de perder o valor.

**Pronto quando:** num projeto com 10 scripts, mudar um arquivo e ver o efeito leva menos de 1 s (do salvar à
recarga, também durante o Play); o projeto de jogo real que levava 88 a 124 s compila do zero em menos de 30 s;
criar um script, editar com IntelliSense e clicar num erro para ir à linha funciona sem configurar nada à mão.

## Fase 18 — Funcionalidades de jogo

*(Análise de 17/10/2026, itens 7, 8 e 9; transparência do item 3.)*

- **Física:**
  - camadas de colisão e máscaras (quem colide com quem; raycast que ignora camadas, não só uma entidade —
    câmera orbital que não bate no próprio jogador);
  - consultas de área: overlap de esfera e caixa, sphere cast ("o que está nesta área", para coletas);
  - personagem: `SetCharacterVelocity` e gravidade ligável e desligável (hoje só há `GetCharacterVelocity`).
- **Áudio:**
  - várias vozes por fonte (`Play` hoje recomeça o som: passos e coletas seguidas se cortam) e
    `PlayOneShot(clipe)`;
  - carregar os clipes ao abrir a cena, fora da thread principal (hoje o primeiro `Play` de cada clipe
    decodifica o arquivo e trava um pouco);
  - grupos de volume (música, efeitos, interface) com volume por grupo, para o menu de opções.
- **Entrada:** prender e esconder o cursor pelos scripts (a `Window` já sabe; falta a API). O controle (gamepad)
  está na Fase 21.
- **Formas prontas:** cilindro, cápsula e cone (hoje só Cubo, Esfera e Plano), com collider certo.
- **Transparência 3D:** materiais com modo opaco, recorte por alpha (folhas, grades) e transparente (vidro,
  água simples), com a cena desenhando os transparentes depois dos opacos, ordenados de trás para frente.
  Hoje o blend está sempre desligado na cena. *(Também era limitação da Fase 7.)*

**Pronto quando:** um jogo de exemplo tem câmera orbital que não atravessa paredes, coleta por área, passos
que não se cortam, música com volume separado, cursor preso no jogo, uma janela de vidro e uma cerca com
recorte por alpha; tudo com testes sem GPU e exemplo no guia.

## Fase 19 — Validação de uso real

Um jogo pequeno feito **à mão** (por uma pessoa, não por agente) do começo ao fim, no editor instalado pelo
Hub, e exportado.

- Roteiro de teste à mão em `docs/teste-manual.md`, reaproveitável a cada versão: câmera livre e seleção por
  clique no viewport, foco do jogo, menus, atalhos, gizmo, arrastar arquivos do painel Projeto e do Explorer,
  popups do Hub e do instalador, Ajuda > Sobre, recarga de scripts com o jogo aberto, recuperação depois de
  uma queda. *(Anotado para a alpha 2: "falta testar à mão"; não testados nas Fases 13 a 15.)*
- Instalação limpa num segundo computador pela rede (`MotorServidor` + `MotorSetup`), sem compilar nada.
- Cada problema achado vira um item com origem e gravidade (na 16, se for pequeno).

**Pronto quando:** o jogo exportado roda no segundo computador e a lista de problemas está anotada e
priorizada.

## Fase 20 — Editor produtivo

- **Antes de tudo, dividir os arquivos que esta fase vai mexer**, um por commit, sem mudar comportamento:
  `ProjectPanel.cpp` (~1200 linhas), `SceneViewport.cpp` e `SceneDocument.cpp` (~600). *(Revisão do código de
  17/10/2026.)*
- **Seleção múltipla** na hierarquia e no viewport (Ctrl/Shift, retângulo), com gizmo e Inspector para vários.
- **Copiar, recortar, colar e duplicar** várias entidades (com filhos), também entre cenas.
- **Snapping** na grade, em ângulos e em passos de escala; grade visível configurável.
- **Busca na hierarquia** (nome e tipo de componente).
- **Janelas Cena e Jogo lado a lado** no Play. *(Limitação da Fase 2.)*
- **Prefab dentro de prefab.** *(Limitação da Fase 6.)*
- **Edições de material e de prefab no desfazer**, e editadas no Play voltam ao parar. *(Limitação da Fase 6.)*
- **Modelos:** malhas de modelo no combo "Malha"; pré-visualizar animação fora do Play. *(Fase 11.)*
- **MCP acompanha:** seleção múltipla, copiar/colar, mover a câmera do editor para um ponto, eventos para o
  agente. *(Pendências da Fase 13.)*

**Pronto quando:** montar uma cena com 50 objetos alinhados na grade, duplicados em grupo e reorganizados por
busca é rápido só com o editor, e o desfazer cobre tudo, inclusive materiais e prefabs.

## Fase 21 — Jogo entregue

- **Opções do jogador:** tela cheia, resolução, vsync e qualidade (sombras, MSAA — hoje sempre 4x, o
  `Renderer::SetSamples` existe mas o projeto não o expõe) num arquivo de configurações do jogo, com API para
  os scripts montarem um menu de opções. Título da janela sem o FPS. *(Fase 10; análise, item 3.)*
- **Identidade:** ícone e nome do `.exe` nas configurações do projeto; versão do jogo.
- **Exportação sempre Release:** Player e `libMotor.dll` de Release da versão instalada, e a DLL de scripts
  recompilada em Release ao exportar. *(Fase 10.)*
- **Salvar progresso:** API de dados persistentes para scripts (chave/valor e arquivos em `%APPDATA%\<Jogo>\`),
  com exemplo no guia.
- **Controle (gamepad)** pela GLFW, com mapeamento de ações ("Pular" = Espaço ou botão A) configurável no
  projeto. *(Análise, item 9: "WASD ou analógico".)*
- **Tela de carregamento** simples ao trocar de cena.

**Pronto quando:** o jogo da Fase 19 exportado abre em tela cheia com ícone próprio, tem menu de opções e de
continuar (save), e é jogável com controle.

---

## Fora da alpha 2

- **Desempenho** (sombras, renderer em lote, cena e editor que escalam): alpha 3.
- **Cenários abertos** (terreno, água, vegetação, partículas) e **renderização 2** (SSAO, reflexos locais):
  fim da alpha 3, depois do desempenho, porque vegetação e partículas dependem de instancing e de sombras
  baratas.
- **Compilador integrado**, atualização do Hub, https e scripts isolados: alpha 4.
- **Depois da alpha 4:** rede (multiplayer), navegação (pathfinding), Lua, glTF com morph targets e câmeras,
  partículas na GPU, céu com nuvens e ciclo de dia e noite.

## Ordem e dependências

```
Fase 16 Robustez e correções ─┬─ Fase 19 Validação ──┬─ Fase 20 Editor produtivo
Fase 17 Scripts ──────────────┤                      └─ Fase 21 Jogo entregue
Fase 18 Funcionalidades ──────┘
```

Fases que não dependem uma da outra podem rodar em paralelo, cada uma no seu branch, respeitando as áreas do
[CLAUDE.md](../../CLAUDE.md) ("Trabalho em paralelo").
