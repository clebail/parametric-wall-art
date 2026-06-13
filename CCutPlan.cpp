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
// Geometrie des assemblages socle/lamelle : dimensions de tenon/mortaise standardisees.
namespace {
using BoardJoint::kTabWidth;
using BoardJoint::kTwoTabMin;
using BoardJoint::kTabPad;

inline bool inBand(float y, float vL, float vR) { return y >= vL && y <= vR; }

// Intersection du segment p(dans bande)->q(hors bande) avec la ligne vL ou vR franchie.
SPoint2 crossY(const SPoint2 &p, const SPoint2 &q, float vL, float vR) {
    const float Y = (q.y < vL) ? vL : vR;
    const float d = q.y - p.y;
    const float t = (std::fabs(d) > 1e-9f) ? (Y - p.y) / d : 0.0f;
    return SPoint2(p.x + t * (q.x - p.x), Y);
}

// Greffe un tenon rectangulaire (largeur 2*halfW en v, centre vc) sur le bord arriere (x mini)
// d'un contour, en le faisant ressortir jusqu'a x = xTip. Retourne false si le bord arriere du
// contour dans la bande n'est pas exploitable (pas de contact franc).
bool attachTenon(Contour &c, float xTip, float vc, float halfW) {
    const float vL = vc - halfW, vR = vc + halfW;
    const int n = int(c.size());
    if (n < 3) return false;

    // Sommet le plus en arriere (x mini) situe dans la bande.
    int dip = -1; float best = 1e30f;
    for (int i = 0; i < n; i++)
        if (inBand(c[i].y, vL, vR) && c[i].x < best) { best = c[i].x; dip = i; }
    if (dip < 0) return false;

    // Sorties de bande en avancant et en reculant le long du contour.
    int f = dip;
    while (inBand(c[(f + 1) % n].y, vL, vR)) { f = (f + 1) % n; if (f == dip) return false; }
    const int fOut = (f + 1) % n;
    const SPoint2 Pf = crossY(c[f], c[fOut], vL, vR);

    int b = dip;
    while (inBand(c[(b - 1 + n) % n].y, vL, vR)) { b = (b - 1 + n) % n; if (b == dip) return false; }
    const int bOut = (b - 1 + n) % n;
    const SPoint2 Pb = crossY(c[b], c[bOut], vL, vR);

    // Les deux franchissements doivent etre sur des lignes opposees (tenon a embouchure nette)
    // et le tenon doit reellement ressortir vers l'arriere.
    if (std::fabs(Pf.y - Pb.y) < halfW) return false;
    if (xTip >= std::min(Pb.x, Pf.x))   return false;

    // Reconstruit le contour : arc complementaire (hors nub) + contour du tenon.
    Contour out;
    out.reserve(n + 4);
    int i = fOut;
    while (true) { out.push_back(c[i]); if (i == bOut) break; i = (i + 1) % n; }
    out.push_back(Pb);
    out.push_back(SPoint2(xTip, Pb.y));
    out.push_back(SPoint2(xTip, Pf.y));
    out.push_back(Pf);
    c.swap(out);
    return true;
}
} // namespace
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
            for (size_t k = 0; k < sl.contours[c].size(); k++) {
                const float x = sl.contours[c][k].x * s;
                const float y = sl.contours[c][k].y * s;
                r.contours[c].push_back(SPoint2(x, y));
                r.minx = std::min(r.minx, x); r.maxx = std::max(r.maxx, x);
                r.miny = std::min(r.miny, y); r.maxy = std::max(r.maxy, y);
            }
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
        const float xTip = gMinX - t;             // pointe du tenon = face arriere du socle
        const float halfW = kTabWidth * 0.5f;

        int maxIdx = 0;
        for (size_t i = 0; i < raws.size(); i++) maxIdx = std::max(maxIdx, raws[i].index);

        std::vector<std::pair<float, float> > mortises;   // (x centre sur le socle, y centre global)

        for (size_t i = 0; i < raws.size(); i++) {
            Raw &r = raws[i];

            // Le dos de la lamelle atteint-il le plan du socle ?
            if (r.minx > gMinX + tol) { m_floating.push_back(r.index); continue; }

            // Contour exterieur = celui dont un sommet atteint le x mini (le plus en arriere).
            int oc = 0; float omin = big;
            for (size_t c = 0; c < r.contours.size(); c++)
                for (size_t k = 0; k < r.contours[c].size(); k++)
                    if (r.contours[c][k].x < omin) { omin = r.contours[c][k].x; oc = int(c); }

            // Plage de contact (en v) : sommets proches du dos.
            float cvmin = big, cvmax = -big;
            for (size_t k = 0; k < r.contours[oc].size(); k++) {
                const SPoint2 &p = r.contours[oc][k];
                if (p.x <= gMinX + tol) { cvmin = std::min(cvmin, p.y); cvmax = std::max(cvmax, p.y); }
            }
            if (cvmax < cvmin) { m_floating.push_back(r.index); continue; }

            // 1 ou 2 tenons selon la taille de la zone de contact.
            std::vector<float> centers;
            if (cvmax - cvmin >= kTwoTabMin) {
                centers.push_back(cvmin + kTabPad + halfW);
                centers.push_back(cvmax - kTabPad - halfW);
            } else {
                centers.push_back(0.5f * (cvmin + cvmax));
            }

            const float xPosBoard = r.index * (t + gap) + t * 0.5f;   // position le long du socle
            int placed = 0;
            for (size_t ci = 0; ci < centers.size(); ci++)
                if (attachTenon(r.contours[oc], xTip, centers[ci], halfW)) {
                    mortises.push_back(std::make_pair(xPosBoard, centers[ci]));
                    placed++;
                }

            if (placed == 0) m_floating.push_back(r.index);
            else             r.minx = std::min(r.minx, xTip);
        }

        // Construit la piece socle : rectangle L x H perce de mortaises standard (t x kTabWidth).
        const float L = float(maxIdx) * (t + gap) + t;
        const float H = gMaxY - gMinY;
        if (H > 0.0f && L > 0.0f && !mortises.empty()) {
            board.sliceIndex = -1;
            board.tx = board.ty = 0; board.sheet = 0;
            Contour outer;
            outer.push_back(SPoint2(0, 0)); outer.push_back(SPoint2(L, 0));
            outer.push_back(SPoint2(L, H)); outer.push_back(SPoint2(0, H));
            board.contours.push_back(outer);

            const float halfT = t * 0.5f;
            for (size_t m = 0; m < mortises.size(); m++) {
                const float xc = mortises[m].first;
                const float yc = mortises[m].second - gMinY;
                Contour hole;
                hole.push_back(SPoint2(xc - halfT, yc - halfW));
                hole.push_back(SPoint2(xc + halfT, yc - halfW));
                hole.push_back(SPoint2(xc + halfT, yc + halfW));
                hole.push_back(SPoint2(xc - halfT, yc + halfW));
                board.contours.push_back(hole);
            }
            board.w = L; board.h = H;
            haveBoard = true;
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

    // Contours (repere y-haut).
    ts << "  <g transform=\"translate(0," << H << ") scale(1,-1)\">\n";
    ts << "    <rect x=\"0\" y=\"0\" width=\"" << W << "\" height=\"" << H
       << "\" fill=\"none\" stroke=\"#888\" stroke-width=\"0.2\"/>\n";

    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;
        for (size_t c = 0; c < p.contours.size(); c++) {
            ts << "    <polygon points=\"";
            for (size_t k = 0; k < p.contours[c].size(); k++)
                ts << (p.tx + p.contours[c][k].x) << "," << (p.ty + p.contours[c][k].y) << " ";
            ts << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.1\"/>\n";
        }
    }
    ts << "  </g>\n";

    // Etiquettes (repere SVG y-bas : on convertit y -> H - y).
    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;
        const float cx = p.tx + p.w * 0.5f;
        const float cy = p.ty + p.h * 0.5f;
        const float fs = std::min(4.0f, std::max(1.0f, p.h * 0.3f));
        const QString label = (p.sliceIndex < 0) ? QString("FOND") : QString::number(p.sliceIndex);
        ts << "  <text x=\"" << cx << "\" y=\"" << (H - cy)
           << "\" font-size=\"" << fs << "\" text-anchor=\"middle\" "
           << "dominant-baseline=\"central\" fill=\"#c00\">" << label << "</text>\n";
    }

    ts << "</svg>\n";
    f.close();
    return true;
}
//-----------------------------------------------------------------------------------------------
// DXF R12 ASCII minimal : section TABLES (calques CUT/LABEL) + ENTITIES (POLYLINE fermees +
// TEXT). Pas de LWPOLYLINE (R14+), pour compat large laser/CNC.
bool CCutPlan::writeDXFSheet(const QString &path, int sheet) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "CCutPlan: ecriture DXF impossible" << path;
        return false;
    }
    QTextStream ts(&f);

    // Paire (code, valeur) DXF.
    #define DXF(code, val) ts << code << "\n" << val << "\n"

    DXF(0, "SECTION"); DXF(2, "HEADER");
    DXF(9, "$ACADVER"); DXF(1, "AC1009");
    DXF(0, "ENDSEC");

    DXF(0, "SECTION"); DXF(2, "TABLES");
    DXF(0, "TABLE"); DXF(2, "LAYER"); DXF(70, 2);
    DXF(0, "LAYER"); DXF(2, "CUT");   DXF(70, 0); DXF(62, 7); DXF(6, "CONTINUOUS");
    DXF(0, "LAYER"); DXF(2, "LABEL"); DXF(70, 0); DXF(62, 1); DXF(6, "CONTINUOUS");
    DXF(0, "ENDTAB");
    DXF(0, "ENDSEC");

    DXF(0, "SECTION"); DXF(2, "ENTITIES");
    for (size_t i = 0; i < m_pieces.size(); i++) {
        const Piece &p = m_pieces[i];
        if (p.sheet != sheet) continue;

        for (size_t c = 0; c < p.contours.size(); c++) {
            DXF(0, "POLYLINE"); DXF(8, "CUT"); DXF(66, 1); DXF(70, 1); // 1 = fermee
            for (size_t k = 0; k < p.contours[c].size(); k++) {
                DXF(0, "VERTEX"); DXF(8, "CUT");
                DXF(10, (p.tx + p.contours[c][k].x));
                DXF(20, (p.ty + p.contours[c][k].y));
            }
            DXF(0, "SEQEND"); DXF(8, "CUT");
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
    }
    DXF(0, "ENDSEC");
    DXF(0, "EOF");

    #undef DXF
    f.close();
    return true;
}
//-----------------------------------------------------------------------------------------------
