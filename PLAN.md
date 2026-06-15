# Plan — Génération d'un plan de découpe d'art mural à partir d'un modèle 3D

## Contexte

L'appli Qt/C++ (`parametric-wall-art`) génère aujourd'hui un art **purement paramétrique** :
`C3dView::draw()` empile en dur des objets `CCouche` (anneaux de 20 segments, diamètre
décroissant) pour former un dôme à facettes de bois texturé. Aucun modèle 3D n'est importé,
et il n'existe aucune sortie « fabrication ».

L'objectif : **importer un modèle 3D (STL), le trancher en lamelles verticales parallèles
(style portrait bois multicouche, « côte à côte »), prévisualiser l'assemblage en 3D, et
exporter un plan de découpe vectoriel (SVG + DXF)** prêt pour laser/CNC.

Décisions déjà actées avec l'utilisateur :
- **Slicing natif** en C++ (loader STL maison + intersection plan×triangles). Pas de binvox,
  pas d'assimp, pas de GPU requis pour le slicing — autonome.
- **Entrée : STL** uniquement (binaire + ASCII).
- **Sortie : SVG + DXF** (une feuille par fichier).
- **Tranches verticales côte à côte**.
- **Intégré dans l'appli Qt** (Qt 5.15.13, g++ 13, build qmake).

État actuel pertinent :
- `common.h` : constantes du mode paramétrique uniquement.
- `C3dView.cpp` : OpenGL fixe (QGLWidget), `draw()` boucle en dur sur les `CCouche`, texture bois chargée.
- `CMainWindow` : `setupUi()` seul ; central widget = `w3d` (C3dView) dans un `QVBoxLayout`. Aucun contrôle UI.
- `.pro` : `QT += core gui opengl widgets`, `CONFIG += c++11`.

---

## Architecture

**Nouveaux fichiers**
- `geometry.h` — types légers partagés : `SVec3 {x,y,z}`, `SPoint2 {x,y}`, `typedef std::vector<SPoint2> Contour`.
- `CMesh.h/.cpp` — chargement STL + stockage triangles + bbox.
- `CSlicer.h/.cpp` — moteur de tranchage → `std::vector<CSlice>` (avec classe `CSlice`).
- `CCutPlan.h/.cpp` — nesting des tranches sur feuilles + écriture SVG et DXF.

**Fichiers modifiés**
- `C3dView.h/.cpp` — nouveau mode rendu : tranches extrudées espacées le long de l'axe.
- `CMainWindow.h/.cpp` — panneau de contrôle latéral (import, paramètres, export) + slots.
- `parametric-wall-art.pro` — ajout sources/headers + `LIBS += -lGLU`.

