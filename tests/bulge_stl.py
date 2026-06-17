#!/usr/bin/env python3
"""Donne du relief a un STL plat (extrusion d'une silhouette).

Lit un STL qui est l'extrusion plate d'un contour 2D (ex. tests/tornade.stl),
et genere un NOUVEAU STL : dos plat (Z=0, cote mur) + face avant bombee.

Le bombe est une coupole : hauteur fonction de la distance au bord de la
silhouette  ->  z = A * sqrt(1 - (1 - d/D)^2)
  - d   = distance du point au contour (0 sur le bord)
  - D   = distance de reference (def. = demi-epaisseur max -> coupole en demi-ellipse)
  - A   = relief max (mm) au coeur des zones epaisses

La silhouette est l'union 2D des triangles projetes (pas besoin de networkx).
Maillage de sortie ferme et etanche (front + dos + parois laterales).

Usage:
  bulge_stl.py in.stl out.stl [--amp 80] [--radius D] [--res 4]
"""
import argparse, sys
import numpy as np
import shapely
import trimesh
from shapely.geometry import Polygon, LineString
from shapely.ops import unary_union
from scipy import ndimage
from scipy.spatial import Delaunay


def silhouette(mesh):
    """Union 2D (plan XY) des triangles -> Polygon de la silhouette."""
    V, F = mesh.vertices, mesh.faces
    polys = []
    for f in F:
        tri = V[f][:, :2]
        p = Polygon(tri)
        if p.area > 1e-9:
            polys.append(p)
    sil = unary_union(polys)
    # on ne garde que la plus grande partie si MultiPolygon
    if sil.geom_type == "MultiPolygon":
        sil = max(sil.geoms, key=lambda g: g.area)
    return sil


def distance_raster(sil, res):
    """Raster booleen interieur + carte de distance au bord (mm) + origine/pas."""
    minx, miny, maxx, maxy = sil.bounds
    xs = np.arange(minx, maxx + res, res)
    ys = np.arange(miny, maxy + res, res)
    XX, YY = np.meshgrid(xs, ys)
    inside = shapely.contains(sil, shapely.points(XX.ravel(), YY.ravel()))
    inside = inside.reshape(len(ys), len(xs))
    d = ndimage.distance_transform_edt(inside) * res
    return inside, d, (minx, miny), res


def sample_d(d, origin, res, pts):
    """Echantillonnage bilineaire de la carte de distance aux positions pts (Nx2)."""
    ox, oy = origin
    gx = (pts[:, 0] - ox) / res
    gy = (pts[:, 1] - oy) / res
    H, W = d.shape
    x0 = np.clip(np.floor(gx).astype(int), 0, W - 2)
    y0 = np.clip(np.floor(gy).astype(int), 0, H - 2)
    fx = np.clip(gx - x0, 0, 1)
    fy = np.clip(gy - y0, 0, 1)
    d00 = d[y0, x0]; d10 = d[y0, x0 + 1]
    d01 = d[y0 + 1, x0]; d11 = d[y0 + 1, x0 + 1]
    return (d00 * (1 - fx) * (1 - fy) + d10 * fx * (1 - fy)
            + d01 * (1 - fx) * fy + d11 * fx * fy)


