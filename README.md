# Parametric Wall Art

Application de bureau (Qt/C++/OpenGL) qui transforme un **modèle 3D STL** en **art mural en bois
multicouche** : le modèle est tranché en lamelles verticales parallèles, prévisualisé en 3D, puis
exporté en **plan de découpe vectoriel (SVG + DXF)** prêt pour une découpe laser, CNC… ou à la main.

> Style « portrait bois multicouche » : on empile des planches découpées, posées côte à côte, dont
> les silhouettes successives reconstituent le volume de l'objet.

---

## Fonctionnalités

- **Import STL** binaire et ASCII (loader maison, auto-détecté, sans dépendance externe).
- **Tranchage natif** du maillage le long d'un axe **X, Y ou Z** (intersection plan × triangles).
- **Prévisualisation 3D** temps réel des lamelles côte à côte (extrusion + texture bois, éclairage
  studio, rotation/zoom à la souris, luminosité réglable).
- **Réglages à chaud** : axe de coupe, nombre de tranches, échelle (mm/unité), épaisseur matériau,
  taille de feuille, marges, espacement. La vue et les compteurs se mettent à jour instantanément.
- **Export du plan de découpe** :
  - **SVG** (un fichier par feuille) — ouvrable dans un navigateur, Inkscape, LightBurn…
  - **DXF R12** (un fichier par feuille) — compatible LibreCAD et la plupart des logiciels laser/CNC.
  - **Nesting** automatique des pièces sur les feuilles + numérotation pour l'ordre d'assemblage.

---

## Comment ça marche

```
  STL ──▶ CMesh ──▶ CSlicer ──▶ contours 2D ──┬──▶ C3dView  (aperçu 3D des lamelles)
 (3D)    (loader)  (tranchage)                 └──▶ CCutPlan (nesting + SVG/DXF)
```

1. **Chargement** : le STL est lu en triangles, avec calcul de la boîte englobante.
2. **Tranchage** : pour chaque lamelle, un plan perpendiculaire à l'axe coupe le maillage au centre
   du « slab ». Les segments d'intersection sont cousus en **contours fermés** (silhouette + trous).
3. **Aperçu** : chaque contour est rempli (faces avant/arrière), extrudé de l'épaisseur de la
   lamelle et affiché côte à côte — l'enveloppe globale du modèle est préservée.
4. **Export** : les contours sont mis à l'échelle (mm), disposés sur des feuilles et écrits en
   SVG/DXF avec une étiquette d'ordre par pièce.

---

## Compilation

**Prérequis** : Qt 5 (testé en 5.15), un compilateur C++11 (g++ 13), et GLU (`libGLU`, fournie avec
la pile OpenGL).

```bash
qmake parametric-wall-art.pro
make
./parametric-wall-art
```

> Le `.pro` lie `-lGLU` (tesselation des faces de lamelles via `gluNewTess`).

---

## Utilisation

1. **Importer STL…** (bouton du panneau ou menu *Fichier*) — par ex. un des modèles de `tests/`.
2. Choisir l'**axe de coupe** (X/Y/Z) et le **nombre de tranches** ; la vue se met à jour.
3. Régler l'**échelle** (mm par unité du modèle) et les paramètres de **feuille** (taille, marge,
   espacement). Le panneau affiche les dimensions réelles, l'épaisseur d'une lamelle, et le nombre
   de pièces / feuilles.
4. **Exporter plan de découpe…** : choisir une base de nom → les fichiers `base_sheet1.svg`,
   `base_sheet1.dxf`, `base_sheet2.svg`, … sont générés.

**Navigation 3D** : clic gauche = rotation, clic droit = rotation (autre axe), molette = zoom.

---

## Modèles de test

Le dossier `tests/` contient des générateurs Python et des STL prêts à l'emploi :

```bash
python3 tests/gen_cube_stl.py      # cube 20 mm (ASCII + binaire)
python3 tests/gen_sphere_stl.py    # sphère rayon 10
python3 tests/gen_teapot_stl.py    # théière (modèle « fun »)
```

### Tests unitaires (headless, Qt5Core seul)

```bash
g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
    tests/test_slicer.cpp CSlicer.cpp CMesh.cpp $(pkg-config --libs Qt5Core) -o /tmp/test_slicer
/tmp/test_slicer tests/cube_ascii.stl tests/sphere.stl
```

(Idem pour `test_mesh.cpp` et `test_cutplan.cpp` — voir [ARCHITECTURE.md](ARCHITECTURE.md).)

---

## Limitations (v1)

- **Coupe alignée aux axes** X/Y/Z uniquement (pas de plan oblique).
- **Pas de compensation kerf** : les contours nominaux (ligne théorique) sont exportés tels quels.
  Beaucoup de logiciels laser/CNC savent appliquer le kerf eux-mêmes.
- **Maillages non étanches** : la couture peut laisser des boucles ouvertes (signalées par un
  avertissement) ; elles sont ignorées à l'export.
- Pas de rotation/recadrage du modèle dans l'UI : le STL est supposé déjà bien orienté.

---

## Pour les développeurs

L'organisation interne, les algorithmes et les points d'extension sont décrits dans
**[ARCHITECTURE.md](ARCHITECTURE.md)**.

---

## Licence

À définir.
