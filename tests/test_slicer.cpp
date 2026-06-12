//-----------------------------------------------------------------------------------------------
// Test headless du moteur de tranchage CSlicer (lien Qt5Core uniquement, pas de GUI/OpenGL).
// Compilation (depuis la racine du projet) :
//   g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
//       tests/test_slicer.cpp CSlicer.cpp CMesh.cpp $(pkg-config --libs Qt5Core) -o /tmp/test_slicer
//-----------------------------------------------------------------------------------------------
#include <cstdio>
#include <cmath>
#include "../CMesh.h"
#include "../CSlicer.h"

static int g_fail = 0;

static void check(bool cond, const char *msg) {
    printf("  [%s] %s\n", cond ? "OK " : "XX ", msg);
    if (!cond) g_fail++;
}

static bool near(float a, float b, float eps = 1e-2f) {
    return std::fabs(a - b) < eps;
}

// bbox 2D d'un contour.
static void contourBBox(const Contour &c, float &minx, float &miny, float &maxx, float &maxy) {
    minx = miny = 1e30f; maxx = maxy = -1e30f;
    for (size_t i = 0; i < c.size(); i++) {
        minx = std::min(minx, c[i].x); maxx = std::max(maxx, c[i].x);
        miny = std::min(miny, c[i].y); maxy = std::max(maxy, c[i].y);
    }
}

//-----------------------------------------------------------------------------------------------
// Cube 20 mm : chaque tranche (peu importe l'axe) doit donner 1 contour carre 20x20.
static void testCube(const char *path) {
    printf("== Cube 20mm (%s) ==\n", path);
    CMesh m;
    if (!m.loadSTL(path)) { check(false, "chargement"); return; }

    CSlicer slicer;
    const int n = 10;
    std::vector<CSlice> slices = slicer.slice(m, CSlicer::AxisZ, n);

    check(slices.size() == size_t(n), "10 tranches generees");
    check(near(slicer.thickness(), 2.0f), "epaisseur slab = 2.0");

    // Positions centrees dans les slabs : 1, 3, 5, ... 19.
    check(near(slices.front().position, 1.0f), "1ere tranche centree a z=1");
    check(near(slices.back().position, 19.0f), "derniere tranche centree a z=19");

    // Tranche mediane : 1 contour carre 20x20.
    const CSlice &mid = slices[n / 2];
    check(mid.contours.size() == 1, "tranche mediane : 1 contour");
    if (!mid.contours.empty()) {
        float a, b, c, d;
        contourBBox(mid.contours[0], a, b, c, d);
        check(near(c - a, 20.0f) && near(d - b, 20.0f), "contour 20x20");
        check(std::fabs(CSlicer::signedArea(mid.contours[0])) > 100.0f, "aire non nulle (~400)");
    }
}

//-----------------------------------------------------------------------------------------------
// Sphere : une tranche au centre doit donner 1 contour ferme de rayon ~= rayon sphere.
static void testSphere(const char *path) {
    printf("== Sphere (%s) ==\n", path);
    CMesh m;
    if (!m.loadSTL(path)) { check(false, "chargement"); return; }

    const SVec3 sz = m.size();
    const float R = sz.x * 0.5f;  // rayon attendu (sphere centree, bbox cubique)
    printf("  (rayon bbox ~= %.3f)\n", R);

    CSlicer slicer;
    const int n = 21;  // impair -> une tranche tombe pres du centre
    std::vector<CSlice> slices = slicer.slice(m, CSlicer::AxisZ, n);
    check(slices.size() == size_t(n), "21 tranches generees");

    // Tranche la plus proche du centre.
    const SVec3 ctr = m.center();
    int best = 0; float bestd = 1e30f;
    for (size_t i = 0; i < slices.size(); i++) {
        float dd = std::fabs(slices[i].position - ctr.z);
        if (dd < bestd) { bestd = dd; best = int(i); }
    }
    const CSlice &mid = slices[best];
    check(mid.contours.size() == 1, "tranche centrale : 1 contour ferme");
    if (!mid.contours.empty()) {
        float a, b, c, d;
        contourBBox(mid.contours[0], a, b, c, d);
        const float rx = (c - a) * 0.5f, ry = (d - b) * 0.5f;
        printf("  (rayon mesure : %.3f x %.3f)\n", rx, ry);
        // Au centre le contour fait ~le rayon plein ; tolerance large (facettisation).
        check(rx > R * 0.85f && rx < R * 1.05f, "rayon X ~= rayon sphere");
        check(ry > R * 0.85f && ry < R * 1.05f, "rayon Y ~= rayon sphere");
    }
}

//-----------------------------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    const char *cube   = argc > 1 ? argv[1] : "tests/cube_ascii.stl";
    const char *sphere = argc > 2 ? argv[2] : "tests/sphere.stl";
    testCube(cube);
    testSphere(sphere);
    printf("\n%s (%d echec(s))\n", g_fail == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
//-----------------------------------------------------------------------------------------------
