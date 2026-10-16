# Roteiro do Motor

O desenvolvimento dividido em **módulos**, na ordem em que um depende do outro. Cada fase termina com
algo que dá para usar e testar no editor, e só começa quando a anterior estiver de pé.

## Onde estamos

| Área | Estado |
|---|---|
| Núcleo (janela, input, loop, log) | ✅ pronto |
| Renderização básica (Blinn-Phong, 1 luz direcional, neblina, MSAA) | ✅ pronto |
| Cena (entidades, hierarquia, transform) | ✅ pronto |
| Sistema de componentes (registro, Inspector/serialização/desfazer automáticos) | ✅ pronto (Fase 1) |
| Modo Play (Play/Pausa/Parar, câmera do jogo, cena restaurada ao parar) | ✅ pronto (Fase 2) |
| Scripts C++ compilados com o editor (Girar, Mover com WASD, Seguir) | ✅ pronto (Fase 3; hot reload pendente) |
| Git + testes `ctest` | ✅ pronto (Fase 0) |
| Editor (hierarquia, inspector, viewport, gizmos, desfazer) | ✅ pronto |
| Projeto e assets (pastas, `.mscene`, PNG/JPG, OBJ, GUID em `.meta`) | ✅ pronto |

Com as Fases 0 e 1 prontas, as fases seguintes podem andar em paralelo (cada uma no seu branch):
um recurso novo é um componente registrado, sem mexer em `Entity`, Inspector, serializador ou desfazer.

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

## Fase 3 — Scripts ✅ (hot reload pendente)

- `motor::Script` em C++: `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy`, acesso à entidade, ao
  transform, à entrada (filtrada pelo foco do jogo) e ao tempo do jogo.
- Scripts são componentes registrados: campos no Inspector, na cena e no desfazer sem código extra.
- API mínima: achar entidade por nome, criar/destruir (adiado para o fim do passo, com filhos), log.
- Scripts compilados junto com o editor (`scripts/`, biblioteca `MotorScripts`): Girar, Mover com WASD, Seguir.

**Próximo passo — hot reload por DLL:**

1. `scripts/` vira uma DLL (`MotorScripts.dll`) que exporta `RegisterAll` e `UnregisterAll`; a engine
   passa a ser uma DLL também (ou exporta o registro), para os dois lados usarem o mesmo `ComponentRegistry`.
2. O editor observa a pasta de scripts do projeto, recompila em segundo plano (CMake) e, só fora do Play:
   salva a cena em JSON na memória, remove os componentes dos tipos da DLL, descarrega a DLL antiga
   (copiada antes com outro nome, para o compilador poder sobrescrever a original), carrega a nova,
   registra de novo e relê a cena do JSON.
3. `ComponentRegistry` precisa de `Unregister(name)` e os `ComponentType` da DLL não podem ser referenciados
   depois de descarregada (desfazer guarda cópias da cena: limpar o histórico ou serializá-lo).

Depois (opcional): Lua como linguagem de script leve.

## Fase 4 — Luzes e câmera completas ✅

- Componentes **Luz**: direcional, pontual e spot; várias por cena (o shader passa a aceitar N luzes).
- A luz direcional sai do painel "Cena" e vira um componente (cenas antigas convertidas ao abrir).
- Ícones no viewport para luzes e câmeras (selecionáveis, com gizmo). O componente Câmera já existe (Fase 2).

**Pronto quando:** uma cena noturna com várias lâmpadas pontuais é montada só pelo editor.

## Fase 5 — Física

- Componentes **Collider** (caixa, esfera, cápsula, malha estática) e **Rigidbody** (massa, gravidade, cinemático).
- Biblioteca: Jolt Physics (madura e rápida) ou um motor próprio simples (caixas e esferas) para manter
  as dependências mínimas — decidir no início da fase.
- Eventos para scripts: `OnCollisionEnter`, triggers; raycast pela API.
- Visualização dos colliders no editor.

**Pronto quando:** caixas caem, empilham e um script de jogador anda e pula sem atravessar o chão.

## Fase 6 — Materiais e prefabs como assets

- **Material** (`.mmat`): cor, textura, brilho; reaproveitado por vários objetos e editado num lugar só.
- **Prefab** (`.mprefab`): uma entidade com filhos salva como asset; instâncias atualizam quando o prefab muda.
- Ambos referenciados por GUID (o sistema de `.meta` já existe).

**Pronto quando:** mudar a cor de um material ou de um prefab atualiza todas as instâncias na cena.

## Fase 7 — Renderização avançada

- Sombras da luz direcional (shadow map com cascatas) e das pontuais.
- PBR (metal/rugosidade), mapas de normal, skybox/cubemap e iluminação ambiente por imagem.
- Pós-processamento: HDR, tone mapping, bloom, FXAA.

**Pronto quando:** a mesma cena fica visivelmente "de jogo moderno" sem queda grande de FPS.

## Fase 8 — Áudio

- Componentes **AudioSource** (som 2D/3D, loop, volume) e **AudioListener** (na câmera).
- Biblioteca: miniaudio (um único header, como o stb_image).
- Arquivos `.wav`/`.ogg`/`.mp3` como assets com GUID.

## Fase 9 — Interface do jogo (HUD)

- Texto e imagens na tela durante o jogo (independente do ImGui do editor).
- Botões simples, âncoras na tela, fontes como asset.

## Fase 10 — Exportar o jogo

- Executável **Player** sem o editor: abre o projeto e roda a cena inicial direto em Play.
- Empacotar os assets usados (só os referenciados) numa pasta de saída.
- Menu Arquivo > Exportar jogo.

**Pronto quando:** uma pasta com o .exe e os assets roda o jogo num computador sem o editor.

## Depois

- Animação esquelética (exige importar glTF em vez de só OBJ).
- Terreno, partículas, navegação (pathfinding).
- Editor: múltiplas seleções na cena, copiar/colar entidades, snapping na grade, busca na hierarquia.

---

## Ordem e dependências

```
Fase 0 Git/testes
   └─ Fase 1 Componentes ──┬─ Fase 2 Play ── Fase 3 Scripts ──┬─ Fase 5 Física
                           │                                  ├─ Fase 8 Áudio
                           ├─ Fase 4 Luzes/câmera             ├─ Fase 9 HUD
                           └─ Fase 6 Materiais/prefabs        └─ Fase 10 Exportar
                                Fase 7 Renderização avançada (depende da 4)
```

Fases no mesmo nível (ex.: 4 e 6, ou 5 e 8) podem ser feitas em qualquer ordem, conforme o que o jogo
que você quer fazer pedir primeiro.
