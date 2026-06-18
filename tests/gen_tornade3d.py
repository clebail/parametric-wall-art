#!/usr/bin/env python3
"""Genere un modele 3D de "tornade design" (sculpture murale parametrique).

Reproduit la piece de reference : forme horizontale allongee faite de deux lobes
bombes relies par une taille pincee et TORSADEE au centre (effet vortex). Le
volume est un loft : une section transversale (superellipse) balayee le long de
l'axe X, dont on fait varier le long du parcours l'echelle, la torsion et
l'ondulation de la colonne. Maillage plein et etanche (rings + 2 capots).

Repere (coherent avec le pipeline parametric-wall-art) :
  X = grande dimension horizontale  -> AXE DE COUPE (les lamelles sont en Y/Z)
  Z = hauteur (verticale sur le mur)
  Y = profondeur (hors du mur)

Usage (headless, sans ouvrir Blender) :
  blender --background --python tests/gen_tornade3d.py -- tests/tornade3d.stl
  blender --background --python tests/gen_tornade3d.py -- tests/tornade3d.stl --preview /tmp/t.png

Tous les parametres de forme sont dans PARAMS (modifiables aussi en --cle valeur).
"""
import sys
import numpy as np
import bpy
import bmesh
import mathutils


# --- Parametres de forme (unites = mm) -------------------------------------
# La silhouette de face = colonne cz(t) +/- demi-hauteur hz(t). hz est la somme
# de DEUX gaussiennes (lobe gauche + lobe droit) : le creux entre les deux donne
# naturellement la TAILLE pincee. Bords haut/bas modules separement (volute +
# goutte). Section = superellipse plus plate (profondeur < hauteur), torsadee.
PARAMS = {
    "length":      820.0,   # longueur totale (X)
    "stations":    260,     # nb de sections le long de X
    "segments":    80,      # nb de points par section

    "size":        140.0,   # demi-hauteur max (Z)
    "depth_ratio": 0.80,    # demi-profondeur / demi-hauteur (epaisseur du relief)
    "belly":       55.0,    # debord : pousse la section vers l'avant aux lobes
                            #          -> partie la plus large devant le plan (surplomb)
    "superell":    2.1,     # exposant superellipse (2=ellipse)

    "lobeL_pos":   0.26,    # position du lobe gauche (t)
    "lobeL_h":     1.05,    # hauteur du lobe gauche
    "lobeL_w":     0.155,   # largeur du lobe gauche
    "lobeR_pos":   0.72,    # position du lobe droit
    "lobeR_h":     0.82,    # hauteur du lobe droit (plus petit)
    "lobeR_w":     0.165,   # largeur du lobe droit
    "floor":       0.10,    # taille mini (taille pincee jamais nulle)
    "round":       0.60,    # <1 = sommets de lobes plus ronds (evite les pics)

    "twist":       1.0,     # torsion globale (rad)
    "swirl":       2.4,     # torsion locale (vortex) a la taille (rad)
    "swirl_width": 0.06,    # etalement de la torsion locale (t)
    "tear":        0.28,    # asymetrie "goutte/virgule" de la section (0..1)

    "drop":        70.0,    # goutte qui pend sous le lobe gauche (mm)
    "drop_pos":    0.34,    # position de la goutte (t)
    "drop_w":      0.085,   # largeur de la goutte (t)
    "waist_rise":  34.0,    # remontee de la colonne a la taille (mm)
    "tilt":        18.0,    # inclinaison generale haut/bas (mm)
}


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def compute_profile(P):
    """Calcule les arrays longitudinaux + la section unitaire. Tout le 'design'
    de la tornade est ici. Retourne un dict reutilise par solide ET lamelles."""
    M, N = P["stations"], P["segments"]
    t = np.linspace(0.0, 1.0, M)
    x = P["length"] * (t - 0.5)

    def gauss(c, w):
        return np.exp(-((t - c) / w) ** 2)

    # Profil de lobes : 2 gaussiennes -> le creux entre les deux = taille pincee.
    # ^plat : aplatit le sommet des lobes (sinon pics pointus facon noeud papillon).
    lobes = (P["lobeL_h"] * gauss(P["lobeL_pos"], P["lobeL_w"])
             + P["lobeR_h"] * gauss(P["lobeR_pos"], P["lobeR_w"]))
    env = np.clip(lobes + P["floor"] * np.sin(np.pi * t), 0.0, None)
    env = (env / max(env.max(), 1e-6)) ** P["round"]   # sommets plus ronds

    hz = P["size"] * env                       # demi-hauteur (Z)
    hy = P["size"] * P["depth_ratio"] * env    # demi-profondeur (Y)

    # Torsion : composante globale + bouffee locale a la taille (le vortex).
    theta = P["twist"] * (t - 0.5) + P["swirl"] * np.tanh((t - 0.5) / P["swirl_width"])

    # Colonne (centre) : inclinaison generale + remontee a la taille - goutte sous
    # le lobe gauche ; leger gauchissement en profondeur.
    cz = (P["tilt"] * (t - 0.5)
          + P["waist_rise"] * gauss(0.5, 0.10)
          - P["drop"] * gauss(P["drop_pos"], P["drop_w"]))
    # Centre de profondeur : pousse vers l'avant aux lobes -> surplomb (la partie
    # la plus large de la section depasse le plan du dos apres la coupe a Z=0).
    cd = P["belly"] * env

    # Section unitaire (superellipse + asymetrie goutte), plan (depth=Y, height=Z)
    a = np.linspace(0.0, 2.0 * np.pi, N, endpoint=False)
    ca, sa = np.cos(a), np.sin(a)
    p = 2.0 / P["superell"]
    uy = np.sign(ca) * np.abs(ca) ** p
    uz = np.sign(sa) * np.abs(sa) ** p
    rmod = 1.0 + P["tear"] * np.cos(a)
    uy *= rmod
    uz *= rmod

    return dict(M=M, N=N, t=t, x=x, hz=hz, hy=hy, theta=theta, cz=cz, cd=cd,
                uy=uy, uz=uz)


