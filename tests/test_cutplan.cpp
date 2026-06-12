//-----------------------------------------------------------------------------------------------
// Test headless du plan de decoupe CCutPlan (lien Qt5Core uniquement).
// Compilation (depuis la racine du projet) :
//   g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
//       tests/test_cutplan.cpp CCutPlan.cpp CSlicer.cpp CMesh.cpp \
//       $(pkg-config --libs Qt5Core) -o /tmp/test_cutplan
//-----------------------------------------------------------------------------------------------
#include <cstdio>
#include <cmath>
#include <QFile>
#include <QString>
#include "../CMesh.h"
#include "../CSlicer.h"
#include "../CCutPlan.h"

static int g_fail = 0;

static void check(bool cond, const char *msg) {
    printf("  [%s] %s\n", cond ? "OK " : "XX ", msg);
    if (!cond) g_fail++;
}

static bool near(float a, float b, float eps = 1e-2f) { return std::fabs(a - b) < eps; }

// Compte les occurrences d'un motif dans un fichier texte.
static int countIn(const QString &path, const QString &needle) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    QString all = QString::fromUtf8(f.readAll());
    return all.count(needle);
}

// Deux pieces se chevauchent-elles (bbox) ?
static bool overlap(const CCutPlan::Piece &a, const CCutPlan::Piece &b) {
    if (a.sheet != b.sheet) return false;
    return !(a.tx + a.w <= b.tx || b.tx + b.w <= a.tx ||
             a.ty + a.h <= b.ty || b.ty + b.h <= a.ty);
}

int main(int argc, char *argv[]) {
    const char *cube = argc > 1 ? argv[1] : "tests/cube_ascii.stl";
    printf("== CCutPlan sur cube 20mm (%s) ==\n", cube);

    CMesh m;
    if (!m.loadSTL(cube)) { check(false, "chargement cube"); return 1; }

    CSlicer slicer;
    const int n = 5;
    std::vector<CSlice> slices = slicer.slice(m, CSlicer::AxisZ, n);
    check(slices.size() == size_t(n), "5 tranches");

    // Echelle 2 -> chaque carre de 20 unites devient 40 mm.
    CCutPlan::Params prm;
    prm.scale = 2.0f;
    prm.sheetW = 600; prm.sheetH = 400;
    prm.margin = 10; prm.spacing = 5;

    CCutPlan plan;
    bool built = plan.build(slices, prm);
    check(built, "build() OK");
    check(plan.pieceCount() == 5, "5 pieces placees");
    check(plan.sheetCount() == 1, "1 feuille (tout tient)");

    // Dimensions a l'echelle : 40x40 mm.
    const std::vector<CCutPlan::Piece> &pcs = plan.pieces();
    bool dimsOK = !pcs.empty();
    for (size_t i = 0; i < pcs.size(); i++)
        dimsOK = dimsOK && near(pcs[i].w, 40.0f) && near(pcs[i].h, 40.0f);
    check(dimsOK, "pieces 40x40 mm (echelle x2)");

    // Aucun chevauchement.
    bool noOverlap = true;
    for (size_t i = 0; i < pcs.size(); i++)
        for (size_t j = i + 1; j < pcs.size(); j++)
            if (overlap(pcs[i], pcs[j])) noOverlap = false;
    check(noOverlap, "pieces non chevauchantes");

    // Toutes dans la feuille.
    bool inSheet = true;
    for (size_t i = 0; i < pcs.size(); i++)
        if (pcs[i].tx < 0 || pcs[i].ty < 0 ||
            pcs[i].tx + pcs[i].w > prm.sheetW || pcs[i].ty + pcs[i].h > prm.sheetH)
            inSheet = false;
    check(inSheet, "pieces dans les bornes de la feuille");

    // Export.
    const QString base = "/tmp/test_cutplan_out";
    check(plan.exportSVG(base), "exportSVG OK");
    check(plan.exportDXF(base), "exportDXF OK");

    const QString svg = base + "_sheet1.svg";
    const QString dxf = base + "_sheet1.dxf";
    check(countIn(svg, "<polygon") == 5, "SVG : 5 polygones");
    check(countIn(svg, "<text") == 5, "SVG : 5 etiquettes");
    check(countIn(dxf, "POLYLINE") == 5, "DXF : 5 polylignes");
    check(countIn(dxf, "\nTEXT\n") == 5, "DXF : 5 textes");
    check(countIn(dxf, "EOF") == 1, "DXF : marqueur EOF");

    printf("\n%s (%d echec(s))\n", g_fail == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
//-----------------------------------------------------------------------------------------------
