#!/usr/bin/env python3
"""Genere une sphere STL ASCII (rayon 10, centree en (10,10,10)) pour tester le slicer."""
import math
import os

R = 10.0
C = (10.0, 10.0, 10.0)
N_LAT = 24   # parallels (hors poles)
N_LON = 32   # meridiens


def vertex(i_lat, j_lon):
    # i_lat de 0 (pole nord) a N_LAT+1 (pole sud)
    theta = math.pi * i_lat / (N_LAT + 1)   # 0..pi
    phi = 2.0 * math.pi * j_lon / N_LON     # 0..2pi
    x = C[0] + R * math.sin(theta) * math.cos(phi)
    y = C[1] + R * math.sin(theta) * math.sin(phi)
    z = C[2] + R * math.cos(theta)
    return (x, y, z)


def normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    n = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return (nx / n, ny / n, nz / n)


tris = []
north = (C[0], C[1], C[2] + R)
south = (C[0], C[1], C[2] - R)

# Calotte nord
for j in range(N_LON):
    a = north
    b = vertex(1, j)
    c = vertex(1, (j + 1) % N_LON)
    tris.append((a, b, c))

# Bandes intermediaires
for i in range(1, N_LAT):
    for j in range(N_LON):
        a = vertex(i, j)
        b = vertex(i + 1, j)
        c = vertex(i + 1, (j + 1) % N_LON)
        d = vertex(i, (j + 1) % N_LON)
        tris.append((a, b, c))
        tris.append((a, c, d))

# Calotte sud
for j in range(N_LON):
    a = south
    b = vertex(N_LAT, (j + 1) % N_LON)
    c = vertex(N_LAT, j)
    tris.append((a, b, c))

here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "sphere.stl"), "w") as f:
    f.write("solid sphere\n")
    for a, b, c in tris:
        f.write("  facet normal %f %f %f\n" % normal(a, b, c))
        f.write("    outer loop\n")
        for p in (a, b, c):
            f.write("      vertex %f %f %f\n" % p)
        f.write("    endloop\n")
        f.write("  endfacet\n")
    f.write("endsolid sphere\n")

print("sphere.stl genere (%d triangles) dans %s" % (len(tris), here))
