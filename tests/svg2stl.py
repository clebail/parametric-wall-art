#!/usr/bin/env python3
"""Convertit un SVG en STL extrude (ajout de profondeur).

Deux modes :
  - par defaut : epaissit chaque trait en ruban (buffer) -> dessin au trait en relief.
  - --fill     : REMPLIT l'interieur des contours fermes (regle pair/impair pour les trous)
                 -> volume PLEIN dans lequel on peut tailler des lamelles.
"""
import argparse
from functools import reduce
import numpy as np
from svgpathtools import svg2paths2
from shapely.geometry import LineString, Polygon, MultiPolygon
from shapely.ops import unary_union
import trimesh


def sample_path(path, n_per_seg=40):
    pts = []
    for seg in path:
        ts = np.linspace(0, 1, n_per_seg)
        for t in ts:
            z = seg.point(t)
            pts.append((z.real, z.imag))
    # supprime les doublons consecutifs
    out = [pts[0]]
    for p in pts[1:]:
        if abs(p[0] - out[-1][0]) > 1e-9 or abs(p[1] - out[-1][1]) > 1e-9:
            out.append(p)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("svg")
    ap.add_argument("stl")
    ap.add_argument("--width", type=float, default=1.0, help="largeur du trait (mm, mode ruban)")
    ap.add_argument("--depth", type=float, default=3.0, help="profondeur / extrusion (mm)")
    ap.add_argument("--fill", action="store_true",
                    help="remplit l'interieur des contours fermes (volume plein) au lieu d'epaissir "
                         "les traits")
    ap.add_argument("--simplify", type=float, default=-1.0,
                    help="tolerance de simplification du contour (mode --fill ; <0 = auto). Les points "
                         "quasi alignes de l'echantillonnage cassent l'etancheite de l'extrusion.")
    args = ap.parse_args()

    paths, attrs, svg_attr = svg2paths2(args.svg)

    if args.fill:
        # Remplissage : chaque path -> anneau ferme -> polygone. Combinaison en regle PAIR/IMPAIR
        # (symmetric_difference) : une zone couverte un nombre impair de fois est pleine, sinon
        # c'est un trou. buffer(0) repare les auto-intersections.
        polys = []
        for p in paths:
            pts = sample_path(p)
            if len(pts) >= 3:
                poly = Polygon(pts).buffer(0)
                if not poly.is_empty:
                    polys.append(poly)
        if not polys:
            raise SystemExit("aucun contour ferme exploitable pour --fill")
        poly = reduce(lambda a, b: a.symmetric_difference(b), polys)
        # Simplifie : l'echantillonnage fin laisse des points quasi colineaires -> triangles
        # degeneres dans les capots -> maillage non etanche. tol auto = 0.02% de la plus grande dim.
        x0, y0, x1, y1 = poly.bounds
        tol = args.simplify if args.simplify >= 0.0 else 2e-4 * max(x1 - x0, y1 - y0)
        if tol > 0.0:
            poly = poly.simplify(tol).buffer(0)
    else:
        lines = []
        for p in paths:
            pts = sample_path(p)
            if len(pts) >= 2:
                lines.append(LineString(pts))
        ribbons = [ln.buffer(args.width / 2.0, cap_style=1, join_style=1) for ln in lines]
        poly = unary_union(ribbons)

    # extrude_polygon n'accepte qu'un Polygon : on extrude chaque morceau d'un MultiPolygon.
    if isinstance(poly, MultiPolygon):
        mesh = trimesh.util.concatenate(
            [trimesh.creation.extrude_polygon(g, height=args.depth) for g in poly.geoms])
    else:
        mesh = trimesh.creation.extrude_polygon(poly, height=args.depth)

    # Nettoyage : fusionne les sommets dupliques (couture), retire les faces degenerees -> etanche.
    mesh.merge_vertices()
    mesh.update_faces(mesh.nondegenerate_faces())
    mesh.update_faces(mesh.unique_faces())
    mesh.remove_unreferenced_vertices()
    mesh.fill_holes()
    # remet l'objet a plat, origine en bas, Y vers le haut
    mesh.apply_transform(trimesh.transformations.scale_matrix(-1, direction=[0, 1, 0]))
    mesh.apply_translation(-mesh.bounds[0])

    mesh.export(args.stl)
    x, y, z = mesh.extents
    print(f"OK -> {args.stl}")
    print(f"  dimensions: {x:.1f} x {y:.1f} x {z:.1f} mm")
    print(f"  triangles : {len(mesh.faces)}  | watertight: {mesh.is_watertight}")


if __name__ == "__main__":
    main()