> L'`.ui` reste tel quel (juste `w3d`). Le panneau de contrôle est construit **en code** dans
> le constructeur de `CMainWindow` (plus robuste que d'éditer le XML `.ui` à la main), en
> remplaçant le `QVBoxLayout` par un `QHBoxLayout` : panneau à gauche, `w3d` à droite.

---

## Phase 1 — Géométrie + chargement STL (`geometry.h`, `CMesh`)

`geometry.h` : structs `SVec3`, `SPoint2`, alias `Contour`, + helpers inline (`sub`, `dot`, etc.) au besoin.

`CMesh` :
```cpp
struct STriangle { SVec3 v[3]; SVec3 normal; };
bool loadSTL(const QString& path);          // auto-détection binaire/ASCII
const std::vector<STriangle>& triangles() const;
SVec3 bboxMin, bboxMax;                      // calculés au chargement
SVec3 size() const;  SVec3 center() const;
```
- **Détection binaire vs ASCII** : ne PAS se fier au mot-clé `solid` (un STL binaire peut
  commencer par « solid »). Lire l'entête 80 o + `uint32` count ; si
  `taille_fichier == 84 + count*50` → binaire, sinon ASCII.
- Binaire : `QFile` + `QDataStream` (little-endian), 50 o/triangle (normale + 3 sommets + 2 o attribut).
- ASCII : parse `facet normal` / `vertex` ligne à ligne.
- `computeBounds()` met à jour bbox.

**Jalon testable headless** : charger un STL connu, vérifier nb triangles + bbox via un petit
`main` de test temporaire ou `qDebug`.

## Phase 2 — Tranchage (`CSlicer`, `CSlice`)

```cpp
class CSlice {
  int index;                          // ordre d'assemblage
  float position;                     // coord. le long de l'axe de coupe
  std::vector<Contour> contours;      // boucles fermées en plan (u,v)
};
class CSlicer {
  enum Axis { AxisX, AxisY, AxisZ };
  std::vector<CSlice> slice(const CMesh&, Axis axis, int nbSlices);
};
```
Algorithme, par tranche `k` :
1. **Position au centre du slab** : `pos_k = min + (k+0.5)*thickness` avec
   `thickness = (max-min)/nbSlices`. Couper au centre évite les hits dégénérés sur sommets/arêtes
   et représente la section médiane de la planche (bonne silhouette pour une planche d'épaisseur finie).
2. **Intersection plan×triangle** : classer les 3 sommets par signe de `coord_axe - pos_k` ;
   les arêtes traversant 0 donnent 2 points d'intersection → un **segment** projeté dans le
   plan 2D (les deux autres axes → `(u,v)`).
3. **Couture en boucles** : indexer les extrémités sur une grille quantifiée à `epsilon` et
   chaîner les segments partageant un point jusqu'à refermer chaque `Contour`.
4. Orientation : signer l'aire de chaque contour ; les boucles internes (sens opposé / contenues)
   = trous à découper, l'externe = contour de coupe.

Mapping axe→(u,v) cohérent (ex. AxisX → (u=Z, v=Y)) pour que les planches soient orientées
« debout, côte à côte » le long de l'axe choisi.

## Phase 3 — Plan de découpe (`CCutPlan` : nesting + SVG + DXF)

Paramètres : échelle (mm / unité modèle), épaisseur matériau (info/étiquette), taille feuille
(L×H mm, déf. 600×400), marge bord, espacement entre pièces. (Compensation kerf : hors v1 —
on exporte les contours nominaux.)

- **Nesting** (shelf/row simple, suffisant) : pour chaque `CSlice`, calculer sa bbox 2D ;
  trier par hauteur décroissante ; placer de gauche à droite, retour à la ligne quand ça
  déborde, nouvelle feuille quand la hauteur est pleine. Chaque pièce reçoit un offset (tx,ty)
  + un n° d'ordre.
- **SVG** (un fichier `_sheetN.svg` par feuille) : unités mm ; chaque contour en
  `<polygon points=...>` `fill:none;stroke:#000;stroke-width:0.1` ; `<text>` = index de tranche ;
  `<rect>` = contour feuille.
- **DXF** (un `_sheetN.dxf` par feuille) : R12 ASCII minimal (compat large laser/CNC) —
  section `ENTITIES`, contours en `POLYLINE`/`VERTEX`/`SEQEND` (pas LWPOLYLINE, R14+), calque
  `CUT` ; étiquettes en `TEXT` calque `LABEL`.
- Dialogue de sauvegarde Qt → base de nom ; on écrit `base_sheet1.svg/.dxf`, etc.

## Phase 4 — UI + prévisualisation 3D

**`CMainWindow`** : remplacer le `verticalLayout` par un `QHBoxLayout` ; à gauche un panneau
(`QFormLayout`) construit en code :
- Bouton « Importer STL… » (`QFileDialog`).
- `QComboBox` axe de coupe (X/Y/Z).
- `QSpinBox` nb tranches (déf. 30).
- `QDoubleSpinBox` échelle (mm/unité), épaisseur matériau, taille feuille L/H, espacement.
- Bouton « Exporter plan de découpe… ».
- Label infos (dimensions réelles, nb pièces, nb feuilles).

Slots : `onImport()` (charge `CMesh`, relance slicing, met à jour la vue + infos),
`onParamsChanged()` (re-slice + refresh), `onExport()` (construit `CCutPlan`, écrit fichiers,
message de confirmation).

**`C3dView`** : ajouter
```cpp
void setSlices(const std::vector<CSlice>& slices, CSlicer::Axis axis,
               float thickness, float gap);
```
- `m_meshMode = true` quand des tranches existent ; `draw()` branche entre l'ancien mode
  paramétrique (`CCouche`) et le nouveau.
- Rendu d'une tranche : **faces avant/arrière remplies** via le tesselateur GLU
  (`gluNewTess`, `GLU_TESS_WINDING_ODD` → gère concavité + trous), **extrudées** de `thickness`
  le long de l'axe, + **parois latérales** (quad-strip autour de chaque contour). Texture bois
  réutilisée. Tranches espacées de `thickness+gap` le long de l'axe → effet « côte à côte ».
- Recentrer/normaliser via la bbox du mesh pour rester dans le frustum existant
  (`glFrustum(... 4.0, 15.0)`, translation `-11`).

## Build

- `.pro` : ajouter `CMesh/CSlicer/CCutPlan/CMainWindow…` aux `SOURCES`, les headers aux
  `HEADERS`, et `LIBS += -lGLU` (pour `gluNewTess`).
- `geometry.h` : header-only, juste référencé.

---

## Vérification (end-to-end)

1. **Build** : `qmake && make` sans erreur (vérifier le lien `-lGLU`).
2. **Slicer headless** : charger un STL simple connu (ex. cube 20 mm ou sphère), `qDebug` le
   nb de tranches et, pour une tranche médiane d'une sphère, vérifier qu'on obtient 1 contour
   fermé de ~bon rayon. Tester un STL **binaire** ET un **ASCII**.
3. **UI** : lancer l'appli, « Importer STL… », vérifier que la prévisualisation 3D montre la
   pile de lamelles côte à côte ressemblant au modèle ; jouer sur nb tranches / axe et voir la
   vue se mettre à jour.
4. **Export** : « Exporter… », ouvrir un `.svg` généré dans un navigateur / Inkscape et un
   `.dxf` dans LibreCAD → vérifier contours fermés, trous présents, étiquettes d'ordre,
   pièces non chevauchantes dans la feuille, dimensions à l'échelle (mesurer une pièce).
5. **Non-régression** : sans modèle importé, le mode paramétrique d'origine s'affiche toujours.

## Notes / risques

- **Maillages non étanches** : la couture peut laisser des boucles ouvertes. v1 : on ferme la
  boucle de force si les extrémités sont proches (< epsilon×k) et on `qWarning` sinon. Robustesse
  accrue possible plus tard (voxelisation native) si besoin — non couvert ici.
- **Trous dans les contours** : gérés au rendu (GLU tess) et à l'export (chaque boucle = un
  `polygon`/`POLYLINE` indépendant ; les logiciels laser interprètent l'imbrication).
- **Pas de compensation kerf** en v1 (contours nominaux) — ajout trivial ultérieur (offset de
  contour) si nécessaire.
- Aucune dépendance externe nouvelle hormis `-lGLU` (déjà présent avec le stack OpenGL/Qt).

---

## Reprise — où on en est

- Statut : **Phases 1, 2, 3 et 4 terminées — v1 fonctionnelle.** ✅
- **Décision** : coupe limitée aux axes **X/Y/Z** en v1 (pas de plan oblique). Pas de rotation/recadrage
  dans l'UI : le STL est supposé déjà bien posé ; on change d'axe au besoin.
- ⚠️ **Reste la vérif visuelle réelle par l'utilisateur** (besoin d'un display) : importer un STL,
  voir les lamelles côte à côte, jouer sur axe/nb tranches, exporter et ouvrir SVG/DXF.

### Refonte affichage (fait, hors phase initiale)
- **Ancien vase paramétrique supprimé** : `CCouche.cpp/.h` + `common.h` retirés (et du `.pro`).
- `C3dView` affiche désormais le **mesh STL importé** : `loadMesh()`, rendu triangles +
  normales, éclairage deux faces (`GL_LIGHT_MODEL_TWO_SIDE`) + `GL_NORMALIZE`, texture bois
  en `GL_MODULATE`, recentrage/normalisation auto (`computeFit`) pour tenir dans le frustum.
- `CMainWindow` : menu **Fichier → Importer STL…** (`QFileDialog`) → `w3d->loadMesh()`.
- Build complet OK (1 warning pré-existant `QWheelEvent::delta`), démarrage sans crash (smoke test offscreen).
- **Éclairage éclairci** : 2 lumières (clé + fill), ambiante globale, matériau diffus blanc
  (texture en plein), fond clair studio au lieu du noir.
- **Slider de luminosité** (réglable à chaud) : `QSlider sliderBrightness` ajouté dans
  `CMainWindow.ui` (plage 20..200 = 0.20x..2.00x), connecté à `onBrightnessChanged` →
  `C3dView::setBrightness()`. Les intensités sont centralisées dans `C3dView::applyLighting()`
  (× `m_brightness`), réappliquées via `makeCurrent()`+`updateGL()`. Le fond s'éclaircit aussi.
- `tests/gen_teapot_stl.py` → `tests/teapot.stl` : petite **théière paramétrique** (corps +
  couvercle + bouton + bec + anse), 3812 triangles, modèle de test « fun ».
- **Titre fenêtre** « Parametric Wall Art » (était « QtWSeg »), titre mis à jour avec le nom de
  fichier à l'import.
- **Icône d'app** : `icons/gen_icon.py` → `icons/appicon.svg` → `icons/appicon.png` (volume tranché
  en lamelles verticales de bois sur fond sombre). Ajoutée au `.qrc` (`:/icons/appicon.png`),
  posée comme `windowIcon` dans le `.ui` (uniquement, pas en code).
