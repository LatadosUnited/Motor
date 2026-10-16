"""Gera Robo.glb: personagem de exemplo do Motor (Fase 11), feito só com caixas, com esqueleto, pele e três
clipes ("Parado", "Andando", "Pulando"). Sem dependências (só a biblioteca padrão do Python).

    python gerar_robo.py            # grava Robo.glb ao lado deste arquivo

O robô tem 1,93 m, os pés em y = 0 e olha para -Z (a frente dos objetos no Motor, como no script Jogador).
"""

import json
import math
import os
import struct
import zlib

# --- Esqueleto: nome, pai, posição relativa ao pai -------------------------------------------------------
JOINTS = [
    ("Quadril", None, (0.0, 0.95, 0.0)),
    ("Tronco", "Quadril", (0.0, 0.15, 0.0)),
    ("Cabeca", "Tronco", (0.0, 0.50, 0.0)),
    ("BracoE", "Tronco", (-0.30, 0.45, 0.0)),   # olhando para -Z, a esquerda do robô é -X
    ("BracoD", "Tronco", (0.30, 0.45, 0.0)),
    ("CoxaE", "Quadril", (-0.12, -0.05, 0.0)),
    ("CanelaE", "CoxaE", (0.0, -0.44, 0.0)),
    ("CoxaD", "Quadril", (0.12, -0.05, 0.0)),
    ("CanelaD", "CoxaD", (0.0, -0.44, 0.0)),
]
INDEX = {name: i for i, (name, _, _) in enumerate(JOINTS)}


def global_position(name):
    x = y = z = 0.0
    while name is not None:
        _, parent, (dx, dy, dz) = JOINTS[INDEX[name]]
        x, y, z = x + dx, y + dy, z + dz
        name = parent
    return (x, y, z)


# --- Malha: caixas presas (peso 1) numa junta --------------------------------------------------------------
# material: 0 = Corpo (azul), 1 = Escuro, 2 = Rosto (com textura na frente)
BOXES = [
    # centro, tamanho, junta, material
    ((0.0, 0.95, 0.0), (0.36, 0.20, 0.22), "Quadril", 1),
    ((0.0, 1.32, 0.0), (0.46, 0.50, 0.28), "Tronco", 0),
    ((0.0, 1.62, 0.0), (0.12, 0.08, 0.12), "Tronco", 1),          # pescoço
    ((0.0, 1.79, 0.0), (0.30, 0.28, 0.28), "Cabeca", 2),
    ((-0.32, 1.30, 0.0), (0.12, 0.52, 0.12), "BracoE", 0),
    ((0.32, 1.30, 0.0), (0.12, 0.52, 0.12), "BracoD", 0),
    ((-0.32, 1.00, 0.0), (0.13, 0.10, 0.13), "BracoE", 1),        # mãos
    ((0.32, 1.00, 0.0), (0.13, 0.10, 0.13), "BracoD", 1),
    ((-0.12, 0.68, 0.0), (0.15, 0.44, 0.15), "CoxaE", 0),
    ((0.12, 0.68, 0.0), (0.15, 0.44, 0.15), "CoxaD", 0),
    ((-0.12, 0.26, 0.0), (0.13, 0.40, 0.13), "CanelaE", 0),
    ((0.12, 0.26, 0.0), (0.13, 0.40, 0.13), "CanelaD", 0),
    ((-0.12, 0.04, -0.05), (0.15, 0.08, 0.26), "CanelaE", 1),     # pés (bico para -Z)
    ((0.12, 0.04, -0.05), (0.15, 0.08, 0.26), "CanelaD", 1),
]

FACES = [  # normal, eixo u, eixo v (u x v = normal: anti-horário visto de fora)
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
]


def build_primitives():
    prims = {m: {"pos": [], "nrm": [], "uv": [], "jnt": [], "wgt": [], "idx": []} for m in range(3)}
    for center, size, joint, material in BOXES:
        p = prims[material]
        half = [s * 0.5 for s in size]
        for n, u, v in FACES:
            base = len(p["pos"])
            for cu, cv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                pos = [center[k] + n[k] * half[k] + cu * u[k] * half[k] + cv * v[k] * half[k] for k in range(3)]
                p["pos"].append(pos)
                p["nrm"].append(list(n))
                if material == 2 and n == (0, 0, -1):
                    # Rosto na frente (-Z). glTF: v = 0 é o alto da imagem.
                    p["uv"].append([(cu + 1) * 0.5, 1.0 - (cv + 1) * 0.5])
                else:
                    p["uv"].append([0.03, 0.03])  # canto liso da textura
                p["jnt"].append([INDEX[joint], 0, 0, 0])
                p["wgt"].append([1.0, 0.0, 0.0, 0.0])
            p["idx"] += [base, base + 1, base + 2, base, base + 2, base + 3]
    return [prims[m] for m in range(3)]


