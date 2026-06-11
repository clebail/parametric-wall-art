#!/usr/bin/env python3
"""Genere un cube 20mm en STL ASCII et binaire pour tester le loader CMesh."""
import struct
import os

# Cube [0,20]^3, 8 sommets
V = [
    (0, 0, 0), (20, 0, 0), (20, 20, 0), (0, 20, 0),
    (0, 0, 20), (20, 0, 20), (20, 20, 20), (0, 20, 20),
]
# 12 triangles (i0,i1,i2, normale)
TRIS = [
    (0, 1, 2, (0, 0, -1)), (0, 2, 3, (0, 0, -1)),   # bas  z=0
    (4, 6, 5, (0, 0, 1)),  (4, 7, 6, (0, 0, 1)),    # haut z=20
    (0, 5, 1, (0, -1, 0)), (0, 4, 5, (0, -1, 0)),   # avant y=0
    (3, 2, 6, (0, 1, 0)),  (3, 6, 7, (0, 1, 0)),    # arriere y=20
    (0, 3, 7, (-1, 0, 0)), (0, 7, 4, (-1, 0, 0)),   # gauche x=0
    (1, 5, 6, (1, 0, 0)),  (1, 6, 2, (1, 0, 0)),    # droite x=20
]

here = os.path.dirname(os.path.abspath(__file__))

# --- ASCII ---
with open(os.path.join(here, "cube_ascii.stl"), "w") as f:
    f.write("solid cube\n")
    for i0, i1, i2, n in TRIS:
        f.write("  facet normal %f %f %f\n" % n)
        f.write("    outer loop\n")
        for idx in (i0, i1, i2):
            f.write("      vertex %f %f %f\n" % V[idx])
        f.write("    endloop\n")
        f.write("  endfacet\n")
    f.write("endsolid cube\n")

# --- BINAIRE ---
with open(os.path.join(here, "cube_bin.stl"), "wb") as f:
    f.write(b"binary cube generated for CMesh test".ljust(80, b"\0"))
    f.write(struct.pack("<I", len(TRIS)))
    for i0, i1, i2, n in TRIS:
        f.write(struct.pack("<3f", *n))
        for idx in (i0, i1, i2):
            f.write(struct.pack("<3f", *V[idx]))
        f.write(struct.pack("<H", 0))

print("cube_ascii.stl et cube_bin.stl generes dans", here)