- ⚠️ Vérif visuelle réelle à faire par l'utilisateur (besoin d'un display) : importer un STL et voir le modèle.

### Phase 1 livrée
- `geometry.h` — types `SVec3`, `SPoint2`, `Contour` + helpers (`dot`, `length`, `axisValue`, opérateurs).
- `CMesh.h/.cpp` — loader STL auto-détecté (binaire via taille fichier `84 + n*50`, sinon ASCII), bbox/size/center.
- `parametric-wall-art.pro` — `CMesh.cpp`/`CMesh.h`/`geometry.h` ajoutés.
- `tests/gen_cube_stl.py` — génère un cube 20 mm en ASCII (`cube_ascii.stl`) et binaire (`cube_bin.stl`).
- `tests/test_mesh.cpp` — test headless (lien Qt5Core seul). **Tous verts** (ASCII + binaire : 12 tris, bbox/size/center corrects).

### Comment relancer la vérif Phase 1
```bash
python3 tests/gen_cube_stl.py
g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
    tests/test_mesh.cpp CMesh.cpp $(pkg-config --libs Qt5Core) -o /tmp/test_mesh
/tmp/test_mesh tests/cube_ascii.stl tests/cube_bin.stl   # -> "TOUS LES TESTS PASSENT"
qmake parametric-wall-art.pro && make                    # build app complet OK
```

