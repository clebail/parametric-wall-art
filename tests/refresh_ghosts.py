#!/usr/bin/env python3
"""Rafraîchit les calques ghost (onion-skin) des SVG de lamelles.

Workflow « SVG par lamelle » (PLAN.md) : après que tu as édité à la main les
calques `lamelle-courante`, ce script reconstruit dans CHAQUE fichier les ghosts
`lamelle-precedente` (= contour courant à jour du fichier n-1) et
`lamelle-suivante` (= contour courant à jour du fichier n+1), pour que les
voisines affichées reflètent tes vraies éditions et pas le hull de départ.

Garanties :
  - le calque `lamelle-courante` n'est JAMAIS modifié (tes éditions sont intactes) ;
  - seuls les deux calques ghost sont remplacés (recréés verrouillés) ;
  - tout le reste du fichier (namedview Inkscape, defs, transforms) est préservé.

Édition sur place. Sauvegarde .bak du dossier conseillée avant (ou git).
Dépend uniquement de la lib standard (xml.etree).

Usage : python3 tests/refresh_ghosts.py [DIR]
        défaut DIR = tests/lamelles
"""
import sys, os, glob, copy
import xml.etree.ElementTree as ET

SVG  = 'http://www.w3.org/2000/svg'
INK  = 'http://www.inkscape.org/namespaces/inkscape'
SODI = 'http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd'

# styles ghost (identiques au générateur stl2lamelles_svg.py)
GHOST_P = 'fill:none;stroke:#e6a0a0;stroke-width:0.6;stroke-dasharray:2,2'
GHOST_N = 'fill:none;stroke:#a0b8e6;stroke-width:0.6;stroke-dasharray:2,2'

CUR_LABEL  = 'lamelle-courante'
PREV_LABEL = 'lamelle-precedente'
NEXT_LABEL = 'lamelle-suivante'
DRAWABLE   = {'path', 'polygon', 'polyline', 'rect', 'circle', 'ellipse'}

for pfx, uri in (('', SVG), ('inkscape', INK), ('sodipodi', SODI),
                 ('xlink', 'http://www.w3.org/1999/xlink'),
                 ('rdf', 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'),
                 ('cc', 'http://creativecommons.org/ns#'),
                 ('dc', 'http://purl.org/dc/elements/1.1/')):
    ET.register_namespace(pfx, uri)


def child_layer(root, label):
    """Calque (direct child de <svg>) portant inkscape:label == label."""
    for g in root.findall(f'{{{SVG}}}g'):
        if g.get(f'{{{INK}}}label') == label:
            return g
    return None


def make_ghost(cur_layer, label, style):
    """Copie verrouillée et restylée du calque courant d'une voisine.

    Copie l'arbre complet (préserve transforms/sous-groupes éventuels d'Inkscape),
    retire les lignes-repère (dos plat Z=0), force le style ghost, purge les id
    pour éviter les collisions dans le fichier cible."""
    g = copy.deepcopy(cur_layer)
    g.set(f'{{{INK}}}label', label)
    g.set('id', label)
    g.set(f'{{{SODI}}}insensitive', 'true')
    g.set('style', 'display:inline')
    for ln in g.findall(f'{{{SVG}}}line'):
        g.remove(ln)
    for el in g.iter():
        tag = el.tag.split('}')[-1]
        if el is not g and 'id' in el.attrib:
            del el.attrib['id']
        if tag in DRAWABLE:
            el.set('style', style)
            for k in ('fill', 'stroke', 'stroke-width', 'stroke-dasharray', 'fill-opacity'):
                el.attrib.pop(k, None)
    return g


def refresh(dir_):
    files = sorted(glob.glob(os.path.join(dir_, 'lamelle_*.svg')))
    if not files:
        sys.exit(f'aucun lamelle_*.svg dans {dir_}')
    n = len(files)
    trees = [ET.parse(f) for f in files]
    roots = [t.getroot() for t in trees]
    currents = [child_layer(r, CUR_LABEL) for r in roots]

    missing = [files[i] for i, c in enumerate(currents) if c is None]
    if missing:
        sys.exit('calque "%s" introuvable dans : %s' % (CUR_LABEL, ', '.join(missing)))

    for i, root in enumerate(roots):
        # retire les anciens ghosts
        for g in list(root.findall(f'{{{SVG}}}g')):
            if g.get(f'{{{INK}}}label') in (PREV_LABEL, NEXT_LABEL):
                root.remove(g)
        # insère les nouveaux ghosts juste AVANT le calque courant (donc dessous)
        idx = list(root).index(currents[i])
        ghosts = []
        if i > 0:
            ghosts.append(make_ghost(currents[i - 1], PREV_LABEL, GHOST_P))
        if i < n - 1:
            ghosts.append(make_ghost(currents[i + 1], NEXT_LABEL, GHOST_N))
        for off, g in enumerate(ghosts):
            root.insert(idx + off, g)

    for f, t in zip(files, trees):
        t.write(f, encoding='UTF-8', xml_declaration=True)
    print(f'{n} SVG rafraîchis dans {dir_} : ghosts recalés sur les contours courants à jour.')


if __name__ == '__main__':
    refresh(sys.argv[1] if len(sys.argv) > 1 else 'tests/lamelles')
