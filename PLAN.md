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

### Planche de fond v3 — intégrée dans la profondeur (fait)
- **La planche ne rallonge plus l'objet** : elle s'intègre dans la profondeur du modèle. Le dos
  des lamelles en contact est **raboté de l'épaisseur planche** (`t`), sauf au niveau des
  **tenons** qui ressortent jusqu'au plan arrière (`u0`) pour s'enficher dans les mortaises. Le
  volume assemblé garde donc l'enveloppe d'origine (`u0..umax`) au lieu de `u0-t..umax`.
- **Logique de jointure centralisée dans `geometry.h`** (`namespace BoardJoint`), partagée par
  l'export et l'aperçu : `clipKeepUGE()` (Sutherland-Hodgman, recule le dos à `u >= uClip`),
  `insertBackVertex()` (insère un sommet sur l'arête de clip plate pour garantir l'accroche du
  tenon), `attachTab()` (ex-`attachTenon`, greffe un tenon jusqu'à `uTip`), `tabCenters()`
  (1 ou 2 tenons selon `kTwoTabMin`). Les anciens helpers locaux de `CCutPlan.cpp` ont été retirés.
- `CCutPlan::build()` : `xClip = gMinX + t`, `xTip = gMinX` (avant : tenon à `gMinX - t`). Clip du
  contour extérieur puis greffe des tenons sur une **copie** (lamelle laissée entière si aucun
  tenon ne s'accroche → flottante). Mortaises et silhouette de la planche inchangées.
- `C3dView::jointContours()` applique le même rabotage+tenons au rendu ; les tenons font partie
  des lamelles (extrudés avec elles). `backPlaneU()` factorise le calcul du plan arrière.
- **Rendu planche tesselé (v3.1)** : `drawBoard()` ne dessine plus des boîtes pleines mais
  **tessèle une vraie plaque trouée** (`u ∈ [u0, u0+t]`, couleur unie) — faces avant/arrière en
  règle d'enroulement IMPAIR + parois le long de `u`. Deux corrections visuelles :
  - **Mortaises visibles** : chaque tenon perce un trou (`t × kTabWidth`) dans la plaque, donc on
    voit les fentes quand on affiche la planche seule (`boardTabs()` mutualisé avec les lamelles).
  - **Creux non comblés** : la hauteur de plaque suit les **intervalles v réels** de la tranche
    (`boardVSpans()` : un intervalle par contour extérieur — orientation = signe du plus grand
    contour ; trous ignorés ; intervalles disjoints non fusionnés). Fini les pavés blancs qui
    bouchaient les concavités du modèle dans l'aperçu.
  - `emitBox`/`emitQuad` retirés (plus utilisés).
  - ⚠ **Divergence assumée aperçu/export** : l'aperçu suit les intervalles v réels (peut montrer
    des fentes/creux) ; l'export `CCutPlan` garde la silhouette **englobante** (staircase bbox),
    donc une planche **connexe et un peu plus généreuse** — sûr pour un support caché derrière.
- **Alerte non-contact préservée** : `CCutPlan` alimente toujours `floatingSlices()` selon 3
  conditions (dos hors contact, pas de bande de contact, aucun tenon greffé). `updateInfo()`
  inchangé. ⚠ tolérance de contact : `tol = t` (modèle) dans l'aperçu vs `max(t,1)` mm à l'export.
- Vérifié headless (cube/sphère) : profondeur lamelle inchangée (tenon au plan arrière), dos
  raboté à `x≈t`, FOND = silhouette (cube 40 mortaises = 20×2 tenons, sphère 18 = 20−2 flottantes).

### Planche de fond v2 — silhouette, visibilité, couleur (fait)
- **Silhouette du modèle** : la planche n'est plus un rectangle plein. Elle épouse la forme du
  STL = **une colonne par lamelle** (étendue v de la tranche), tuilées le long de l'axe.
  - `CCutPlan::build()` : le contour extérieur du socle est construit comme un profil en escalier
    (bord haut gauche→droite sur les `maxy`, bord bas droite→gauche sur les `miny` de chaque
    colonne `[idx·pitch, (idx+1)·pitch]`, dernière large de `t`), puis la pièce socle est
    normalisée (bbox→origine, `w`/`h` recalculés). Mortaises inchangées.
  - `C3dView::drawBoard()` : le socle est rendu en colonnes `emitBox` par lamelle (étendue v),
    au lieu d'une seule boîte. Vérifié headless : sphère → contour FOND = 80 sommets (vs 4).
- **Deux cases d'affichage** (`m_showSlices`, `m_showBoard`), **actives uniquement** si
  « Générer planche de fond » est cochée (sinon désactivées + lamelles toujours visibles) :
  masquer/afficher les lamelles et la planche dans l'aperçu 3D indépendamment. `C3dView::
  setSlicesVisible()` (garde le bloc lamelles de `drawSlices`) + `setBoard(enabled,…)` piloté par
  `m_showBoard`. Logique centralisée dans `CMainWindow::applyBoardPreview()`, slot `onViewChanged`.
- **Couleur de planche** (pas de texture bois) : `m_boardColorBtn` ouvre un `QColorDialog`
  (`onBoardColorPick`), **blanc par défaut**, pastille de couleur sur le bouton
  (`updateBoardColorSwatch`). `C3dView::setBoardColor()` stocke un RGB ; `drawBoard()` désactive
  la texture et pose la couleur via `glMaterialfv(GL_AMBIENT_AND_DIFFUSE)`, puis restaure le
  matériau blanc (texture bois) pour le reste de la scène.

### Joint multi-contour : tenons/mortaises sur chaque morceau (corps + anse + bec) (fait)
- Bug : le code ne tenonnait qu'**un seul** contour par tranche (le plus en arrière). Sur une
  tranche corps **+ anse** (deux contours extérieurs distincts atteignant le fond), seul le corps
  recevait tenon + mortaise ; l'anse n'avait ni tenon ni mortaise, et son morceau de planche
  manquait (l'orientation d'aire du slicer étant **incohérente**, la détection extérieur/trou par
  le signe ratait l'anse).
- `geometry.h` : `pointInPolygon()` + `outerContourMask()` — classe extérieur vs trou par
  **containment** (robuste, indépendant du signe d'aire). `BoardJoint::integrateContourBack()` —
  rabote+tenonne **un** contour et renvoie les centres v posés (pour les mortaises).
- `CCutPlan::build()` et `C3dView` (`jointContours`, `sliceTabCenters` ex-`boardTabs`, `boardVSpans`)
  itèrent désormais sur **tous les contours extérieurs** atteignant le fond → chaque morceau
  (corps, anse, bec) est raboté, tenonné, et perce sa mortaise. `boardVSpans` utilise aussi
  `outerContourMask` → plus de morceau de planche manquant. Vérifié : théière, FOND passe à
  47 contours (mortaises de l'anse incluses), lamelle corps+anse = 2 contours tenonnés.

### Planche de fond — contours lisses, inscrits dans le modèle (fait)
- Avant : planche en **escalier** (colonnes rectangulaires par tranche, sur tout le pas) →
  décrochés + dépasse la silhouette du modèle.
- `geometry.h` : `buildSmoothLobes(slices, capWidth)` — relie les extrémités v **au centre de
  chaque tranche** par des polylignes (suivi de bandes : chaque span prolonge la bande qui le
  recouvre le plus en v ; bandes disjointes = corps/anse/bec séparés, creux non comblé). Contour
  **lisse** et **inscrit** (jamais plus large/haut que le modèle).
- `C3dView::drawBoard` et `CCutPlan::build` construisent désormais les lobes + mortaises et les
  tessèlent/exportent en une passe. Vérifié théière : FOND = 2 lobes lisses (sommets en diagonale,
  zéro décroché), hauteur 172.6 mm < 178.2 mm du modèle (✓ pas plus grand).

### Option forme du fond : Lisse / Escalier (fait)
- `CCutPlan::Params::boardSmooth` (déf. true) + combo UI **« Forme du fond »** (Lisse / Escalier),
  actif si planche générée. Transmis à l'aperçu via `C3dView::setBoard(..., smooth, ...)`.
- **Lisse** : enveloppe connexe + trous de creux (silhouette d'un seul tenant, inscrite).
- **Escalier (par lamelle)** : même `buildSmoothLobes` avec `treadHalfWidth = t/2`. Liaisons en
  **vraies marches à angle droit** (palier horizontal à la hauteur précédente + montée verticale,
  **aucun trait oblique** — vérifié : contour théière = 118 segments horizontaux + 60 verticaux,
  0 oblique). Fond **connexe d'un seul tenant**, trou de l'anse préservé.
- **Épaisseur planche = lamelle (aperçu)** : `drawBoard`/`jointContours`/`sliceTabCenters` utilisent
  `m_sliceThickness` (épaisseur vue d'une lamelle) au lieu de `m_boardThickMm/scale`, sinon la
  planche paraissait plus épaisse que les lamelles (artefact du pas constant). Export inchangé.

### Export groupé par pièce — SVG `<g>` + DXF blocs (fait)
- **SVG** : un `<g id="lamelle-N">` par lamelle et un `<g id="fond">` pour la planche (contours +
  étiquette + numéros). Le miroir Y est appliqué **point par point** (`y→H-y`) au lieu d'un groupe
  `scale(1,-1)`, pour ne pas miroiter les textes. Vérifié : 30 groupes lamelle + `fond`, XML valide.
- **DXF** : un **BLOC** R12 par pièce (`lamelle-N` / `fond`), inséré une fois (`INSERT`) → chaque
  pièce est un groupe sélectionnable, calques `CUT`/`LABEL` conservés à l'intérieur. Vérifié :
  31 BLOCK/ENDBLK/INSERT appariés, EOF présent.

### Fond d'un seul tenant + numéros de lamelle (fait)
- **Planche connexe** : `geometry.h::boardEnvelopeAndGaps()` sépare l'**enveloppe** (un span
  min..max par tranche → un seul lobe reliant corps/anse/bec) et les **trous** (gaps entre spans
  d'une même tranche, ex. l'ouverture de l'anse). Enveloppe − trous = exactement la silhouette.
  `drawBoard` et `CCutPlan::build` construisent l'enveloppe (outer) + les trous de creux (+
  mortaises) et tessèlent/exportent. Vérifié : théière FOND = 1 lobe + 1 trou (anse) + mortaises.
- **Numéros de lamelle sur le fond** : `CCutPlan::Piece::Mark {x,y,n}` ; une marque par lamelle
  accrochée (à sa mortaise), normalisée avec la pièce. Export SVG (`<text>` bleu) et DXF (TEXT
  calque LABEL). Vérifié : théière → 25 numéros sur le fond.

### Filtre des petits îlots (fait)
- Contours détachés minuscules (ex. section du couvercle isolée) parasitaient le plan de coupe.
- `geometry.h` : `contourArea()` + `filterSmallIslands(contours, minArea)` — supprime les contours
  dont |aire| < seuil **en gardant toujours le plus grand** (le corps de la lamelle).
- `CCutPlan::Params::minIslandArea` (mm²) ; filtrage en phase 1 (contours mis à l'échelle) avant
  bbox/joint. `C3dView` : `m_minIslandArea` (unités modèle²) + `visibleContours()`, appliqué dans
  `drawSlices`/`jointContours`/`boardTabs`/`boardVSpans`/`backPlaneU`.
- UI : `m_minIsland` (« Aire mini îlot », mm², déf. 0 = off). `CMainWindow` convertit mm²→modèle²
  (`/scale²`) pour l'aperçu et passe `minIslandArea` à l'export. Vérifié : îlot #9 (aire 0.09)
  filtré dès seuil > 0.09.

### Théière de test refondue — union booléenne propre, bec + anse pleins (fait)
- L'ancienne théière (ellipsoïdes + bec + anse en **tubes fins ouverts qui s'auto-intersectaient**)
  donnait un maillage non étanche et des lamelles non accrochables.
- `tests/gen_teapot_stl.py` réécrit avec **trimesh** : corps + couvercle + bouton (ellipsoïdes) +
  **bec (tronc de cône plein, `frustum()` triangulé à la main, sans scipy)** + **anse (tore plein)**,
  le tout **fusionné par union booléenne** (`trimesh.boolean.union`, backend manifold3d) → un seul
  manifold étanche, sans surface interne (donc pas de contours en double au tranchage).
- Troncature **au centre** (plan z=0) via **intersection booléenne avec une boîte** (z≥0) : fond
  plat, chaque section atteint le fond avec une profondeur = épaisseur locale.
- Résultat (~4458 tris, watertight) : **découpable selon X**, seules ~quelques lamelles d'extrémité
  du bec/anse (slivers fins) flottent → gérées par le **filtre d'îlots**. Aucun avertissement « non
  étanche » sur X. Y/Z restent non plats par nature (fond unique en z).
- **Réglages géométrie actuels** (itérés visuellement) :
  - **Bec** : `frustum(0.40, 0.27, 1.60)` (allongé), bout **franc** (pas de pointe). Son capot est
    incliné (⊥ à l'axe du bec) → la troncature **clippe aussi en X** (`x_cut = maxX − 0.22`) pour
    terminer le bec par une **face verticale** : la dernière lamelle est une coupe pleine, pas un éclat.
  - **Anse** : tore `major=0.62, minor=0.20` translaté `(-1.15, 0.20, 0)`. Plus petite et **poussée
    vers −x** pour que sa boucle enferme du vide **hors du corps** → **trou de l'anse visible**, tout
    en restant fusionnée par son côté intérieur. Sommet plafonné ~y=1.0 (sous le couvercle).
  - ⚠ **`scale z ×1.8` ANNULÉ** : l'utilisateur l'a retiré (relief à l'épaisseur naturelle).
- **Dépendances** (nouvelles, pour ce script seulement) : `pip install trimesh manifold3d numpy`
  (trimesh déjà utilisé par `tests/svg2stl.py` ; manifold3d = backend booléen).

### Planche de fond — ponts bornés au recouvrement (escalier) (fait — « presque bon », à reprendre)
Trois défauts visuels de la planche (mode escalier) corrigés dans `geometry.h::buildSmoothLobes`,
partagé aperçu (`C3dView`) + export (`CCutPlan`) :
- **Vide trop large quand une tranche est sautée** : les positions d'axe encodent l'index réel
  (`index·(t+gap)`), mais seules les tranches **non vides** entrent dans le tableau. Un index sauté
  (modèle absent : bout du bec/anse) était enjambé par **un seul long palier** → vide 2× un gap.
  → param **`maxStep`** (= `pitch·1.5`) : au-delà, on ne ponte pas, la bande se ferme par une **face
  verticale** et une nouvelle repart.
- **Planche qui pointe dans le vide au-dessus d'une lamelle courte** : le palier de liaison était
  posé à la hauteur de la lamelle **précédente** ; près du bouton/anse (lamelle bien plus haute que
  sa voisine) la planche dépassait dans le gap. → le pont suit désormais le **recouvrement** des
  deux marches voisines (`top = min` des sommets, `bot = max` des bas) → jamais plus grand que la
  plus petite des deux voisines.
- **Planche qui déborde dans un trou** (ouverture de l'anse) : le pont par recouvrement, appliqué à
  un **trou**, le rétrécissait. → param **`holeMode`** : pour les trous le pont est **inversé**
  (`top = max`, `bot = min` = **union**) → le trou reste le plus ouvert possible.
- État : **« presque bon, suffisant pour le test »** côté utilisateur — il reste un léger débordement
  résiduel dans la zone de l'anse, **à reprendre plus tard**.

### Axe de coupe : Z retiré + planche toujours sur le plan X,Y (fait)
- **Axe Z supprimé de l'UI** : le combo « Axe de coupe » (`m_axisCombo`, `CMainWindow.ui`) ne propose
  plus que **X / Y** (un axe de coupe en Z n'a pas de sens pour un art mural). L'enum `CSlicer::Axis`
  garde `AxisZ` comme cas par défaut inoffensif (non sélectionnable).
- **Planche de fond toujours sur le plan X,Y** : `u` (profondeur, normale à la planche) = **toujours
  Z**. `CSlicer::project` : `AxisX → (u=Z, v=Y)` (inchangé) ; `AxisY → (u=Z, v=X)` (était `(u=X,
  v=Z)`, planche sur Y,Z). Inverse `C3dView::uvTo3D` mis à jour en conséquence (`case 1 : X=v, Y=axisPos,
  Z=u`). Le reste du pipeline travaille en `(u,v)` donc suit automatiquement (aperçu + export).

### svg2stl : mode `--fill` (volume plein) (fait)
- Besoin : à partir d'un SVG, obtenir un **volume PLEIN** (pas un dessin au trait en rubans creux)
  dans lequel **tailler des lamelles**.
- `tests/svg2stl.py` → option **`--fill`** : chaque path fermé devient un `Polygon`, combinés en
  **règle pair/impair** (`reduce(symmetric_difference)`) pour gérer d'éventuels trous, puis extrudé
  en profondeur. Le mode rubans (`buffer`) reste le défaut.
- **Étanchéité** : l'échantillonnage fin (40 pts/seg) laissait des points quasi colinéaires →
  triangles dégénérés dans les capots → maillage **non étanche**. Corrigé par un `simplify`
  automatique (tol = 0.02 % de la plus grande dim, réglable via `--simplify`) + nettoyage
  post-extrusion (`merge_vertices`, `nondegenerate_faces`, `unique_faces`, `fill_holes`).
- Résultat `tests/tornade.svg --fill --depth 30` → `tornade.stl` : 372×939×30 mm, **920 tris,
  watertight=True**. Prisme à fond plat (profondeur uniforme) → entièrement découpable selon X.
- ⚠ Dépendances supplémentaires de `svg2stl.py` : `svgpathtools`, `shapely` (en plus de `trimesh`,
  `numpy`). Installées dans le venv `/tmp/tpv` (qui a déjà trimesh/manifold3d/numpy).

### Outils de génération de STL de test
- `tests/gen_cube_stl.py` / `gen_sphere_stl.py` / `gen_teapot_stl.py` — modèles paramétriques.
- `tests/svg2stl.py` (nouveau) — **convertit un SVG au trait en STL extrudé** : échantillonne
  chaque path (`svgpathtools`, 40 pts/segment, dédoublonnage), épaissit les traits en rubans
  (`shapely` `buffer(width/2)`), fusionne (`unary_union`), extrude en profondeur (`trimesh
  extrude_polygon`), miroite l'axe Y (SVG = Y vers le bas) et recale l'origine en bas. Usage :
  `python3 tests/svg2stl.py in.svg out.stl --width 1.0 --depth 3.0` (largeur trait / profondeur
  en mm). Affiche dimensions, nb triangles, `watertight`. Dépendances : `numpy`, `svgpathtools`,
  `shapely`, `trimesh`. Exemple fourni : `tests/tornade.svg` → `tests/tornade.stl`.

### Relief 3D sur silhouette plate — `tests/bulge_stl.py` (fait — v1, à améliorer)
- Besoin : `tornade.stl` est un **prisme plat** (profondeur Z uniforme 30 mm) ; le trancher donne des
  lamelles toutes à la même profondeur (pas le bombé organique d'un vrai art mural). On veut donner du
  **relief 3D** à la silhouette avant de la trancher.
- `tests/bulge_stl.py` (nouveau) : lit un STL plat (extrusion d'un contour) et écrit un **nouveau** STL
  **dos plat (Z=0, côté mur) + face avant bombée** (coupole). `tornade.stl` reste intact.
  - Silhouette 2D = **union shapely des triangles projetés** (pas de `networkx`, donc on évite
    `trimesh.section`/`fix_normals` qui en dépendent dans le venv `/tmp/tpv`).
  - Distance au bord (`scipy.ndimage.distance_transform_edt`) → hauteur `z = A·√(1−(1−d/D)²)`
    (coupole en demi-ellipse : 0 sur le contour, A au cœur). `D` déf. = demi-épaisseur max.
  - Maillage fermé : **Delaunay** (`scipy.spatial`) des points intérieurs (grille) + contour
    rééchantillonné, triangles à centroïde hors silhouette jetés ; faces avant (CCW→+Z) + dos
    (winding inverse→−Z) + parois latérales (boucle de bord en CCW). Winding géré à la main.
  - Usage : `python3 tests/bulge_stl.py in.stl out.stl [--amp 80] [--radius D] [--res 4]`.
  - Résultat `tornade.stl --amp 80 --res 4` → **`tests/tornade_relief.stl`** : 372×939×**80 mm**,
    54 828 tris, **0 arête ouverte / 1 corps** (juste 4 arêtes degré-4 = 2 points de pincement du
    tourbillon, non-manifold mais sans effet sur le slicing/Blender).
  - ⚠ La `tornade.stl` étant un **bloc plein** (`svg2stl --fill`), on obtient **une seule grande
    coupole**, pas des tubes-par-bras comme la photo de réf. — inhérent à la forme de départ.
  - Dépendance ajoutée au venv `/tmp/tpv` pour l'aperçu : `matplotlib` (heightmap + profil de côté).
- **Décision utilisateur** : il va **tenter de faire mieux le relief sous Blender** (forme plus
  organique, tubes/nervures comme la photo) et reviendra ensuite. `bulge_stl.py` reste le fallback
  procédural. → reprendre quand il aura son STL Blender.

### Planche de fond — empreinte au fond + tenons proportionnels (fait, 2026-06-18)
Deux corrections côté accroche/silhouette de la planche, **partagées aperçu (`C3dView`) + export
(`CCutPlan`)** via `geometry.h` :
- **Empreinte au fond (pas le surplomb)** : sur un modèle à débord (`belly` de la tornade), la
  planche suivait la **silhouette complète** des lamelles (la plus large = le ventre qui bombe vers
  l'avant) au lieu du **dos plat**. Nouveau `geometry.h::backFootprintVSpan()` : l'étendue v d'un
  contour est limitée à la **bande arrière** `[u0, u0+tol]` (sommets dans la bande + intersections
  des arêtes avec `u=u0+tol` = le chord exact). Branché dans `C3dView::boardVSpans` (prend `u0`) et
  la boucle de spans de `CCutPlan::build`. Vérifié tornade : hauteur fond 290.4 mm < 301.8 mm de la
  silhouette (le surplomb tornade est surtout en profondeur Z, donc −4 % global, mais par colonne la
  planche colle au dos).
- **Tenons PROPORTIONNELS (largeur ~1/3 de la hauteur de contact)** : avant `kTabWidth=10` mm fixe
  → ridicule sur une grande lamelle. Désormais demi-largeur = `(kTabFrac·H)/2` bornée à
  `[kTabMinW=10, kTabMaxW=60]` mm. **Garde-fou** : au-delà de `kTwoTabMin=120` mm de contact →
  **2 tenons** écartés (anti-gauchissement), retombe à 1 s'ils se chevauchent. `tabCenters()`
  renvoie maintenant centre **+ demi-largeur** par tenon ; `integrateContourBack()` renvoie
  `vector<pair(centre, demi-largeur)>` et prend `mmToUnit` (= `1/scale` à l'aperçu, `1` à l'export)
  pour convertir les constantes mm. Les **mortaises** sont percées à la **même** demi-largeur que
  leur tenon (propagée des deux côtés). `kTabWidth` conservé = `kTabMinW` (plancher, compat).
  Vérifié tornade (60 tranches) : 63 tenons de **15.9 → 60.0 mm** (plafond atteint), 3 contours à
  2 tenons. Cube inchangé (H=20 → plancher 10 mm, 1 tenon) → non-régression `test_cutplan` verte.
- **Plan SVG/DXF « étiré en X » = échelle NON uniforme (corrigé)** : l'utilisateur veut
  lamelles + vides en **matière réelle** (ex. 5 mm chacun) ; l'objet **s'allonge en X** pour que
  tout rentre, et **Y/Z s'allongent du même ratio** (proportions conservées, juste plus grand).
  - Bug : `currentScale = t·n / taille_axe` n'incluait **pas le vide** → Y/Z étaient mis à l'échelle
    sur `t·n` alors que l'axe de coupe (planche, mortaises) couvrait `n·t+(n-1)·g` → **déformation**
    (X étiré vs Y/Z). (Une 1ʳᵉ tentative — « pitch=t, mortaise=t·t/(t+g) » — corrigeait la
    déformation mais réduisait la **matière** à 2,6 mm au lieu de 5 → mauvais sens, **annulée**.)
  - Fix : **échelle uniforme incluant le vide** — `CMainWindow::currentScale = (n·t+(n-1)·g)/taille_axe`,
    appliquée aux **3 axes**. `CCutPlan` revenu au pas réel `pitch = t+gap`, mortaise = **`t`**
    (matière). « Taille finale » = `taille_modèle × s` sur les 3 axes (= longueur assemblée sur
    l'axe). Label info → « Lamelle : t mm (matière) ».
  - Vérifié (`/tmp/test_uniform.cpp`, tornade, t=5/n=40) : ratio planche **X:Y ~2,62 constant** =
    ratio modèle (820:314=2,61) quel que soit le vide (0→12 mm) ; la planche grandit uniformément
    (197→668 mm), mortaises = 5 mm. Preview (proportions modèle) + SVG + DXF cohérents. `test_cutplan`
    et `test_board_footprint` verts.

### Mortaises du fond en bleu pointillé (SVG + DXF) (fait, 2026-06-18)
- `CCutPlan::Piece` : champ **`mortiseStart`** (index du 1er contour mortaise dans `contours`, −1 si
  aucune). Posé dans `build()` après les lobes+creux, avant la boucle des mortaises.
- **SVG** : les contours `>= mortiseStart` sortent en `stroke="#00a"` (bleu) + `stroke-dasharray="1.5,1"`
  (pointillés) au lieu de noir continu. Le reste (silhouette fond + lamelles) inchangé.
- **DXF** : nouvelle table `LTYPE` (`CONTINUOUS` + `DASHED` 2 mm/1 mm) + calque **`MORTAISE`**
  (couleur 5 = bleu, linetype `DASHED`). Les polylignes mortaises passent sur ce calque (héritent
  bleu+pointillé via BYLAYER) ; coupe sur `CUT`, étiquettes sur `LABEL`.
- Vérifié visuellement (rendu Inkscape du SVG) : fond = contour noir continu + mortaises bleu tireté.

### À améliorer plus tard (TODO)
- **Position du socle** : actuellement figée au plan arrière (u mini global). Beaucoup de lamelles
  d'un modèle organique (ex. bec/anse de la théière) seront « flottantes » → revoir : socle
  positionnable, ou tenons de longueur variable, ou plusieurs socles.
- Choix du **côté du socle** (Dessous / Arrière) exposé dans l'UI (pour l'instant codé « arrière »).
- ~~Affiner la **détection de contact** / tolérance, et le seuil **1 vs 2 tenons**.~~ → seuil 1 vs 2
  tenons revu (proportionnel, `kTwoTabMin=120`). Reste à affiner la **tolérance de contact** (`tol`).
- `attachTab()` : gère le cas générique (1 entrée / 1 sortie de bande). Cas tordus (rentrées
  multiples, contour très concave au dos) → tenon ignoré (lamelle comptée flottante). À durcir.
- Vérif visuelle réelle de la feature socle (display) : position, nombre de tenons, mortaises
  alignées avec les tenons à l'assemblage.
- ~~Avertissement `QWheelEvent::delta` toujours présent~~ → **corrigé** : `wheelEvent` utilise
  désormais `event->angleDelta().y()` (API non dépréciée Qt 5.15). Build sans aucun warning.

---

## Modèle 3D « tornade » paramétrique sous Blender — `tests/gen_tornade3d.py` (EN COURS)

> **REPRISE 2026-06-18 (RÉSOLU)** : épaisseur **arbitrée**. Comparé à la réf vidéo (frames
> `ffmpeg` `f_03`/`f_05` = relief profond, charnu) + rendu de 2 variantes lamelles : A=`0.66/34`
> (121 mm, +50 %) vs B=`0.58/22` (105 mm, +31 %). Les aperçus de FACE sont quasi identiques (la
> profondeur ne se lit pas sous l'angle 3/4 frontal du `--preview`) → décision sur chiffres+réf.
> **L'utilisateur a choisi A (`depth_ratio 0.66` + `belly 34.0`, 820×303×121 mm)** — déjà les
> valeurs en place, donc aucun changement de forme. **Épaisseur figée.**
> - Nettoyage fait : `PARAMS["curve_y"]` (inutilisé) **retiré**.
> - `tests/tornade3d.stl` **régénéré** (820×303×121, 1.2M) après suppression de `curve_y` — OK.

### But
Créer un **vrai modèle 3D paramétrique** de la sculpture murale « tornade » (et non plus une
silhouette 2D bombée comme `svg2stl.py` + `bulge_stl.py`). Référence = **`tornade.mp4`** (14 s, à la
racine du repo) : pièce **horizontale allongée**, relief mural **à dos plat**, faite de lamelles
verticales ; **deux lobes** bombés reliés par une **taille pincée et torsadée** (effet vortex) ;
lobe gauche plus gros, une **goutte** qui pend sous le lobe gauche, ça s'effile aux deux bouts.
(Frames d'analyse extraites via `ffmpeg -i tornade.mp4 -vf fps=... <jpg>` — la frame gros plan « f_04 »
est la plus parlante : dos plat au mur, bombé qui déborde, planches qui s'évasent à la taille.)

### Environnement
- **Blender 3.4.1** installé. API STL ancienne : `bpy.ops.import_mesh.stl` / `export_mesh.stl` (le
  script gère aussi la nouvelle `wm.stl_import/export`). Le script active `io_mesh_stl` si besoin.
- `trimesh`/`shapely` **absents** du Python système → on passe par le Python embarqué de Blender
  (numpy dispo). D'où le choix `bpy` et pas `trimesh` pour ce générateur.
- Lancement **headless** : `blender --background --python tests/gen_tornade3d.py -- <args>`.
- ⚠ Rendu : build Blender **sans OpenImageDenoiser** → `scene.cycles.use_denoising = False` (sinon
  `RuntimeError: Build without OpenImageDenoiser`). Caméra : `clip_end = 10000` (objet à ~1200-1300 u,
  far-clip défaut 1000 le rendait invisible → écran gris/noir au début, bug corrigé).

### Conventions de repère (alignées projet, cf. `bulge_stl.py` « dos plat Z=0 »)
- **X** = grande dimension horizontale = **axe de coupe** (lamelles verticales). Dans l'appli : coupe **X**.
- **Y** = hauteur (silhouette de face). **Z** = profondeur (hors du mur), **dos plat à Z=0**.
- Objet placé dans l'**octant positif** (min à l'origine), comme les autres STL du projet.

### Architecture du script
- `PARAMS` (dict en haut) = tout le « design ». Override en `--clé valeur`.
- `compute_profile(P)` : arrays longitudinaux le long de X + section unitaire. **Toute la forme est
  ici** :
  - **Lobes** = somme de 2 gaussiennes (`lobeL_*`, `lobeR_*`) → le **creux entre les deux = taille
    pincée** naturellement. `floor` = taille mini. `round` (<1) arrondit le sommet des lobes (sinon
    pics façon nœud papillon).
  - `hz` = demi-hauteur (Y), `hy` = demi-profondeur (Z) = `size*depth_ratio*env`.
  - **Torsion** `theta` = torsion globale (`twist`) + bouffée locale à la taille (`swirl`,
    `swirl_width`, via `tanh`) → le **vortex**.
  - `cz` = colonne (centre hauteur) : `tilt` + `waist_rise` − `drop` (goutte sous le lobe gauche).
  - `cd` = **centre de profondeur** = `belly*env` : pousse la section vers l'avant aux lobes → partie
    la plus large **devant le plan** après coupe → **surplomb/débord**.
- `section_ring(pr,i)` → points `(hauteur Y, profondeur Z)` de la section i, **tournée** par `theta`.
  Couper en Z **ne touche pas** la hauteur Y → la torsion reste visible comme un bombé avant qui
  tourne (sans manger la silhouette).
- `build_solid(P)` : loft des sections (rings + 2 apex) → volume plein étanche.
- `build_lamellae(P, n, gap)` : **aperçu du PRODUIT FINI** = N planches plates espacées (= ce que fera
  le slicer). Pour juger le vrai rendu, pas pour exporter le livrable.
- `make_object(verts, faces, flat_back=True)` : mesh + `remove_doubles` + **tronque au plan X,Y (Z=0)**
  via `bmesh.ops.bisect_plane(plane_no=(0,0,1), clear_inner=True)` + `holes_fill` (= **dos plat**) +
  recalc normales + **place en octant positif**. `--full` désactive le dos plat.
- `render_preview(obj, png)` : Cycles CPU, vue 3/4 de face (caméra +Z, légère contre-plongée),
  `view_transform='Standard'`, soleil avant-haut. **Best-effort** (juste pour comparer à la vidéo).
- Args : `out` (STL), `--preview <png>`, `--lamellae N`, `--gap f`, `--full`, + tous les `PARAMS`.

### État / ce qui marche
- STL généré OK, **dos plat**, dims ~**820 × 268 × 80 mm** (X × Y × Z).
- Aperçu **lamelles** (`--lamellae 70 --gap 0.45`) : **ressemble bien à la réf** (2 lobes, taille
  torsadée façon vortex, compo horizontale). Validé « pas mal » par l'utilisateur.
- Le solide brut seul paraît fade : **normal**, l'effet vient du tranchage → toujours juger via
  `--lamellae` (ou en tranchant le STL dans l'appli).

### Épaisseur finale + décision « sculptage à la main » (2026-06-18)
- **Épaisseur poussée 2 fois** à la demande : `depth_ratio` 0.66 → **0.80**, `belly` 34 → **55**.
  Dims finales **820 × 314 × 151 mm** (profondeur Z 121 → 151 mm). Solide livrable régénéré.
  Pour juger le surplomb : vue **de profil** (le `--preview` frontal ne le montre pas) — script
  jetable `/tmp/sideview.py` (caméra +X rasante, `clip_end` relevé) sur le STL lamellé.
- **Expérience d'asymétrie haut/bas ABANDONNÉE puis RETIRÉE du code** : tenté `top_gain`/`bot_gain`
  + 2e goutte + `hz_top`/`hz_bot` séparés (dessus lisse / dessous qui pend) pour coller à la réf →
  rendait la forme « moustache/manta » symétrique et trop haute, sans capturer l'**aile gauche
  effilée** ni la **virgule asymétrique** de la vidéo. Tous ces ajouts ont été **annulés**
  (générateur revenu à l'état lobes symétriques, `swirl 2.4`). `grep hz_top|bot_gain|drop2` = 0.
- **DÉCISION UTILISATEUR (actée)** : le générateur paramétrique est un **bon brouillon de forme**
  mais ne clonera pas la sculpture organique (aile/virgule/vortex) sans refonte lourde à rendement
  décroissant. → **L'utilisateur sculptera la forme fidèle à la MAIN sous Blender** ; côté Claude on
  **tranche son STL** et on gère tout le pipeline découpe/planche (qui marche sur n'importe quel STL).
  `gen_tornade3d.py` reste comme fallback procédural.
- **EN ATTENTE** : le STL Blender de l'utilisateur, à importer/trancher (axe X) dans l'appli.

### Reprise — commandes
```bash
# Aperçu produit fini (le bon juge visuel) — regarder /tmp/t_lam.png :
blender --background --python tests/gen_tornade3d.py -- /tmp/lam.stl \
        --lamellae 70 --gap 0.45 --preview /tmp/t_lam.png
# STL livrable (solide dos plat, à trancher dans l'appli) :
blender --background --python tests/gen_tornade3d.py -- tests/tornade3d.stl --preview /tmp/t_solid.png
```

### Prototype tornade par BOOLÉENS — `tests/proto_tornade_bool.py` (fait — v1, 2026-06-18)
- À partir de la silhouette plate `tests/tornade.stl`, recette booléenne : MOULE = silhouette
  épaissie en Z (cookie-cutter) ; RELIEF = union(silhouette fine + 2 ellipsoïdes lobes + ellipsoïde
  taille + goutte) ; RESULT = MOULE ∩ RELIEF (retaille au contour) ; **Twist** (Simple Deform, axe X,
  bande à la taille) = vortex ; **dos plat** (bisect Z=0) ; **Remesh voxel** + smooth. Sortie STL +
  aperçu **lamelles** (intersection avec un peigne de N boîtes).
- **Clé apprise** : les ellipsoïdes de lobe doivent être **assez grands pour couvrir la silhouette**
  (sinon l'intersection garde une « cacahuète » et perd le contour ailé). v1 OK : résultat
  927×369×187 mm (silhouette 939×372), 2 lobes + taille torsadée + dos plat, fidèle à la gestalt vidéo.
- Entièrement paramétrique (`--clé valeur`, dict `PARAMS`). À pousser : `twist_deg` (~130) pour un
  vortex plus marqué, bombé moindre aux extrémités (ailes/goutte), `voxel` plus fin (facettes).
- Décision : base pour que l'utilisateur fignole les primitives ; le pipeline découpe gère ce STL.

### TODO tornade3d
- ~~Vérifier la modif épaisseur+débord ; régler `depth_ratio`/`belly`.~~ → **FAIT, 0.80/55 figé.**
- ~~Nettoyage : `PARAMS["curve_y"]` plus utilisé → à retirer.~~ → **FAIT.**
- ~~Pousser la ressemblance fine (asymétrie aile/virgule).~~ → **ABANDONNÉ** : sculptage Blender à la
  main par l'utilisateur (cf. décision ci-dessus). Le paramétrique reste un brouillon.
- **EN ATTENTE du STL Blender utilisateur** → l'importer/trancher (axe X) dans l'appli, valider le
  plan de découpe réel (besoin display).
- Optionnel : peaufiner le cadrage caméra du `--preview` (ajouter une vue de profil ? cf.
  `/tmp/sideview.py`).
- `tests/gen_tornade3d.py` et `tests/tornade3d.stl` ne sont **pas commités** (untracked).

---

## Système PHOTO → LAMELLES — `tests/views2hull.py` (EN COURS, 2026-06-18)

### Idée / pourquoi
Plutôt que sculpter un solide 3D puis le trancher, **générer directement le solide-maître à partir
de 2 photos** de la sculpture de réf. Reformulation clé : une sculpture-lamelles = une pile de
**sections 2D (Y,Z)** le long de X, chaque section pilotée par quelques **courbes 1D de x** :
`top(x)`, `bot(x)` (silhouette hauteur), `depth(x)`, `belly(x)` (bombé), `twist(x)` (vortex).
Le solide se branche ensuite sur le pipeline existant (slicer/`CCutPlan`/aperçu) qui marche sur
n'importe quel STL.

**Discussion de fond (actée)** :
- L'extraction sur photo n'est pas un bricolage : `top(x)/bot(x)` = **min/max par colonne** d'un
  masque de silhouette. C'est l'entrée *naturelle* du modèle.
- Combiner deux vues malgré leurs perspectives : **ne PAS rectifier en 2D** ; aligner seulement le
  **paramètre 1D X** (les lamelles comptables = pierre de Rosette ; ou repères pointe/taille/pointe).
- **Photogrammétrie sur `tornade.mp4` ÉCARTÉE** : sujet quasi pire-cas (blanc mat sans texture,
  lamelles répétitives → faux appariements, lames fines auto-occultantes, vidéo compressée). En
  plus on reconstruirait l'objet *déjà lamellé* qu'on jette en re-tranchant. (1920×1080, 30 fps,
  419 frames, caméra en travelling pas en orbite.)
- **Visual hull / space carving** = le bon outil pour cette vidéo (silhouettes sur fond noir, immune
  à la texture/répétition, donne l'enveloppe lisse à trancher). La **méthode 2 vues ortho** retenue
  ici = un **visual hull dégénéré à 2 vues** (face + dessus), zéro calibration.

### Entrées
- `/home/julfab/Images/tornade.png` — **vue de FACE** (709×266, blanc sur mur gris clair).
- `/home/julfab/Images/tornade2.png` — **vue de DESSUS oblique** (804×226, bois clair sur fond sombre).
- `/home/julfab/Images/tornadeSP.png` — coupe annotée par l'utilisateur (réglages surplomb).
- Réf forme : `tornade.png` (2 lobes + taille pincée + effilement aux 2 bouts).

### Pipeline (`tests/views2hull.py`, dépend de numpy/scipy/PIL/matplotlib UNIQUEMENT —
pas de trimesh/cv2/skimage ; writer STL binaire + Otsu maison)
- `silhouette_face` : segmentation par **TEXTURE locale** (écart-type sur fenêtre 7px). Indispensable
  car blanc-sur-gris = trop peu de contraste pour un seuil de luminance ; les lamelles striées ont
  une forte variance locale, le mur lisse non. Puis closing/fill/largest-CC/opening.
- `silhouette_top` : seuil **luminance** (Otsu) — bois clair sur fond sombre, trivial.
- `extract_topbot` : `top(x)/bot(x)` = min/max par colonne du masque de face.
- `depth_from_top(beta=0.45, depth_max=170)` : l'épaisseur apparente oblique ≈ `a·hauteur + b·depth` ;
  la hauteur étant connue (face), on la **soustrait** (`Ln - beta·Hn`) pour **isoler `depth(x)`**,
  recalé sur la plage X normalisée (`resample_norm`). L'oblique non calibré ne donne que la **forme**
  → amplitude `depth_max` = **choix de design** (mm). Résultat : profil 2 lobes, pincé à la taille,
  effilé aux bouts.
- `build_sections(length, n, swirl=0.9)` : section (Y,Z) fermée, **DOS PLAT z=0** ; échelle uniforme
  (X et Y) `px2mm = length/(x1-x0)`. Front = **demi-ellipse** `D·√(1−((y−yc)/hy)²)` avec `D=depth(x)`,
  sommet `yc` **migré** par `swirl·tanh((i−waist)/sw)` → effet **vortex SANS rotation rigide**
  (la rotation rigide soulevait le dos — corrigé). Loft + capots → `write_stl` (STL binaire).
- `render_lamellae` : aperçu 3/4 matplotlib (Poly3DCollection).
- Conventions repère = projet : **X = axe de coupe (longueur)**, **Y = hauteur**, **Z = profondeur
  hors mur**, **dos plat à Z=0**.

### État / acquis
- Chaîne **2 photos → silhouette (face) + profondeur (dessus) → solide dos-plat → STL** fonctionnelle.
- **Dos plat** vérifié z=0 exact. Vortex par migration du sommet (pas de rotation).
- Dernier STL : **52 lamelles, épaisseur 15 mm, vide 15 mm** → longueur assemblée X = 1545 mm,
  dims **1545 × 468 × 170 mm**, échelle 2,23 mm/px → `/tmp/tornade_hull.stl` (5824 tris).

### Réglages « surplomb » — ESSAYÉS PUIS ANNULÉS (cf. `tornadeSP.png`)
L'utilisateur a fait reculer l'état jusqu'au **galbe ellipse pur**. Pistes testées et **retirées** :
- `front_z = max(overhang, galbe)` (socle plat mini) → rejeté = « surépaisseur du fond ».
- `D = max(depth, min_bulge)` (plancher d'amplitude) → trop, annulé.
- demi-ellipses évasées `×(1+overhang)` (déborde de la base) → ne correspondait pas au tracé.
- galbe en **2 demi-ellipses** (couvre toute la hauteur, supprime le plat) → ne correspondait pas
  au **trait rouge** non plus.
- « stade » (front plein + coins arrondis) → proposé, **pas appliqué** (interrompu).

### ⚠ PROBLÈME OUVERT (point de reprise)
Le galbe **ellipse unique + migration du sommet (swirl)** clippe à z=0 au-dessus de `yc+hy` → une
**bande PLATE sans épaisseur en haut** de certaines lamelles (vu sur la **lamelle n°4** : plat
y≈218..245). L'utilisateur veut qu'**à la place du plat, le galbe prenne la forme du trait rouge**
(remplir vers le haut), mais aucune des formes essayées ci-dessus ne collait à son rouge. **État figé
sur le galbe ellipse (plat présent)** en attendant **sa prochaine stratégie**.

### TODO photo→lamelles
- Caler la forme du haut du galbe sur le **trait rouge** de `tornadeSP.png` (stratégie utilisateur à venir).
- Brancher aussi `belly(x)` (centre de profondeur) si besoin ; `twist` quantifié depuis la vue de dessus.
- Trancher `/tmp/tornade_hull.stl` selon X dans l'appli → plan de découpe réel (besoin display).
- `tests/views2hull.py` **pas commité** (untracked) ; sorties dans `/tmp/`.

---

## Workflow « SVG par lamelle, sculpté à la main » (À FAIRE — repris sur l'autre poste, 2026-06-19)

### Idée (validée avec l'utilisateur)
Plutôt que sculpter un solide 3D, l'utilisateur **modèle chaque section 2D à la main** en SVG, avec
les voisines en référence (**onion-skinning**). Aller-retour :
1. **Moi** : échantillonner un STL source en **52 sections** le long de l'axe **X** → produire
   **52 SVG** (un par lamelle), chacun avec **3 calques verrouillés ghost** :
   - `lamelle-precedente` (i-1) — référence, verrouillée
   - `lamelle-courante` (i) — **le seul déverrouillé, à éditer**
   - `lamelle-suivante` (i+1) — référence, verrouillée
   - (la #1 sans précédente, la #52 sans suivante).
2. **Utilisateur** : édite à la main le contour du calque `lamelle-courante` de chaque SVG.
3. **Moi** : relis le calque `lamelle-courante` de chaque SVG, extrude (5 mm) + vide (5 mm),
   loft/empile → **STL dos plat**, à trancher ensuite dans l'appli (axe X).

### Décisions actées (via questions du 2026-06-19)
- **Géométrie** : **52 lamelles**, épaisseur **5 mm**, vide **5 mm**.
- **Source des sections de départ** : **régénérer le hull `views2hull.py`** (choix utilisateur),
  repli possible sur `tests/tornade3d.stl` (déjà sur disque) si le hull n'est pas régénérable.
- **Orientation SVG** : **Y vertical** (hauteur), **Z horizontal** (profondeur hors mur),
  **dos plat Z=0 sur la ligne du bas**. Unités **mm**.
- **Voisines** : **ghost sur calques Inkscape verrouillés** (gris clair, non éditables par erreur).

### Contraintes de l'aller-retour (à respecter en éditant les SVG)
- Garder les **chemins fermés** (sinon pas d'extrusion).
- Ne pas renommer/supprimer le calque `lamelle-courante` (c'est ce que je relis).
- Le **dos plat (Z=0)** = ligne de référence du bas, à ne pas déplacer sauf intention.

### ⚠ POURQUOI C'EST EN PAUSE — bloqueurs sur le poste actuel (`/home/corentin`)
La régénération du hull est **impossible ici** :
- **Photos manquantes** : `views2hull.py` attend `/home/julfab/Images/tornade.png` (face),
  `tornade2.png` (**dessus oblique**), `tornadeSP.png` (coupe annotée). Ce home (`/home/julfab`)
  n'existe pas sur ce poste. Trouvé seulement la **vue de FACE** : `/home/corentin/Images/tornade.png`
  et `/home/corentin/perso/tornade.png` (+ `tornade_preview.png`). **Pas de `tornade2.png` (dessus)
  ni de `tornadeSP.png`** → impossible de reconstruire le profil de profondeur `depth(x)`.
- **scipy absent** du Python système (`ModuleNotFoundError: No module named 'scipy'`). numpy présent.
  Le venv `/tmp/tpv` n'est qu'un **symlink cassé** vers `/usr/bin/python3` (pas un vrai venv), et
  `/tmp` est régulièrement purgé (le `/tmp/tornade_hull.stl` d'hier a disparu).

### Reprise sur l'autre poste (qui a tous les outils)
- Récupérer/poser les **3 photos** là où `views2hull.py` les attend (ou ajuster les chemins).
- Avoir **numpy + scipy + PIL + matplotlib** dans un vrai venv.
- Régénérer le hull (`python3 tests/views2hull.py …`), puis me demander de produire les 52 SVG
  selon les décisions ci-dessus. Repli : `tests/tornade3d.stl` si on saute le hull.
- Rappel : `tests/views2hull.py` est **untracked** (pas commité).

### Étape 1 LIVRÉE — `tests/stl2lamelles_svg.py` (2026-06-21, sur le poste `/home/julfab`)
Poste complet retrouvé : 3 photos dans `/home/julfab/Images`, scipy+numpy+PIL+matplotlib OK,
Blender 3.4.1, **Inkscape** dispo. Bloqueurs levés.
- **Hull régénéré** : `python3 tests/views2hull.py` → `/tmp/tornade_hull.stl`, copié stable en
  **`tests/tornade_hull.stl`** (6720 tris, **820×249×170 mm**, dos plat z=0). Aperçu `/tmp/hull_preview.png`
  conforme (2 lobes + taille pincée).
- **Nouveau script `tests/stl2lamelles_svg.py`** (numpy seul) : lit un STL binaire, le tranche en N
  plans X (centres de N tranches égales, évite les capots des bouts), raccorde les segments en boucles
  fermées (y,z), puis écrit 1 SVG/lamelle avec **3 calques** : `lamelle-precedente` (ghost rouge
  pointillé, **verrouillé** `sodipodi:insensitive`), `lamelle-suivante` (ghost bleu pointillé,
  **verrouillé**), `lamelle-courante` (contour noir + remplissage gris léger, **éditable**) + un repère
  pointillé du **dos plat Z=0**. **Cadre commun** (même viewBox) sur toutes les lamelles → onion-skin
  exact. Unités mm.
- **52 SVG produits** → `tests/lamelles/lamelle_01..52.svg`. Cadre commun W(z)=193 × H(y)=273 mm,
  z∈[0,169], 1 boucle propre/section (90-110 pts). Vérifié visuellement (`inkscape … --export-type=png`
  sur la lamelle 26) : courante + voisines ghost se superposent bien, dos plat = bord **gauche** (Z=0).
- ⚠ **À confirmer avec l'utilisateur** : orientation. Le plan disait « Y vertical, Z horizontal »
  (respecté) **mais aussi** « dos plat sur la ligne du bas » → contradictoire. Rendu actuel = dos plat
  sur le **bord gauche vertical**. Bascule = 1 flag (`to_svg`) si l'utilisateur veut le dos en bas.
- **Source = hull** ; repli `tests/tornade3d.stl` non utilisé (le hull marche).
- **`tests/refresh_ghosts.py`** (stdlib xml only) : après édition manuelle des calques `lamelle-courante`,
  reconstruit dans chaque SVG les ghosts `lamelle-precedente`/`lamelle-suivante` à partir des contours
  courants **à jour** des voisines (les ghosts du générateur sont des photos figées du hull). Ne touche
  JAMAIS la courante ; ghosts recréés verrouillés ; préserve namedview/defs/transforms Inkscape ;
  édition sur place. Testé : édition simulée sur la 26 → bien propagée au ghost `precedente` de la 27.
- ⚠ Limite ghosts : ils ne se ré-affichent pas à la volée pendant l'édition ; relancer
  `python3 tests/refresh_ghosts.py` entre deux passes pour recaler les voisines.
- Untracked à éventuellement commiter : `tests/stl2lamelles_svg.py`, `tests/refresh_ghosts.py`,
  `tests/tornade_hull.stl`, `tests/lamelles/`, `tests/views2hull.py`.

### Étape 3 LIVRÉE — `tests/lamelles_svg2stl.py` (2026-06-21)
Reconstruit le STL depuis les SVG édités. Relit **uniquement** le calque `lamelle-courante` de chaque
`lamelle_*.svg` (ghosts ignorés), garde la plus grande boucle (jette la `<line>` repère + sous-chemins
dégénérés), extrude chaque lamelle de **5 mm** avec **5 mm** de vide, empile le long de X → **52 planches
disjointes** (= l'objet fini), dos plat, octant positif, mm.
- **Parseur de chemin SVG maison** (pas de lib dispo) : M/L/H/V/C/S/Q/T/Z, abs+rel, Béziers
  échantillonnés (16 seg), transforms matrix/translate/scale/rotate. Inverse du mapping générateur :
  `Z = x−PAD`, `Y = (vbH−PAD)−y` (PAD=12, vbH lu du viewBox), puis recalage commun Y,Z ≥ 0.
- **Capots** triangulés par ear-clipping (gère le concave) ; parois latérales en quads.
- Vérifié : Inkscape réécrit les contours édités en **bspline→Bézier cubiques** (`C`) ; le parseur les
  reproduit **fidèlement** (comparé rendu Inkscape lamelle_03 vs section reconstruite = identiques).
- 1er run sur les 52 (4 éditées 01-04 + 48 hull) : `tests/tornade_lamelles.stl`, 22984 tris,
  **515×249×169 mm**. Aperçu `/tmp/lam_stl.png` OK (2 lobes + taille).
- **CHANGEMENT (demande user)** : la sortie par défaut est désormais le **SOLIDE PLEIN lofté** (le
  « volume 3D total »), pas les planches — l'utilisateur lamellera plus tard dans l'appli. `--planks`
  garde le mode 52 planches disjointes.
- **Loft anti-vrillage par RIBS (correctif clé)** : 1re version (resampling longueur d'arc + ancre
  coin bas-dos) **vrillait** salement (hélices/pincements, surtout au bout effilé — cf. user). Cause =
  mauvaise correspondance des points entre sections. Refait par **reparamétrage par HAUTEUR** (`ribs`,
  R=90) : à chaque niveau Y, Z arrière (min) + Z avant (max) → boucle structurée 2R pts, correspondance
  parfaite section-à-section, **impossible à vriller**, et marche quel que soit le nb de nœuds du SVG.
  → L'utilisateur peut dessiner librement en Bézier, AUCUNE contrainte de nb/ordre de points.
  Vérifié au rendu (bout effilé propre). Reste le facettage en X = résolution 52 sections (normal).
- `tests/tornade_lamelles.stl` (solide) : 16636 tris, **820×249×169 mm**.
- **`tests/make_blend.py`** : `blender --background --python tests/make_blend.py -- STL OUT.blend`.
  Importe le STL, redresse (modèle Y=hauteur → Blender Z=haut), matériau bois, caméra **ORTHO** cadrée
  (vue face légèrement plongeante, `clip_end=span*10` sinon objet hors far-clip), soleil, vue 3D
  sauvegardée en CAMERA + shading MATERIAL. Centre/dims calculés depuis les **sommets** (la bbox est
  périmée après transform_apply) + **shade smooth** (affichage lisse, STL reste facetté). Sortie :
  **`tests/tornade_lamelles.blend`** (2,4 Mo), vérifié au rendu EEVEE = volume plein lisse conforme
  (2 lobes + taille torsadée, bout effilé propre).
- Untracked : `tests/lamelles_svg2stl.py`, `tests/make_blend.py`, `tests/tornade_lamelles.stl`,
  `tests/tornade_lamelles.blend`.

**TODO suite** : l'utilisateur finit d'éditer les 52 calques `lamelle-courante` (4/52 faits) ;
relancer `refresh_ghosts.py` entre passes ; puis `lamelles_svg2stl.py` → STL final → trancher dans
l'appli (axe X) pour le plan de découpe réel (besoin display).
