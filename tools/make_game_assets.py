#!/usr/bin/env python3
"""Builds Coin Hunt's models (.glb) and sounds (.wav) from code.

The plan's asset workflow is Blender exporting glTF. Until there are real
models, this script writes small low-poly ones the same way an exporter would,
which also makes it a readable example of what is inside a .glb file:

    a 12-byte header, then a JSON chunk describing nodes, meshes and
    accessors, then a binary chunk holding the actual vertex and index data.

Sounds are synthesised from sine and square waves and written as 16-bit mono
WAV files. Everything is deterministic, so rerunning gives identical files.

Usage:  python3 tools/make_game_assets.py
Needs only the Python standard library. The outputs are committed, so you only
run this after changing it.
"""

import json
import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "games" / "coin_hunt" / "assets" / "models"
SOUNDS = ROOT / "games" / "coin_hunt" / "assets" / "sounds"


# --- Geometry -----------------------------------------------------------------
#
# A Mesh collects flat-shaded triangles: every face gets its own vertices so it
# can have its own normal and colour. Triangles wind counter-clockwise seen
# from outside, as glTF and the engine expect.

class Mesh:
    def __init__(self):
        self.positions, self.normals, self.colors, self.indices = [], [], [], []

    def quad(self, a, b, c, d, color):
        """Adds the quad a-b-c-d, given counter-clockwise from outside."""
        n = normalize(cross(sub(b, a), sub(c, a)))
        base = len(self.positions)
        for p in (a, b, c, d):
            self.positions.append(p)
            self.normals.append(n)
            self.colors.append(color)
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def triangle(self, a, b, c, color):
        n = normalize(cross(sub(b, a), sub(c, a)))
        base = len(self.positions)
        for p in (a, b, c):
            self.positions.append(p)
            self.normals.append(n)
            self.colors.append(color)
        self.indices += [base, base + 1, base + 2]

    def box(self, center, size, color, top=None):
        """An axis-aligned box. `top` optionally colours the +Y face differently."""
        cx, cy, cz = center
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        # Each face: (normal axis corners), listed counter-clockwise from outside.
        x0, x1, y0, y1, z0, z1 = cx - hx, cx + hx, cy - hy, cy + hy, cz - hz, cz + hz
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), color)  # +X
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), color)  # -X
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), top or color)  # +Y
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), color)  # -Y
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), color)  # +Z
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), color)  # -Z

    def disc_z(self, center, radius, depth, color, rim, segments=20):
        """A coin-like cylinder whose round faces point along +Z and -Z."""
        cx, cy, cz = center
        zf, zb = cz + depth / 2, cz - depth / 2
        ring = [(cx + radius * math.cos(2 * math.pi * i / segments),
                 cy + radius * math.sin(2 * math.pi * i / segments)) for i in range(segments)]
        for i in range(segments):
            (ax, ay), (bx, by) = ring[i], ring[(i + 1) % segments]
            self.triangle((cx, cy, zf), (ax, ay, zf), (bx, by, zf), color)  # front
            self.triangle((cx, cy, zb), (bx, by, zb), (ax, ay, zb), color)  # back
            self.quad((ax, ay, zb), (bx, by, zb), (bx, by, zf), (ax, ay, zf), rim)  # edge

    def octahedron(self, center, radius, colors):
        cx, cy, cz = center
        top, bottom = (cx, cy + radius, cz), (cx, cy - radius, cz)
        ring = [(cx + radius, cy, cz), (cx, cy, cz - radius), (cx - radius, cy, cz), (cx, cy, cz + radius)]
        for i in range(4):
            a, b = ring[i], ring[(i + 1) % 4]
            self.triangle(a, b, top, colors[i % len(colors)])
            self.triangle(b, a, bottom, colors[(i + 1) % len(colors)])


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


# --- Writing a .glb -------------------------------------------------------------

