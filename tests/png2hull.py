#!/usr/bin/env python3
"""Photo de FACE (PNG) -> solide-maître dos plat (STL), sans vue de dessus.

Successeur de views2hull.py quand on n'a que la vue de face :
  1. Silhouette : seuil de luminance (sculpture sombre sur mur clair ; l'ombre
     portée, grise, est exclue), closing horizontal pour boucher les vides entre
     lamelles, plus grande CC, fill, lissage du contour.
  2. INFLATION EN TUBES : d = distance au bord, R = rayon local du bras (valeur
     de d sur l'axe médian le plus proche), z = k·√(2·R·d − d²) — chaque bras
     devient un tube de section ~circulaire qui suit sa propre direction (la
     taille diagonale reste fine, contrairement au calcul colonne par colonne).
  3. Sections (Y,Z) le long de X tirées de cette carte de hauteur ; dos plat z=0.
  4. Loft + capots -> STL binaire.

Repère projet : X = axe de coupe (longueur), Y = hauteur, Z = profondeur.

Usage : python3 png2hull.py photo.png out.stl [--length 820] [--n 160]
        [--k 0.9] [--max-depth 0] [--preview apercu.png]
Dépendances : numpy, scipy, PIL (+ matplotlib pour --preview).
"""
import argparse
import struct
import numpy as np
from PIL import Image
from scipy import ndimage as ndi


def otsu(v):
    h, e = np.histogram(v, 256)
    c = (e[:-1] + e[1:]) / 2
    w0 = np.cumsum(h); w1 = w0[-1] - w0
    m0 = np.cumsum(h * c) / np.maximum(w0, 1)
    m1 = (np.sum(h * c) - np.cumsum(h * c)) / np.maximum(w1, 1)
    return c[np.argmax(w0 * w1 * (m0 - m1) ** 2)]


def silhouette(path, thr=0.4, close_w=25, soften=14.):
    """Masque de la sculpture. thr = fraction du seuil d'Otsu : l'ombre portée
    sur le mur est grise (~80-100), la sculpture quasi noire."""
    g = np.asarray(Image.open(path).convert('RGB')).astype(float).mean(2)
    g = ndi.gaussian_filter(g, 1.5)
    fg = g < thr * otsu(g)
    # closing horizontal : les lamelles sont ~verticales, les vides aussi
    fg = ndi.binary_closing(fg, structure=np.ones((3, close_w)))
    fg = ndi.binary_closing(fg, iterations=3)
    lbl, n = ndi.label(fg)
    if n:
        sizes = ndi.sum(np.ones_like(lbl), lbl, range(1, n + 1))
        fg = lbl == (np.argmax(sizes) + 1)
    fg = ndi.binary_fill_holes(fg)
    # contour lissé (efface l'escalier des bouts de lamelles)
    return ndi.gaussian_filter(fg.astype(float), soften) > 0.5


def masked_blur(a, mask, s):
    num = ndi.gaussian_filter(np.where(mask, a, 0.), s)
    den = ndi.gaussian_filter(mask.astype(float), s)
    return np.where(mask, num / np.maximum(den, 1e-6), 0.)


def inflate(fg, k=0.9, r_blur=12.):
    """Carte de hauteur z (px) : tubes de rayon local R le long de l'axe médian."""
    d = ndi.distance_transform_edt(fg)
    ridge = fg & (d >= ndi.maximum_filter(d, 5) - 0.75) & (d > 3)
    _, (iy, ix) = ndi.distance_transform_edt(~ridge, return_indices=True)
    R = masked_blur(d[iy, ix], fg, r_blur)
    R = np.maximum(R, d)                 # R ne peut pas être < distance au bord
    z = k * np.sqrt(np.clip(2 * R * d - d * d, 0, None))
    return masked_blur(z, fg, 3.) * fg


def build_sections(fg, z, *, length, n, ny=64, max_depth=0.):
    cols = np.where(fg.any(0))[0]
    x0, x1 = cols[0], cols[-1]
    px2mm = length / (x1 - x0)
    ymax = np.where(fg.any(1))[0][-1]
    zmm = z * px2mm
    if max_depth > 0:
        zmm *= max_depth / zmm.max()
    sections = []
    # centres de n tranches égales : évite les pointes dégénérées
    for xp in x0 + (np.arange(n) + 0.5) * (x1 - x0) / n:
        c = int(round(xp))
        ys_on = np.where(fg[:, c])[0]
        yp = np.linspace(ys_on[0], ys_on[-1], ny)
        fz = np.interp(yp, np.arange(fg.shape[0]), zmm[:, c])
        fz[0] = fz[-1] = 0.
        ys = (ymax - yp) * px2mm                       # image y-bas -> modèle y-haut
        ys, fz = ys[::-1], fz[::-1]                    # bas -> haut
        # dos (ny pts, bas -> haut) puis face avant sans ses 2 bouts (déjà sur le dos)
        yy = np.concatenate([ys, ys[-2:0:-1]])
        zz = np.concatenate([np.zeros(ny), fz[-2:0:-1]])
        sections.append(np.column_stack([np.full(yy.size, (xp - x0) * px2mm), yy, zz]))
    return sections, px2mm


