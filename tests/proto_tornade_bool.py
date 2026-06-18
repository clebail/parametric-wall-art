#!/usr/bin/env python3
"""Prototype "tornade" par operations BOOLEENNES sur primitives, a partir de la
silhouette plate tests/tornade.stl.

Recette (cf. discussion) :
  1. MOULE  = silhouette importee, epaissie en profondeur (Z) -> cookie-cutter.
  2. RELIEF = union(silhouette fine + ellipsoides lobes + ellipsoide taille + goutte).
  3. RESULT = MOULE ∩ RELIEF      (retaille le relief au contour exact de la tornade)
  4. TORSION (Simple Deform Twist) autour de X, localisee a la taille = le VORTEX.
  5. DOS PLAT : on coupe au plan Z=0 (garde l'avant).
  6. Remesh voxel + smooth -> manifold propre.
Sortie : STL solide (le lamellage est gere par l'appli C++/Qt, pas ici) + apercu du solide.

Repere projet : X = longueur (axe de coupe), Y = hauteur, Z = profondeur (dos plat Z=0).

Usage :
  blender --background --python tests/proto_tornade_bool.py -- /tmp/torb.stl --preview /tmp/torb.png
Tous les PARAMS sont reglables en --cle valeur.
"""
import bpy, bmesh, sys, math, os
from mathutils import Vector

# Nom de l'objet source dans la scene (GUI). S'il existe, on part de lui ; sinon on importe le STL.
SOURCE_OBJ = "Tornade"


def find_stl():
    """Trouve tornade.stl (a cote du script), que Blender soit lance depuis n'importe ou."""
    cands = []
    try:    cands.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tornade.stl"))
    except NameError: pass                       # __file__ absent si script colle a la main
    cands += ["tests/tornade.stl",
              os.path.expanduser("~/dev/parametric-wall-art/tests/tornade.stl")]
    for c in cands:
        if os.path.exists(c): return c
    return cands[-1]

# Fractions de la longueur L (axe X) et hauteur Hy (axe Y), repere centre a l'origine.
PARAMS = {
    "depth_mask":  0.55,   # profondeur du moule (frac L) -> doit depasser les bombes
    # lobe gauche (gros) : assez GRAND en X/Y pour couvrir la silhouette -> on garde le contour
    "lL_x": -0.20, "lL_y": 0.06, "lL_sx": 0.46, "lL_sy": 0.74, "lL_sz": 0.17,
    # lobe droit (plus petit)
    "lR_x":  0.26, "lR_y": -0.02, "lR_sx": 0.42, "lR_sy": 0.62, "lR_sz": 0.14,
    # taille (garde un peu d'epaisseur au creux entre les deux lobes)
    "w_x":   0.04, "w_y": 0.0,  "w_sx": 0.16, "w_sy": 0.34, "w_sz": 0.11,
    # goutte sous le lobe gauche
    "d_x":  -0.20, "d_y": -0.28, "d_sx": 0.12, "d_sy": 0.22, "d_sz": 0.11,
    # base fine (garantit la silhouette complete, meme aux pointes)
    "base_depth": 0.06,    # profondeur de la silhouette fine (frac L)
    # vortex
    "twist_deg": 95.0,     # angle de torsion (deg) autour de X
    "twist_lo":  0.38,     # debut bande de torsion (0..1 le long de X)
    "twist_hi":  0.64,     # fin   bande de torsion
    # finition
    "voxel":     0.008,    # taille voxel remesh (frac L) ; 0 = pas de remesh
}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[0] if argv and not argv[0].startswith("--") else "/tmp/torb.stl"
    preview = None
    i = 1 if (argv and not argv[0].startswith("--")) else 0
    while i < len(argv):
        a = argv[i]
        if a == "--preview":   preview = argv[i + 1]; i += 2; continue
        if a.startswith("--") and a[2:] in PARAMS:
            PARAMS[a[2:]] = float(argv[i + 1]); i += 2; continue
        i += 1
    return out, preview


