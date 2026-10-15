# Roteiro do Motor

O desenvolvimento dividido em **módulos**, na ordem em que um depende do outro. Cada fase termina com
algo que dá para usar e testar no editor, e só começa quando a anterior estiver de pé.

## Onde estamos

| Área | Estado |
|---|---|
| Núcleo (janela, input, loop, log) | ✅ pronto |
| Renderização básica (Blinn-Phong, 1 luz direcional, neblina, MSAA) | ✅ pronto |
| Cena (entidades, hierarquia, transform) | ✅ pronto, mas só com **um** tipo de componente fixo (`MeshRenderer`) |
| Editor (hierarquia, inspector, viewport, gizmos, desfazer) | ✅ pronto |
| Projeto e assets (pastas, `.mscene`, PNG/JPG, OBJ, GUID em `.meta`) | ✅ pronto |

O gargalo para tudo que vem depois (scripts, física, câmera, luzes) é o item da cena: hoje cada novo
recurso exigiria mexer à mão na `Entity`, no Inspector, no serializador e no desfazer. Por isso a
Fase 1 vem antes do modo Play.

---

## Fase 0 — Controle de versão *(meio dia)*

A pasta ainda não é um repositório git. Antes das refatorações grandes:

- `git init`, `.gitignore` (já existe), primeiro commit.
- Testes sem interface que fizemos até aqui (projeto, assets, GUID, desfazer) viram alvos `ctest`.

**Pronto quando:** `ctest` roda todos os testes e cada fase seguinte vira um commit (ou branch).

## Fase 1 — Sistema de componentes *(base de tudo)*

Trocar o `std::optional<MeshRenderer>` fixo por componentes registráveis.

- Registro de tipos de componente: nome, criar, copiar, comparar, salvar/carregar (JSON), desenhar no Inspector.
- Campos descritos uma vez (reflexão simples): o Inspector e o serializador são gerados a partir deles.
- `MeshRenderer` migrado para o sistema novo; cenas antigas continuam abrindo.
- "Adicionar componente" no Inspector lista tudo que está registrado.

**Pronto quando:** criar um componente novo é escrever uma struct + uma linha de registro, e ele já
aparece no Inspector, é salvo na cena e entra no desfazer sem tocar em mais nada.

## Fase 2 — Modo Play

- Botões ▶ Play / ⏸ Pausa / ⏹ Parar na barra do viewport (e Ctrl+P).
- Ao dar Play, a cena é copiada (a cópia profunda de `Scene` já existe); ao parar, volta como estava.
- Loop de jogo separado do editor: `OnStart`, `OnUpdate(dt)`, `OnFixedUpdate` só rodam em Play.
- Componente **Câmera**: o Play renderiza pela câmera da cena (o editor continua com a câmera livre).
- Aviso visual de que está em Play (borda/tingimento), e alterações durante o Play são descartadas.

**Pronto quando:** dá para dar Play, ver a cena pela câmera do jogo, pausar e parar voltando ao estado original.

## Fase 3 — Scripts

- Componente **Script** em C++: classe com `OnStart`, `OnUpdate`, `OnDestroy`, acesso à entidade,
  ao transform, ao `Input` e ao tempo.
- Campos do script aparecem no Inspector (usa a reflexão da Fase 1).
- Scripts do jogo ficam numa pasta do projeto e são compilados numa DLL, recarregada sem fechar o editor
  (hot reload). Alternativa mais simples para começar: scripts compilados junto com o editor.
- API mínima: encontrar entidade por nome, criar/destruir entidades, log.
- Depois (opcional): Lua como linguagem de script leve.

**Pronto quando:** um script "gira o objeto" e um "move o jogador com WASD" funcionam no Play.

## Fase 4 — Luzes e câmera completas

- Componentes **Luz**: direcional, pontual e spot; várias por cena (o shader passa a aceitar N luzes).
- A luz direcional sai do painel "Cena" e vira um componente (cenas antigas convertidas ao abrir).
- Ícones no viewport para luzes e câmeras (selecionáveis, com gizmo).

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
