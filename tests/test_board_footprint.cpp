// Verifie que la planche de fond suit l'EMPREINTE ARRIERE et non le surplomb.
// Compare la hauteur (v) de la piece FOND a la hauteur de la silhouette complete des lamelles.
#include <cstdio>
#include <algorithm>
#include "CMesh.h"
#include "CSlicer.h"
#include "CCutPlan.h"

int main(int argc, char *argv[]) {
    const char *path = argc > 1 ? argv[1] : "tests/tornade3d.stl";
    CMesh mesh;
    if (!mesh.loadSTL(path)) { printf("ECHEC chargement %s\n", path); return 1; }

    CSlicer slicer;
    std::vector<CSlice> slices = slicer.slice(mesh, CSlicer::AxisX, 60);

    // Hauteur (v) de la silhouette COMPLETE de toutes les lamelles (avant correctif = taille planche).
    float fullVmin = 1e30f, fullVmax = -1e30f;
    for (size_t s = 0; s < slices.size(); s++)
        for (size_t c = 0; c < slices[s].contours.size(); c++)
            for (size_t k = 0; k < slices[s].contours[c].size(); k++) {
                fullVmin = std::min(fullVmin, slices[s].contours[c][k].y);
                fullVmax = std::max(fullVmax, slices[s].contours[c][k].y);
            }

    CCutPlan::Params prm;
    prm.scale = 1.0f;
    prm.generateBoard = true;
    prm.sliceThickness = mesh.size().x / 60.0f;   // ~pas d'une lamelle
    prm.gapThickness = prm.sliceThickness * 0.1f;
    prm.boardSmooth = true;
    prm.sheetW = 5000; prm.sheetH = 5000;

    CCutPlan plan;
    if (!plan.build(slices, prm)) { printf("ECHEC build\n"); return 1; }

    const CCutPlan::Piece *board = 0;
    for (size_t i = 0; i < plan.pieces().size(); i++)
        if (plan.pieces()[i].sliceIndex == -1) board = &plan.pieces()[i];
    if (!board) { printf("ECHEC: pas de piece FOND\n"); return 1; }

    // Statistiques de largeur des tenons (proportionnels) + nb de lamelles a 2 tenons.
    float twMin = 1e30f, twMax = -1e30f; int nTab = 0, nTwo = 0;
    for (size_t s = 0; s < slices.size(); s++) {
        std::vector<char> outer = outerContourMask(slices[s].contours);
        for (size_t c = 0; c < slices[s].contours.size(); c++) {
            if (!outer[c]) continue;
            Contour cc = slices[s].contours[c];
            std::vector<std::pair<float,float> > tabs =
                BoardJoint::integrateContourBack(cc, mesh.bboxMin().z, prm.sliceThickness,
                                                 std::max(prm.sliceThickness, 1.0f), 1.0f);
            if (tabs.size() >= 2) nTwo++;
            for (size_t i = 0; i < tabs.size(); i++) {
                const float w = 2.0f * tabs[i].second;
                twMin = std::min(twMin, w); twMax = std::max(twMax, w); nTab++;
            }
        }
    }
    printf("Tenons : %d poses, largeur %.1f..%.1f mm, %d contour(s) a 2 tenons\n",
           nTab, twMin, twMax, nTwo);

    const float full = (fullVmax - fullVmin) * prm.scale;
    printf("Silhouette complete (v)  : %.1f mm\n", full);
    printf("Hauteur planche FOND (h) : %.1f mm\n", board->h);
    printf("Largeur planche FOND (w) : %.1f mm\n", board->w);
    printf("Ratio fond/silhouette    : %.0f %%\n", 100.0f * board->h / full);
    printf("%s\n", board->h < full * 0.98f
           ? "OK: la planche est plus petite que la silhouette (suit le dos, pas le surplomb)"
           : "ATTENTION: planche ~ silhouette complete (suit encore le surplomb ?)");
    return 0;
}
