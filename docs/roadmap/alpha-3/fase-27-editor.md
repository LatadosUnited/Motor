---
tipo: fase
alpha: 3
fase: 27
titulo: Editor que escala
status: pronta
prioridade: 5
areas: [editor, scene]
branch: fase-27-editor
inicio: 2026-10-25
fim: 2026-10-25
---

# Fase 27 — Editor que escala

## Objetivo

Editar a cena de referência (~5000 entidades) sem travar. O editor fazia com a cena inteira, várias vezes por
segundo, o que só servia para cenas pequenas: gravava a cena em JSON a cada 0,5 s para saber se havia alteração não
salva (e a cada 0,2 s com o servidor MCP ligado), comparava a cena inteira com a última foto do desfazer a cada
quadro, guardava uma cópia inteira da cena por passo, desenhava as 5000 linhas da Hierarquia e relia as pastas do
projeto de tempos em tempos na thread principal.

## Tarefas

- [x] Tarefa 1: Desfazer por diferenças, sem comparar a cena inteira — 46bb7f8
- [x] Tarefa 2: Checar "não salvo" sem gravar a cena em JSON a cada 0,5 s — 46bb7f8
- [x] Tarefa 3: Acelerar gravar e ler a cena em JSON (hoje ~330 ms) — c745ff7, 160f50a
- [x] Tarefa 4: Cortar os 6 a 7 ms de CPU do ImGui — e491bf5, 46bb7f8, 93be891
- [x] Tarefa 5: Lista virtual na Hierarquia com milhares de entidades — e491bf5
- [x] Tarefa 6: Corrigir o FPS por baixo dos botões de Play no viewport estreito — 373b9ed
- [x] Tarefa 7: Reler os arquivos do projeto só quando a pasta muda — 93be891

## Pronto quando

Com a referência aberta no editor Release, nenhum quadro acima de 100 ms e o editor parado abaixo de 16 ms.

## Feito

### Como medir

O editor ganhou `--perf <csv>` e `--segundos S`, como o Player, e contadores de CPU por painel e por parte da
varredura dos arquivos (`ImGui: Hierarquia`, `ImGui: MCP`, `Update: assets (.meta)`...). Projeto do bench com a
referência como cena inicial, editor Release com `--no-settings`, janela 1386×788, máquina de referência:

- **parado:** 25 s sem mexer, sem o servidor MCP;
- **editando:** com o MCP, 8 movimentos, renomear duas vezes, duplicar, 3 desfazer, 3 refazer e salvar, ~0,7 s
  entre cada um.

Quadros do segundo 5 em diante. Base = o `main` (bd7f696) com só o `--perf`.

### Resultado (25/10/2026)

| Medida | Antes | Depois |
|---|---|---|
| Parado: quadro mediano | 30–31 ms | 30–31 ms (a GPU limita: 27–28 ms) |
| Parado: quadros acima de 100 ms em 20 s | **37–38** (até 830 ms, a cada 0,5 s) | **0** (máximo 34 ms) |
| Parado: CPU por quadro (Update + Render + ImGui) | ~21 ms | **~10,5 ms** |
| ImGui por quadro | 14,7 ms | 3,7 ms |
| Hierarquia | 5,3 ms | 0,4 ms |
| Comparação do desfazer | 3,9 ms todo quadro | 0,5 ms (só depois de entrada) |
| Com o MCP ligado: quadro mediano | **305 ms** (o MCP checava "não salvo" a cada 0,2 s) | 30 ms |
| Editando: quadros acima de 100 ms | todos | só os 2 do salvar (abaixo) |
| Memória depois de ~17 passos de desfazer | +39 MB | +3 MB |
| Salvar a cena (editor) | ~460 ms (e mais um JSON para o "não salvo") | 90–157 ms |
| `test_desempenho` `cena_json` | 255 ms | **31 ms** |
| `test_desempenho` `cena_ler` | 232 ms | 179 ms |

O que mudou:

