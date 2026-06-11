//-----------------------------------------------------------------------------------------------
// Test headless du loader CMesh (lien Qt5Core uniquement, pas de GUI/OpenGL).
// Compilation (depuis la racine du projet) :
//   g++ -fPIC -std=c++11 $(pkg-config --cflags Qt5Core) \
//       tests/test_mesh.cpp CMesh.cpp $(pkg-config --libs Qt5Core) -o /tmp/test_mesh
//-----------------------------------------------------------------------------------------------
#include <cstdio>
#include <cmath>
#include "../CMesh.h"

static int g_fail = 0;

static void check(bool cond, const char *msg) {
    printf("  [%s] %s\n", cond ? "OK " : "XX ", msg);
    if (!cond) g_fail++;
}

static bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) < eps;
}

static void testFile(const char *path, const char *label) {
    printf("== %s (%s) ==\n", label, path);
    CMesh m;
    bool ok = m.loadSTL(path);
    check(ok, "chargement");
    check(m.triangles().size() == 12, "12 triangles (cube)");

    SVec3 mn = m.bboxMin(), mx = m.bboxMax(), sz = m.size(), c = m.center();
    check(near(mn.x, 0) && near(mn.y, 0) && near(mn.z, 0), "bboxMin = (0,0,0)");
    check(near(mx.x, 20) && near(mx.y, 20) && near(mx.z, 20), "bboxMax = (20,20,20)");
    check(near(sz.x, 20) && near(sz.y, 20) && near(sz.z, 20), "size = (20,20,20)");
    check(near(c.x, 10) && near(c.y, 10) && near(c.z, 10), "center = (10,10,10)");
}

int main(int argc, char *argv[]) {
    const char *ascii = argc > 1 ? argv[1] : "tests/cube_ascii.stl";
    const char *bin   = argc > 2 ? argv[2] : "tests/cube_bin.stl";
    testFile(ascii, "STL ASCII");
    testFile(bin,   "STL binaire");
    printf("\n%s (%d echec(s))\n", g_fail == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
//-----------------------------------------------------------------------------------------------