# --- Textura do rosto (PNG 32x32 feito à mão) --------------------------------------------------------------
def face_png():
    w = h = 32
    rows = []
    for y in range(h):
        row = bytearray([0])  # filtro "nenhum"
        for x in range(w):
            r, g, b = 205, 215, 225  # metal claro
            eye = (abs(x - 10) <= 3 or abs(x - 21) <= 3) and 9 <= y <= 14
            pupil = (abs(x - 10) <= 1 or abs(x - 21) <= 1) and 11 <= y <= 13
            mouth = 9 <= x <= 22 and 21 <= y <= 23
            if eye:
                r, g, b = 255, 255, 255
            if pupil:
                r, g, b = 20, 30, 60
            if mouth:
                r, g, b = 60, 70, 90
            row += bytes((r, g, b))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


# --- Animações ---------------------------------------------------------------------------------------------
def quat_axis(axis, degrees):
    a = math.radians(degrees) * 0.5
    s = math.sin(a)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(a)]


X = (1, 0, 0)
Z = (0, 0, 1)


def rot_track(joint, times, axis, degrees):
    return (joint, "rotation", times, [quat_axis(axis, d) for d in degrees])


def pos_track(joint, times, offsets):
    rest = JOINTS[INDEX[joint]][2]
    return (joint, "translation", times, [[rest[0] + o[0], rest[1] + o[1], rest[2] + o[2]] for o in offsets])


# Rotação positiva em X leva a perna/braço (apontando para baixo) para a frente (-Z).
CLIPS = {
    "Parado": [
        pos_track("Tronco", [0, 1, 2], [(0, 0, 0), (0, 0.012, 0), (0, 0, 0)]),
        rot_track("Cabeca", [0, 1, 2], X, [0, -4, 0]),
        rot_track("BracoE", [0, 1, 2], Z, [-4, -7, -4]),
        rot_track("BracoD", [0, 1, 2], Z, [4, 7, 4]),
    ],
    "Andando": [
        rot_track("CoxaE", [0, 0.5, 1], X, [28, -28, 28]),
        rot_track("CoxaD", [0, 0.5, 1], X, [-28, 28, -28]),
        rot_track("CanelaE", [0, 0.25, 0.5, 0.75, 1], X, [0, 0, -10, -45, 0]),
        rot_track("CanelaD", [0, 0.25, 0.5, 0.75, 1], X, [-10, -45, 0, 0, -10]),
        rot_track("BracoE", [0, 0.5, 1], X, [-24, 24, -24]),
        rot_track("BracoD", [0, 0.5, 1], X, [24, -24, 24]),
        pos_track("Quadril", [0, 0.25, 0.5, 0.75, 1], [(0, 0, 0), (0, -0.035, 0), (0, 0, 0), (0, -0.035, 0), (0, 0, 0)]),
        rot_track("Tronco", [0, 0.5, 1], (0, 1, 0), [6, -6, 6]),
    ],
    "Pulando": [
        rot_track("CoxaE", [0, 0.15, 0.8], X, [0, 45, 40]),
        rot_track("CoxaD", [0, 0.15, 0.8], X, [0, 30, 25]),
        rot_track("CanelaE", [0, 0.15, 0.8], X, [0, -70, -65]),
        rot_track("CanelaD", [0, 0.15, 0.8], X, [0, -50, -45]),
        rot_track("BracoE", [0, 0.15, 0.8], Z, [0, -150, -145]),
        rot_track("BracoD", [0, 0.15, 0.8], Z, [0, 150, 145]),
        rot_track("Cabeca", [0, 0.15, 0.8], X, [0, 10, 8]),
    ],
}


