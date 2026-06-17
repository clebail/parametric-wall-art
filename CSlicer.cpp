//-----------------------------------------------------------------------------------------------
#include <map>
#include <cmath>
#include <utility>
#include <QtDebug>
#include "CSlicer.h"
//-----------------------------------------------------------------------------------------------
// Projection 3D -> 2D selon l'axe de coupe. Les deux composantes restantes deviennent (u,v),
// orientees pour que les planches tiennent "debout, cote a cote" le long de l'axe choisi.
SPoint2 CSlicer::project(const SVec3 &p, Axis axis) {
    switch (axis) {
        // u = profondeur (normale a la planche de fond) : TOUJOURS Z -> planche sur le plan X,Y.
        case AxisX: return SPoint2(p.z, p.y);  // u=Z, v=Y
        case AxisY: return SPoint2(p.z, p.x);  // u=Z, v=X
        default:    return SPoint2(p.x, p.y);  // AxisZ (retire de l'UI) : u=X, v=Y
    }
}
//-----------------------------------------------------------------------------------------------
float CSlicer::signedArea(const Contour &c) {
    double a = 0.0;
    const size_t n = c.size();
    for (size_t i = 0; i < n; i++) {
        const SPoint2 &p = c[i];
        const SPoint2 &q = c[(i + 1) % n];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return float(a * 0.5);
}
//-----------------------------------------------------------------------------------------------
// Classe les 3 sommets par signe de (axisValue - pos) ; chaque arete traversant le plan donne
// un point d'intersection. Un triangle traverse par le plan produit exactement 2 points -> un
// segment, ajoute a outPts (paire de points consecutifs). On coupe au centre des slabs, donc les
// hits exactement sur un sommet (d == 0) sont rares ; on les gere quand meme via un epsilon.
void CSlicer::intersectTriangle(const CMesh::STriangle &t, Axis axis, float pos,
                                std::vector<SPoint2> &outPts) {
    const int ai = int(axis);
    float d[3];
    for (int k = 0; k < 3; k++)
        d[k] = axisValue(t.v[k], ai) - pos;

    // Snap des sommets quasi-coplanaires pour stabiliser le classement.
    const float eps = 1e-6f;
    for (int k = 0; k < 3; k++)
        if (std::fabs(d[k]) < eps) d[k] = 0.0f;

    // Tous du meme cote (et aucun sur le plan) -> pas de coupe.
    if ((d[0] > 0 && d[1] > 0 && d[2] > 0) || (d[0] < 0 && d[1] < 0 && d[2] < 0))
        return;

    SPoint2 pts[3];
    int np = 0;

    for (int k = 0; k < 3 && np < 3; k++) {
        const int j = (k + 1) % 3;
        if (d[k] == 0.0f) {
            // Sommet exactement sur le plan : c'est un point d'intersection.
            pts[np++] = project(t.v[k], axis);
        } else if ((d[k] < 0 && d[j] > 0) || (d[k] > 0 && d[j] < 0)) {
            // Arete (k,j) traversant le plan : interpolation lineaire.
            const float w = d[k] / (d[k] - d[j]);
            const SVec3 p = t.v[k] + (t.v[j] - t.v[k]) * w;
            pts[np++] = project(p, axis);
        }
    }

    if (np == 2) {
        outPts.push_back(pts[0]);
        outPts.push_back(pts[1]);
    }
    // np == 0 (frole le plan) ou np == 3 (cas degenere) : ignore.
}
//-----------------------------------------------------------------------------------------------
// Couture : segPts contient des paires de points (2i, 2i+1). On quantifie les extremites sur une
// grille a epsilon et on chaine les segments partageant un point jusqu'a refermer chaque boucle.
std::vector<Contour> CSlicer::stitch(const std::vector<SPoint2> &segPts, float eps) {
    typedef std::pair<int, int> Key;
    const size_t nSeg = segPts.size() / 2;
    std::vector<Contour> loops;
    if (nSeg == 0) return loops;
    if (eps <= 0) eps = 1e-4f;

    // Cle de grille pour un point.
    struct Q { float e; Key operator()(const SPoint2 &p) const {
        return Key(int(std::lround(p.x / e)), int(std::lround(p.y / e))); } };
    Q q; q.e = eps;

    // Index : cle -> liste des segments qui y touchent.
    std::map<Key, std::vector<int> > inc;
    for (size_t i = 0; i < nSeg; i++) {
        inc[q(segPts[2 * i])].push_back(int(i));
        inc[q(segPts[2 * i + 1])].push_back(int(i));
    }

    std::vector<bool> used(nSeg, false);
    int openLoops = 0;

    for (size_t s = 0; s < nSeg; s++) {
        if (used[s]) continue;
        used[s] = true;

        Contour c;
        const Key startKey = q(segPts[2 * s]);
        c.push_back(segPts[2 * s]);

        SPoint2 cur = segPts[2 * s + 1];
        Key curKey = q(cur);
        bool closed = false;

        while (true) {
            c.push_back(cur);
            if (curKey == startKey) { closed = true; break; }

            int next = -1;
            const std::vector<int> &cand = inc[curKey];
            for (size_t t = 0; t < cand.size(); t++)
                if (!used[cand[t]]) { next = cand[t]; break; }
            if (next < 0) break;  // boucle ouverte

            used[next] = true;
            const Key ka = q(segPts[2 * next]);
            const Key kb = q(segPts[2 * next + 1]);
            if (ka == curKey) { cur = segPts[2 * next + 1]; curKey = kb; }
            else              { cur = segPts[2 * next];     curKey = ka; }
        }

        if (closed && c.size() > 1) c.pop_back();  // retire le doublon du point de depart
        if (c.size() >= 3) loops.push_back(c);
        else if (!closed)  openLoops++;
    }

    if (openLoops > 0)
        qWarning() << "CSlicer: maillage non etanche -" << openLoops
                   << "boucle(s) ouverte(s) ignoree(s) dans une tranche.";
    return loops;
}
//-----------------------------------------------------------------------------------------------
std::vector<CSlice> CSlicer::slice(const CMesh &mesh, Axis axis, int nbSlices) {
    std::vector<CSlice> result;
    m_thickness = 0;
    if (mesh.empty() || nbSlices < 1)
        return result;

    const int ai = int(axis);
    const float lo = axisValue(mesh.bboxMin(), ai);
    const float hi = axisValue(mesh.bboxMax(), ai);
    const float span = hi - lo;
    if (span <= 0)
        return result;

    m_thickness = span / nbSlices;
    // Tolerance de couture proportionnelle a la taille du modele.
    const SVec3 sz = mesh.size();
    const float diag = length(sz);
    const float eps = (diag > 0 ? diag : 1.0f) * 1e-5f;

    const std::vector<CMesh::STriangle> &tris = mesh.triangles();
    result.reserve(nbSlices);

    for (int k = 0; k < nbSlices; k++) {
        CSlice sl;
        sl.index = k;
        sl.position = lo + (k + 0.5f) * m_thickness;

        std::vector<SPoint2> segPts;
        for (size_t i = 0; i < tris.size(); i++)
            intersectTriangle(tris[i], axis, sl.position, segPts);

        sl.contours = stitch(segPts, eps);
        result.push_back(sl);
    }
    return result;
}
//-----------------------------------------------------------------------------------------------