def clean(keep=None):
    # Vide la scene (cube par defaut + run precedent) SAUF l'objet 'keep' (la source). Garde l'UI,
    # le workspace et le script ouvert -> re-run propre en GUI. Via l'API data (pas d'operateur,
    # donc pas de souci de contexte selon la zone active).
    for o in list(bpy.data.objects):
        if o is keep:
            continue
        bpy.data.objects.remove(o, do_unlink=True)
    for blk in (bpy.data.meshes, bpy.data.cameras, bpy.data.lights):
        for d in list(blk):
            if d.users == 0:
                blk.remove(d)


def import_stl(path):
    try:    bpy.ops.wm.stl_import(filepath=path)
    except Exception:
        bpy.ops.preferences.addon_enable(module="io_mesh_stl")
        bpy.ops.import_mesh.stl(filepath=path)
    return bpy.context.selected_objects[0]


def apply_all(obj):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def boolean(target, cutter, op):
    m = target.modifiers.new("b", "BOOLEAN")
    m.operation = op; m.solver = "EXACT"; m.object = cutter
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def ellipsoid(name, cx, cy, cz, sx, sy, sz):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, location=(cx, cy, cz))
    o = bpy.context.active_object
    o.name = name; o.scale = (sx, sy, sz)
    apply_all(o)
    return o


def make_object(verts, faces, name):
    me = bpy.data.meshes.new(name); me.from_pydata(verts, [], faces); me.update()
    o = bpy.data.objects.new(name, me); bpy.context.collection.objects.link(o)
    return o


def bbox(o):
    cs = [o.matrix_world @ Vector(c) for c in o.bound_box]
    mn = Vector((min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs)))
    mx = Vector((max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs)))
    return mn, mx