# --- Montagem do glTF ---------------------------------------------------------------------------------------
class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)})
        self.bin += data
        return len(self.views) - 1

    def accessor(self, values, kind, comps, component=5126, minmax=False):
        flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else [v])]
        fmt = {5126: "f", 5121: "B", 5123: "H"}[component]
        acc = {"bufferView": self.view(struct.pack("<%d%s" % (len(flat), fmt), *flat)),
               "componentType": component, "count": len(values), "type": kind}
        if minmax:
            acc["min"] = [min(v[k] for v in values) for k in range(comps)]
            acc["max"] = [max(v[k] for v in values) for k in range(comps)]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def main():
    b = Builder()
    primitives = []
    for material, p in enumerate(build_primitives()):
        primitives.append({
            "attributes": {
                "POSITION": b.accessor(p["pos"], "VEC3", 3, minmax=True),
                "NORMAL": b.accessor(p["nrm"], "VEC3", 3),
                "TEXCOORD_0": b.accessor(p["uv"], "VEC2", 2),
                "JOINTS_0": b.accessor(p["jnt"], "VEC4", 4, component=5121),
                "WEIGHTS_0": b.accessor(p["wgt"], "VEC4", 4),
            },
            "indices": b.accessor(p["idx"], "SCALAR", 1, component=5123),
            "material": material,
        })

    # Nós: 0 = Robo (raiz), 1 = malha com pele, 2.. = juntas.
    nodes = [{"name": "Robo", "children": [1, 2]}, {"name": "Corpo", "mesh": 0, "skin": 0}]
    for name, parent, offset in JOINTS:
        node = {"name": name, "translation": list(offset)}
        children = [2 + i for i, (_, p, _) in enumerate(JOINTS) if p == name]
        if children:
            node["children"] = children
        nodes.append(node)

    ibms = []
    for name, _, _ in JOINTS:
        gx, gy, gz = global_position(name)
        ibms.append([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -gx, -gy, -gz, 1])
    skin = {"joints": [2 + i for i in range(len(JOINTS))], "skeleton": 2,
            "inverseBindMatrices": b.accessor(ibms, "MAT4", 16)}

    animations = []
    for clip, tracks in CLIPS.items():
        samplers, channels = [], []
        for joint, path, times, values in tracks:
            samplers.append({"input": b.accessor([float(t) for t in times], "SCALAR", 1, minmax=False),
                             "output": b.accessor(values, "VEC4" if path == "rotation" else "VEC3", 4 if path == "rotation" else 3),
                             "interpolation": "LINEAR"})
            # glTF exige min/max no accessor de tempo.
            b.accessors[samplers[-1]["input"]]["min"] = [float(min(times))]
            b.accessors[samplers[-1]["input"]]["max"] = [float(max(times))]
            channels.append({"sampler": len(samplers) - 1, "target": {"node": 2 + INDEX[joint], "path": path}})
        animations.append({"name": clip, "samplers": samplers, "channels": channels})

    png = face_png()
    image_view = b.view(png)

    doc = {
        "asset": {"version": "2.0", "generator": "Motor gerar_robo.py"},
        "scene": 0,
        "scenes": [{"name": "Robo", "nodes": [0]}],
        "nodes": nodes,
        "meshes": [{"name": "Robo", "primitives": primitives}],
        "skins": [skin],
        "animations": animations,
        "materials": [
            {"name": "Corpo", "pbrMetallicRoughness": {"baseColorFactor": [0.05, 0.25, 0.65, 1.0], "metallicFactor": 0.3, "roughnessFactor": 0.4}},
            {"name": "Escuro", "pbrMetallicRoughness": {"baseColorFactor": [0.04, 0.04, 0.05, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}},
            {"name": "Rosto", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.5}},
        ],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728}],
        "images": [{"name": "Rosto", "bufferView": image_view, "mimeType": "image/png"}],
        "bufferViews": b.views,
        "accessors": b.accessors,
        "buffers": [{"byteLength": len(b.bin)}],
    }

    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    data = bytes(b.bin)
    while len(data) % 4:
        data += b"\0"
    total = 12 + 8 + len(text) + 8 + len(data)
    out = struct.pack("<III", 0x46546C67, 2, total)
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(data), 0x004E4942) + data

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Robo.glb")
    with open(path, "wb") as f:
        f.write(out)
    print("Robo.glb: %d bytes, %d juntas, clipes: %s" % (len(out), len(JOINTS), ", ".join(CLIPS)))


if __name__ == "__main__":
    main()
