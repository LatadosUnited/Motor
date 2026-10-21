"""Gera as páginas do site local dos docs a partir dos mapas dos módulos (docs/<módulo>/mapa.md).

Uso:  python docs/site/gerar.py
Para cada módulo com mapa.md, lê o bloco ```mermaid``` e o título e escreve docs/site/<módulo>.html (e um
index.html com a lista). As páginas abrem com duplo clique; o Mermaid vem da CDN (precisa de internet).
Só a biblioteca padrão do Python.
"""
import html
import json
import re
from pathlib import Path

MERMAID = "https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.esm.min.mjs"  # versão fixada
DOCS = Path(__file__).resolve().parent.parent
SITE = DOCS / "site"
MODULES = [("engine", "Engine"), ("editor", "Editor"), ("hub", "Hub"), ("instalador", "Instalador")]

PAGE = """<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{titulo} · Motor</title>
<style>
  :root {{ --bg: #f7f7f5; --fg: #1f1f1d; --muted: #6b6a66; --line: #d9d8d2; --card: #ffffff; --accent: #534ab7; }}
  @media (prefers-color-scheme: dark) {{
    :root {{ --bg: #1b1b1a; --fg: #ecebe6; --muted: #a3a29c; --line: #3a3a37; --card: #242423; --accent: #afa9ec; }}
  }}
  * {{ box-sizing: border-box; }}
  html, body {{ height: 100%; }}
  body {{ margin: 0; display: flex; flex-direction: column; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, "Segoe UI", sans-serif; }}
  header {{ display: flex; flex-wrap: wrap; align-items: center; gap: 12px 20px; padding: 14px 20px;
           border-bottom: 1px solid var(--line); background: var(--card); }}
  header h1 {{ font-size: 18px; font-weight: 600; margin: 0; }}
  header .sub {{ color: var(--muted); font-size: 13px; }}
  nav a {{ color: var(--accent); text-decoration: none; margin-right: 12px; font-size: 14px; }}
  nav a.atual {{ color: var(--fg); font-weight: 600; }}
  .barra {{ margin-left: auto; display: flex; gap: 6px; }}
  button {{ font: inherit; font-size: 13px; padding: 5px 10px; border: 1px solid var(--line); border-radius: 6px;
           background: var(--bg); color: var(--fg); cursor: pointer; }}
  button:hover {{ border-color: var(--muted); }}
  #palco {{ flex: 1; min-height: 0; overflow: hidden; cursor: grab; touch-action: none; }}
  #palco.arrastando {{ cursor: grabbing; }}
  #mapa {{ transform-origin: 0 0; display: inline-block; padding: 24px; }}
  #aviso {{ padding: 24px; color: var(--muted); }}
  .lista {{ max-width: 720px; margin: 32px auto; padding: 0 20px; }}
  .lista a {{ display: block; padding: 14px 16px; margin-bottom: 10px; background: var(--card); border: 1px solid var(--line);
             border-radius: 10px; color: var(--fg); text-decoration: none; }}
  .lista a:hover {{ border-color: var(--accent); }}
  .lista span {{ color: var(--muted); font-size: 13px; }}
</style>
</head>
<body>
<header>
  <div><h1>{titulo}</h1><div class="sub">{sub}</div></div>
  <nav>{nav}</nav>
  {barra}
</header>
{corpo}
</body>
</html>
"""

BARRA = """<div class="barra">
    <button id="menos" title="Diminuir">−</button>
    <button id="mais" title="Aumentar">+</button>
    <button id="ajustar" title="Caber na tela">Ajustar</button>
    <button id="baixar" title="Salvar o desenho como SVG">Baixar SVG</button>
  </div>"""

