//-----------------------------------------------------------------------------------------------
#ifndef GEOMETRY_H
#define GEOMETRY_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <cmath>
#include <algorithm>
//-----------------------------------------------------------------------------------------------
// Types geometriques legers partages par le loader, le slicer et l'export.
//-----------------------------------------------------------------------------------------------
struct SVec3 {
    float x, y, z;
    SVec3(void) : x(0), y(0), z(0) {}
    SVec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
//-----------------------------------------------------------------------------------------------
struct SPoint2 {
    float x, y;
    SPoint2(void) : x(0), y(0) {}
    SPoint2(float x_, float y_) : x(x_), y(y_) {}
};
//-----------------------------------------------------------------------------------------------
// Contour ferme (fermeture implicite : le dernier point se relie au premier).
typedef std::vector<SPoint2> Contour;
//-----------------------------------------------------------------------------------------------
// Aire signee d'un contour (formule du lacet). Le signe indique l'orientation.
inline double contourArea(const Contour &c) {
    double a = 0.0; const size_t n = c.size();
    for (size_t k = 0; k < n; k++) {
        const SPoint2 &p = c[k], &q = c[(k + 1) % n];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return 0.5 * a;
}
//-----------------------------------------------------------------------------------------------
// Test point-dans-polygone (lancer de rayon).
inline bool pointInPolygon(const SPoint2 &pt, const Contour &c) {
    bool in = false; const size_t n = c.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((c[i].y > pt.y) != (c[j].y > pt.y)) &&
            (pt.x < (c[j].x - c[i].x) * (pt.y - c[i].y) / (c[j].y - c[i].y) + c[i].x))
            in = !in;
    }
    return in;
}
//-----------------------------------------------------------------------------------------------
// Pour chaque contour : 1 s'il est EXTERIEUR (morceau plein), 0 si c'est un TROU (contenu dans un
// contour d'aire superieure). Determination par containment (robuste meme si l'orientation/signe
// d'aire des contours du slicer n'est pas coherente). Plusieurs exterieurs disjoints -> tous a 1.
inline std::vector<char> outerContourMask(const std::vector<Contour> &cs) {
    const size_t n = cs.size();
    std::vector<char> outer(n, 1);
    std::vector<double> ar(n, 0.0);
    for (size_t i = 0; i < n; i++) ar[i] = std::fabs(contourArea(cs[i]));
    for (size_t i = 0; i < n; i++) {
        if (cs[i].size() < 3) { outer[i] = 0; continue; }
        for (size_t j = 0; j < n; j++) {
            if (i == j || ar[j] <= ar[i] || cs[j].size() < 3) continue;
            if (pointInPolygon(cs[i][0], cs[j])) { outer[i] = 0; break; }  // contenu -> trou
        }
    }
    return outer;
}
//-----------------------------------------------------------------------------------------------
// Supprime les contours dont |aire| < minArea (petits ilots / trous parasites), en conservant
// TOUJOURS le plus grand (le corps de la tranche). minArea dans l'unite des contours (au carre).
inline void filterSmallIslands(std::vector<Contour> &contours, float minArea) {
    if (minArea <= 0.0f || contours.size() <= 1) return;
    int biggest = 0; double best = -1.0;
    for (size_t c = 0; c < contours.size(); c++) {
        const double a = std::fabs(contourArea(contours[c]));
        if (a > best) { best = a; biggest = int(c); }
    }
    std::vector<Contour> kept;
    kept.reserve(contours.size());
    for (size_t c = 0; c < contours.size(); c++)
        if (int(c) == biggest || std::fabs(contourArea(contours[c])) >= minArea)
            kept.push_back(contours[c]);
    contours.swap(kept);
}
//-----------------------------------------------------------------------------------------------
// Construit des contours LISSES (sans decrochés) pour la planche de fond, a partir des intervalles
// v couverts par chaque tranche. Entree : suite (triee par position d'axe croissante) de
// (axisPos, [ (vlo,vhi), ... ]). On suit chaque "bande" en reliant ses extremes au CENTRE des
// tranches (polylignes obliques) -> contour inscrit dans le modele (jamais plus large/haut) et
// lisse. Les bandes disjointes (corps / anse) restent separees (le creux n'est pas comble).
// capWidth : demi-largeur donnee a une bande presente sur une seule tranche.
// treadHalfWidth : 0 -> contour lisse (un point au centre de chaque tranche, liaisons en diagonale)
//                  >0 -> escalier (marche plate de largeur 2*treadHalfWidth par tranche, liaisons
//                        verticales = decrochés). Dans les deux cas le contour reste connexe et les
//                        trous (gaps) sont rendus separement -> meme machinerie de bandes.
// maxStep : si > 0, deux tranches consecutives du tableau dont les positions d'axe sont distantes
//           de plus de maxStep ne sont PAS reliees (une tranche a ete sautee -> modele absent la).
//           Les bandes actives sont fermees par une face verticale et de nouvelles repartent : pas
//           de long palier qui creerait un "vide" plus large qu'un gap normal.
// holeMode : false -> bandes PLEINES (planche). Le pont entre deux marches suit le recouvrement
//            (top=min, bot=max) -> ne depasse jamais la plus petite des voisines.
//            true  -> bandes de TROUS (creux a evider). Pont inverse (top=max, bot=min) = union ->
//            le trou reste le plus ouvert possible, la planche ne deborde pas dans l'ouverture.
inline std::vector<Contour> buildSmoothLobes(
        const std::vector<std::pair<float, std::vector<std::pair<float, float> > > > &slices,
        float capWidth, float treadHalfWidth = 0.0f, float maxStep = 0.0f, bool holeMode = false) {
    struct Band { std::vector<SPoint2> top, bot; float lo, hi; };
    std::vector<Band> active;
    std::vector<Contour> out;

    // Ajoute un cran de profil. Lisse : un point au centre (liaisons en diagonale). Escalier :
    // marche plate de largeur 2*tread reliee a la precedente par un PONT (palier horizontal). Plein :
    // pont = recouvrement (top min / bot max). Trou : pont = union (top max / bot min).
    auto pushPt = [&](std::vector<SPoint2> &prof, float ax, float val, bool isTop) {
        if (treadHalfWidth > 0.0f) {
            if (!prof.empty()) {
                const float prev   = prof.back().y;
                const bool   useMin = (isTop != holeMode);
                const float bridge = useMin ? std::min(prev, val) : std::max(prev, val);
                prof.push_back(SPoint2(prof.back().x, bridge));               // descente/montee au pont
                prof.push_back(SPoint2(ax - treadHalfWidth, bridge));         // palier du pont
            }
            prof.push_back(SPoint2(ax - treadHalfWidth, val));                // montee verticale
            prof.push_back(SPoint2(ax + treadHalfWidth, val));                // marche plate
        } else {
            prof.push_back(SPoint2(ax, val));
        }
    };

    auto finalize = [&](const Band &b) {
        Contour c;
        if (b.top.size() == 1) {                     // une seule tranche, mode lisse -> rectangle
            const float x = b.top[0].x;
            c.push_back(SPoint2(x - capWidth, b.bot[0].y));
            c.push_back(SPoint2(x + capWidth, b.bot[0].y));
            c.push_back(SPoint2(x + capWidth, b.top[0].y));
            c.push_back(SPoint2(x - capWidth, b.top[0].y));
        } else {
            for (size_t i = 0; i < b.top.size(); i++) c.push_back(b.top[i]);          // dessus G->D
            for (size_t i = b.bot.size(); i-- > 0; ) c.push_back(b.bot[i]);            // dessous D->G
        }
        if (c.size() >= 3) out.push_back(c);
    };

    float prevAx = 0.0f; bool havePrev = false;
    for (size_t s = 0; s < slices.size(); s++) {
        const float ax = slices[s].first;
        const std::vector<std::pair<float, float> > &spans = slices[s].second;

        // Tranche sautee (modele absent) : on n'enjambe pas -> ferme tout par une face verticale.
        if (maxStep > 0.0f && havePrev && (ax - prevAx) > maxStep) {
            for (size_t b = 0; b < active.size(); b++) finalize(active[b]);
            active.clear();
        }
        prevAx = ax; havePrev = true;

        std::vector<char> usedSpan(spans.size(), 0), ext(active.size(), 0);

        for (size_t b = 0; b < active.size(); b++) {            // prolonge chaque bande active
            int best = -1; float bestov = 0.0f;
            for (size_t k = 0; k < spans.size(); k++) {
                if (usedSpan[k]) continue;
                const float ov = std::min(spans[k].second, active[b].hi)
                               - std::max(spans[k].first, active[b].lo);
                if (ov > bestov) { bestov = ov; best = int(k); }
            }
            if (best >= 0) {
                usedSpan[best] = 1; ext[b] = 1;
                pushPt(active[b].top, ax, spans[best].second, true);
                pushPt(active[b].bot, ax, spans[best].first, false);
                active[b].lo = spans[best].first; active[b].hi = spans[best].second;
            }
        }
        std::vector<Band> keep;                                // ferme les bandes non prolongees
        for (size_t b = 0; b < active.size(); b++) {
            if (ext[b]) keep.push_back(active[b]); else finalize(active[b]);
        }
        active.swap(keep);
        for (size_t k = 0; k < spans.size(); k++) if (!usedSpan[k]) {   // demarre les nouvelles
            Band nb; nb.lo = spans[k].first; nb.hi = spans[k].second;
            pushPt(nb.top, ax, spans[k].second, true);
            pushPt(nb.bot, ax, spans[k].first, false);
            active.push_back(nb);
        }
    }
    for (size_t b = 0; b < active.size(); b++) finalize(active[b]);
    return out;
}
//-----------------------------------------------------------------------------------------------
// A partir des spans v par tranche, separe l'ENVELOPPE (un span min..max par tranche -> donne une
// planche d'un seul tenant) et les TROUS (gaps entre spans consecutifs d'une meme tranche, ex.
// l'ouverture de l'anse). Enveloppe moins trous = exactement la silhouette du modele.
inline void boardEnvelopeAndGaps(
        const std::vector<std::pair<float, std::vector<std::pair<float, float> > > > &slices,
        std::vector<std::pair<float, std::vector<std::pair<float, float> > > > &env,
        std::vector<std::pair<float, std::vector<std::pair<float, float> > > > &gaps) {
    for (size_t s = 0; s < slices.size(); s++) {
        std::vector<std::pair<float, float> > spans = slices[s].second;
        if (spans.empty()) continue;
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<float, float> > one(1, std::make_pair(spans.front().first,
                                                                    spans.back().second));
        env.push_back(std::make_pair(slices[s].first, one));
        std::vector<std::pair<float, float> > g;
        for (size_t i = 0; i + 1 < spans.size(); i++)
            if (spans[i + 1].first > spans[i].second)
                g.push_back(std::make_pair(spans[i].second, spans[i + 1].first));
        if (!g.empty()) gaps.push_back(std::make_pair(slices[s].first, g));
    }
}
//-----------------------------------------------------------------------------------------------
// Geometrie du joint socle/lamelle, partagee par le plan de decoupe et la previsualisation.
// Convention : dans un contour de tranche, .x = u (profondeur, normale au plan du socle) et
// .y = v (le long du socle). La planche s'integre dans la profondeur du modele : le dos de la
// lamelle est recule de l'epaisseur planche (clip a u >= uClip), sauf des tenons qui ressortent
// jusqu'au plan arriere (uTip) pour s'enficher dans les mortaises de la planche.
namespace BoardJoint {
const float kTabWidth  = 10.0f;   // largeur d'un tenon/mortaise (mm), constante
const float kTwoTabMin = 40.0f;   // hauteur de contact au-dela de laquelle on met 2 tenons
const float kTabPad    = 4.0f;    // retrait des tenons par rapport aux bords du contact (mm)

inline bool inVBand(float v, float vL, float vR) { return v >= vL && v <= vR; }

// Intersection du segment p(dans bande)->q(hors bande) avec la ligne vL ou vR franchie.
inline SPoint2 crossVBand(const SPoint2 &p, const SPoint2 &q, float vL, float vR) {
    const float V = (q.y < vL) ? vL : vR;
    const float d = q.y - p.y;
    const float t = (std::fabs(d) > 1e-9f) ? (V - p.y) / d : 0.0f;
    return SPoint2(p.x + t * (q.x - p.x), V);
}

// Clip Sutherland-Hodgman d'un demi-plan : ne garde que la partie u (= x) >= uClip. Recule donc
// le dos de la lamelle de l'epaisseur de la planche.
inline Contour clipKeepUGE(const Contour &in, float uClip) {
    const int n = int(in.size());
    if (n < 3) return in;
    Contour out;
    out.reserve(n + 4);
    for (int i = 0; i < n; i++) {
        const SPoint2 &A = in[i];
        const SPoint2 &B = in[(i + 1) % n];
        const bool inA = A.x >= uClip, inB = B.x >= uClip;
        if (inA) out.push_back(A);
        if (inA != inB) {
            const float d = B.x - A.x;
            const float t = (std::fabs(d) > 1e-9f) ? (uClip - A.x) / d : 0.0f;
            out.push_back(SPoint2(uClip, A.y + t * (B.y - A.y)));
        }
    }
    return out;
}

// Apres clip, le dos est un segment plat (x = uClip) dont les sommets sont aux extremites, hors
// bande du tenon : attachTab n'y trouverait pas de "creux". On insere donc un sommet (uClip, v)
// sur cette arete pour garantir un point dans la bande. true si insere.
inline bool insertBackVertex(Contour &c, float uClip, float v) {
    const int n = int(c.size());
    const float eps = 1e-4f * (std::fabs(uClip) + 1.0f);
    for (int i = 0; i < n; i++) {
        const SPoint2 &A = c[i];
        const SPoint2 &B = c[(i + 1) % n];
        if (std::fabs(A.x - uClip) < eps && std::fabs(B.x - uClip) < eps) {
            const float lo = std::min(A.y, B.y), hi = std::max(A.y, B.y);
            if (v > lo + eps && v < hi - eps) {
                c.insert(c.begin() + (i + 1), SPoint2(uClip, v));
                return true;
            }
        }
    }
    return false;
}

// Greffe un tenon rectangulaire (largeur 2*halfW en v, centre vc) sur le bord arriere (u mini)
// d'un contour, en le faisant ressortir jusqu'a u = uTip. false si le dos dans la bande n'est
// pas exploitable (pas de contact franc).
inline bool attachTab(Contour &c, float uTip, float vc, float halfW) {
    const float vL = vc - halfW, vR = vc + halfW;
    const int n = int(c.size());
    if (n < 3) return false;

    int dip = -1; float best = 1e30f;
    for (int i = 0; i < n; i++)
        if (inVBand(c[i].y, vL, vR) && c[i].x < best) { best = c[i].x; dip = i; }
    if (dip < 0) return false;

    int f = dip;
    while (inVBand(c[(f + 1) % n].y, vL, vR)) { f = (f + 1) % n; if (f == dip) return false; }
    const int fOut = (f + 1) % n;
    const SPoint2 Pf = crossVBand(c[f], c[fOut], vL, vR);

    int b = dip;
    while (inVBand(c[(b - 1 + n) % n].y, vL, vR)) { b = (b - 1 + n) % n; if (b == dip) return false; }
    const int bOut = (b - 1 + n) % n;
    const SPoint2 Pb = crossVBand(c[b], c[bOut], vL, vR);

    if (std::fabs(Pf.y - Pb.y) < halfW) return false;
    if (uTip >= std::min(Pb.x, Pf.x))   return false;

    Contour out;
    out.reserve(n + 4);
    int i = fOut;
    while (true) { out.push_back(c[i]); if (i == bOut) break; i = (i + 1) % n; }
    out.push_back(Pb);
    out.push_back(SPoint2(uTip, Pb.y));
    out.push_back(SPoint2(uTip, Pf.y));
    out.push_back(Pf);
    c.swap(out);
    return true;
}

// Centres des tenons selon l'etendue de contact en v (1 ou 2 selon kTwoTabMin). Retourne le
// nombre de tenons et remplit out[].
inline int tabCenters(float cvmin, float cvmax, float halfW, float out[2]) {
    if (cvmax < cvmin) return 0;
    if (cvmax - cvmin >= kTwoTabMin) {
        out[0] = cvmin + kTabPad + halfW;
        out[1] = cvmax - kTabPad - halfW;
        return 2;
    }
    out[0] = 0.5f * (cvmin + cvmax);
    return 1;
}

// Integre la planche dans le dos d'UN contour exterieur : si le contour atteint le plan arriere
// u0 (a tol pres), rabote son dos a u0+t et greffe 1-2 tenons jusqu'a u0. Modifie c et renvoie les
// centres v des tenons reellement poses (pour creer les mortaises). Renvoie vide (c inchange) si
// le contour ne touche pas le fond ou si aucun tenon ne s'accroche.
inline std::vector<float> integrateContourBack(Contour &c, float u0, float t, float tol, float halfW) {
    std::vector<float> placed;
    if (c.size() < 3) return placed;
    float cvmin = 1e30f, cvmax = -1e30f;
    for (size_t k = 0; k < c.size(); k++)
        if (c[k].x <= u0 + tol) { cvmin = std::min(cvmin, c[k].y); cvmax = std::max(cvmax, c[k].y); }
    if (cvmax < cvmin) return placed;     // ne touche pas le fond
    float centers[2];
    const int nc = tabCenters(cvmin, cvmax, halfW, centers);
    Contour work = clipKeepUGE(c, u0 + t);
    for (int i = 0; i < nc; i++) {
        insertBackVertex(work, u0 + t, centers[i]);
        if (attachTab(work, u0, centers[i], halfW)) placed.push_back(centers[i]);
    }
    if (!placed.empty()) c.swap(work);
    return placed;
}
} // namespace BoardJoint
//-----------------------------------------------------------------------------------------------
inline SVec3 operator-(const SVec3 &a, const SVec3 &b) { return SVec3(a.x-b.x, a.y-b.y, a.z-b.z); }
inline SVec3 operator+(const SVec3 &a, const SVec3 &b) { return SVec3(a.x+b.x, a.y+b.y, a.z+b.z); }
inline SVec3 operator*(const SVec3 &a, float s)        { return SVec3(a.x*s, a.y*s, a.z*s); }
inline float dot(const SVec3 &a, const SVec3 &b)       { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float length(const SVec3 &a)                    { return std::sqrt(dot(a, a)); }
//-----------------------------------------------------------------------------------------------
// Acces a une composante par index d'axe (0=X, 1=Y, 2=Z).
inline float axisValue(const SVec3 &v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}
//-----------------------------------------------------------------------------------------------
#endif // GEOMETRY_H
//-----------------------------------------------------------------------------------------------
