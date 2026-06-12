# Architecture — Parametric Wall Art

Document destiné aux développeurs souhaitant faire évoluer le projet. Il décrit le pipeline, le rôle
de chaque module, les algorithmes clés, les conventions de coordonnées, et les points d'extension.

Pour l'usage et la compilation, voir [README.md](README.md).

---

## 1. Vue d'ensemble du pipeline

```
 fichier.stl
     │  loadSTL()
     ▼
  ┌────────┐   slice(mesh, axis, n)   ┌──────────┐
  │ CMesh  │ ───────────────────────▶ │ CSlicer  │
  │triangles│                          │ + CSlice │
  │ + bbox │                          └────┬─────┘
  └────────┘                               │ std::vector<CSlice>
                                           │ (contours 2D par tranche)
                         ┌─────────────────┴─────────────────┐
                         ▼                                     ▼
                 ┌───────────────┐                    ┌────────────────┐
                 │   C3dView     │ setSlices(...)      │   CCutPlan     │ build(...)
                 │ aperçu OpenGL │                     │ nesting+export │
                 └───────────────┘                    └───────┬────────┘
                                                              │ exportSVG/DXF()
                                                              ▼
                                                   base_sheetN.svg / .dxf

      Orchestration : CMainWindow (UI Qt + slots)
```

Le flux est **unidirectionnel** : `CMainWindow` charge le mesh dans `C3dView`, récupère le mesh,
appelle `CSlicer`, puis pousse le résultat vers `C3dView` (aperçu) **et** `CCutPlan` (export).

---

## 2. Modules

| Fichier | Rôle | Dépendances |
|---|---|---|
| `geometry.h` | Types légers partagés + helpers (header-only) | `<vector>`, `<cmath>` |
| `CMesh.{h,cpp}` | Loader STL (binaire/ASCII) + bbox | Qt5Core (`QFile`, `QDataStream`) |
| `CSlicer.{h,cpp}` | Tranchage plan × triangles + couture des contours | `CMesh`, `geometry.h` |
| `CCutPlan.{h,cpp}` | Nesting des pièces + export SVG/DXF | `CSlicer`, Qt5Core |
| `C3dView.{h,cpp}` | Widget OpenGL : aperçu mesh **et** lamelles | Qt OpenGL, GLU, `CSlicer` |
| `CMainWindow.{h,cpp}` | Fenêtre + panneau de contrôle (construit en code) | tout le reste, Qt Widgets |
| `main.cpp` | Point d'entrée Qt | `CMainWindow` |

### `geometry.h`
- `SVec3 {x,y,z}`, `SPoint2 {x,y}`, `typedef std::vector<SPoint2> Contour`.
- Opérateurs (`+ - *`), `dot`, `length`, et `axisValue(v, axis)` (0=X, 1=Y, 2=Z).
- **Convention** : un `Contour` est fermé implicitement (le dernier point se relie au premier).

### `CMesh`
- `loadSTL()` auto-détecte le format : on **ne se fie pas** au mot-clé `solid` (un STL binaire peut
  commencer par « solid »). On compare la taille du fichier à `84 + nbTriangles*50`.
- Binaire : `QDataStream` little-endian, 50 o/triangle. ASCII : parsing ligne à ligne.
- `computeBounds()` calcule la bbox au chargement. Accesseurs `bboxMin/Max`, `size`, `center`.

### `CSlicer` / `CSlice`
- `CSlice` = `{ int index, float position, std::vector<Contour> contours }`.
- `slice(mesh, axis, nbSlices)` produit un `CSlice` par lamelle. Voir §3.
- Statiques réutilisables : `project(p, axis)` (3D→2D), `signedArea(contour)`, `thickness()`.