### Phase 2 livrée
- `CSlicer.h/.cpp` — `CSlice` { `index`, `position`, `contours` } + `CSlicer::slice(mesh, axis, nbSlices)`.
  Coupe au centre de chaque slab (`pos = lo + (k+0.5)*thickness`), intersection plan×triangle
  (classement par signe de `axisValue - pos`, interpolation d'arête, snap epsilon des sommets coplanaires),
  projection 2D `project()` (AxisX→(Z,Y), AxisY→(X,Z), AxisZ→(X,Y)), couture des segments en boucles
  fermées via grille quantifiée (`stitch()`), `qWarning` sur boucles ouvertes. Helpers `thickness()`,
  `signedArea()`.
- `parametric-wall-art.pro` — `CSlicer.cpp`/`CSlicer.h` ajoutés.
- `tests/gen_sphere_stl.py` → `tests/sphere.stl` (rayon 10 centré, 1536 tris).
- `tests/test_slicer.cpp` — test headless. **Tous verts** : cube (10 tranches, épaisseur 2, contour 20×20,
  aire ~400) + sphère (tranche centrale = 1 contour fermé, rayon mesuré ~= rayon sphère).

#### Comment relancer la vérif Phase 2
```bash
python3 tests/gen_cube_stl.py && python3 tests/gen_sphere_stl.py
g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
    tests/test_slicer.cpp CSlicer.cpp CMesh.cpp $(pkg-config --libs Qt5Core) -o /tmp/test_slicer
/tmp/test_slicer tests/cube_ascii.stl tests/sphere.stl   # -> "TOUS LES TESTS PASSENT"
```

### Phase 3 livrée
- `CCutPlan.h/.cpp` — `Params` (échelle mm/unité, épaisseur matériau, taille feuille L×H, marge,
  espacement), `Piece` (contours mm normalisés + tx/ty + n° feuille). `build()` : une pièce par
  tranche non vide (mise à l'échelle + normalisation bbox), **nesting par rangées** (tri hauteur
  décroissante, remplissage gauche→droite, retour ligne au débordement, nouvelle feuille quand plein),
  `qWarning` si pièce > feuille. `exportSVG()`/`exportDXF()` : un fichier `<base>_sheetN.svg|dxf` par feuille.
  - SVG : unités mm, `viewBox`, groupe `translate+scale(1,-1)` (repère y-haut), `<rect>` feuille,
    `<polygon>` par contour (`stroke 0.1`), `<text>` n° de tranche (hors groupe pour éviter le miroir).
  - DXF : R12 ASCII (`AC1009`), TABLES calques `CUT`/`LABEL`, `POLYLINE`/`VERTEX`/`SEQEND` fermées,
    `TEXT` centré. Pas de LWPOLYLINE (compat large).
- `parametric-wall-art.pro` — `CCutPlan.cpp`/`CCutPlan.h` ajoutés.
- `tests/test_cutplan.cpp` — test headless. **Tous verts** : cube → 5 pièces 40×40 mm (échelle ×2),
  1 feuille, non chevauchantes, dans les bornes ; SVG = 5 polygones + 5 étiquettes ; DXF = 5 polylignes
  + 5 textes + EOF.

#### Comment relancer la vérif Phase 3
```bash
g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
    tests/test_cutplan.cpp CCutPlan.cpp CSlicer.cpp CMesh.cpp \
    $(pkg-config --libs Qt5Core) -o /tmp/test_cutplan
/tmp/test_cutplan tests/cube_ascii.stl   # -> "TOUS LES TESTS PASSENT"
# Fichiers exemples ecrits dans /tmp/test_cutplan_out_sheet1.svg / .dxf
```

### Phase 4 livrée
- `C3dView` — mode tranches : `setSlices(slices, axis, thickness, gap)` / `clearSlices()`, accesseur
  `mesh()`. `draw()` branche `drawSlices()` (si tranches) sinon `drawMesh()`. Rendu d'une lamelle :
  faces avant/arrière via **tesselateur GLU** (`GLU_TESS_WINDING_ODD` → concavités + trous),
  extrudées de `thickness` le long de l'axe, **parois latérales** en quad-strip. Texture bois réutilisée
  (UV planaire XY). Espacement `thickness+gap` → effet « côte à côte ». `computeSliceFit()` recentre/
  normalise la pile étalée dans le frustum. Callbacks GLU + `uvTo3D()` (inverse de `CSlicer::project`).
- `CMainWindow` — panneau de contrôle **construit en code** (le `.ui` reste w3d + slider lumière) :
  central passé en `QHBoxLayout` (panneau gauche / vue+luminosité droite via reparentage avant
  `setCentralWidget`). Contrôles : bouton Importer, combo axe X/Y/Z, spin nb tranches (déf. 30),
  spins échelle/épaisseur/feuille L·H/marge/espacement, bouton Exporter, label infos. Slots
  `onImport`/`onParamsChanged`/`onExport` + `reslice()` (slice → `setSlices` → `m_plan.build` → infos).
- `parametric-wall-art.pro` — `LIBS += -lGLU` ajouté.
- Build complet OK (seul warning restant : `QWheelEvent::delta` pré-existant). Smoke test offscreen OK.

#### Comment relancer la vérif (tous les tests headless)
```bash
python3 tests/gen_cube_stl.py && python3 tests/gen_sphere_stl.py
for t in mesh slicer cutplan; do
  case $t in
   mesh)    SRC="tests/test_mesh.cpp CMesh.cpp"; ARGS="tests/cube_ascii.stl tests/cube_bin.stl";;
   slicer)  SRC="tests/test_slicer.cpp CSlicer.cpp CMesh.cpp"; ARGS="tests/cube_ascii.stl tests/sphere.stl";;
   cutplan) SRC="tests/test_cutplan.cpp CCutPlan.cpp CSlicer.cpp CMesh.cpp"; ARGS="tests/cube_ascii.stl";;
  esac
  g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) $SRC $(pkg-config --libs Qt5Core) -o /tmp/test_$t
  echo "== $t =="; /tmp/test_$t $ARGS | tail -1
done
qmake parametric-wall-art.pro && make    # build app complet
```

> **v1 complète.** Reste la vérif visuelle réelle (display) + idées v2 hors périmètre : compensation
> kerf, rotation/recadrage du modèle, plan de coupe oblique, voxelisation pour maillages non étanches.

---

## Itération v2 — UI .ui, échelle pilotée, vide, repère, planche de fond

### UI entièrement déplacée dans le `.ui` (fait)
- Tout le panneau de contrôle est désormais décrit dans `CMainWindow.ui` (plus de construction
  en code). `mainLayout` `QHBoxLayout` (stretch 0,1) : `controlPanel` (gauche) + `rightWidget`
  (vue 3D + slider luminosité). Champs à largeur homogène (`QFormLayout` +
  `fieldGrowthPolicy = AllNonFixedFieldsGrow`).
- Groupe **Découpe** : axe X/Y/Z, **Nb lamelles** (ex « Nb tranches »), **Épaisseur vide**,
  **Épaisseur lamelle** (éditable), case **Générer planche de fond**.
- Groupe **Matériau / feuille** : **Échelle** (label lecture seule), **Taille finale** (label),
  largeur/hauteur feuille, marge, espacement pièces. Champ « épaisseur matériau » supprimé.

### Échelle pilotée par nb × épaisseur lamelle (fait)
- `currentScale() = épaisseur_lamelle × nb_lamelles / taille_modèle_sur_axe`. L'échelle est
  donc **dérivée** (label lecture seule), pilotée par les paramètres de découpe.
- `CCutPlan::Params` : champ `materialThickness` retiré ; ajout `generateBoard`,
  `sliceThickness`, `gapThickness`.

### Épaisseur vide = préserve les proportions (fait, bug corrigé)
- Le vide **n'allonge pas** l'objet : le pas de tranche du preview reste **constant**
  (`m_slicer.thickness() = taille_axe / n`) ; lamelle et vide se **partagent** ce pas selon le
  ratio mm `t:g` (`CMainWindow::sliceViewSplit()`). En montant le vide, la lamelle s'amincit, le
  vide grandit à sa place — la forme générale est conservée (corrige l'étirement X/Z observé).
- `C3dView::computeSliceFit()` voit un spread constant → fit stable, sections à taille apparente
  constante.
- **Taille finale** : sur l'axe de coupe = `n × lamelle + (n-1) × vide` (volume réel monté au
  mur, **augmente** avec le vide) ; les deux autres axes = `taille × échelle`.

### Repère X/Y/Z (fait)
- `C3dView::drawAxisIndicator()` : petit trièdre en bas à gauche (viewport 70px), suivant les
  rotations de la scène. X rouge, Y vert, Z bleu, + **lettres** X/Y/Z en segments de lignes au
  bout de chaque axe (couleur de l'axe).

### Planche de fond (socle à fentes + tenons) (fait — v1 de la feature)
Décisions actées : **socle à fentes + tenons** ; monté à l'**arrière** (u mini = « Z min » pour
coupe X) ; socle **perpendiculaire aux lamelles** = le long de l'axe de coupe (la formulation
« perpendiculaire à l'axe » était à interpréter ainsi, sinon le socle ne tiendrait rien).
- `geometry.h` → `namespace BoardJoint` : constantes **partagées** plan-de-découpe / preview —
  `kTabWidth=10`, `kTwoTabMin=40`, `kTabPad=4` (mm).
- `CCutPlan::build()` refondu en 4 phases : (1) contours mis à l'échelle **non normalisés** +
  bbox global ; (2) si socle : greffe les **tenons** sur le contour arrière des lamelles en
  contact (`attachTenon()` — union locale rectangle/contour sans lib de clipping), 1 ou 2 tenons
  selon la hauteur de contact (`kTwoTabMin`), collecte les **mortaises** ; construit la pièce
  **socle** = rectangle `L×H` percé de mortaises standard (`t × kTabWidth`), `sliceIndex=-1`,
  étiquette « FOND » ; (3) normalisation par pièce (tenons inclus) ; (4) nesting (inchangé).
  - Contact : une lamelle est « en contact » si son dos atteint le plan du socle (`u_min` global)
    à `tol = épaisseur lamelle` près. Sinon → **flottante** (`m_floating`), pas de tenon.
  - Export SVG/DXF : le socle sort comme une pièce normale (contour + trous mortaises + label).
- `CMainWindow` : case `m_genBoard` → `onParamsChanged` (rebuild). Alerte dans le label infos :
  `⚠ N lamelle(s) sans contact avec le fond : …`. `currentParams()` transmet
  generateBoard/sliceThickness/gapThickness.
- **Aperçu 3D** : `C3dView::setBoard(enabled, scale, thicknessMm)` + `drawBoard()` (appelé en fin
  de `drawSlices`, même transfo modèle). Dessine le **socle** (plaque texturée le long de l'axe,
  plan arrière) + les **tenons** des lamelles en contact (boîtes texturées), en unités modèle via
  `emitBox()`/`emitQuad()`. Mêmes constantes `BoardJoint` /échelle → cohérent avec l'export.

### Outils de génération de STL de test
- `tests/gen_cube_stl.py` / `gen_sphere_stl.py` / `gen_teapot_stl.py` — modèles paramétriques.
- `tests/svg2stl.py` (nouveau) — **convertit un SVG au trait en STL extrudé** : échantillonne
  chaque path (`svgpathtools`, 40 pts/segment, dédoublonnage), épaissit les traits en rubans
  (`shapely` `buffer(width/2)`), fusionne (`unary_union`), extrude en profondeur (`trimesh
  extrude_polygon`), miroite l'axe Y (SVG = Y vers le bas) et recale l'origine en bas. Usage :
  `python3 tests/svg2stl.py in.svg out.stl --width 1.0 --depth 3.0` (largeur trait / profondeur
  en mm). Affiche dimensions, nb triangles, `watertight`. Dépendances : `numpy`, `svgpathtools`,
  `shapely`, `trimesh`. Exemple fourni : `tests/tornade.svg` → `tests/tornade.stl`.

### À améliorer plus tard (TODO)
- **Position du socle** : actuellement figée au plan arrière (u mini global). Beaucoup de lamelles
  d'un modèle organique (ex. bec/anse de la théière) seront « flottantes » → revoir : socle
  positionnable, ou tenons de longueur variable, ou plusieurs socles.
- Choix du **côté du socle** (Dessous / Arrière) exposé dans l'UI (pour l'instant codé « arrière »).
- Affiner la **détection de contact** / tolérance, et le seuil **1 vs 2 tenons**.
- `attachTenon()` : gère le cas générique (1 entrée / 1 sortie de bande). Cas tordus (rentrées
  multiples, contour très concave au dos) → tenon ignoré (lamelle comptée flottante). À durcir.
- Vérif visuelle réelle de la feature socle (display) : position, nombre de tenons, mortaises
  alignées avec les tenons à l'assemblage.
- Avertissement `QWheelEvent::delta` toujours présent (pré-existant, non bloquant).