def write_glb(path, mesh, name):
    count = len(mesh.positions)
    # The binary chunk: positions, normals, colours (3 floats each), then indices.
    blob = bytearray()
    views, accessors = [], []

    def add_view(data, target):
        while len(blob) % 4:  # glTF wants every view 4-byte aligned
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    def vec3_accessor(values, with_bounds=False):
        data = b"".join(struct.pack("<3f", *v) for v in values)
        acc = {"bufferView": add_view(data, 34962), "componentType": 5126, "count": count, "type": "VEC3"}
        if with_bounds:  # POSITION must say its min and max
            acc["min"] = [min(v[i] for v in values) for i in range(3)]
            acc["max"] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(acc)
        return len(accessors) - 1

    pos = vec3_accessor(mesh.positions, with_bounds=True)
    nrm = vec3_accessor(mesh.normals)
    col = vec3_accessor(mesh.colors)
    idx_data = struct.pack(f"<{len(mesh.indices)}I", *mesh.indices)
    accessors.append({"bufferView": add_view(idx_data, 34963), "componentType": 5125,
                      "count": len(mesh.indices), "type": "SCALAR"})
    idx = len(accessors) - 1
    while len(blob) % 4:
        blob.append(0)

    gltf = {
        "asset": {"version": "2.0", "generator": "game-engine tools/make_game_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": pos, "NORMAL": nrm, "COLOR_0": col},
            "indices": idx}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)  # the JSON chunk is padded with spaces

    with open(path, "wb") as f:
        total = 12 + 8 + len(js) + 8 + len(blob)
        f.write(struct.pack("<4sII", b"glTF", 2, total))      # header: magic, version, length
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)    # chunk 0: JSON
        f.write(struct.pack("<I4s", len(blob), b"BIN\0") + blob)  # chunk 1: binary data
    print(f"wrote {path.relative_to(ROOT)} ({count} vertices)")


def make_models():
    MODELS.mkdir(parents=True, exist_ok=True)

    # Player: a little robot whose origin is the centre of its 0.5 m sphere
    # collider. It faces +Z, glTF's "forward" for models.
    m = Mesh()
    blue, dark, light = (0.25, 0.45, 0.95), (0.12, 0.14, 0.2), (0.85, 0.88, 0.95)
    m.box((-0.17, -0.38, 0.0), (0.2, 0.24, 0.3), dark)    # left foot
    m.box((0.17, -0.38, 0.0), (0.2, 0.24, 0.3), dark)     # right foot
    m.box((0.0, -0.05, 0.0), (0.66, 0.44, 0.5), blue)     # body
    m.box((0.0, 0.33, 0.0), (0.5, 0.34, 0.44), light)     # head
    m.box((-0.11, 0.35, 0.225), (0.09, 0.12, 0.02), dark)  # eyes
    m.box((0.11, 0.35, 0.225), (0.09, 0.12, 0.02), dark)
    m.box((0.0, 0.56, 0.0), (0.05, 0.14, 0.05), dark)     # antenna
    m.box((0.0, 0.66, 0.0), (0.1, 0.08, 0.1), (1.0, 0.35, 0.3))
    write_glb(MODELS / "player.glb", m, "player")

    # Coin: a gold disc standing upright, with a raised centre.
    m = Mesh()
    m.disc_z((0, 0, 0), 0.35, 0.08, (1.0, 0.78, 0.2), (0.85, 0.55, 0.1))
    m.disc_z((0, 0, 0), 0.2, 0.12, (1.0, 0.88, 0.45), (0.85, 0.55, 0.1))
    write_glb(MODELS / "coin.glb", m, "coin")

    # Crate: a 1 m cube with a darker frame along its edges.
    m = Mesh()
    wood, frame, t = (0.72, 0.5, 0.28), (0.45, 0.28, 0.14), 0.12
    m.box((0, 0, 0), (0.96, 0.96, 0.96), wood)
    for a in (-0.5 + t / 2, 0.5 - t / 2):
        for b in (-0.5 + t / 2, 0.5 - t / 2):
            m.box((0, a, b), (1.0, t, t), frame)  # edges along X
            m.box((a, 0, b), (t, 1.0, t), frame)  # along Y
            m.box((a, b, 0), (t, t, 1.0), frame)  # along Z
    write_glb(MODELS / "crate.glb", m, "crate")

    # Platform: a 1 m block of earth with grass on top, meant to be scaled
    # out sideways into floors and ledges.
    m = Mesh()
    m.box((0, -0.1, 0), (1.0, 0.8, 1.0), (0.5, 0.36, 0.24))
    m.box((0, 0.4, 0), (1.0, 0.2, 1.0), (0.35, 0.62, 0.3), top=(0.42, 0.75, 0.35))
    write_glb(MODELS / "platform.glb", m, "platform")

    # Enemy: a spinning red diamond with white eyes.
    m = Mesh()
    m.octahedron((0, 0, 0), 0.5, [(0.9, 0.2, 0.2), (0.6, 0.1, 0.12)])
    m.box((-0.12, 0.1, 0.33), (0.1, 0.12, 0.1), (1, 1, 1))
    m.box((0.12, 0.1, 0.33), (0.1, 0.12, 0.1), (1, 1, 1))
    write_glb(MODELS / "enemy.glb", m, "enemy")