def main():
    out, preview = parse_args()
    P = PARAMS

    # Source : si un objet "Tornade" existe deja dans la scene (GUI), on part de LUI (copie) ;
    # sinon (mode ligne de commande) on importe la silhouette tornade.stl.
    src = bpy.data.objects.get(SOURCE_OBJ)
    if src is not None:
        clean(keep=src)                              # vide la scene SAUF la source
        sil = src.copy(); sil.data = src.data.copy() # travaille sur une COPIE (original intact)
        bpy.context.collection.objects.link(sil)
        src.hide_viewport = True                     # cache l'original
    else:
        clean()
        sil = import_stl(find_stl())
    sil.name = "sil"
    # Oriente : plus grande dim -> X, plus petite (profondeur) -> Z.
    mn, mx = bbox(sil); d = mx - mn
    if d.y > d.x:                       # portrait -> tourne 90 autour de Z
        sil.rotation_euler = (0, 0, math.radians(90)); apply_all(sil)
    # centre a l'origine
    mn, mx = bbox(sil); ctr = (mn + mx) / 2
    sil.location = -ctr; apply_all(sil)
    mn, mx = bbox(sil); d = mx - mn
    L, Hy, D = d.x, d.y, d.z
    print("silhouette  L=%.1f Hy=%.1f D=%.1f" % (L, Hy, D))

    # 1) MOULE : silhouette epaissie en Z (cookie-cutter pleine profondeur)
    mask = sil.copy(); mask.data = sil.data.copy(); mask.name = "mask"
    bpy.context.collection.objects.link(mask)
    mask.scale = (1, 1, P["depth_mask"] * L / max(D, 1e-6)); apply_all(mask)

    # 2) RELIEF : base fine (silhouette) + primitives bombees
    relief = sil; relief.name = "relief"
    relief.scale = (1, 1, P["base_depth"] * L / max(D, 1e-6)); apply_all(relief)
    prims = [
        ellipsoid("lobeL", P["lL_x"]*L, P["lL_y"]*Hy, 0, P["lL_sx"]*L, P["lL_sy"]*Hy, P["lL_sz"]*L),
        ellipsoid("lobeR", P["lR_x"]*L, P["lR_y"]*Hy, 0, P["lR_sx"]*L, P["lR_sy"]*Hy, P["lR_sz"]*L),
        ellipsoid("waist", P["w_x"]*L,  P["w_y"]*Hy,  0, P["w_sx"]*L,  P["w_sy"]*Hy,  P["w_sz"]*L),
        ellipsoid("drop",  P["d_x"]*L,  P["d_y"]*Hy,  0, P["d_sx"]*L,  P["d_sy"]*Hy,  P["d_sz"]*L),
    ]
    for pr in prims:
        boolean(relief, pr, "UNION")

    # 3) RESULT = MOULE ∩ RELIEF (retaille au contour)
    boolean(relief, mask, "INTERSECT")
    res = relief; res.name = "Tornade"

    # 4) VORTEX : Simple Deform Twist autour de X, localise a la taille
    if abs(P["twist_deg"]) > 0.01:
        m = res.modifiers.new("tw", "SIMPLE_DEFORM")
        m.deform_method = "TWIST"; m.deform_axis = "X"
        m.angle = math.radians(P["twist_deg"])
        m.limits = (P["twist_lo"], P["twist_hi"])
        bpy.context.view_layer.objects.active = res
        bpy.ops.object.modifier_apply(modifier=m.name)

    # 5) DOS PLAT : coupe au plan Z=0, garde l'avant (Z>=0)
    me = res.data
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:]+bm.edges[:]+bm.faces[:], dist=1e-4,
                           plane_co=(0, 0, 0), plane_no=(0, 0, 1),
                           clear_inner=True, clear_outer=False)
    bmesh.ops.holes_fill(bm, edges=bm.edges, sides=0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free(); me.update()

    # 6) Remesh voxel + smooth
    if P["voxel"] > 0:
        m = res.modifiers.new("rm", "REMESH"); m.mode = "VOXEL"
        m.voxel_size = P["voxel"] * L
        bpy.context.view_layer.objects.active = res
        bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.object.shade_smooth()

    # place octant positif (convention projet)
    mn, mx = bbox(res);
    for v in res.data.vertices: v.co -= mn
    res.data.update()
    mn, mx = bbox(res); d = mx - mn
    print("RESULT  %.1f x %.1f x %.1f mm" % (d.x, d.y, d.z))

    export_stl(res, out)

    # apercu du SOLIDE (le lamellage est gere par l'appli C++/Qt, pas ici)
    if preview:
        render(res, preview)


def export_stl(o, path):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True); bpy.context.view_layer.objects.active = o
    try:    bpy.ops.wm.stl_export(filepath=path, export_selected_objects=True)
    except Exception: bpy.ops.export_mesh.stl(filepath=path, use_selection=True)
    print("SAVED", path)


def render(obj, path):
    import numpy as np
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.samples = 24; sc.cycles.use_denoising = False
    sc.render.resolution_x = 1000; sc.render.resolution_y = 560
    sc.view_settings.view_transform = "Standard"
    mn, mx = bbox(obj); ctr = (mn + mx) / 2; span = max((mx - mn))
    cam_d = bpy.data.cameras.new("C"); cam = bpy.data.objects.new("C", cam_d)
    sc.collection.objects.link(cam); cam_d.clip_end = 100000
    cam.location = (ctr.x, ctr.y - 0.55 * span, ctr.z + 1.35 * span)
    cam.rotation_euler = (ctr - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sd = bpy.data.lights.new("S", "SUN"); sd.energy = 4.0
    sun = bpy.data.objects.new("S", sd); sc.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(38), math.radians(-22), 0)
    sc.world = bpy.data.worlds.new("W"); sc.world.use_nodes = True
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.04, 0.04, 0.04, 1)
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("PREVIEW", path)


main()