def section_ring(pr, i):
    """Section a la station i -> (hauteur Y, profondeur Z), tournee par la torsion.
    Y = hauteur (silhouette de face), Z = profondeur (hors du mur). Le dos plat est
    obtenu en aval en coupant a Z=0 ; couper Z ne touche pas la hauteur Y."""
    ct, st = np.cos(pr["theta"][i]), np.sin(pr["theta"][i])
    H = pr["hz"][i] * pr["uz"]   # hauteur
    D = pr["hy"][i] * pr["uy"]   # profondeur
    Hr = H * ct - D * st
    Dr = H * st + D * ct
    return pr["cz"][i] + Hr, pr["cd"][i] + Dr


def build_solid(P):
    """Volume plein etanche (loft des sections le long de X)."""
    pr = compute_profile(P)
    M, N, x = pr["M"], pr["N"], pr["x"]

    verts = [(x[0], pr["cz"][0], 0.0)]   # apex debut (Y=hauteur, Z=profondeur)
    apex_start = 0
    for i in range(1, M - 1):
        Yh, Zd = section_ring(pr, i)
        for j in range(N):
            verts.append((x[i], Yh[j], Zd[j]))
    apex_end = len(verts)
    verts.append((x[-1], pr["cz"][-1], 0.0))

    n_rings = M - 2
    ring = lambda r: 1 + r * N
    faces = []
    for j in range(N):
        faces.append((apex_start, ring(0) + (j + 1) % N, ring(0) + j))
    for r in range(n_rings - 1):
        for j in range(N):
            j1 = (j + 1) % N
            faces.append((ring(r) + j, ring(r) + j1,
                          ring(r + 1) + j1, ring(r + 1) + j))
    last = n_rings - 1
    for j in range(N):
        faces.append((apex_end, ring(last) + j, ring(last) + (j + 1) % N))
    return verts, faces


def build_lamellae(P, n_planks, gap_frac):
    """Apercu du PRODUIT FINI : N planches plates espacees (comme le tranchage de
    l'appli). Chaque planche = section au centre du slab, extrudee en X."""
    pr = compute_profile(P)
    M, N, x = pr["M"], pr["N"], pr["x"]
    L = P["length"]
    pitch = L / n_planks
    half = pitch * (1.0 - gap_frac) / 2.0

    verts, faces = [], []
    for p in range(n_planks):
        tc = (p + 0.5) / n_planks
        i = int(round(tc * (M - 1)))
        i = min(max(i, 1), M - 2)
        Yh, Zd = section_ring(pr, i)
        xc = L * (tc - 0.5)
        base = len(verts)
        # anneau avant (x+half) puis arriere (x-half)
        for j in range(N):
            verts.append((xc + half, Yh[j], Zd[j]))
        for j in range(N):
            verts.append((xc - half, Yh[j], Zd[j]))
        cf = len(verts); verts.append((xc + half, pr["cz"][i], 0.0))
        cb = len(verts); verts.append((xc - half, pr["cz"][i], 0.0))
        for j in range(N):
            j1 = (j + 1) % N
            faces.append((cf, base + j, base + j1))               # capot avant
            faces.append((cb, base + N + j1, base + N + j))       # capot arriere
            faces.append((base + j, base + N + j, base + N + j1, base + j1))  # paroi
    return verts, faces


