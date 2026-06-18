#!/usr/bin/env python3
"""Étape (a) du système photo->lamelles : hull dégénéré "2 vues".

Pour l'instant : silhouette de FACE (texture) -> top(x)/bot(x) réels ;
profondeur paramétrique (bombé dos-plat proportionnel à la hauteur, pincé à la
taille) + torsion au niveau de la taille (vortex). Sortie :
  - rendu lamelles 3/4 (PNG)  : pour juger la ressemblance à la réf
  - STL du solide-maître loftée : à trancher ensuite dans l'appli

Dépendances : numpy, scipy, matplotlib, PIL (pas de trimesh/cv2).
"""
import sys, struct
import numpy as np
from PIL import Image
from scipy import ndimage as ndi
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


# ---------- segmentation ----------
def otsu(v):
    v8 = np.clip(v / (v.max() + 1e-9) * 255, 0, 255).astype(np.uint8)
    h, _ = np.histogram(v8.ravel(), 256, (0, 256))
    tot = v8.size; sumv = (np.arange(256) * h).sum(); wB = 0; sumB = 0.; best = 0; thr = 0
    for t in range(256):
        wB += h[t]
        if wB == 0: continue
        wF = tot - wB
        if wF == 0: break
        sumB += t * h[t]; mB = sumB / wB; mF = (sumv - sumB) / wF
        var = wB * wF * (mB - mF) ** 2
        if var > best: best = var; thr = t
    return thr / 255. * v.max()


def _clean(fg):
    fg = ndi.binary_fill_holes(fg)
    lbl, n = ndi.label(fg)
    if n:
        sizes = ndi.sum(np.ones_like(lbl), lbl, range(1, n + 1))
        fg = lbl == (np.argmax(sizes) + 1)
    return ndi.binary_fill_holes(fg)


def silhouette_face(path, win=7):
    """Masque de FACE par TEXTURE locale (lamelles striées, mur lisse)."""
    g = np.asarray(Image.open(path))[..., :3].astype(float).mean(2)
    m = ndi.uniform_filter(g, win); sq = ndi.uniform_filter(g * g, win)
    tex = np.sqrt(np.maximum(sq - m * m, 0))
    fg = tex > otsu(tex) * 0.9
    fg = ndi.binary_closing(fg, iterations=4)
    fg = _clean(fg)
    return ndi.binary_opening(fg, iterations=2)


def silhouette_top(path):
    """Masque de la vue de DESSUS (bois clair sur fond sombre) par luminance."""
    g = np.asarray(Image.open(path))[..., :3].astype(float).mean(2)
    fg = g > otsu(g)
    fg = ndi.binary_closing(fg, iterations=2)
    fg = _clean(fg)
    return ndi.binary_opening(fg, iterations=1)


def thickness(fg):
    """Épaisseur (bot-top) par colonne, NaN si colonne vide."""
    W = fg.shape[1]
    th = np.full(W, np.nan)
    for x in range(W):
        ys = np.where(fg[:, x])[0]
        if ys.size:
            th[x] = ys.max() - ys.min()
    return th


def resample_norm(profile, n):
    """Rééchantillonne un profil (indexé px) sur n points, sur sa plage valide
    normalisée [0,1] -> aligne l'axe X entre deux vues d'échelles différentes."""
    v = ~np.isnan(profile)
    idx = np.where(v)[0]
    x0, x1 = idx[0], idx[-1]
    src = (np.arange(len(profile))[x0:x1 + 1] - x0) / (x1 - x0)
    val = smooth(profile, 6)[x0:x1 + 1]
    return np.interp(np.linspace(0, 1, n), src, val)


def extract_topbot(fg):
    H, W = fg.shape
    top = np.full(W, np.nan); bot = np.full(W, np.nan)
    for x in range(W):
        ys = np.where(fg[:, x])[0]
        if ys.size: top[x] = ys.min(); bot[x] = ys.max()
    return top, bot


def smooth(a, k):
    """Lissage gaussien 1D en ignorant les NaN."""
    v = ~np.isnan(a)
    a0 = np.where(v, a, 0.)
    num = ndi.gaussian_filter1d(a0, k)
    den = ndi.gaussian_filter1d(v.astype(float), k)
    return num / np.maximum(den, 1e-6)