def write_stl(sections, path):
    """Loft des sections (boucles de même longueur) + 2 capots -> STL binaire."""
    tris = []
    for a, b in zip(sections[:-1], sections[1:]):
        m = len(a)
        for j in range(m):
            j2 = (j + 1) % m
            tris.append((a[j], a[j2], b[j2]))
            tris.append((a[j], b[j2], b[j]))
    for cap, flip in ((sections[0], False), (sections[-1], True)):
        c = cap.mean(0)
        for j in range(len(cap)):
            j2 = (j + 1) % len(cap)
            tris.append((c, cap[j2], cap[j]) if flip else (c, cap[j], cap[j2]))
    with open(path, 'wb') as f:
        f.write(b'\0' * 80); f.write(struct.pack('<I', len(tris)))
        for v0, v1, v2 in tris:
            nrm = np.cross(v1 - v0, v2 - v0); ln = np.linalg.norm(nrm)
            f.write(struct.pack('<3f', *(nrm / ln if ln else np.zeros(3))))
            for v in (v0, v1, v2):
                f.write(struct.pack('<3f', *v))
            f.write(b'\0\0')
    return len(tris)


def render_slats(sections, n_slats=60, fill=0.5, azim=22., elev=-10., px=1.0):
    """Rendu « photo » des lamelles (nuage de points + z-buffer) : chaque lamelle
    = la section au centre de sa tranche, extrudée sur fill·pas. Vue tournée de
    azim° autour de Y (on voit les flancs gauches) et elev° vers le bas."""
    L = max(s[0, 0] for s in sections)
    pitch = L / n_slats
    xs_sec = np.array([s[0, 0] for s in sections])
    P, N = [], []
    for i in range(n_slats):
        xa = i * pitch; xb = xa + fill * pitch
        s = sections[int(np.argmin(abs(xs_sec - (xa + xb) / 2)))]
        ny = (len(s) + 2) // 2
        ys = s[:ny, 1]
        fz = np.concatenate([[0.], s[ny:, 2][::-1], [0.]])
        yf = np.arange(ys[0], ys[-1], 0.5)
        zf = np.interp(yf, ys, fz)
        dz = np.gradient(zf, 0.5)
        for x in np.arange(xa, xb, 0.5):               # face avant
            P.append(np.column_stack([np.full_like(yf, x), yf, zf]))
            N.append(np.column_stack([np.zeros_like(yf), -dz, np.ones_like(yf)]))
        for y, zt in zip(yf, zf):                          # flanc gauche (x = xa)
            zz = np.arange(0, zt, 0.4)
            P.append(np.column_stack([np.full_like(zz, xa), np.full_like(zz, y), zz]))
            N.append(np.tile([-1., 0., 0.], (zz.size, 1)))
    P = np.vstack(P); N = np.vstack(N)
    N /= np.linalg.norm(N, axis=1, keepdims=True)
    a, e = np.radians(azim), np.radians(elev)
    Ry = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(e), -np.sin(e)], [0, np.sin(e), np.cos(e)]])
    Rm = Rx @ Ry
    Q = P @ Rm.T
    light = np.array([-0.5, 0.6, 0.65]); light /= np.linalg.norm(light)
    view = np.array([0, 0, 1.]) @ Rm                   # direction caméra (repère objet)
    h = light + view; h /= np.linalg.norm(h)
    col = np.clip(0.04 + 0.3 * np.clip(N @ light, 0, 1)
                  + 0.6 * np.clip(N @ h, 0, 1) ** 12, 0, 1)
    u = ((Q[:, 0] - Q[:, 0].min()) / px).astype(int)
    v = ((Q[:, 1].max() - Q[:, 1]) / px).astype(int)
    img = np.full((v.max() + 1, u.max() + 1), 0.85)
    order = np.argsort(Q[:, 2])                        # du fond vers l'avant
    img[v[order], u[order]] = col[order]
    return img


def preview(path_png, fg, z, sections, out):
    import matplotlib; matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axs = plt.subplots(4, 1, figsize=(16, 19))
    axs[0].imshow(np.asarray(Image.open(path_png).convert('L')), cmap='gray')
    axs[0].contour(fg, [0.5], colors='r', linewidths=1)
    axs[0].set_title('photo + silhouette extraite')
    zm = np.where(fg, z, np.nan)
    im = axs[1].imshow(zm, cmap='viridis')
    axs[1].contour(zm, 12, colors='w', linewidths=0.5)
    axs[1].set_title('profondeur (relative)')
    fig.colorbar(im, ax=axs[1], fraction=0.015)
    axs[2].imshow(render_slats(sections), cmap='gray', vmin=0, vmax=1)
    axs[2].set_title('rendu lamelles (vue 3/4 gauche)')
    axs[3].imshow(render_slats(sections, azim=0., elev=-65.), cmap='gray', vmin=0, vmax=1)
    axs[3].set_title('rendu lamelles (vue plongeante, depuis le haut)')
    for ax in axs:
        ax.set_axis_off()
    fig.tight_layout(); fig.savefig(out, dpi=80)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('photo'); ap.add_argument('out')
    ap.add_argument('--length', type=float, default=820.)
    ap.add_argument('--n', type=int, default=160, help='nb de sections du loft')
    ap.add_argument('--k', type=float, default=0.9, help='aplatissement des tubes (1 = rond)')
    ap.add_argument('--thr', type=float, default=0.4, help='seuil silhouette (× Otsu)')
    ap.add_argument('--max-depth', type=float, default=0., help='profondeur max imposée (mm)')
    ap.add_argument('--preview')
    a = ap.parse_args()
    fg = silhouette(a.photo, a.thr)
    z = inflate(fg, a.k)
    secs, px2mm = build_sections(fg, z, length=a.length, n=a.n, max_depth=a.max_depth)
    ntri = write_stl(secs, a.out)
    dims = np.ptp(np.vstack(secs), 0)
    print(f"{a.out}: {ntri} tris, {dims[0]:.0f} x {dims[1]:.0f} x {dims[2]:.0f} mm "
          f"({px2mm:.3f} mm/px)")
    if a.preview:
        preview(a.photo, fg, z, secs, a.preview)
        print("aperçu ->", a.preview)
