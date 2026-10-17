# Roteiro do Motor

O desenvolvimento dividido em **módulos**, na ordem em que um depende do outro. Cada fase termina com
algo que dá para usar e testar no editor, e só começa quando a anterior estiver de pé.

## Onde estamos

| Área | Estado |
|---|---|
| Núcleo (janela, input, loop, log) | ✅ pronto |
| Renderização (PBR, sombras, céu HDR e luz do céu, HDR com bloom/tone mapping/FXAA, até 32 luzes) | ✅ pronto (Fase 7) |
| Cena (entidades, hierarquia, transform) | ✅ pronto |
| Sistema de componentes (registro, Inspector/serialização/desfazer automáticos) | ✅ pronto (Fase 1) |
| Modo Play (Play/Pausa/Parar, câmera do jogo, cena restaurada ao parar) | ✅ pronto (Fase 2) |
| Scripts C++ por projeto (DLL compilada pelo editor, recarga sem fechar) | ✅ pronto (Fase 3) |
| Luzes (direcional, pontual, spot como componentes) e ícones no viewport | ✅ pronto (Fase 4) |
| Física (Jolt: colliders, rigidbody, triggers, raycast, personagem) | ✅ pronto (Fase 5) |
| Materiais (`.mmat`) e prefabs (`.mprefab`) com overrides | ✅ pronto (Fase 6) |
| Modelos glTF (`.gltf`/`.glb`) e animação esquelética (Animator) | ✅ pronto (Fase 11) |
| Interface do jogo (texto, imagens, botões, fontes SDF, troca de cena) | ✅ pronto (Fase 9) |
| Guia e referência de scripts (`docs/`), com os exemplos testados | ✅ pronto (Fase 12) |
| Servidor MCP do editor (agentes veem e mexem no editor e testam o jogo) | ✅ pronto (Fase 13) |
| Git + testes `ctest` | ✅ pronto (Fase 0) |
| Editor (hierarquia, inspector, viewport, gizmos, desfazer) | ✅ pronto |
| Projeto e assets (pastas, `.mscene`, PNG/JPG, OBJ, GUID em `.meta`) | ✅ pronto |
| Base para trabalho em paralelo (camadas, peças do editor, extensões; ver CLAUDE.md) | ✅ pronto |