# ---------- construction des sections ----------
def build_sections(top, bot, depth_mm, *, length=820., n=60, swirl=0.9):
    """Renvoie une liste de sections : chacune = boucle (x,y,z) fermée, DOS PLAT (z=0).
    depth_mm : profondeur (mm) par lamelle, déjà extraite/échantillonnée (n valeurs)."""
    W = len(top)
    valid = ~np.isnan(top)
    x0, x1 = np.where(valid)[0][[0, -1]]
    px2mm = length / (x1 - x0)                 # échelle uniforme
    xs_px = np.linspace(x0, x1, n)
    topm = smooth(top, 6); botm = smooth(bot, 6)
    ny = 28
    H = np.interp(xs_px, np.arange(W), botm) - np.interp(xs_px, np.arange(W), topm)
    # taille = position du min de hauteur dans le tiers central
    c0, c1 = int(0.30 * n), int(0.70 * n)
    waist = c0 + int(np.argmin(H[c0:c1]))
    sw = max(0.08 * n, 3.)
    sections = []
    for i, xpx in enumerate(xs_px):
        x = (xpx - x0) * px2mm
        t_top = np.interp(xpx, np.arange(W), topm)
        b_bot = np.interp(xpx, np.arange(W), botm)
        # image y-bas -> modèle y-haut
        ytop = (botm[valid].max() - t_top) * px2mm   # haut du modèle
        ybot = (botm[valid].max() - b_bot) * px2mm
        cy = 0.5 * (ytop + ybot); hy = 0.5 * (ytop - ybot)
        D = depth_mm[i]                                       # profondeur (vue de dessus)
        s = np.tanh((i - waist) / sw) * swirl                 # -1..1, bascule à la taille
        ys = np.linspace(ybot, ytop, ny)
        # bombé asymétrique : le SOMMET du galbe migre en Y -> effet vortex,
        # mais le DOS reste plat (z=0 sur toute la hauteur).
        yc = cy + 0.45 * s * hy
        tnorm = (ys - yc) / max(hy, 1e-6)
        front_z = D * np.sqrt(np.clip(1 - tnorm ** 2, 0, 1))
        # boucle fermée : dos plat (z=0) montant, puis face galbée descendante
        yy = np.concatenate([ys, ys[::-1]])
        zz = np.concatenate([np.zeros(ny), front_z[::-1]])
        loop = np.column_stack([np.full(yy.size, x), yy, zz])
        sections.append(loop)
    return sections, px2mm


# ---------- sorties ----------
def render_lamellae(sections, png, gap_frac=0.35, elev=18, azim=-72):
    pitch = sections[1][0, 0] - sections[0][0, 0]
    t = pitch * (1 - gap_frac)
    fig = plt.figure(figsize=(13, 5)); ax = fig.add_subplot(111, projection='3d')
    order = sorted(range(len(sections)), key=lambda i: -sections[i][0, 0])  # arrière->avant approx
    polys = [];
    for s in sections:
        polys.append(s[:, [0, 1, 2]])
    pc = Poly3DCollection([s for s in polys], facecolor=(0.86, 0.84, 0.80),
                          edgecolor=(0.35, 0.34, 0.32), linewidths=0.3)
    ax.add_collection3d(pc)
    allp = np.vstack(sections)
    ax.set_xlim(allp[:, 0].min(), allp[:, 0].max())
    ax.set_ylim(allp[:, 1].min(), allp[:, 1].max())
    ax.set_zlim(0, allp[:, 2].max() * 3)
    ax.set_box_aspect((allp[:, 0].ptp(), allp[:, 1].ptp(), allp[:, 2].ptp() + 1))
    ax.view_init(elev=elev, azim=azim); ax.set_axis_off()
    fig.savefig(png, dpi=100, bbox_inches='tight'); print("  ->", png)


