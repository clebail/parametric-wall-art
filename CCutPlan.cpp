//-----------------------------------------------------------------------------------------------
#include <algorithm>
#include <limits>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QtDebug>
#include "CCutPlan.h"
//-----------------------------------------------------------------------------------------------
bool CCutPlan::build(const std::vector<CSlice> &slices, const Params &params) {
    m_params = params;
    m_pieces.clear();
    m_sheetCount = 0;

    const float s = params.scale > 0 ? params.scale : 1.0f;

    // 1) Construit une piece par tranche non vide : contours mis a l'echelle puis normalises
    //    (bbox min ramenee a l'origine).
    for (size_t i = 0; i < slices.size(); i++) {
        const CSlice &sl = slices[i];
        if (sl.contours.empty())
            continue;

        const float big = std::numeric_limits<float>::max();
        float minx = big, miny = big, maxx = -big, maxy = -big;
        for (size_t c = 0; c < sl.contours.size(); c++)
            for (size_t k = 0; k < sl.contours[c].size(); k++) {
                const float x = sl.contours[c][k].x * s;
                const float y = sl.contours[c][k].y * s;
                minx = std::min(minx, x); maxx = std::max(maxx, x);
                miny = std::min(miny, y); maxy = std::max(maxy, y);
            }
        if (maxx < minx)
            continue;

        Piece p;
        p.sliceIndex = sl.index;
        p.w = maxx - minx;
        p.h = maxy - miny;
        p.tx = p.ty = 0;
        p.sheet = 0;
        p.contours.resize(sl.contours.size());
        for (size_t c = 0; c < sl.contours.size(); c++) {
            p.contours[c].reserve(sl.contours[c].size());
            for (size_t k = 0; k < sl.contours[c].size(); k++)
                p.contours[c].push_back(SPoint2(sl.contours[c][k].x * s - minx,
                                                sl.contours[c][k].y * s - miny));
        }
        m_pieces.push_back(p);
    }

    if (m_pieces.empty())
        return false;

    // 2) Nesting par rangees (shelf) : tri par hauteur decroissante, remplissage gauche->droite,
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
        ts << "  <text x=\"" << cx << "\" y=\"" << (H - cy)
           << "\" font-size=\"" << fs << "\" text-anchor=\"middle\" "
           << "dominant-baseline=\"central\" fill=\"#c00\">" << p.sliceIndex << "</text>\n";
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
        DXF(0, "TEXT"); DXF(8, "LABEL");
        DXF(10, cx); DXF(20, cy); DXF(40, fs);
        DXF(50, 0); DXF(72, 1); DXF(73, 2);   // alignement horizontal centre / vertical milieu
        DXF(11, cx); DXF(21, cy);             // point d'alignement (requis si 72/73 != 0)
        DXF(1, p.sliceIndex);
    }
    DXF(0, "ENDSEC");
    DXF(0, "EOF");

    #undef DXF
    f.close();
    return true;
}
//-----------------------------------------------------------------------------------------------