def make_object(verts, faces, flat_back=True, name="Tornade3D"):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    if flat_back:
        # Tronquage : coupe au plan X,Y (Z=0), on garde l'avant (Z>=0) -> dos plat
        # cote mur. Les bords ouverts crees par la coupe sont reboucles a plat.
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-4,
                               plane_co=(0.0, 0.0, 0.0), plane_no=(0.0, 0.0, 1.0),
                               clear_inner=True, clear_outer=False)
        bmesh.ops.holes_fill(bm, edges=bm.edges, sides=0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()

    # Place dans l'octant positif (min a l'origine), convention du projet (svg2stl)
    me.update()
    if me.vertices:
        mn = [min(v.co[k] for v in me.vertices) for k in range(3)]
        for v in me.vertices:
            v.co.x -= mn[0]; v.co.y -= mn[1]; v.co.z -= mn[2]
        me.update()
    return obj


def export_stl(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    # Blender 3.4 : addon io_mesh_stl (active si besoin)
    if not hasattr(bpy.ops.export_mesh, "stl"):
        bpy.ops.preferences.addon_enable(module="io_mesh_stl")
    bpy.ops.export_mesh.stl(filepath=path, use_selection=True)


def render_preview(obj, path):
    """Rendu CPU (Cycles) vu de face, comme la video. Best-effort."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False  # ce build Blender n'a pas OpenImageDenoiser
    scene.render.resolution_x = 1000
    scene.render.resolution_y = 560
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'  # evite l'ecrasement filmique
    scene.world = bpy.data.worlds.new("W")
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.18, 0.18, 0.20, 1.0)  # fond gris
    bg.inputs[1].default_value = 0.6                      # ambiance (visibilite garantie)

    # centre + etendue de l'objet pour viser/cadrer la camera
    bb = np.array([obj.matrix_world @ v.co for v in obj.data.vertices])
    ctr = mathutils.Vector(bb.mean(axis=0))
    span = float((bb.max(axis=0) - bb.min(axis=0)).max())

    # vue 3/4 de face : la pièce bombe vers +Z (avant), dos plat a Z=0 (mur)
    cam_data = bpy.data.cameras.new("Cam")
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    # centree en X (compo horizontale), legerement en contre-plongee, en avant (+Z)
    cam.location = (ctr.x, ctr.y - 0.45 * span, ctr.z + 1.45 * span)
    d = ctr - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam_data.lens = 55.0
    cam_data.clip_start = 1.0
    cam_data.clip_end = 10000.0   # sinon objet hors du far-clip (defaut 1000)
    scene.camera = cam

    sun_data = bpy.data.lights.new("Sun", type='SUN')
    sun_data.energy = 4.0
    sun = bpy.data.objects.new("Sun", sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (np.radians(35), np.radians(-25), np.radians(0))  # lumiere avant-haut

    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("out", help="chemin STL de sortie")
    ap.add_argument("--preview", default="", help="chemin PNG de rendu (optionnel)")
    ap.add_argument("--lamellae", type=int, default=0,
                    help="apercu PRODUIT FINI : genere N planches espacees au lieu du solide")
    ap.add_argument("--gap", type=float, default=0.35, help="jeu entre planches (fraction du pas)")
    ap.add_argument("--full", action="store_true",
                    help="volume complet (pas de dos plat) au lieu du relief mural tronque")
    # overrides de PARAMS : --key value
    for k, v in PARAMS.items():
        ap.add_argument(f"--{k}", type=type(v), default=None)
    return ap.parse_args(argv)


def main():
    args = parse_args()
    P = dict(PARAMS)
    for k in PARAMS:
        val = getattr(args, k)
        if val is not None:
            P[k] = val

    bpy.ops.wm.read_factory_settings(use_empty=True)
    if args.lamellae > 0:
        verts, faces = build_lamellae(P, args.lamellae, args.gap)
    else:
        verts, faces = build_solid(P)
    obj = make_object(verts, faces, flat_back=not args.full)

    export_stl(obj, args.out)

    # rapport
    me = obj.data
    bb = np.array([v.co[:] for v in me.vertices])
    dim = bb.max(axis=0) - bb.min(axis=0)
    n_open = sum(1 for e in me.edges if len(
        [f for f in me.polygons if e.vertices[0] in f.vertices and e.vertices[1] in f.vertices]) == 1)
    print(f"OK -> {args.out}")
    print(f"  dimensions (X,Y,Z) : {dim[0]:.0f} x {dim[1]:.0f} x {dim[2]:.0f} mm")
    print(f"  sommets {len(me.vertices)}  faces {len(me.polygons)}")

    if args.preview:
        render_preview(obj, args.preview)
        print(f"  preview -> {args.preview}")


if __name__ == "__main__":
    main()
