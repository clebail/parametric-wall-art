#!/usr/bin/env python3
"""Étape 1 du workflow « SVG par lamelle, sculpté à la main » (PLAN.md).

Échantillonne un STL source en N sections (Y,Z) le long de l'axe X (= axe de
coupe) et écrit un SVG par lamelle, prêt à éditer à la main sous Inkscape, avec
onion-skinning : 3 calques

  - lamelle-precedente (i-1)  : ghost, VERROUILLÉ (sodipodi:insensitive)
  - lamelle-courante   (i)    : le SEUL déverrouillé, à éditer
  - lamelle-suivante   (i+1)  : ghost, VERROUILLÉ

Conventions (cf. PLAN.md) : Y vertical (hauteur), Z horizontal (profondeur hors
mur), dos plat à Z=0, unités mm. TOUTES les lamelles partagent le même cadre
(même viewBox) pour que les voisines se superposent exactement.

Aucune dépendance externe (numpy seulement). STL binaire en entrée/lecture.

Usage :
  python3 tests/stl2lamelles_svg.py [SRC.stl] [N] [OUTDIR]
  défauts : tests/tornade_hull.stl  52  tests/lamelles
"""
import sys, os, struct
import numpy as np

# ----- paramètres (overridables en argv) -----
SRC    = sys.argv[1] if len(sys.argv) > 1 else 'tests/tornade_hull.stl'
N      = int(sys.argv[2]) if len(sys.argv) > 2 else 52
OUTDIR = sys.argv[3] if len(sys.argv) > 3 else 'tests/lamelles'
PAD    = 12.0          # marge (mm) autour de la forme dans le SVG
TOL    = 1e-4          # tolérance de raccord des segments (en unités modèle/TOL)
# rappel (non dessiné dans la section, appliqué à l'extrusion ensuite) :
LAM_THICK, LAM_GAP = 5.0, 5.0


# ---------- lecture STL binaire ----------
def read_stl(path):
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[80:84])[0]
    tris = np.empty((n, 3, 3), np.float64)
    off = 84
    for i in range(n):
        off += 12  # saute la normale
        v = struct.unpack('<9f', d[off:off + 36]); off += 36 + 2
        tris[i] = np.array(v).reshape(3, 3)
    return tris


# ---------- section d'un plan X = c ----------
def section(tris, c):
    """Renvoie la liste des segments [(p0, p1)] dans le plan X=c, p=(y, z)."""
    segs = []
    s = tris[:, :, 0] - c                      # signe par sommet (T,3)
    cross = ~((s > 0).all(1) | (s < 0).all(1))  # triangle traversé par le plan
    for tri, sg in zip(tris[cross], s[cross]):
        pts = []
        for a, b in ((0, 1), (1, 2), (2, 0)):
            sa, sb = sg[a], sg[b]
            if (sa <= 0 < sb) or (sb <= 0 < sa):
                t = sa / (sa - sb)
                p = tri[a] + t * (tri[b] - tri[a])
                pts.append((p[1], p[2]))       # (y, z)
        if len(pts) == 2:
            segs.append((pts[0], pts[1]))
    return segs


def chain(segs):
    """Raccorde les segments en boucles fermées (y, z)."""
    from collections import defaultdict
    key = lambda p: (round(p[0] / TOL), round(p[1] / TOL))
    adj = defaultdict(list)
    for i, (a, b) in enumerate(segs):
        adj[key(a)].append(i); adj[key(b)].append(i)
    used = [False] * len(segs)
    loops = []
    for start in range(len(segs)):
        if used[start]:
            continue
        used[start] = True
        a, b = segs[start]
        loop = [a, b]; cur = b
        while True:
            nxt = None
            for j in adj[key(cur)]:
                if not used[j]:
                    nxt = j; break
            if nxt is None:
                break
            used[nxt] = True
            p0, p1 = segs[nxt]
            cur = p1 if key(p0) == key(cur) else p0
            if key(cur) == key(loop[0]):
                break
            loop.append(cur)
        if len(loop) >= 3:
            loops.append(np.array(loop))
    return loops


def poly_area(loop):
    y, z = loop[:, 0], loop[:, 1]
    return 0.5 * abs(np.dot(y, np.roll(z, 1)) - np.dot(z, np.roll(y, 1)))


# ---------- SVG ----------
SVG_HEAD = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<svg xmlns="http://www.w3.org/2000/svg" '
    'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
    'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd" '
    'width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">\n'
)


