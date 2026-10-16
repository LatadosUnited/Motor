# Roteiro do Motor

O desenvolvimento dividido em **módulos**, na ordem em que um depende do outro. Cada fase termina com
algo que dá para usar e testar no editor, e só começa quando a anterior estiver de pé.

## Onde estamos

| Área | Estado |
|---|---|
| Núcleo (janela, input, loop, log) | ✅ pronto |
| Renderização básica (Blinn-Phong, até 32 luzes, neblina, MSAA) | ✅ pronto |
| Cena (entidades, hierarquia, transform) | ✅ pronto |
| Sistema de componentes (registro, Inspector/serialização/desfazer automáticos) | ✅ pronto (Fase 1) |
| Modo Play (Play/Pausa/Parar, câmera do jogo, cena restaurada ao parar) | ✅ pronto (Fase 2) |
| Scripts C++ por projeto (DLL compilada pelo editor, recarga sem fechar) | ✅ pronto (Fase 3) |
| Luzes (direcional, pontual, spot como componentes) e ícones no viewport | ✅ pronto (Fase 4) |
| Física (Jolt: colliders, rigidbody, triggers, raycast, personagem) | ✅ pronto (Fase 5) |
| Materiais (`.mmat`) e prefabs (`.mprefab`) com overrides | ✅ pronto (Fase 6) |
| Git + testes `ctest` | ✅ pronto (Fase 0) |
| Editor (hierarquia, inspector, viewport, gizmos, desfazer) | ✅ pronto |
| Projeto e assets (pastas, `.mscene`, PNG/JPG, OBJ, GUID em `.meta`) | ✅ pronto |
| Base para trabalho em paralelo (camadas, peças do editor, extensões; ver CLAUDE.md) | ✅ pronto |

**Usável hoje:** prototipar dentro do editor (cenas, luzes, física, scripts com recarga, Play).
**Ainda não:** entregar um jogo. Falta exportar (Fase 10), interface do jogo (9), som (8), reaproveitar
objetos e materiais (6), modelos além de OBJ e animação (11) e o visual moderno (7). O uso real pelo
editor (cliques, atalhos, Play com física) foi pouco testado à mão.

## Prioridades

Próximas fases, nesta ordem de importância:

| # | Fase | Por quê | Pode rodar junto com |
|---|---|---|---|
| 1 | **Fase 9 — Interface do jogo (HUD)** | sem texto e botões na tela não há jogo (vida, pontos, menus) | 6, 12 |
| 2 | **Fase 6 — Materiais e prefabs** | reaproveitar objetos e materiais; base dos modelos 3D | 9, 12 |
| 3 | **Fase 11 — Modelos 3D (glTF) e animação** | personagens e cenários de verdade, além de OBJ | 9, 12 (depois da 6) |
| 4 | **Fase 12 — Documentação de scripts** | quem faz o jogo precisa saber escrever scripts sem ler o código da engine | todas (só documentação e exemplos) |

Depois delas: Fase 10 (exportar), Fase 8 (áudio) e Fase 7 (renderização avançada). A 11 depende da 6
(o glTF traz materiais); 6 e 11 mexem nos mesmos arquivos de assets e não devem rodar ao mesmo tempo.

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
nas malhas embutidas (Cubo, Plano) usa a caixa dos limites, porque a `Mesh` só guarda os dados na GPU
(resolvido na Fase 11, com a cópia na CPU).

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

## Fase 7 — Renderização avançada

- Sombras da luz direcional (shadow map com cascatas) e das pontuais.
- PBR (metal/rugosidade), mapas de normal, skybox/cubemap e iluminação ambiente por imagem.
- Pós-processamento: HDR, tone mapping, bloom, FXAA.

**Pronto quando:** a mesma cena fica visivelmente "de jogo moderno" sem queda grande de FPS.

## Fase 8 — Áudio

- Componentes **AudioSource** (som 2D/3D, loop, volume) e **AudioListener** (na câmera).
- Biblioteca: miniaudio (um único header, como o stb_image).
- Arquivos `.wav`/`.ogg`/`.mp3` como assets com GUID.

## Fase 9 — Interface do jogo (HUD) ⭐ prioridade 1

- Texto e imagens na tela durante o jogo (independente do ImGui do editor).
- Botões simples, âncoras na tela, fontes como asset.
- Camada nova `ui/`, desenhada depois da cena; componentes de UI registrados (`MOTOR_COMPONENT_MODULE`) e
  sistema do `SceneRuntime` para os cliques; API para os scripts (mudar texto, mostrar/esconder, botão clicado).
- Editável no editor: os elementos aparecem por cima do viewport na visão do jogo.

**Pronto quando:** um jogo de exemplo mostra vida e pontuação atualizadas por script e um menu com botão
"Jogar" que troca de cena.

## Fase 10 — Exportar o jogo

- Executável **Player** sem o editor: abre o projeto e roda a cena inicial direto em Play.
- Empacotar os assets usados (só os referenciados) numa pasta de saída.
- Menu Arquivo > Exportar jogo.

**Pronto quando:** uma pasta com o .exe e os assets roda o jogo num computador sem o editor.

## Fase 11 — Modelos 3D (glTF) e animação ⭐ prioridade 3

Depende da Fase 6 (o glTF traz materiais e vira prefab).

- Importar **glTF 2.0** (`.gltf`/`.glb`) como asset com GUID: malhas, hierarquia de nós, materiais e
  texturas. O arquivo vira um prefab que dá para arrastar para a cena. Biblioteca a decidir (ex.: cgltf, um
  único header), com aprovação do usuário.
- A `Mesh` guarda uma cópia das posições e índices na CPU (raycast exato e collider de malha da física para
  qualquer malha, inclusive as embutidas).
- **Animação esquelética:** esqueleto e skinning no shader, componente Animator (tocar, misturar e trocar de
  clipe), clipes do glTF como assets; API para os scripts.

**Pronto quando:** um personagem glTF animado anda pela cena controlado pelo script Jogador, trocando entre
as animações parado, andando e pulando.

## Fase 12 — Documentação de scripts ⭐ prioridade 4

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

## Depois

- Terreno, partículas, navegação (pathfinding).
- Editor: múltiplas seleções na cena, copiar/colar entidades, snapping na grade, busca na hierarquia.

---

## Ordem e dependências

```
Fase 0 Git/testes
   └─ Fase 1 Componentes ──┬─ Fase 2 Play ── Fase 3 Scripts ──┬─ Fase 5 Física ✅
                           │                                  ├─ Fase 8 Áudio
                           ├─ Fase 4 Luzes/câmera ✅          ├─ Fase 9 HUD           ⭐1
                           ├─ Fase 6 Materiais/prefabs ⭐2    ├─ Fase 10 Exportar
                           │     └─ Fase 11 Modelos 3D ⭐3    └─ Fase 12 Doc. scripts ⭐4
                           └─ Fase 7 Renderização avançada (depende da 4)
```

As prioridades (⭐) estão na seção "Prioridades", no topo. Fases que não dependem uma da outra podem rodar em
paralelo, cada uma no seu branch, respeitando as áreas do CLAUDE.md ("Trabalho em paralelo").
