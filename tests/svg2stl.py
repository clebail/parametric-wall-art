#!/usr/bin/env python3
"""Convertit un SVG au trait en STL extrude (ajout de profondeur)."""
import argparse
import numpy as np
from svgpathtools import svg2paths2
from shapely.geometry import LineString
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
    ap.add_argument("--width", type=float, default=1.0, help="largeur du trait (mm)")
    ap.add_argument("--depth", type=float, default=3.0, help="profondeur / extrusion (mm)")
    args = ap.parse_args()

    paths, attrs, svg_attr = svg2paths2(args.svg)
    lines = []
    for p in paths:
        pts = sample_path(p)
        if len(pts) >= 2:
            lines.append(LineString(pts))

    # le SVG a l'axe Y vers le bas : on miroite pour un rendu correct
    ribbons = [ln.buffer(args.width / 2.0, cap_style=1, join_style=1) for ln in lines]
    poly = unary_union(ribbons)

    mesh = trimesh.creation.extrude_polygon(poly, height=args.depth)
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