def boundary_loops(faces):
    """Aretes presentes dans 1 seul triangle -> boucles ordonnees de sommets."""
    from collections import defaultdict
    cnt = defaultdict(int)
    for a, b, c in faces:
        for e in ((a, b), (b, c), (c, a)):
            cnt[tuple(sorted(e))] += 1
    edges = [e for e, n in cnt.items() if n == 1]
    adj = defaultdict(list)
    for a, b in edges:
        adj[a].append(b); adj[b].append(a)
    loops = []
    used = set()
    for a, b in edges:
        if (a, b) in used or (b, a) in used:
            continue
        loop = [a, b]; used.add((a, b))
        cur, prev = b, a
        while True:
            nxts = [n for n in adj[cur] if n != prev
                    and (cur, n) not in used and (n, cur) not in used]
            if not nxts:
                break
            nx = nxts[0]
            used.add((cur, nx))
            if nx == loop[0]:
                break
            loop.append(nx); prev, cur = cur, nx
        loops.append(loop)
    return loops


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--amp", type=float, default=80.0, help="relief max mm")
    ap.add_argument("--radius", type=float, default=0.0,
                    help="distance de reference D (def. 0 = demi-epaisseur max)")
    ap.add_argument("--res", type=float, default=4.0, help="pas du maillage avant (mm)")
    ap.add_argument("--draster", type=float, default=1.0, help="pas du raster de distance")
    args = ap.parse_args()

    mesh = trimesh.load(args.inp)
    sil = silhouette(mesh)
    print(f"silhouette: aire {sil.area:.0f} mm2, trous {len(sil.interiors)}")

    inside, draster, origin, dres = distance_raster(sil, args.draster)
    D = args.radius if args.radius > 0 else float(draster.max())
    A = args.amp
    print(f"coupole: A={A} mm, D={D:.1f} mm")

    # 1) points interieurs sur grille (loin du bord pour eviter les slivers)
    minx, miny, maxx, maxy = sil.bounds
    res = args.res
    xs = np.arange(minx + res, maxx, res)
    ys = np.arange(miny + res, maxy, res)
    XX, YY = np.meshgrid(xs, ys)
    cand = np.column_stack([XX.ravel(), YY.ravel()])
    dc = sample_d(draster, origin, dres, cand)
    cand = cand[dc > 0.6 * res]

    # 2) points du contour reechantillonnes
    ring = sil.exterior
    n = max(8, int(ring.length / res))
    bs = np.array([ring.interpolate(t, normalized=True).coords[0]
                   for t in np.linspace(0, 1, n, endpoint=False)])

    pts = np.vstack([cand, bs])
    # Delaunay puis on jette les triangles dont le centroide sort de la silhouette
    tri = Delaunay(pts)
    cent = pts[tri.simplices].mean(axis=1)
    keep = shapely.contains(sil, shapely.points(cent[:, 0], cent[:, 1]))
    faces = tri.simplices[keep]
    print(f"sommets {len(pts)}, triangles avant {len(faces)}")

    # hauteur de chaque sommet
    dv = sample_d(draster, origin, dres, pts)
    t = np.clip(dv / D, 0, 1)
    z = A * np.sqrt(np.clip(1 - (1 - t) ** 2, 0, 1))

    N = len(pts)
    front = np.column_stack([pts, z])
    back = np.column_stack([pts, np.zeros(N)])
    verts = np.vstack([front, back])

    def signed_area(a, b, c):
        return ((pts[b, 0] - pts[a, 0]) * (pts[c, 1] - pts[a, 1])
                - (pts[c, 0] - pts[a, 0]) * (pts[b, 1] - pts[a, 1]))

    all_faces = []
    for a, b, c in faces:          # face avant : CCW -> normale +Z
        if signed_area(a, b, c) < 0:
            b, c = c, b
        all_faces.append([a, b, c])
        all_faces.append([a + N, c + N, b + N])   # dos : winding inverse -> normale -Z
    for loop in boundary_loops(faces):   # parois laterales (boucle en CCW)
        area = sum(pts[loop[i], 0] * pts[loop[(i + 1) % len(loop)], 1]
                   - pts[loop[(i + 1) % len(loop)], 0] * pts[loop[i], 1]
                   for i in range(len(loop)))
        if area < 0:
            loop = loop[::-1]
        L = len(loop)
        for i in range(L):
            a = loop[i]; b = loop[(i + 1) % L]
            # CCW exterieur : interieur a gauche -> normale vers l'exterieur
            all_faces.append([a, a + N, b + N])
            all_faces.append([a, b + N, b])

    out = trimesh.Trimesh(vertices=verts, faces=np.array(all_faces), process=True)
    out.merge_vertices()
    print(f"sortie: {len(out.faces)} triangles, watertight={out.is_watertight}")
    print(f"bbox: {np.round(out.extents,1)}")
    out.export(args.out)
    print("ecrit:", args.out)


if __name__ == "__main__":
    main()