- **"Não salvo" sem JSON (Tarefa 2):** o `UndoHistory` dá uma versão a cada passo, que volta ao desfazer; a cena
  está salva quando a versão é a de quando salvou. A checagem exata (fechar, MCP) compara a cena com a última foto,
  sem JSON. Era o maior travamento: ~800 ms a cada 0,5 s no editor.
- **Desfazer por entidades (Tarefa 1):** cada foto guarda as entidades como cópias imutáveis divididas entre as
  fotos; um passo novo copia só as que mudaram. No editor a comparação com a cena só roda depois de entrada (tecla,
  clique, roda, texto; mexer o mouse não conta), de um pedido do MCP ou a cada 0,25 s. Desfazer, refazer e salvar
  comparam antes. `Scene` ganhou `SameEntity`, `FromEntities` e `NextEntityId`.
- **JSON (Tarefa 3):** o texto é escrito direto (`JsonWriter`), sem a árvore do nlohmann, no mesmo formato
  (conferido byte a byte com o `main` nas 10 cenas do bench e em dois projetos, como cena e como prefab). A leitura
  não copia mais a lista de entidades. O salvar mostra no log o tempo do JSON e da gravação.
- **Hierarquia (Tarefa 5):** lista das linhas visíveis desenhada com o `ImGuiListClipper`; pais fechados num
  conjunto próprio, e a seleção nova abre o caminho até ela uma vez.
- **ImGui (Tarefa 4):** Hierarquia e desfazer acima; o painel Scripts listava a pasta `Scripts/` a cada quadro
  (~4 ms).
- **Arquivos do projeto (Tarefa 7):** a varredura de 2 em 2 s (`.meta`, materiais, modelos, prefabs) custava ~150
  ms num quadro; o painel Projeto relia a pasta a cada segundo. Agora só quando o sistema avisa que algo mudou na
  pasta (`FolderWatcher`, `FindFirstChangeNotificationW`).
- **Viewport estreito (Tarefa 6):** o texto do canto encurta para caber antes dos botões de Play e, se nem o FPS
  couber, vai para baixo.

## Limitações

- **Salvar ainda passa de 100 ms no editor:** 90–157 ms para a referência (o JSON leva 31 ms fora do editor e 53 ms
  com a GPU ocupada; no editor, com a memória das entidades espalhada depois do desfazer, 80–140 ms). Dois segundos
  depois, o vigia vê o arquivo gravado e a varredura dos arquivos roda uma vez: ~145 ms no projeto em `%TEMP%` (o
  antivírus olha cada arquivo), ~70 ms numa pasta comum. Próximos passos: gravar numa thread separada a partir de uma
  cópia da cena, e não varrer de novo o que o próprio editor gravou.
- **Ler a cena** (abrir): 179 ms, dos quais ~150 no parse do nlohmann, que lê cada número com o `strtod` do MinGW
  (~0,7 µs por número). Mais rápido só trocando o leitor de JSON (biblioteca nova: perguntar antes).
- **O quadro parado continua em ~30 ms:** a CPU gasta ~10,5 ms, mas a GPU do viewport leva 27–28 ms nesta máquina
  (sombras e pós da referência). O critério "abaixo de 16 ms" vale para a CPU.
- **Componentes com campo Custom grande** (`PrefabInstance` com overrides) ainda passam pelo `json::parse` ao
  gravar: na cena do projeto Joseon (580 entidades, 528 instâncias) o JSON caiu só de 102 para 72 ms.
- **Recarregar os scripts e mudar um prefab ficaram mais caros com histórico longo:** `ForEachScene` monta cada foto
  como cena (~4 ms por foto da referência) para trocar os componentes.
- Mudanças que não vêm de entrada nem do MCP (prefab alterado no disco, navmesh gerado em segundo plano) viram passo
  de desfazer até 0,25 s depois, no próximo quadro sem interação.
- Não testado à mão: arrastar na Hierarquia com a lista longa e rolagem (o MCP não cobre), e o texto do viewport
  com a janela estreitada pelo usuário (conferido só numa captura com o viewport de 826 px).