### `CCutPlan`
- `Params` : échelle (mm/unité), épaisseur matériau (étiquette), taille feuille L×H, marge, espacement.
- `Piece` : contours en **mm normalisés** (bbox min à l'origine) + offset `(tx,ty)` + n° de feuille.
- `build()` : une pièce par tranche non vide → **nesting par rangées** (voir §5).
- `exportSVG()` / `exportDXF()` : un fichier par feuille.

### `C3dView`
- Double mode : `drawMesh()` (triangles bruts) et `drawSlices()` (lamelles). `draw()` branche selon
  `m_sliceMode`. `setSlices()` bascule en mode lamelles, `clearSlices()` revient au mesh.
- Rendu des lamelles via le **tesselateur GLU** (voir §6).

### `CMainWindow`
- Le `.ui` ne contient que `w3d` (la vue) + le slider de luminosité. **Le panneau de contrôle est
  construit en code** dans `buildControlPanel()` (plus robuste que d'éditer le XML à la main).
- Le constructeur reparente `w3d` + le slider dans un conteneur droit, crée le panneau à gauche, et
  installe un `QHBoxLayout` via `setCentralWidget()`.
- Slot central : `reslice()` = `CSlicer::slice` → `w3d->setSlices` → `m_plan.build` → `updateInfo`.
  Tout changement de paramètre (`onParamsChanged`) relance `reslice()` (coût négligeable).

---

## 3. Algorithme de tranchage (`CSlicer::slice`)

Pour chaque tranche `k` (`thickness = span_axe / nbSlices`) :

1. **Position au centre du slab** : `pos = lo + (k+0.5)*thickness`. Couper au centre évite les hits
   dégénérés sur sommets/arêtes et représente la section médiane de la planche.
2. **Intersection plan × triangle** (`intersectTriangle`) : on classe les 3 sommets par signe de
   `axisValue - pos`. Chaque arête traversant le plan donne un point d'intersection (interpolation
   linéaire) ; les sommets quasi-coplanaires (`|d| < eps`) sont traités comme points d'intersection.
   Un triangle traversé produit **exactement 2 points → un segment**, projeté en 2D via `project()`.
3. **Couture en boucles** (`stitch`) : les extrémités des segments sont quantifiées sur une grille à
   `epsilon` (proportionnel à la diagonale du modèle). On chaîne les segments partageant un point
   jusqu'à refermer chaque `Contour`. Les boucles ouvertes (maillage non étanche) sont comptées et
   signalées par un `qWarning`, puis ignorées.

> **Orientation / trous** : `signedArea()` donne le sens (CCW/CW). En v1 on conserve toutes les
> boucles telles quelles ; l'imbrication trou/contour est gérée **en aval** (règle d'enroulement
> impaire au rendu, boucles indépendantes à l'export).

---

## 4. Conventions de coordonnées (axe → plan u,v)

`CSlicer::project()` projette un point 3D dans le plan 2D `(u,v)` selon l'axe de coupe :

| Axe | u | v | reconstruction 3D (`uvTo3D` dans `C3dView`) |
|-----|---|---|---|
| X   | Z | Y | `(pos, v, u)` |
| Y   | X | Z | `(u, pos, v)` |
| Z   | X | Y | `(u, v, pos)` |

`uvTo3D()` (statique, fichier-local dans `C3dView.cpp`) est l'**inverse exact** de `project()`. Toute
modification de l'un doit être répercutée sur l'autre.

---

## 5. Nesting (`CCutPlan::build`)

Algorithme « shelf / rangées », simple mais suffisant :

1. Une `Piece` par tranche non vide ; contours mis à l'échelle puis **normalisés** (bbox min → origine).
2. Tri des pièces par **hauteur décroissante**.
3. Remplissage **gauche → droite** ; retour à la ligne quand la pièce déborde en largeur ; **nouvelle
   feuille** quand la rangée déborde en hauteur. Marge + espacement respectés.
4. Chaque pièce reçoit `(tx, ty)` et un numéro de feuille ; un `qWarning` est émis si une pièce est
   plus grande que la feuille utile.

---

## 6. Rendu des lamelles (`C3dView::drawSlices`)

- **Faces avant/arrière** : tesselateur GLU (`gluNewTess`) en mode `GLU_TESS_WINDING_ODD` → gère
  concavités **et** trous. Les callbacks `*_DATA` reçoivent un `TessCtx` (bbox pour les UV bois +
  liste des sommets créés par `combine`, à libérer après `gluTessEndPolygon`). On fixe
  `gluTessNormal` + `glNormal` à `±axe` avant chaque face.
- **Extrusion** : front = `centre + halfT`, back = `centre − halfT`.
- **Parois latérales** : un `GL_QUAD_STRIP` par contour, normale en plan perpendiculaire à la tangente.
- **Enveloppe préservée** ⚠️ : le **pas** des lamelles vaut l'épaisseur réelle (`pitch = thickness`),
  donc la pile reconstruit exactement la bbox du modèle. Le jeu visuel entre planches est pris **en
  dedans** de chaque lamelle (`halfT = (thickness − gap)/2`), **jamais ajouté** — sinon l'objet
  s'allongerait le long de l'axe (un cube deviendrait un parallélépipède). `computeSliceFit()` utilise
  donc le même recentrage/échelle uniforme que l'affichage du mesh.

> Important pour les callbacks GLU : les sommets passés à `gluTessVertex` doivent rester **valides**
> jusqu'à `gluTessEndPolygon`. On pré-dimensionne un `std::vector<GLdouble>` par face (réservé une
> fois → adresses stables) et on y indexe.

