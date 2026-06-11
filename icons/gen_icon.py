#!/usr/bin/env python3
"""Genere l'icone de l'app : un volume tranche en lamelles verticales de bois (cote a cote),
sur fond sombre arrondi. Produit appicon.svg puis (via rasterisation externe) appicon.png.
"""
import math, os

S = 256                      # taille canvas
PAD = 30                     # marge interne
N = 11                       # nombre de lamelles
INNER = S - 2 * PAD
MIDX = S / 2.0
BASE = S - PAD - 6           # ligne de pose des lamelles
HMAX = INNER * 0.80          # hauteur lamelle centrale
HMIN = INNER * 0.20          # hauteur lamelle de bord

slot = INNER / N
bw = slot * 0.66             # largeur lamelle
rx = bw * 0.42               # arrondi haut

bars = []
for i in range(N):
    cx = PAD + slot * (i + 0.5)
    xn = (cx - MIDX) / (INNER / 2.0)            # -1 .. 1
    h = HMIN + (HMAX - HMIN) * math.sqrt(max(0.0, 1.0 - xn * xn))
    x = cx - bw / 2.0
    y = BASE - h
    bars.append((x, y, bw, h))

svg = []
svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" viewBox="0 0 {S} {S}">')
svg.append('''  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#1d4a4f"/>
      <stop offset="1" stop-color="#11272b"/>
    </linearGradient>
    <linearGradient id="wood" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0"   stop-color="#f0c489"/>
      <stop offset="0.5" stop-color="#c88a4a"/>
      <stop offset="1"   stop-color="#7d4f24"/>
    </linearGradient>
  </defs>''')

# fond arrondi
svg.append(f'  <rect x="6" y="6" width="{S-12}" height="{S-12}" rx="46" ry="46" fill="url(#bg)"/>')
# ligne de pose
svg.append(f'  <rect x="{PAD-2}" y="{BASE+3:.1f}" width="{INNER+4}" height="5" rx="2.5" fill="#0c1c1f"/>')

# lamelles
for (x, y, w, h) in bars:
    svg.append(
        f'  <rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
        f'rx="{rx:.1f}" ry="{rx:.1f}" fill="url(#wood)" stroke="#5a3a1c" stroke-width="1.3"/>'
    )

svg.append('</svg>')

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "appicon.svg")
with open(out, "w") as f:
    f.write("\n".join(svg))
print("icone SVG generee :", out)
