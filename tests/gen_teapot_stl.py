#!/usr/bin/env python3
"""Genere une petite theiere parametrique (corps + couvercle + bec + anse) en STL binaire.
Geometrie 100% generee (ellipsoides + tubes), pas de dataset externe.
"""
import struct, math, os

tris = []  # liste de triangles : ((x,y,z),(x,y,z),(x,y,z))

def add_tri(a, b, c):
    tris.append((a, b, c))

def add_quad(a, b, c, d):
    add_tri(a, b, c)
    add_tri(a, c, d)

# --- helpers vecteurs ---
def vsub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def vadd(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def vscale(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def vcross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def vlen(a): return math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2])
def vnorm(a):
    l = vlen(a)
    return (a[0]/l, a[1]/l, a[2]/l) if l > 1e-9 else (0.0, 0.0, 0.0)

# --- ellipsoide (corps, couvercle, bouton, ...) ---
def ellipsoid(center, sx, sy, sz, nlat=22, nlon=30):
    cx, cy, cz = center
    def V(i, j):
        theta = math.pi * i / nlat          # 0 (pole haut) .. pi (pole bas)
        phi = 2 * math.pi * j / nlon
        x = sx * math.sin(theta) * math.cos(phi)
        y = sy * math.cos(theta)
        z = sz * math.sin(theta) * math.sin(phi)
        return (cx + x, cy + y, cz + z)
    for i in range(nlat):
        for j in range(nlon):
            a = V(i, j); b = V(i, (j+1) % nlon)
            c = V(i+1, (j+1) % nlon); d = V(i+1, j)
            if i == 0:
                add_tri(a, c, d)            # cap pole haut
            elif i == nlat-1:
                add_tri(a, b, d)            # cap pole bas
            else:
                add_quad(a, b, c, d)

# --- tube le long d'un chemin (bec, anse) ; extremites ouvertes ---
def tube(path, radii, nseg=16):
    n = len(path)
    rings = []
    for i in range(n):
        if i == 0:
            tang = vsub(path[1], path[0])
        elif i == n-1:
            tang = vsub(path[n-1], path[n-2])
        else:
            tang = vsub(path[i+1], path[i-1])
        tang = vnorm(tang)
        up = (0.0, 1.0, 0.0)
        if abs(vcross(tang, up)[0]) + abs(vcross(tang, up)[1]) + abs(vcross(tang, up)[2]) < 1e-6:
            up = (1.0, 0.0, 0.0)
        side = vnorm(vcross(tang, up))
        up2 = vnorm(vcross(side, tang))
        r = radii[i]
        ring = []
        for k in range(nseg):
            a = 2 * math.pi * k / nseg
            off = vadd(vscale(side, r*math.cos(a)), vscale(up2, r*math.sin(a)))
            ring.append(vadd(path[i], off))
        rings.append(ring)
    for i in range(n-1):
        for k in range(nseg):
            k2 = (k+1) % nseg
            add_quad(rings[i][k], rings[i][k2], rings[i+1][k2], rings[i+1][k])

def bezier3(p0, p1, p2, n):
    pts = []
    for i in range(n):
        t = i / (n-1)
        u = 1-t
        x = u*u*p0[0] + 2*u*t*p1[0] + t*t*p2[0]
        y = u*u*p0[1] + 2*u*t*p1[1] + t*t*p2[1]
        z = u*u*p0[2] + 2*u*t*p1[2] + t*t*p2[2]
        pts.append((x, y, z))
    return pts

# ===== Construction de la theiere =====
# Corps (ellipsoide aplati)
ellipsoid((0, 0, 0), 1.4, 1.05, 1.4, nlat=26, nlon=36)
# Couvercle (disque bombe) + bouton
ellipsoid((0, 1.02, 0), 0.62, 0.18, 0.62, nlat=14, nlon=28)
ellipsoid((0, 1.28, 0), 0.16, 0.18, 0.16, nlat=12, nlon=18)

# Bec verseur (tube courbe qui s'amincit)
spout = bezier3((1.15, -0.05, 0), (2.05, 0.15, 0), (2.35, 1.05, 0), 14)
tube(spout, [0.30 - 0.16*(i/13) for i in range(14)], nseg=18)

# Anse (arc tubulaire cote oppose)
handle = []
for i in range(16):
    a = math.radians(118 + (242-118) * i/15)   # bombe vers -x
    handle.append((-0.95 + 0.85*math.cos(a), 0.45 + 0.78*math.sin(a), 0.0))
tube(handle, [0.13]*16, nseg=14)

# ===== Ecriture STL binaire =====
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "teapot.stl")
with open(out, "wb") as f:
    f.write(b"theiere parametrique - test parametric-wall-art".ljust(80, b"\0"))
    f.write(struct.pack("<I", len(tris)))
    for a, b, c in tris:
        n = vnorm(vcross(vsub(b, a), vsub(c, a)))
        f.write(struct.pack("<3f", *n))
        f.write(struct.pack("<3f", *a))
        f.write(struct.pack("<3f", *b))
        f.write(struct.pack("<3f", *c))
        f.write(struct.pack("<H", 0))

print("theiere generee :", out, "-", len(tris), "triangles")
