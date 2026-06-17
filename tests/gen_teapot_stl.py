#!/usr/bin/env python3
"""Genere une theiere de test : corps + couvercle + bouton + bec (tronc de cone plein) +
anse (tore plein), FUSIONNES en un seul maillage etanche (union booleenne), puis tronques par
le plan x,y AU CENTRE (z=0) -> relief a fond plat, pose pour une decoupe selon X.

Pourquoi une union booleenne : des solides qui se chevauchent sans etre fusionnes produisent des
surfaces internes -> contours en double au tranchage (mauvais pour la decoupe). L'union donne un
manifold propre. Le bec et l'anse sont des volumes PLEINS et assez epais (en z) pour atteindre le
fond plat apres la coupe au centre : le modele est decoupable selon X (seuls quelques slivers
d'extremite du bec/anse restent fins -> geres par le filtre d'ilots de l'appli).

Dependances : trimesh + un backend booleen (manifold3d recommande). Installation :
    pip install trimesh manifold3d numpy
"""
import os
import sys
import numpy as np

try:
    import trimesh
except ImportError:
    sys.exit("trimesh requis : pip install trimesh manifold3d numpy")


def scaled_sphere(sx, sy, sz, t=(0, 0, 0), subdiv=4):
    """Icosphere mise a l'echelle non uniforme (ellipsoide) puis translatee."""
    m = trimesh.creation.icosphere(subdivisions=subdiv, radius=1.0)
    m.apply_transform(np.diag([sx, sy, sz, 1.0]))
    m.apply_translation(t)
    return m


def frustum(r0, r1, h, n=32):
    """Tronc de cone plein (rayon r0 en z=0 -> r1 en z=h), maillage etanche construit a la main
    (parois + 2 capots), sans dependance a scipy/qhull."""
    ang = np.linspace(0, 2 * np.pi, n, endpoint=False)
    bot = np.c_[r0 * np.cos(ang), r0 * np.sin(ang), np.zeros(n)]
    top = np.c_[r1 * np.cos(ang), r1 * np.sin(ang), np.full(n, h)]
    verts = np.vstack([bot, top, [0.0, 0.0, 0.0], [0.0, 0.0, h]])
    ib, it, icb, ict = 0, n, 2 * n, 2 * n + 1
    faces = []
    for k in range(n):
        k2 = (k + 1) % n
        faces += [[ib + k, ib + k2, it + k2], [ib + k, it + k2, it + k]]  # paroi
        faces += [[icb, ib + k2, ib + k]]                                 # capot bas (-z)
        faces += [[ict, it + k, it + k2]]                                 # capot haut (+z)
    return trimesh.Trimesh(vertices=verts, faces=np.array(faces), process=True)


# ===== Pieces (toutes symetriques / centrees autour du plan z=0) =====
parts = [
    scaled_sphere(1.40, 1.05, 1.40),                  # corps (panse aplatie)
    scaled_sphere(0.62, 0.20, 0.62, (0, 1.00, 0), 3), # couvercle
    scaled_sphere(0.20, 0.24, 0.20, (0, 1.28, 0), 3), # bouton
]

# Bec : tronc de cone plein, bout epais (pas de pointe), oriente vers +x / +y dans le plan z=0.
spout = frustum(0.40, 0.27, 1.60)   # bout franc et epais -> derniere lamelle bien visible
d = np.array([0.90, 0.42, 0.0]); d /= np.linalg.norm(d)
spout.apply_transform(trimesh.geometry.align_vectors([0, 0, 1], d))
spout.apply_translation([0.95, -0.15, 0.0])
parts.append(spout)

# Anse : tore plein dans le plan z=0, cote -x. Un cote du tube s'enfonce dans le corps (fusion),
# mais la boucle est poussee vers -x pour enfermer du vide HORS du corps -> trou visible.
# Sommet plafonne ~y=1.0 (sous le couvercle) : plus basse que la version d'origine.
handle = trimesh.creation.torus(major_radius=0.62, minor_radius=0.20,
                                major_sections=44, minor_sections=22)
handle.apply_translation([-1.15, 0.20, 0.0])
parts.append(handle)

# ===== Union booleenne (manifold propre) =====
try:
    mesh = trimesh.boolean.union(parts)
except Exception as e:
    sys.exit("union booleenne impossible (backend manquant ?) : %r\n"
             "Installe un backend : pip install manifold3d" % e)
print("union : watertight=%s, %d faces" % (mesh.is_watertight, len(mesh.faces)))

# ===== Troncature : plan x,y AU CENTRE (z=0), on garde +z, via intersection avec une boite =====
# Le volume est symetrique en z : couper a z=0 fait que chaque section atteint le fond plat avec
# une profondeur = epaisseur locale du modele -> lamelles accrochables (decoupable selon X).
# On clippe aussi le bout du bec par un plan X=const : son capot incline donnerait sinon une
# derniere lamelle en pointe ; coupe verticale -> derniere lamelle pleine, comme la precedente.
lo, hi = mesh.bounds[0] - 1.0, mesh.bounds[1] + 1.0
x_cut = mesh.bounds[1][0] - 0.22          # retire le capot oblique du bec (face verticale nette)
box = trimesh.creation.box(extents=[x_cut - lo[0], hi[1] - lo[1], hi[2]])
box.apply_translation([(lo[0] + x_cut) / 2, (lo[1] + hi[1]) / 2, hi[2] / 2.0])  # z 0->hi, x lo->x_cut
mesh = trimesh.boolean.intersection([mesh, box])
print("coupe z>=0, bec @x<=%.3f : watertight=%s, %d faces, z=%s" %
      (x_cut, mesh.is_watertight, len(mesh.faces), mesh.bounds[:, 2].tolist()))

# ===== Ecriture STL binaire =====
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "teapot.stl")
mesh.export(out)
print("theiere generee :", out, "-", len(mesh.faces), "triangles")
