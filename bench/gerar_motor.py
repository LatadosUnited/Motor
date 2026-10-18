"""Gera o projeto de comparação do Motor (bench/README.md): as 7 cenas do conjunto, num projeto só.

Uso: python bench/gerar_motor.py <pasta de saída>
A pasta recebe "Motor Bench/" com o .mproject, Assets/Scenes/<cena>.mscene e os scripts de bench/motor/Scripts.
O projeto gêmeo da Unity (bench/unity) monta as mesmas cenas com os mesmos números (BenchRunner.cs).
"""
import json
import math
import os
import shutil
import sys

ESPACO = 2.5
CENAS = ["vazia", "cubos1000", "cubos1000sombra", "cubos10000", "fisica1000", "scripts1000", "spawn200"]

AMBIENTE = {
    "skyColor": [0.53, 0.7, 0.88], "fogDensity": 0.0, "ambient": [0.12, 0.14, 0.18], "sky": "Color",
    "skyTexture": None, "skyIntensity": 1.0, "skyRotation": 0.0, "skyLight": 1.0, "shadowDistance": 150.0,
    "exposure": 1.0, "toneMapper": "ACES", "bloom": True, "bloomIntensity": 0.5, "bloomThreshold": 1.0, "fxaa": True,
}


def renderer(malha, cor, textura=None, especular=0.5):
    return {"type": "MeshRenderer", "mesh": malha, "material": None, "texture": textura, "color": cor,
            "metallic": 0.0, "roughness": 0.5, "specular": especular, "normalMap": None, "normalStrength": 1.0,
            "metallicRoughnessMap": None, "occlusionMap": None, "occlusionStrength": 1.0,
            "emissive": [0, 0, 0], "emissiveIntensity": 1.0, "emissiveMap": None}


class Cena:
    def __init__(self):
        self.entidades = []

    def add(self, nome, pos, comps, escala=(1, 1, 1), rot=(0, 0, 0, 1)):
        self.entidades.append({"id": len(self.entidades) + 1, "name": nome, "active": True, "parent": 0,
                               "transform": {"position": list(pos), "rotation": list(rot), "scale": list(escala)},
                               "components": comps})


def grade(n):
    lado = math.ceil(math.sqrt(n))
    meio = (lado - 1) * ESPACO / 2
    return [((k % lado) * ESPACO - meio, (k // lado) * ESPACO - meio) for k in range(n)], lado


def montar(nome):
    c = Cena()
    sombra = nome == "cubos1000sombra"
    n = {"cubos1000": 1000, "cubos1000sombra": 1000, "cubos10000": 10000, "scripts1000": 1000}.get(nome, 0)
    lado_chao = 90.0 if nome != "cubos10000" else 260.0
    if nome != "vazia":
        chao = [renderer("Plano", [1, 1, 1, 1], "Xadrez", 0.1)]
        if nome == "fisica1000":
            chao.append({"type": "Collider", "shape": "Box", "size": [1, 1, 1], "center": [0, -0.5, 0],
                         "isTrigger": False, "friction": 0.6, "restitution": 0.0})
        c.add("Chão", (0, 0, 0), chao, escala=(lado_chao, 1, lado_chao))
    for k, (x, z) in enumerate(grade(n)[0] if n else []):
        comps = [renderer("Cubo", [0.3, 0.6, 0.95, 1])]
        if nome == "scripts1000":
            comps.append({"type": "Bobber"})
        c.add(f"Cubo{k}", (x, 0.5, z), comps)
    if nome == "fisica1000":
        # 10 x 10 x 10, camadas desalinhadas em meio cubo e soltas do alto: desmoronam numa pilha e ficam se
        # chocando por vários segundos (colunas retas assentam e dormem logo).
        for k in range(1000):
            i, j, h = k % 10, (k // 10) % 10, k // 100
            meio = 0.6 if h % 2 else 0.0
            c.add(f"Corpo{k}", ((i - 4.5) * 1.2 + meio, 8 + h * 1.6, (j - 4.5) * 1.2 + meio),
                  [renderer("Cubo", [0.95, 0.6, 0.3, 1]),
                   {"type": "Collider", "shape": "Box", "size": [1, 1, 1], "center": [0, 0, 0], "isTrigger": False,
                    "friction": 0.6, "restitution": 0.0},
                   {"type": "Rigidbody", "mass": 1.0, "useGravity": True, "isKinematic": False,
                    "linearDamping": 0.05, "angularDamping": 0.05}])
    if nome == "spawn200":
        c.add("Gerador", (0, -10, 0), [renderer("Cubo", [0.95, 0.3, 0.4, 1]), {"type": "Spawner", "quantidade": 200}])
    altura, distancia = (110, 210) if nome == "cubos10000" else (45, 85)
    # Câmera olhando para -Z, inclinada 30 graus para baixo.
    c.add("Câmera", (0, altura, distancia), [{"type": "Camera", "fov": 60.0, "near": 0.1, "far": 600.0, "main": True}],
          rot=(-0.258819, 0, 0, 0.9659258))
    c.add("Sol", (0, 10, 0), [{"type": "Light", "kind": "Directional", "color": [1, 1, 1], "intensity": 1.0,
                               "range": 10.0, "innerAngle": 25.0, "outerAngle": 35.0, "castShadows": sombra,
                               "shadowQuality": "Medium"}],
          rot=(-0.43409288, 0.4027245, 0.2260129, 0.773495))
    return {"format": "motor-scene", "version": 6, "environment": AMBIENTE, "entities": c.entidades}


def main():
    raiz = os.path.join(sys.argv[1], "Motor Bench")
    os.makedirs(os.path.join(raiz, "Assets", "Scenes"), exist_ok=True)
    for nome in CENAS:
        with open(os.path.join(raiz, "Assets", "Scenes", nome + ".mscene"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(montar(nome), f, ensure_ascii=False)
    scripts = os.path.join(os.path.dirname(os.path.abspath(__file__)), "motor", "Scripts")
    shutil.copytree(scripts, os.path.join(raiz, "Scripts"), dirs_exist_ok=True)
    with open(os.path.join(raiz, "Motor Bench.mproject"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": "motor-project", "version": 2, "name": "Motor Bench", "engineVersion": "0.2.0",
                   "startupScene": "Assets/Scenes/vazia.mscene"}, f, ensure_ascii=False, indent=4)
    print(raiz)


if __name__ == "__main__":
    main()