def layer(label, locked, body):
    lock = ' sodipodi:insensitive="true"' if locked else ''
    return (f'  <g inkscape:groupmode="layer" inkscape:label="{label}"'
            f' id="{label}"{lock}>\n{body}  </g>\n')


def path_d(loop, to_svg):
    pts = [to_svg(y, z) for y, z in loop]
    d = 'M ' + ' L '.join(f'{x:.3f},{yy:.3f}' for x, yy in pts) + ' Z'
    return d


def main():
    tris = read_stl(SRC)
    xmin, xmax = tris[:, :, 0].min(), tris[:, :, 0].max()
    # plans aux centres de N tranches égales (évite les capots dégénérés des bouts)
    step = (xmax - xmin) / N
    xs = xmin + (np.arange(N) + 0.5) * step

    # 1) extraire toutes les sections, garder la/les boucle(s) significative(s)
    all_loops = []
    for c in xs:
        loops = chain(section(tris, c))
        loops = [lp for lp in loops if poly_area(lp) > 1.0]   # mm²
        all_loops.append(loops)

    # 2) cadre GLOBAL commun (onion-skin) à partir de toutes les boucles
    allp = np.vstack([lp for loops in all_loops for lp in loops])
    ymin, ymax = allp[:, 0].min(), allp[:, 0].max()
    zmin, zmax = min(allp[:, 1].min(), 0.0), allp[:, 1].max()  # dos plat z=0 inclus
    W = (zmax - zmin) + 2 * PAD
    H = (ymax - ymin) + 2 * PAD
    # (y,z) modèle -> (x,y) SVG : z horizontal, y vertical (haut = +y), mm
    to_svg = lambda y, z: ((z - zmin) + PAD, (ymax - y) + PAD)

    def loops_body(loops, style):
        return ''.join(f'    <path d="{path_d(lp, to_svg)}" style="{style}"/>\n'
                       for lp in loops)

    GHOST_P = 'fill:none;stroke:#e6a0a0;stroke-width:0.6;stroke-dasharray:2,2'
    GHOST_N = 'fill:none;stroke:#a0b8e6;stroke-width:0.6;stroke-dasharray:2,2'
    CURRENT = 'fill:#d8d8d8;fill-opacity:0.35;stroke:#000000;stroke-width:1.0'
    BACK    = (f'    <line x1="{to_svg(ymin,0)[0]:.3f}" y1="{to_svg(ymin,0)[1]:.3f}"'
               f' x2="{to_svg(ymax,0)[0]:.3f}" y2="{to_svg(ymax,0)[1]:.3f}"'
               f' style="stroke:#bbbbbb;stroke-width:0.4;stroke-dasharray:1,2"/>\n')

    os.makedirs(OUTDIR, exist_ok=True)
    for i in range(N):
        parts = SVG_HEAD.format(w=f'{W:.2f}', h=f'{H:.2f}')
        # ghost précédente (verrouillée)
        if i > 0:
            parts += layer('lamelle-precedente', True, loops_body(all_loops[i - 1], GHOST_P))
        # ghost suivante (verrouillée)
        if i < N - 1:
            parts += layer('lamelle-suivante', True, loops_body(all_loops[i + 1], GHOST_N))
        # courante (déverrouillée) : repère dos plat Z=0 + contour éditable
        body = BACK + loops_body(all_loops[i], CURRENT)
        parts += layer('lamelle-courante', False, body)
        parts += '</svg>\n'
        open(os.path.join(OUTDIR, f'lamelle_{i + 1:02d}.svg'), 'w').write(parts)

    npts = [sum(len(lp) for lp in loops) for loops in all_loops]
    print(f'{SRC}: {tris.shape[0]} tris  X=[{xmin:.1f},{xmax:.1f}]  pitch={step:.1f}mm')
    print(f'{N} SVG -> {OUTDIR}/lamelle_01..{N:02d}.svg')
    print(f'cadre commun (mm) : W(z)={W:.1f}  H(y)={H:.1f}  ;  z in [0,{zmax:.0f}]  y span {ymax-ymin:.0f}')
    print(f'pts/section : min={min(npts)} max={max(npts)}  '
          f'(boucles : {min(len(l) for l in all_loops)}..{max(len(l) for l in all_loops)})')
    print(f'rappel extrusion : épaisseur {LAM_THICK}mm + vide {LAM_GAP}mm (non dessiné)')


if __name__ == '__main__':
    main()
