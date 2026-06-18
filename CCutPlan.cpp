//-----------------------------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QtDebug>
#include "CCutPlan.h"
//-----------------------------------------------------------------------------------------------
bool CCutPlan::build(const std::vector<CSlice> &slices, const Params &params) {
    m_params = params;
    m_pieces.clear();
    m_floating.clear();
    m_sheetCount = 0;

    const float s   = params.scale > 0 ? params.scale : 1.0f;
    const float big = std::numeric_limits<float>::max();

    // 1) Contours mis a l'echelle (NON normalises) + bbox global, pour pouvoir greffer tenons et
    //    placer le socle dans un repere commun avant de normaliser chaque piece.
    struct Raw { int index; std::vector<Contour> contours; float minx, miny, maxx, maxy; };
    std::vector<Raw> raws;
    float gMinX = big, gMinY = big, gMaxY = -big;

    for (size_t i = 0; i < slices.size(); i++) {
        const CSlice &sl = slices[i];
        if (sl.contours.empty())
            continue;

        Raw r;
        r.index = sl.index;
        r.minx = big; r.miny = big; r.maxx = -big; r.maxy = -big;
        r.contours.resize(sl.contours.size());
        for (size_t c = 0; c < sl.contours.size(); c++) {
            r.contours[c].reserve(sl.contours[c].size());
            for (size_t k = 0; k < sl.contours[c].size(); k++)
                r.contours[c].push_back(SPoint2(sl.contours[c][k].x * s, sl.contours[c][k].y * s));
        }

        // Filtre les petits ilots (mm^2, contours mis a l'echelle) avant tout calcul de bbox.
        filterSmallIslands(r.contours, params.minIslandArea);
        if (r.contours.empty())
            continue;

        for (size_t c = 0; c < r.contours.size(); c++)
            for (size_t k = 0; k < r.contours[c].size(); k++) {
                const float x = r.contours[c][k].x, y = r.contours[c][k].y;
                r.minx = std::min(r.minx, x); r.maxx = std::max(r.maxx, x);
                r.miny = std::min(r.miny, y); r.maxy = std::max(r.maxy, y);
            }
        if (r.maxx < r.minx)
            continue;
        gMinX = std::min(gMinX, r.minx);
        gMinY = std::min(gMinY, r.miny);
        gMaxY = std::max(gMaxY, r.maxy);
        raws.push_back(r);
    }

    if (raws.empty())
        return false;

    // 2) Planche de fond optionnelle : greffe les tenons sur les lamelles en contact et collecte
    //    les mortaises correspondantes. Le socle est perpendiculaire aux lamelles (le long de
    //    l'axe de coupe), au plan arriere (x mini = bord "Z min" de la section).
    Piece board; bool haveBoard = false;
    if (params.generateBoard) {
        const float t    = params.sliceThickness > 0 ? params.sliceThickness : 10.0f;
        const float gap  = params.gapThickness;
        const float tol  = std::max(t, 1.0f);     // tolerance de contact avec le plan du fond
        // Pas reel d'assemblage = lamelle + vide. L'echelle (params.scale) inclut deja le vide
        // (cf. CMainWindow::currentScale) et s'applique uniformement a Y/Z -> proportions conservees,
        // la planche fait n*(t+gap) en X sans deformation (juste plus grande). Mortaise = t (matiere).
        const float pitch = t + gap;

        struct Mortise { float x, y, hw; };               // centre axe, centre v, demi-largeur v
        std::vector<Mortise> mortises;
        std::vector<Mark> marks;                           // numero de lamelle sur le fond

        for (size_t i = 0; i < raws.size(); i++) {
            Raw &r = raws[i];

            // Aucun contour n'atteint le plan du socle -> lamelle flottante.
            if (r.minx > gMinX + tol) { m_floating.push_back(r.index); continue; }

            // Chaque contour EXTERIEUR (corps, anse, bec...) atteignant le fond est rabote de t et
            // recoit ses tenons proportionnels ; chaque tenon donne une mortaise (meme taille) sur
            // le socle (meme position axe). Export en mm -> mmToUnit = 1.
            const float xPosBoard = (r.index + 0.5f) * pitch;
            const std::vector<char> outer = outerContourMask(r.contours);
            int placed = 0;
            float firstV = 0.0f; bool haveV = false;
            for (size_t c = 0; c < r.contours.size(); c++) {
                if (!outer[c]) continue;
                const std::vector<std::pair<float, float> > got =
                    BoardJoint::integrateContourBack(r.contours[c], gMinX, t, tol, 1.0f);
                for (size_t m = 0; m < got.size(); m++) {
                    Mortise mo; mo.x = xPosBoard; mo.y = got[m].first; mo.hw = got[m].second;
                    mortises.push_back(mo);
                    if (!haveV) { firstV = got[m].first; haveV = true; }
                }
                placed += int(got.size());
            }
            if (placed == 0) m_floating.push_back(r.index);
            else { Mark mk; mk.x = xPosBoard; mk.y = firstV; mk.n = r.index; marks.push_back(mk); }
        }

        // Construit la piece socle : contour(s) LISSE(S) suivant la silhouette du modele (lobes
        // inscrits, sans decroche, jamais plus larges/hauts que le modele), perces des mortaises.
        const float H = gMaxY - gMinY;
        if (H > 0.0f && !mortises.empty()) {
            // Spans v (contours exterieurs) par tranche, triees par index, en plan (axe, v).
            std::vector<Raw*> sorted;
            for (size_t i = 0; i < raws.size(); i++) sorted.push_back(&raws[i]);
            std::sort(sorted.begin(), sorted.end(),
                      [](const Raw *a, const Raw *b){ return a->index < b->index; });

            std::vector<std::pair<float, std::vector<std::pair<float, float> > > > spansBySlice;
            for (size_t i = 0; i < sorted.size(); i++) {
                const Raw *r = sorted[i];
                const std::vector<char> outer = outerContourMask(r->contours);
                std::vector<std::pair<float, float> > spans;
                for (size_t c = 0; c < r->contours.size(); c++) {
                    if (!outer[c]) continue;
                    // Empreinte AU FOND (bande arriere [gMinX, gMinX+tol]) et non la silhouette
                    // debordante : la planche suit le dos plat, pas le surplomb (ventre).
                    float vmin, vmax;
                    if (backFootprintVSpan(r->contours[c], gMinX, tol, vmin, vmax))
                        spans.push_back(std::make_pair(vmin, vmax));
                }
                if (!spans.empty())
                    spansBySlice.push_back(std::make_pair((r->index + 0.5f) * pitch, spans));
            }

            // Fond connexe (enveloppe + trous de creux). Lisse = liaisons en diagonale ; escalier =
            // marches plates larges de la lamelle (t) -> decrochés, sans depasser les lamelles.
            std::vector<std::pair<float, std::vector<std::pair<float, float> > > > env, gaps;
            boardEnvelopeAndGaps(spansBySlice, env, gaps);
            const float treadHW = params.boardSmooth ? 0.0f : t * 0.5f;
            const float maxStep = pitch * 1.5f;       // au-dela = tranche sautee -> on ne ponte pas
            std::vector<Contour> lobes    = buildSmoothLobes(env,  pitch * 0.5f, treadHW, maxStep, false);
            std::vector<Contour> gapHoles = buildSmoothLobes(gaps, pitch * 0.5f, treadHW, maxStep, true);

          if (!lobes.empty()) {
            board.sliceIndex = -1;
            board.tx = board.ty = 0; board.sheet = 0;
            board.marks = marks;
            for (size_t i = 0; i < lobes.size(); i++)    board.contours.push_back(lobes[i]);
            for (size_t i = 0; i < gapHoles.size(); i++) board.contours.push_back(gapHoles[i]);

            board.mortiseStart = int(board.contours.size());   // les contours suivants = mortaises
            const float halfT = t * 0.5f;   // largeur mortaise = epaisseur matiere de la lamelle
            for (size_t m = 0; m < mortises.size(); m++) {
                const float xc = mortises[m].x;
                const float yc = mortises[m].y;
                const float hw = mortises[m].hw;       // meme demi-largeur que le tenon
                Contour hole;
                hole.push_back(SPoint2(xc - halfT, yc - hw));
                hole.push_back(SPoint2(xc + halfT, yc - hw));
                hole.push_back(SPoint2(xc + halfT, yc + hw));
                hole.push_back(SPoint2(xc - halfT, yc + hw));
                board.contours.push_back(hole);
            }

            // Normalise la piece socle (bbox -> origine) et fixe w,h pour le nesting.
            float bminx = big, bminy = big, bmaxx = -big, bmaxy = -big;
            for (size_t c = 0; c < board.contours.size(); c++)
                for (size_t k = 0; k < board.contours[c].size(); k++) {
                    bminx = std::min(bminx, board.contours[c][k].x);
                    bmaxx = std::max(bmaxx, board.contours[c][k].x);
                    bminy = std::min(bminy, board.contours[c][k].y);
                    bmaxy = std::max(bmaxy, board.contours[c][k].y);
                }
            for (size_t c = 0; c < board.contours.size(); c++)
                for (size_t k = 0; k < board.contours[c].size(); k++) {
                    board.contours[c][k].x -= bminx;
                    board.contours[c][k].y -= bminy;
                }
            for (size_t i = 0; i < board.marks.size(); i++) {
                board.marks[i].x -= bminx;
                board.marks[i].y -= bminy;
            }
            board.w = bmaxx - bminx; board.h = bmaxy - bminy;
            haveBoard = true;
          }
        }
    }

    // 3) Normalise chaque tranche (bbox min, tenons inclus, ramenee a l'origine) -> Piece.
    for (size_t i = 0; i < raws.size(); i++) {
        Raw &r = raws[i];
        float minx = big, miny = big, maxx = -big, maxy = -big;
        for (size_t c = 0; c < r.contours.size(); c++)
            for (size_t k = 0; k < r.contours[c].size(); k++) {
                minx = std::min(minx, r.contours[c][k].x); maxx = std::max(maxx, r.contours[c][k].x);
                miny = std::min(miny, r.contours[c][k].y); maxy = std::max(maxy, r.contours[c][k].y);
            }
        Piece p;
        p.sliceIndex = r.index;
        p.w = maxx - minx; p.h = maxy - miny;
        p.tx = p.ty = 0; p.sheet = 0;
        p.contours.resize(r.contours.size());
        for (size_t c = 0; c < r.contours.size(); c++) {
            p.contours[c].reserve(r.contours[c].size());
            for (size_t k = 0; k < r.contours[c].size(); k++)
                p.contours[c].push_back(SPoint2(r.contours[c][k].x - minx,
                                                r.contours[c][k].y - miny));
        }
        m_pieces.push_back(p);
    }
    if (haveBoard)
        m_pieces.push_back(board);

    if (m_pieces.empty())
        return false;

    // 4) Nesting par rangees (shelf) : tri par hauteur decroissante, remplissage gauche->droite,
    //    retour a la ligne au depassement, nouvelle feuille quand la hauteur est pleine.
    std::vector<int> order(m_pieces.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return m_pieces[a].h > m_pieces[b].h;
    });

    const float m = params.margin;
    const float gap = params.spacing;
    int sheet = 0;
    float cx = m, cy = m, shelfH = 0;

    for (size_t o = 0; o < order.size(); o++) {
        Piece &p = m_pieces[order[o]];

        if (p.w > params.sheetW - 2 * m || p.h > params.sheetH - 2 * m)
            qWarning() << "CCutPlan: piece" << p.sliceIndex << "plus grande que la feuille utile"
                       << "(" << p.w << "x" << p.h << "mm) - debordement possible.";

        // Retour a la ligne si la piece ne tient pas sur la rangee courante (rangee non vide).
        if (cx > m && cx + p.w > params.sheetW - m) {
            cx = m;
            cy += shelfH + gap;
            shelfH = 0;
        }
        // Nouvelle feuille si la piece ne tient pas verticalement.
        if (cy + p.h > params.sheetH - m) {
            sheet++;
            cx = m; cy = m; shelfH = 0;
        }

        p.sheet = sheet;
        p.tx = cx;
        p.ty = cy;

        cx += p.w + gap;
        shelfH = std::max(shelfH, p.h);
    }

    m_sheetCount = sheet + 1;
    return true;
}
//-----------------------------------------------------------------------------------------------
bool CCutPlan::exportSVG(const QString &basePath) const {
    if (m_pieces.empty()) return false;
    bool ok = true;
    for (int s = 0; s < m_sheetCount; s++)
        ok = writeSVGSheet(QString("%1_sheet%2.svg").arg(basePath).arg(s + 1), s) && ok;
    return ok;
}
//-----------------------------------------------------------------------------------------------
bool CCutPlan::exportDXF(const QString &basePath) const {
    if (m_pieces.empty()) return false;
    bool ok = true;
    for (int s = 0; s < m_sheetCount; s++)
        ok = writeDXFSheet(QString("%1_sheet%2.dxf").arg(basePath).arg(s + 1), s) && ok;
    return ok;
}
//-----------------------------------------------------------------------------------------------
// SVG en unites mm. Le groupe principal est retourne (translate+scale y) pour passer en repere
// y-haut (comme le physique / le DXF) ; les etiquettes sont posees hors de ce groupe pour ne pas
// etre miroitees.
bool CCutPlan::writeSVGSheet(const QString &path, int sheet) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "CCutPlan: ecriture SVG impossible" << path;
        return false;
    }
    QTextStream ts(&f);
    const float W = m_params.sheetW, H = m_params.sheetH;

    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ts << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "width=\"" << W << "mm\" height=\"" << H << "mm\" "
       << "viewBox=\"0 0 " << W << " " << H << "\">\n";

    // Bordure de feuille. Le miroir Y (repere physique y-haut -> SVG y-bas) est applique point par
    // point (y -> H - y) plutot que par un groupe scale(1,-1), pour ne pas miroiter les textes.
    ts << "  <rect x=\"0\" y=\"0\" width=\"" << W << "\" height=\"" << H
       << "\" fill=\"none\" stroke=\"#888\" stroke-width=\"0.2\"/>\n";

    // Un groupe SVG par piece : <g id="lamelle-N"> ou <g id="fond"> (contours + etiquettes).
    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;

        const QString gid = (p.sliceIndex < 0) ? QString("fond")
                                               : QString("lamelle-%1").arg(p.sliceIndex);
        ts << "  <g id=\"" << gid << "\">\n";

        for (size_t c = 0; c < p.contours.size(); c++) {
            const bool mortaise = (p.mortiseStart >= 0 && int(c) >= p.mortiseStart);
            ts << "    <polygon points=\"";
            for (size_t k = 0; k < p.contours[c].size(); k++)
                ts << (p.tx + p.contours[c][k].x) << "," << (H - (p.ty + p.contours[c][k].y)) << " ";
            if (mortaise)   // mortaises du fond : bleu + pointilles (pas le contour de coupe)
                ts << "\" fill=\"none\" stroke=\"#00a\" stroke-width=\"0.1\" "
                   << "stroke-dasharray=\"1.5,1\"/>\n";
            else
                ts << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.1\"/>\n";
        }

        const float cx = p.tx + p.w * 0.5f;
        const float cy = p.ty + p.h * 0.5f;
        const float fs = std::min(4.0f, std::max(1.0f, p.h * 0.3f));
        const QString label = (p.sliceIndex < 0) ? QString("FOND") : QString::number(p.sliceIndex);
        ts << "    <text x=\"" << cx << "\" y=\"" << (H - cy)
           << "\" font-size=\"" << fs << "\" text-anchor=\"middle\" "
           << "dominant-baseline=\"central\" fill=\"#c00\">" << label << "</text>\n";

        // Numeros de lamelle sur le fond (pres de chaque mortaise).
        for (size_t m = 0; m < p.marks.size(); m++) {
            const float mx = p.tx + p.marks[m].x;
            const float my = p.ty + p.marks[m].y;
            ts << "    <text x=\"" << mx << "\" y=\"" << (H - my)
               << "\" font-size=\"3\" text-anchor=\"middle\" "
               << "dominant-baseline=\"central\" fill=\"#00a\">" << p.marks[m].n << "</text>\n";
        }

        ts << "  </g>\n";
    }

    ts << "</svg>\n";
    f.close();
    return true;
}
//-----------------------------------------------------------------------------------------------
// DXF R12 ASCII minimal : calques CUT/LABEL + un BLOC par piece (lamelle-N / fond), insere une
// fois -> chaque piece est un groupe selectionnable (analogue aux <g> du SVG), les calques
// CUT/LABEL restant a l'interieur. Pas de LWPOLYLINE (R14+), pour compat large laser/CNC.
bool CCutPlan::writeDXFSheet(const QString &path, int sheet) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "CCutPlan: ecriture DXF impossible" << path;
        return false;
    }
    QTextStream ts(&f);

    // Paire (code, valeur) DXF.
    #define DXF(code, val) ts << code << "\n" << val << "\n"

    auto blockName = [](const Piece &p) {
        return (p.sliceIndex < 0) ? QString("fond") : QString("lamelle-%1").arg(p.sliceIndex);
    };
    // Entites d'une piece (coordonnees absolues, calques CUT/LABEL).
    auto emitPiece = [&](const Piece &p) {
        for (size_t c = 0; c < p.contours.size(); c++) {
            // Mortaises du fond sur calque MORTAISE (bleu pointille via la def de calque/LTYPE) ;
            // le reste (contours de coupe) sur CUT.
            const char *lay = (p.mortiseStart >= 0 && int(c) >= p.mortiseStart) ? "MORTAISE" : "CUT";
            DXF(0, "POLYLINE"); DXF(8, lay); DXF(66, 1); DXF(70, 1); // 1 = fermee
            for (size_t k = 0; k < p.contours[c].size(); k++) {
                DXF(0, "VERTEX"); DXF(8, lay);
                DXF(10, (p.tx + p.contours[c][k].x));
                DXF(20, (p.ty + p.contours[c][k].y));
            }
            DXF(0, "SEQEND"); DXF(8, lay);
        }
        const float cx = p.tx + p.w * 0.5f;
        const float cy = p.ty + p.h * 0.5f;
        const float fs = std::min(4.0f, std::max(1.0f, p.h * 0.3f));
        const QString label = (p.sliceIndex < 0) ? QString("FOND") : QString::number(p.sliceIndex);
        DXF(0, "TEXT"); DXF(8, "LABEL");
        DXF(10, cx); DXF(20, cy); DXF(40, fs);
        DXF(50, 0); DXF(72, 1); DXF(73, 2);   // alignement horizontal centre / vertical milieu
        DXF(11, cx); DXF(21, cy);             // point d'alignement (requis si 72/73 != 0)
        DXF(1, label);
        for (size_t m = 0; m < p.marks.size(); m++) {   // numeros de lamelle sur le fond
            const float mx = p.tx + p.marks[m].x;
            const float my = p.ty + p.marks[m].y;
            DXF(0, "TEXT"); DXF(8, "LABEL");
            DXF(10, mx); DXF(20, my); DXF(40, 3.0);
            DXF(50, 0); DXF(72, 1); DXF(73, 2);
            DXF(11, mx); DXF(21, my);
            DXF(1, p.marks[m].n);
        }
    };

    DXF(0, "SECTION"); DXF(2, "HEADER");
    DXF(9, "$ACADVER"); DXF(1, "AC1009");
    DXF(0, "ENDSEC");

    DXF(0, "SECTION"); DXF(2, "TABLES");
    // Types de ligne : CONTINUOUS (coupe) + DASHED (mortaises). Motif DASHED = 2 mm trait, 1 mm vide.
    DXF(0, "TABLE"); DXF(2, "LTYPE"); DXF(70, 2);
    DXF(0, "LTYPE"); DXF(2, "CONTINUOUS"); DXF(70, 0); DXF(3, "Solid line"); DXF(72, 65); DXF(73, 0); DXF(40, 0.0);
    DXF(0, "LTYPE"); DXF(2, "DASHED"); DXF(70, 0); DXF(3, "Dashed __ __"); DXF(72, 65); DXF(73, 2); DXF(40, 3.0); DXF(49, 2.0); DXF(49, -1.0);
    DXF(0, "ENDTAB");
    DXF(0, "TABLE"); DXF(2, "LAYER"); DXF(70, 3);
    DXF(0, "LAYER"); DXF(2, "CUT");      DXF(70, 0); DXF(62, 7); DXF(6, "CONTINUOUS");
    DXF(0, "LAYER"); DXF(2, "LABEL");    DXF(70, 0); DXF(62, 1); DXF(6, "CONTINUOUS");
    DXF(0, "LAYER"); DXF(2, "MORTAISE"); DXF(70, 0); DXF(62, 5); DXF(6, "DASHED");  // 5 = bleu
    DXF(0, "ENDTAB");
    DXF(0, "ENDSEC");

    // Un bloc par piece (entites a coordonnees absolues, base 0,0).
    DXF(0, "SECTION"); DXF(2, "BLOCKS");
    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;
        const QString name = blockName(p);
        DXF(0, "BLOCK"); DXF(8, "0"); DXF(2, name); DXF(70, 0);
        DXF(10, 0.0); DXF(20, 0.0); DXF(30, 0.0);
        DXF(3, name);
        emitPiece(p);
        DXF(0, "ENDBLK"); DXF(8, "0");
    }
    DXF(0, "ENDSEC");

    // Insertion de chaque bloc (a l'origine : les entites sont deja en absolu).
    DXF(0, "SECTION"); DXF(2, "ENTITIES");
    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;
        DXF(0, "INSERT"); DXF(8, "0"); DXF(2, blockName(p));
        DXF(10, 0.0); DXF(20, 0.0);
    }
    DXF(0, "ENDSEC");
    DXF(0, "EOF");

    #undef DXF
    f.close();
    return true;
}
//-----------------------------------------------------------------------------------------------