MAPA = """<div id="palco"><div id="mapa"><div id="aviso">Desenhando o mapa… (o Mermaid vem da internet; sem conexão, o
  mapa não aparece: veja o <code>{md}</code> no Obsidian)</div></div></div>
<script type="module">
import mermaid from '{mermaid}';
const fonte = {fonte};
const escuro = matchMedia('(prefers-color-scheme: dark)').matches;
mermaid.initialize({{ startOnLoad: false, theme: escuro ? 'dark' : 'default',
  fontFamily: 'system-ui, "Segoe UI", sans-serif', mindmap: {{ padding: 12 }} }});
const mapa = document.getElementById('mapa'), palco = document.getElementById('palco');
try {{
  const {{ svg }} = await mermaid.render('mapa-svg', fonte);
  mapa.innerHTML = svg;
}} catch (e) {{
  mapa.innerHTML = '<div id="aviso">Não deu para desenhar o mapa: ' + e.message + '</div>';
}}
let escala = 1, x = 0, y = 0;
const aplicar = () => mapa.style.transform = `translate(${{x}}px, ${{y}}px) scale(${{escala}})`;
function ajustar() {{
  const s = mapa.querySelector('svg'); if (!s) return;
  const r = s.getBoundingClientRect(), w = r.width / escala, h = r.height / escala;
  escala = Math.min(palco.clientWidth / (w + 48), palco.clientHeight / (h + 48), 1.5);
  x = (palco.clientWidth - (w + 48) * escala) / 2; y = (palco.clientHeight - (h + 48) * escala) / 2; aplicar();
}}
function zoom(f, cx = palco.clientWidth / 2, cy = palco.clientHeight / 2) {{
  const n = Math.min(Math.max(escala * f, 0.2), 5);
  x = cx - (cx - x) * (n / escala); y = cy - (cy - y) * (n / escala); escala = n; aplicar();
}}
document.getElementById('mais').onclick = () => zoom(1.25);
document.getElementById('menos').onclick = () => zoom(0.8);
document.getElementById('ajustar').onclick = ajustar;
document.getElementById('baixar').onclick = () => {{
  const s = mapa.querySelector('svg'); if (!s) return;
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([s.outerHTML], {{ type: 'image/svg+xml' }}));
  a.download = '{nome}-mapa.svg'; a.click();
}};
palco.addEventListener('wheel', e => {{ e.preventDefault(); const r = palco.getBoundingClientRect();
  zoom(e.deltaY < 0 ? 1.1 : 0.9, e.clientX - r.left, e.clientY - r.top); }}, {{ passive: false }});
let ini = null;
palco.addEventListener('pointerdown', e => {{ ini = {{ px: e.clientX, py: e.clientY, x, y }};
  palco.setPointerCapture(e.pointerId); palco.classList.add('arrastando'); }});
palco.addEventListener('pointermove', e => {{ if (!ini) return; x = ini.x + e.clientX - ini.px; y = ini.y + e.clientY - ini.py; aplicar(); }});
palco.addEventListener('pointerup', () => {{ ini = null; palco.classList.remove('arrastando'); }});
addEventListener('resize', ajustar);
ajustar();
</script>"""


def ler_mapa(md: Path):
    texto = md.read_text(encoding="utf-8")
    bloco = re.search(r"```mermaid\n(.*?)```", texto, re.S)
    titulo = re.search(r"^# (.+)$", texto, re.M)
    data = re.search(r"^atualizado: (\S+)$", texto, re.M)
    if not bloco:
        return None
    return bloco.group(1), (titulo.group(1) if titulo else md.parent.name), (data.group(1) if data else "")


def nav(atual, com_mapa):
    links = ['<a href="index.html"%s>Início</a>' % (' class="atual"' if atual == "index" else "")]
    for nome, rotulo in MODULES:
        if nome in com_mapa:
            classe = ' class="atual"' if nome == atual else ""
            links.append(f'<a href="{nome}.html"{classe}>{rotulo}</a>')
    return "".join(links)


def main():
    mapas = {}
    for nome, rotulo in MODULES:
        md = DOCS / nome / "mapa.md"
        if md.exists() and (lido := ler_mapa(md)):
            mapas[nome] = (rotulo, md, *lido)
    for nome, (rotulo, md, fonte, titulo, data) in mapas.items():
        rel = md.relative_to(DOCS.parent).as_posix()
        corpo = MAPA.format(mermaid=MERMAID, fonte=json.dumps(fonte, ensure_ascii=False).replace("</", "<\\/"), nome=nome, md=html.escape(rel))
        pagina = PAGE.format(titulo=html.escape(titulo), sub=f"Gerado de {html.escape(rel)} (atualizado em {data})",
                             nav=nav(nome, mapas), barra=BARRA, corpo=corpo)
        (SITE / f"{nome}.html").write_text(pagina, encoding="utf-8", newline="\n")
        print(f"{nome}.html <- {rel}")
    itens = "".join(f'<a href="{n}.html"><b>{r}</b><br><span>{html.escape(t)} · atualizado em {d}</span></a>'
                    for n, (r, _, _, t, d) in mapas.items())
    faltam = [r for n, r in MODULES if n not in mapas]
    if faltam:
        itens += f'<p class="sub" style="color:var(--muted)">Ainda sem mapa: {", ".join(faltam)}.</p>'
    pagina = PAGE.format(titulo="Motor — mapas dos módulos", sub="Site local gerado de docs/<módulo>/mapa.md",
                         nav=nav("index", mapas), barra="", corpo=f'<div class="lista">{itens}</div>')
    (SITE / "index.html").write_text(pagina, encoding="utf-8", newline="\n")
    print("index.html")


if __name__ == "__main__":
    main()