def write_stl(sections, path):
    """Loft des sections (boucles de même longueur) + 2 capots -> STL binaire."""
    tris = []
    for a, b in zip(sections[:-1], sections[1:]):
        m = len(a)
        for k in range(m):
            k2 = (k + 1) % m
            tris.append((a[k], a[k2], b[k2]))
            tris.append((a[k], b[k2], b[k]))
    for cap, flip in ((sections[0], False), (sections[-1], True)):
        c = cap.mean(0)
        m = len(cap)
        for k in range(m):
            k2 = (k + 1) % m
            tri = (c, cap[k], cap[k2]) if not flip else (c, cap[k2], cap[k])
            tris.append(tri)
    with open(path, 'wb') as f:
        f.write(b'\0' * 80); f.write(struct.pack('<I', len(tris)))
        for v0, v1, v2 in tris:
            n = np.cross(v1 - v0, v2 - v0); ln = np.linalg.norm(n)
            n = n / ln if ln else np.zeros(3)
            f.write(struct.pack('<3f', *n))
            for v in (v0, v1, v2): f.write(struct.pack('<3f', *v))
            f.write(b'\0\0')
    print(f"  -> {path}  ({len(tris)} tris)")


def depth_from_top(face_top, face_bot, top_path, n, length=820.,
                   beta=0.45, depth_max=170., flip=False):
    """Extrait depth(x) de la vue de dessus oblique.
    L_oblique ~= a*hauteur + b*profondeur ; la hauteur est connue (face) -> on
    la soustrait pour isoler la profondeur. Renvoie n valeurs en mm + diagnostics."""
    fg = silhouette_top(top_path)
    L = thickness(fg)                                  # épaisseur apparente (px)
    Ln = resample_norm(L, n)
    if flip:
        Ln = Ln[::-1]
    # hauteur de face, sur la meme grille normalisee
    Hn = resample_norm(face_bot - face_top, n)
    Ln /= max(Ln.max(), 1e-6); Hn /= max(Hn.max(), 1e-6)
    d = np.clip(Ln - beta * Hn, 0, None)               # isole le bombe
    d = smooth(d, max(n * 0.03, 1.))
    d /= max(d.max(), 1e-6)
    return d * depth_max, fg, Ln, Hn


if __name__ == '__main__':
    face = sys.argv[1] if len(sys.argv) > 1 else '/home/julfab/Images/tornade.png'
    top_view = sys.argv[2] if len(sys.argv) > 2 else '/home/julfab/Images/tornade2.png'
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    fg = silhouette_face(face)
    top, bot = extract_topbot(fg)
    depth_mm, fgtop, Ln, Hn = depth_from_top(top, bot, top_view, n)
    secs, px2mm = build_sections(top, bot, depth_mm, n=n)
    allp = np.vstack(secs)
    dims = allp.max(0) - allp.min(0)
    print(f"sections={len(secs)}  dims X*Y*Z = {dims[0]:.0f} x {dims[1]:.0f} x {dims[2]:.0f} mm")
    print(f"depth(x) mm: min={depth_mm.min():.0f} max={depth_mm.max():.0f} "
          f"mean={depth_mm.mean():.0f}")
    # diagnostic : profil de profondeur vs vue de dessus
    fig, ax = plt.subplots(2, 1, figsize=(10, 6))
    ax[0].imshow(np.asarray(Image.open(top_view)).mean(2), cmap='gray')
    ax[0].contour(fgtop, [0.5], colors='r', linewidths=1); ax[0].set_title('dessus: silhouette')
    xs = np.linspace(0, 1, n)
    ax[1].plot(xs, Ln, label='épaisseur oblique (norm.)')
    ax[1].plot(xs, Hn, label='hauteur face (norm.)')
    ax[1].plot(xs, depth_mm / depth_mm.max(), 'k', lw=2, label='depth(x) isolée (norm.)')
    ax[1].legend(); ax[1].set_title('extraction profondeur'); ax[1].set_xlabel('x normalisé')
    fig.tight_layout(); fig.savefig('/tmp/depth_extract.png', dpi=90); print("  -> /tmp/depth_extract.png")
    render_lamellae(secs, '/tmp/hull_preview.png')
    write_stl(secs, '/tmp/tornade_hull.stl')