---

## 7. Système de build

- `qmake` + `make`. `SOURCES`/`HEADERS` listent tous les `.cpp/.h` ; `FORMS` contient le `.ui` ;
  `RESOURCES` le `.qrc` (texture bois `:/textures/boisClair.jpg`, icône `:/icons/appicon.png`).
- `CONFIG += c++11`, `LIBS += -lGLU`.
- **Aucune dépendance externe** hors Qt + OpenGL/GLU (le slicing est 100 % maison).

---

## 8. Tests

Tests **headless** (lien `Qt5Core` seul, pas de GUI/OpenGL), un fichier par couche dans `tests/` :

| Test | Couvre | Données |
|---|---|---|
| `test_mesh.cpp` | loader STL, bbox | `cube_ascii.stl`, `cube_bin.stl` |
| `test_slicer.cpp` | tranchage + couture | cube (carré 20×20), sphère (contour central ≈ rayon) |
| `test_cutplan.cpp` | nesting + export | cube → 5 pièces, vérifie SVG/DXF (compte polygones/textes) |

Compilation type :

```bash
g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
    tests/test_cutplan.cpp CCutPlan.cpp CSlicer.cpp CMesh.cpp \
    $(pkg-config --libs Qt5Core) -o /tmp/test_cutplan
/tmp/test_cutplan tests/cube_ascii.stl
```

Les générateurs `tests/gen_*_stl.py` (re)créent les STL de référence. La vérification **visuelle**
(aperçu 3D, ouverture des SVG/DXF) reste manuelle car elle nécessite un display.

---

## 9. Points d'extension (idées v2)

| Évolution | Où intervenir | Notes |
|---|---|---|
| **Compensation kerf** | `CCutPlan` (offset de contour ±kerf/2) | utiliser `signedArea()` pour distinguer contour externe (offset +) et trous (offset −). *Hors périmètre actuel : découpe manuelle.* |
| **Plan de coupe oblique** | `CSlicer` (normale arbitraire) + `project`/`uvTo3D` + UI | alourdit le mapping 2D ; revoir aussi le fit de l'aperçu. |
| **Rotation/recadrage du modèle** | UI + transform appliqué avant slicing | évite de dépendre de l'orientation du STL. |
| **Maillages non étanches** | `CSlicer::stitch` (fermeture forcée) ou voxelisation native | aujourd'hui les boucles ouvertes sont juste signalées. |
| **Optimisation du nesting** | `CCutPlan::build` | l'algo « shelf » est volontairement simple. |
| **Autres formats d'entrée** (OBJ…) | nouveau loader, même interface que `CMesh` | garder `triangles()` + bbox. |

---

## 10. Style de code

- En-têtes de séparation `//----...` entre fonctions, signatures `void f(void)`, membres `m_xxx`.
- Commentaires en français, sans accents dans le code source (cohérence avec l'existant).
- Privilégier l'absence de dépendances externes : tout le cœur géométrique est implémenté à la main.