**Alpha 1 concluída** (16/10/2026): Fases 0 a 7, 9, 11, 12 e 13. Dá para montar e jogar dentro do editor
cenas com visual moderno (PBR, sombras, céu HDR, pós-processamento), luzes, física, scripts com recarga,
modelos glTF animados e HUD, e um agente consegue fazer o mesmo pelo servidor MCP.
**Ainda não:** entregar um jogo. Falta exportar (Fase 10) e som (8), os próximos passos, que abrem a alpha
2. Depois deles vem uma validação de uso real (o editor foi pouco testado à mão; ver "Anotado para a alpha
2") e, então, a fase "Ambiente" (água, terreno, partículas e vegetação sobre a base da Fase 7).

## Prioridades

Próximas fases, nesta ordem de importância:

| # | Fase | Por quê | Pode rodar junto com |
|---|---|---|---|
| 1 | ~~Fase 9 — Interface do jogo (HUD)~~ ✅ | sem texto e botões na tela não há jogo (vida, pontos, menus) | 11 |
| 2 | ~~Fase 6 — Materiais e prefabs~~ ✅ | reaproveitar objetos e materiais; base dos modelos 3D | — |
| 3 | ~~Fase 11 — Modelos 3D (glTF) e animação~~ ✅ | personagens e cenários de verdade, além de OBJ | — |
| 4 | ~~Fase 12 — Documentação de scripts~~ ✅ | quem faz o jogo precisa saber escrever scripts sem ler o código da engine | — |

As quatro estão prontas, e as Fases 13 (servidor MCP) e 7 (renderização avançada) também: a Alpha 1 está
fechada. A 11 depende da 6 (o glTF traz materiais); 6 e 11 mexem nos mesmos arquivos de assets e não devem
rodar ao mesmo tempo.

Alpha 2, nesta ordem:

| # | Passo | Por quê | Pode rodar junto com |
|---|---|---|---|
| 5 | Fase 10 — Exportar o jogo | sem exportar não há jogo entregue, só o editor | 8 |
| 6 | Fase 8 — Áudio | jogo sem som não está completo | 10 (a exportação precisa empacotar os sons) |
| 7 | Validação de uso real | depois de 8 e 10: fazer um jogo pequeno do começo ao fim, exportado, testando à mão o que o MCP não cobre (ver "Anotado para a alpha 2") | — |
| 8 | Desfazer que escala e arquivos grandes divididos | ver "Anotado para a alpha 2" | Ambiente (áreas diferentes) |
| 9 | Fase "Ambiente" | água, terreno, partículas e vegetação sobre a base da Fase 7 | — |

---

## Fase 0 — Controle de versão ✅

A pasta ainda não é um repositório git. Antes das refatorações grandes:

- `git init`, `.gitignore` (já existe), primeiro commit.
- Testes sem interface que fizemos até aqui (projeto, assets, GUID, desfazer) viram alvos `ctest`.

**Pronto quando:** `ctest` roda todos os testes e cada fase seguinte vira um commit (ou branch).

## Fase 1 — Sistema de componentes ✅

Trocar o `std::optional<MeshRenderer>` fixo por componentes registráveis.

- Registro de tipos de componente: nome, criar, copiar, comparar, salvar/carregar (JSON), desenhar no Inspector.
- Campos descritos uma vez (reflexão simples): o Inspector e o serializador são gerados a partir deles.
- `MeshRenderer` migrado para o sistema novo; cenas antigas continuam abrindo.
- "Adicionar componente" no Inspector lista tudo que está registrado.

**Pronto quando:** criar um componente novo é escrever uma struct + uma linha de registro, e ele já
aparece no Inspector, é salvo na cena e entra no desfazer sem tocar em mais nada.

## Fase 2 — Modo Play ✅

- Botões ▶ Play / ⏸ Pausa / ⏭ Avançar quadro / ⏹ Parar no topo do viewport, menu Jogo e Ctrl+P.
- Ao dar Play, a cena é copiada; ao parar, volta como estava (`editor/src/PlayMode`).
- Loop de jogo separado do editor (`motor::SceneRuntime`): `OnStart`, `OnUpdate(dt)`, `OnFixedUpdate` só rodam em Play.
- Componente **Câmera** (fov, perto, longe, principal): o Play renderiza pela câmera principal da cena;
  F8 alterna para a câmera livre do editor sem parar.
- Borda azul (laranja em pausa) no viewport e aviso na barra de menus; desfazer desligado e salvar bloqueado
  durante o Play; alterações feitas no Play são descartadas.

Limitações conhecidas: a câmera do jogo ignora a rotação em torno do próprio eixo (roll); o viewport é um
só (não há janelas "Cena" e "Jogo" lado a lado).

## Fase 3 — Scripts ✅

- `motor::Script` em C++: `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy`, acesso à entidade, ao
  transform, à entrada (filtrada pelo foco do jogo) e ao tempo do jogo.
- Scripts são componentes registrados: campos no Inspector, na cena e no desfazer sem código extra.
- API mínima: achar entidade por nome, criar/destruir (adiado para o fim do passo, com filhos), log.
- **Scripts por projeto com recarga:** os `.cpp` da pasta `Scripts/` do projeto viram uma DLL compilada pelo
  editor (ao abrir o projeto, ao salvar um arquivo, F7); a DLL é recarregada sem fechar o editor (adiada até
  parar o Play), com o estado dos componentes preservado. A engine é a `libMotor.dll`, compartilhada.
- Componente de script que não carregou (removido, renomeado, com erro) fica guardado como "desconhecido",
  com os dados, até o script voltar. Painel e menu Scripts mostram erros e a saída da compilação.
- Modelos de exemplo copiados para projetos novos: Girar, MoverWASD, Seguir, Jogador.

Depois (opcional): Lua como linguagem de script leve.

## Fase 4 — Luzes e câmera completas ✅

- Componentes **Luz**: direcional, pontual e spot; várias por cena (o shader passa a aceitar N luzes).
- A luz direcional sai do painel "Cena" e vira um componente (cenas antigas convertidas ao abrir).
- Ícones no viewport para luzes e câmeras (selecionáveis, com gizmo). O componente Câmera já existe (Fase 2).

**Pronto quando:** uma cena noturna com várias lâmpadas pontuais é montada só pelo editor.

## Fase 5 — Física ✅

- Componentes **Collider** (caixa, esfera, cápsula, malha estática, trigger) e **Rigidbody** (massa,
  gravidade, cinemático).
- Biblioteca: Jolt Physics v5.6.0 (multithread e SIMD; SSE4.2 como base para o jogo rodar em CPUs sem AVX2).
  Camada `physics/`, sistema do `SceneRuntime` (`MOTOR_RUNTIME_SYSTEM`) que roda antes dos scripts.
- Eventos para scripts: `OnCollisionEnter/Exit`, `OnTriggerEnter/Exit`; raycast, forças e velocidade pela API.
- Controlador de personagem (CharacterVirtual do Jolt) e modelo de script Jogador (WASD + Espaço).
- Visualização dos colliders no editor (botão "Colliders" no viewport).
- Desempenho (`test_physics_bench`, i5 de 4 núcleos): ~2 ms por passo com 1000 caixas, ~7 ms com 4000.

Pendências: picos isolados de até ~185 ms em passos soltos no teste de carga (investigar); collider de malha
nas malhas embutidas (Cubo, Plano) usava a caixa dos limites, porque a `Mesh` só guardava os dados na GPU
(✅ resolvido na Fase 11, com a cópia na CPU).

## Fase 6 — Materiais e prefabs como assets ✅

- **Material** (`.mmat`): cor, textura, especular e brilho; o Mesh Renderer aponta para ele por GUID
  (`sharedMaterial`) e todos os objetos compartilham o mesmo objeto, então editar muda todos. Sem asset,
  vale o material próprio do componente: cenas antigas abrem sem perder nada. Criar no painel Projeto ou
  pelo botão "Salvar como asset"; editar no Inspector (gravado ao soltar o controle); arquivos editados
  fora do editor são relidos.
- **Prefab** (`.mprefab`): entidade com filhos no mesmo formato da cena; cada entidade da instância tem o
  componente `PrefabInstance` (GUID do prefab, id estável da entidade no prefab, overrides). As instâncias
  seguem o prefab a cada mudança (inclusive entidades e componentes novos ou removidos); o que foi mudado na
  instância vira override (comparando com o prefab). Aplicar, reverter e desvincular; criar pela Hierarquia
  ou arrastando para o painel Projeto; instanciar arrastando para o viewport ou a Hierarquia.
- Cena versão 5; `kScriptApiVersion` 6. Testes: `test_materials`, `test_prefabs`.
- Pronto para a Fase 11: o importador glTF pode gravar `.mmat` (`AssetLibrary::WriteMaterialFile`) e montar
  a hierarquia do modelo como prefab (`PrefabLibrary::WriteFile`).

Limitações: sem prefab dentro de prefab; edições de material e de prefab não entram no desfazer (são
gravadas no arquivo na hora), e editá-los durante o Play também fica valendo depois de parar; scripts ainda
não instanciam prefabs.

**Pronto quando:** mudar a cor de um material ou de um prefab atualiza todas as instâncias na cena. ✅

## Fase 7 — Renderização avançada ✅

- Sombras da luz direcional (shadow map com cascatas) e das pontuais.
- PBR (metal/rugosidade), mapas de normal, skybox/cubemap e iluminação ambiente por imagem.
- Pós-processamento: HDR, tone mapping, bloom, FXAA.

**Pronto quando:** a mesma cena fica visivelmente "de jogo moderno" sem queda grande de FPS. ✅

Feito:
- O renderer guarda os pedidos (`Submit*`) e desenha tudo no `EndScene`, em passadas: sombras → cena em HDR
  (RGBA16F, MSAA 4x) com o céu ao fundo → bloom → tone mapping (ACES, Reinhard, nenhum) e gama → FXAA →
  contornos do editor (testam a profundidade da cena numa textura). Peças privadas em `engine/src/renderer/`:
  `Shadows`, `Sky`, `PostProcess`; `Shader::FromFiles` aceita `#include` e variantes por `#define`.
- **Sombras:** a primeira luz direcional com sombra usa 4 cascatas (divisão "prática", esfera estável e
  centro preso à grade de texels, mistura entre cascatas, PCF com comparação no hardware); pontuais e spots
  usam um cubemap array com a distância até a luz (até 4, as mais perto da câmera que iluminam algo
  visível). Componente Luz com "Projeta sombra" e "Qualidade" (baixa/média/alta = 1024/2048/4096 por
  cascata, 256/512/1024 por face). Funciona com malhas animadas (variante com pele do shader de sombra).
  Recorte por frustum na cena e em cada passada de sombra.
- **PBR:** Cook-Torrance com GGX e Smith com correlação. `Material` com metal, rugosidade, reflexo, mapas
  de normal (base tangente pelas derivadas da tela, sem tangentes nos vértices), metal/rugosidade (como no
  glTF), oclusão e emissão. Mapas de dados lidos sem sRGB (texture view linear). `.mmat` versão 2; a 1 é
  convertida (brilho → rugosidade). O importador glTF lê fatores e mapas (inclui
  `KHR_materials_emissive_strength`).
- **Céu:** cor (degradê) ou imagem HDR equirretangular (`.hdr`, asset "Céu HDR", stb_image que já existia),
  com brilho e rotação. Luz do céu: irradiância em harmônicos esféricos (calculada na CPU) e reflexo
  pré-filtrado por rugosidade (cubemap com mips) com a tabela da BRDF (CPU). A neblina usa o horizonte.
- **Ambiente** no painel Cena e no `scene_environment` do MCP: céu, luz do céu, neblina, luz ambiente,
  distância das sombras, exposição, tone mapping, bloom (intensidade e limiar) e FXAA. Cena versão 6 (as
  antigas abrem com os padrões; o "shininess" do Mesh Renderer vira "roughness"). `kScriptApiVersion` 9.
- **Desempenho:** contadores no viewport (draws, draws e passes de sombra, triângulos). Na cena de exemplo
  com o robô animado e uma lâmpada (editor Debug, i5): ~48 FPS contra ~50 antes; 71 draws (9 da cena, o
  resto das 4 cascatas e das 6 faces da lâmpada).
- Base para a fase "Ambiente": `Renderer::SceneDepthTexture()`, `SceneColorTexture()` (HDR antes do pós),
  `SkyCubemap()` e `ReflectionCubemap()`.
- Testes sem GPU: `test_render` (cascatas, recorte, faces do cubo, BRDF e amostragem, harmônicos, céu,
  imagem HDR, cena v6 e migração); `test_materials`, `test_gltf` e `test_mcp` atualizados.
- Verificado pelo servidor MCP do editor: a mesma cena antes e depois, com e sem Play, com céu HDR e de noite.

Limitações: sem oclusão de ambiente na tela (SSAO), reflexos só do céu (sem probes nem SSR), sem exposição
automática, sem objetos transparentes ordenados; modelos importados antes da Fase 7 só ganham os mapas PBR
ao importar de novo (menu do painel Projeto); spots usam um cubo inteiro de sombra.

## Fase 8 — Áudio

- Componentes **AudioSource** (som 2D/3D, loop, volume) e **AudioListener** (na câmera).
- Biblioteca: miniaudio (um único header, como o stb_image).
- Arquivos `.wav`/`.ogg`/`.mp3` como assets com GUID.

## Fase 9 — Interface do jogo (HUD) ✅

- Texto e imagens na tela durante o jogo (independente do ImGui do editor).
- Botões simples, âncoras na tela, fontes como asset.
- Camada nova `ui/`, desenhada depois da cena; componentes de UI registrados (`MOTOR_COMPONENT_MODULE`) e
  sistema do `SceneRuntime` para os cliques; API para os scripts (mudar texto, mostrar/esconder, botão clicado).
- Editável no editor: os elementos aparecem por cima do viewport na visão do jogo.

**Pronto quando:** um jogo de exemplo mostra vida e pontuação atualizadas por script e um menu com botão
"Jogar" que troca de cena.

Feito: camada `ui/` (componentes `UiRect`, `UiText`, `UiImage`, `UiButton`; layout com âncoras numa tela de
referência de 1080 de altura; fontes `.ttf`/`.otf` como asset, em atlas SDF pelo `imstb_truetype.h` do
ImGui; `UiRenderer` com o `Renderer2D` novo: um VBO dinâmico, lotes por textura e geometria em cache),
`UiSystem` (cliques, `SetText`, `SetVisible`, `WasClicked`) e `Script::LoadScene` (troca de cena no Play).
No editor, a UI aparece por cima do viewport, com seleção e arraste. Exemplos `docs/examples/Hud.cpp` e
`MenuPrincipal.cpp`, rodados pelo `test_script_docs`; testes em `tests/test_ui.cpp`.

## Fase 10 — Exportar o jogo

- Executável **Player** sem o editor: abre o projeto e roda a cena inicial direto em Play.
- Empacotar os assets usados (só os referenciados) numa pasta de saída.
- Menu Arquivo > Exportar jogo.

**Pronto quando:** uma pasta com o .exe e os assets roda o jogo num computador sem o editor.

## Fase 11 — Modelos 3D (glTF) e animação ✅

Depende da Fase 6 (o glTF traz materiais e vira prefab).

- Importar **glTF 2.0** (`.gltf`/`.glb`) como asset com GUID: malhas, hierarquia de nós, materiais e
  texturas. O arquivo vira um prefab que dá para arrastar para a cena. Biblioteca a decidir (ex.: cgltf, um
  único header), com aprovação do usuário.
- A `Mesh` guarda uma cópia das posições e índices na CPU (raycast exato e collider de malha da física para
  qualquer malha, inclusive as embutidas).
- **Animação esquelética:** esqueleto e skinning no shader, componente Animator (tocar, misturar e trocar de
  clipe), clipes do glTF como assets; API para os scripts.

**Pronto quando:** um personagem glTF animado anda pela cena controlado pelo script Jogador, trocando entre
as animações parado, andando e pulando. ✅

Feito:
- Biblioteca **cgltf v1.15** (um header C99, MIT, sem dependências), fixada por URL + SHA256. `LoadGltf` lê
  `.gltf` (data URIs ou `.bin`/imagens externos, caminhos UTF-8) e `.glb` para `ModelData` (CPU); `BuildModel`
  cria o asset `Model` (uma `Mesh` por primitiva, esqueleto com todos os nós, peles, clipes).
- Asset com GUID (`AssetType::Model`); malhas e clipes são partes do arquivo: `"<guid>#<nome>"`. O modelo é
  relido se o arquivo mudar.
- `ImportModel` gera `<nome>.mprefab` e `<nome>_materiais/` (`.mmat` da Fase 6 e texturas embutidas); o
  editor importa os modelos novos ou alterados sozinho, e arrastar o `.glb` instancia o prefab.
- `Mesh` guarda posições e índices na CPU e só sobe para a GPU no primeiro desenho: raycast exato do editor
  (triângulos) e collider de malha da física para qualquer malha (pendência da Fase 5).
- Skinning no shader (`skinned.vert`, até 128 juntas, UBO), `Renderer::SubmitSkinned`; componente **Animator**
  (Play, CrossFade, Stop, velocidade, loop), `AnimationSystem` no `SceneRuntime` (`kSystemOrderAnimation`,
  depois dos scripts), `Script::GetAnimator()`. `kScriptApiVersion` 8 (7 é o da Fase 9).
- Modelo de exemplo `engine/assets/models/Robo.glb` (gerado por `gerar_robo.py`) e o script Jogador trocando
  Parado/Andando/Pulando. Guia e referência dos scripts com a seção Animação. Teste: `test_gltf`.

Limitações: sem morph targets, câmeras e luzes do glTF; só juntas são animadas (nós sem pele ficam parados);
materiais PBR aproximados (normal, metal e emissão ficam para a Fase 7); malhas de modelo não aparecem no combo
"Malha" do Inspector; sem pré-visualização da animação fora do Play.

## Fase 12 — Documentação de scripts ✅

Hoje o uso de scripts está espalhado no README e nos headers. Quem faz o jogo deve conseguir escrever scripts
sem ler o código da engine.

- Guia passo a passo em `docs/scripts.md`: criar o primeiro script, campos no Inspector (`Field` e opções),
  ciclo de vida (`OnStart` a `OnDestroy`), entrada, tempo, achar/criar/destruir entidades, física (eventos,
  raycast, personagem), sistemas (`GetSystem`), recarga e o que é preservado, erros comuns de compilação.
- Referência da API dos scripts, mantida junto com `Script.h` e `ScriptModule.h`.
- Receitas: jogador, câmera que segue, porta com trigger, inimigo que persegue, coletável com pontuação.
- Os modelos de `editor/templates/Scripts/` comentados como exemplos da documentação, e um teste que
  compila todos os exemplos do guia (para ele não ficar desatualizado).

**Pronto quando:** alguém que sabe C++ básico, sem conhecer o Motor, faz um mini jogo (andar, coletar,
pontuar) só com o guia.

## Fase 13 — Servidor MCP do editor ✅

Para um agente (Claude Code) usar o editor como uma pessoa usaria, sem simular mouse nem teclado do sistema:
- Servidor MCP (transporte HTTP, JSON-RPC) dentro do editor, em `127.0.0.1:47800`. Liga pelo menu Ver ou
  com `--mcp`. Biblioteca: cpp-httplib v0.58.0, só no editor. Recusa pedidos vindos de páginas da web.
- 30 ferramentas:
  - cena: hierarquia, entidades, componentes e campos, ambiente;
  - desfazer e refazer;
  - arquivos: cenas, assets, prefabs, materiais e texturas;
  - Play, com teclas e cliques de UI simulados só para o jogo;
  - scripts: compilar e ver os erros;
  - log e captura do viewport em PNG.
- Os pedidos são atendidos na thread principal, uma vez por quadro, pelos mesmos caminhos da interface. Por
  isso cada mudança vira um passo de desfazer.
- Testado no editor de verdade: um script recebeu tecla e clique simulados, e a captura do Play saiu pela câmera
  do jogo.

Corrigido depois (5a95745): com a janela minimizada o MCP parava de responder. Minimizado, o loop pulava o
quadro inteiro, inclusive o `OnImGui`, onde rodava o `Pump` do MCP, e os pedidos davam tempo esgotado (o editor
parecia travado); sem o `SwapBuffers` também não havia espera do vsync e o loop girava a milhares de FPS. Agora
a parte do quadro sem interface (pedidos de Play e desfazer, `Pump` do MCP, exclusões, prefabs e o passo de
desfazer) roda também minimizado (`EditorApp::UpdateMinimized`), e o loop dorme 10 ms por volta.

Pendências: mover a câmera do editor para um ponto escolhido, além de focar uma entidade; arrastar o gizmo e
outras ações só de mouse; notificações para o agente (hoje ele pergunta com `editor_status` e `log_read`).

## Anotado para a alpha 2

Problemas encontrados por um agente testando o editor pelo MCP (16/10/2026):

- **Pasta vazia não vira projeto.** Abrir o editor numa pasta vazia mostra só a cena de exemplo, e o MCP
  não tem como criar projeto. O agente precisou montar o `.mproject` e a pasta `Scripts/` à mão, copiando os
  modelos da engine, e reabrir o editor apontando para o projeto. Saídas possíveis (dá para fazer as duas):
  - ferramenta `project_create` no MCP (nome e pasta), com o mesmo resultado de Arquivo > Novo projeto;
  - criar o projeto sozinho quando o editor abre uma pasta vazia.
- **Agentes mexem na configuração do usuário.** Abrir o editor grava `%APPDATA%\Motor\editor.json` (último
  projeto, recentes). Testes automáticos esquecem a cópia que o CLAUDE.md pede; aconteceu duas vezes: o
  último projeto do usuário virou um projeto de teste. Saída: opção para o editor usar outro arquivo de
  configuração ou não gravar nenhum (ex.: `--settings <arquivo>` ou `--no-settings`), usada pelos agentes.
- **Falta testar à mão** (o MCP não cobre): teclado e mouse de verdade no viewport (câmera livre, seleção
  por clique, foco do jogo), menus, gizmo e arrastar arquivos (do painel Projeto e do Explorer). Entra na
  validação de uso real, depois das Fases 8 e 10: um jogo pequeno feito no editor do começo ao fim e exportado.

Pontos de atenção da revisão do código (17/10/2026):

- **Desfazer não escala.** `UndoHistory::Track` compara a cena inteira com a foto anterior a cada quadro
  (`Scene::operator==`) e guarda até 100 cópias completas da cena. É simples e correto hoje, mas com cenas
  de milhares de entidades (ou terreno e vegetação da fase "Ambiente") pesa em CPU e memória. Saídas
  possíveis: comparar só as entidades marcadas como alteradas no quadro (marca de "sujo" ao escrever num
  campo), guardar diferenças por entidade em vez da cena inteira, ou limitar o histórico por memória e não
  por número de passos. Medir antes (cena de teste com 5000 entidades) e manter o desfazer automático: o
  componente novo continua entrando nele sem código extra.
- **Arquivos crescendo.** `editor/src/ProjectPanel.cpp` (~1200 linhas) e `engine/src/physics/PhysicsSystem.cpp`
  (~940) são os maiores; vêm depois `AssetLibrary.cpp`, `SceneSerializer.cpp`, `SceneViewport.cpp`,
  `McpController.cpp` e `SceneDocument.cpp` (~580 a 600). Dividir por assunto sem mudar comportamento (ex.:
  no painel Projeto, a árvore de pastas, a grade de arquivos, as operações de arquivo e a importação; na
  física, corpos, personagem, eventos e consultas), um arquivo por vez, cada um num commit, com o `ctest`
  passando. Melhor fazer quando a área não tiver outra tarefa em andamento.

## Depois

- Terreno, partículas, navegação (pathfinding).
- Editor: múltiplas seleções na cena, copiar/colar entidades, snapping na grade, busca na hierarquia.

---

## Ordem e dependências

```
Fase 0 Git/testes
   └─ Fase 1 Componentes ──┬─ Fase 2 Play ── Fase 3 Scripts ──┬─ Fase 5 Física ✅
                           │                                  ├─ Fase 8 Áudio
                           ├─ Fase 4 Luzes/câmera ✅          ├─ Fase 9 HUD           ✅
                           ├─ Fase 6 Materiais/prefabs ✅     ├─ Fase 10 Exportar
                           │     └─ Fase 11 Modelos 3D ✅     └─ Fase 12 Doc. scripts ✅
                           └─ Fase 7 Renderização avançada ✅
```

As prioridades (⭐) estão na seção "Prioridades", no topo. Fases que não dependem uma da outra podem rodar em
paralelo, cada uma no seu branch, respeitando as áreas do CLAUDE.md ("Trabalho em paralelo").