# --- Sounds -------------------------------------------------------------------------

RATE = 22050


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32000)) for s in samples))
    print(f"wrote {path.relative_to(ROOT)} ({len(samples) / RATE:.2f} s)")


def tone(freq, seconds, volume=0.5, shape="square", decay=6.0, slide=0.0):
    """One note. `slide` bends the pitch by that many Hz over its length."""
    out, phase = [], 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / RATE
        phase += (freq + slide * t / seconds) / RATE
        if shape == "square":
            s = 1.0 if (phase % 1.0) < 0.5 else -1.0
        elif shape == "triangle":
            s = 4 * abs((phase % 1.0) - 0.5) - 1
        else:
            s = math.sin(2 * math.pi * phase)
        attack = min(1.0, i / (0.004 * RATE))  # 4 ms fade in avoids a click
        out.append(s * volume * attack * math.exp(-decay * t))
    return out


def mix(*tracks):
    out = [0.0] * max(len(t) for t in tracks)
    for t in tracks:
        for i, s in enumerate(t):
            out[i] += s
    return out


def make_sounds():
    SOUNDS.mkdir(parents=True, exist_ok=True)
    write_wav(SOUNDS / "coin.wav", tone(988, 0.07, 0.3, decay=4) + tone(1319, 0.25, 0.3, decay=10))
    write_wav(SOUNDS / "jump.wav", tone(300, 0.18, 0.25, decay=8, slide=500))

    random.seed(7)
    hurt = [s * (0.6 + 0.4 * random.random()) for s in tone(220, 0.35, 0.35, decay=6, slide=-150)]
    write_wav(SOUNDS / "hurt.wav", hurt)

    win = []
    for f in (523, 659, 784):
        win += tone(f, 0.12, 0.3, decay=3)
    win += tone(1047, 0.6, 0.3, decay=3)
    write_wav(SOUNDS / "win.wav", win)

    lose = []
    for f in (392, 370, 349):
        lose += tone(f, 0.2, 0.3, shape="triangle", decay=2)
    lose += tone(330, 0.7, 0.3, shape="triangle", decay=2)
    write_wav(SOUNDS / "lose.wav", lose)

    # Music: four bars of bass and an arpeggio, 120 bpm, made to loop.
    beat = 0.5
    chords = [(220.0, (440, 523, 659)), (175.0, (349, 440, 523)), (262.0, (392, 523, 659)), (196.0, (392, 494, 587))]
    bass, arp = [], []
    for root, notes in chords:
        for b in range(4):
            bass += tone(root if b % 2 == 0 else root * 1.5, beat, 0.22, shape="triangle", decay=2.5)
            for k in range(2):
                arp += tone(notes[(b * 2 + k) % 3] * 2, beat / 2, 0.06, shape="square", decay=9)
    write_wav(SOUNDS / "music.wav", mix(bass, arp))


if __name__ == "__main__":
    make_models()
    make_sounds()
