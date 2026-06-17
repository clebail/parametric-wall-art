//-----------------------------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <vector>
#include <QtDebug>
#include <QColor>
#include <QWheelEvent>
#include <QMouseEvent>
#include "C3dView.h"   // tire les en-tetes GL de Qt en premier
#include <GL/glu.h>    // glu.h apres, pour eviter le conflit GL_GLEXT_VERSION
//-----------------------------------------------------------------------------------------------
#ifndef CALLBACK
#define CALLBACK
#endif
//-----------------------------------------------------------------------------------------------
// Contexte passe au tesselateur GLU (via polygon_data) pour calculer les coordonnees de texture
// et liberer les sommets crees par le callback "combine".
namespace {
struct TessCtx {
    float mnx, mny, szx, szy;          // bbox XY du mesh -> mapping UV bois planaire
    std::vector<GLdouble*> combined;   // sommets alloues par combine, a liberer
};
// Mapping (u,v) plan + position sur l'axe -> coordonnees 3D monde (inverse de CSlicer::project).
inline void uvTo3D(int axis, float u, float v, float axisPos, GLdouble out[3]) {
    switch (axis) {
        case 0: out[0] = axisPos; out[1] = v; out[2] = u; break;   // X : Y=v, Z=u
        case 1: out[0] = v; out[1] = axisPos; out[2] = u; break;   // Y : X=v, Z=u
        default: out[0] = u; out[1] = v; out[2] = axisPos; break;  // Z : X=u, Y=v
    }
}
void CALLBACK tessBegin(GLenum type, void *) { glBegin(type); }
void CALLBACK tessEnd(void *) { glEnd(); }
void CALLBACK tessVertex(void *vd, void *pd) {
    const GLdouble *v = static_cast<const GLdouble *>(vd);
    const TessCtx *c = static_cast<const TessCtx *>(pd);
    const float tu = c->szx > 1e-6f ? (float(v[0]) - c->mnx) / c->szx : 0.0f;
    const float tv = c->szy > 1e-6f ? (float(v[1]) - c->mny) / c->szy : 0.0f;
    glTexCoord2f(tu, tv);
    glVertex3dv(v);
}
void CALLBACK tessCombine(GLdouble coords[3], void *[4], GLfloat [4],
                          void **outData, void *pd) {
    TessCtx *c = static_cast<TessCtx *>(pd);
    GLdouble *nv = new GLdouble[3];
    nv[0] = coords[0]; nv[1] = coords[1]; nv[2] = coords[2];
    c->combined.push_back(nv);
    *outData = nv;
}
} // namespace
//-----------------------------------------------------------------------------------------------
C3dView::C3dView(QWidget *parent) : QGLWidget(parent) {
    scale = 1.0;
    rotx = roty = rotz = 0.0;
    roty = 45.0;
    rotx = 30.0;
    m_hasMesh = false;
    m_fitScale = 1.0f;
    m_brightness = 1.0f;
    m_sliceAxis = CSlicer::AxisX;
    m_sliceThickness = m_sliceGap = 0.0f;
    m_sliceMode = false;
    m_showSlices = true;
    m_sliceFitScale = 1.0f;
    m_boardEnabled = false;
    m_boardVisible = false;
    m_boardSmooth = true;
    m_boardScale = 1.0f;
    m_boardThickMm = 10.0f;
    m_boardColor[0] = m_boardColor[1] = m_boardColor[2] = 1.0f;   // blanc par defaut
    m_minIslandArea = 0.0f;
}
//-----------------------------------------------------------------------------------------------
C3dView::~C3dView(void) {
}
//-----------------------------------------------------------------------------------------------
void C3dView::initializeGL() {
    GLfloat mat_specular[]   = { 0.5, 0.5, 0.5, 1.0 };
    GLfloat mat_shininess[]  = { 24.0 };
    GLfloat mat_diffuse[]    = { 1.0, 1.0, 1.0, 1.0 };   // blanc : laisse la texture s'exprimer en plein

    // Positions des lumieres (directionnelles, w=0) ; les intensites sont gerees par applyLighting().
    GLfloat light0_position[] = {  5.0,  8.0, 10.0, 0.0 };
    GLfloat light1_position[] = { -6.0,  3.0, -4.0,  0.0 };

    glShadeModel(GL_SMOOTH);

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);

    glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
    glLightfv(GL_LIGHT1, GL_POSITION, light1_position);

    applyLighting();   // intensites + couleur de fond, fonction de m_brightness

    // Modeles STL quelconques : pas de culling + eclairage deux faces, et normalisation
    // des normales (necessaire car on applique un glScalef de mise a l'echelle).
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_DEPTH_TEST);

    loadTexture(":/textures/boisClair.jpg", &texture);
}
//-----------------------------------------------------------------------------------------------
void C3dView::applyLighting(void) {
    const float b = m_brightness;

    // Lumiere cle (claire) + remplissage (douce) + ambiante globale, le tout * b.
    GLfloat light0_ambient[]  = { 0.45f*b, 0.45f*b, 0.45f*b, 1.0f };
    GLfloat light0_diffuse[]  = { 0.95f*b, 0.95f*b, 0.92f*b, 1.0f };
    GLfloat light0_specular[] = { 0.50f*b, 0.50f*b, 0.50f*b, 1.0f };
    GLfloat light1_diffuse[]  = { 0.45f*b, 0.47f*b, 0.55f*b, 1.0f };
    GLfloat global_ambient[]  = { 0.40f*b, 0.40f*b, 0.40f*b, 1.0f };

    glLightfv(GL_LIGHT0, GL_AMBIENT,  light0_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  light0_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);
    glLightfv(GL_LIGHT1, GL_DIFFUSE,  light1_diffuse);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);

    // Fond legerement eclairci avec la luminosite (clamp a blanc).
    int g = static_cast<int>(std::min(1.0f, 0.82f * b) * 255.0f);
    int r = std::min(255, g + 4);
    qglClearColor(QColor(r, std::min(255, g + 2), std::min(255, g + 8)));
}
//-----------------------------------------------------------------------------------------------
void C3dView::setBrightness(float b) {
    m_brightness = b;
    makeCurrent();
    applyLighting();
    doneCurrent();
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    GLdouble x = static_cast<GLdouble>(width) / static_cast<GLdouble>(height ? height : 1);
    glFrustum(-x, x, -1.0, 1.0, 4.0, 15.0);
    glMatrixMode(GL_MODELVIEW);
}
//-----------------------------------------------------------------------------------------------
void C3dView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -11.0);

    glRotatef(rotx, 1.0, 0.0, 0.0);
    glRotatef(roty, 0.0, 1.0, 0.0);
    glRotatef(rotz, 0.0, 0.0, 1.0);

    glScalef(scale, scale, scale);

    draw();
    drawAxisIndicator();
}
//-----------------------------------------------------------------------------------------------
// Repere XYZ en bas a gauche : rouge=X, vert=Y, bleu=Z.
// Rendu dans un petit viewport independant avec uniquement les rotations de la scene.
void C3dView::drawAxisIndicator(void) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    const int sz = 70, mg = 8;
    glViewport(mg, mg, sz, sz);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1.8, 1.8, -1.8, 1.8, -5.0, 5.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRotatef(rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(roty, 0.0f, 1.0f, 0.0f);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.5f);

    const float L = 1.0f;
    glBegin(GL_LINES);
    glColor3f(0.9f, 0.15f, 0.15f); glVertex3f(0,0,0); glVertex3f(L,0,0);  // X rouge
    glColor3f(0.15f, 0.80f, 0.15f); glVertex3f(0,0,0); glVertex3f(0,L,0); // Y vert
    glColor3f(0.25f, 0.45f, 1.00f); glVertex3f(0,0,0); glVertex3f(0,0,L); // Z bleu
    glEnd();

    // Petits points aux extremites pour les distinguer
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    glColor3f(0.9f, 0.15f, 0.15f); glVertex3f(L,0,0);
    glColor3f(0.15f, 0.80f, 0.15f); glVertex3f(0,L,0);
    glColor3f(0.25f, 0.45f, 1.00f); glVertex3f(0,0,L);
    glEnd();

    // Lettres X / Y / Z (segments de lignes) au bout de chaque axe, plan XY.
    const float h = 0.16f;          // demi-taille du glyphe
    const float xc = L + 0.30f;     // centre lettre X (sur +X)
    const float yc = L + 0.30f;     // centre lettre Y (sur +Y)
    const float zc = L + 0.30f;     // centre lettre Z (sur +Z)
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    // X (rouge) : deux diagonales croisees
    glColor3f(0.9f, 0.15f, 0.15f);
    glVertex3f(xc - h, -h, 0); glVertex3f(xc + h,  h, 0);
    glVertex3f(xc - h,  h, 0); glVertex3f(xc + h, -h, 0);
    // Y (vert) : deux branches + tige
    glColor3f(0.15f, 0.80f, 0.15f);
    glVertex3f(-h, yc + h, 0); glVertex3f(0, yc, 0);
    glVertex3f( h, yc + h, 0); glVertex3f(0, yc, 0);
    glVertex3f( 0, yc,     0); glVertex3f(0, yc - h, 0);
    // Z (bleu) : haut, diagonale, bas
    glColor3f(0.25f, 0.45f, 1.00f);
    glVertex3f(-h,  h, zc); glVertex3f( h,  h, zc);
    glVertex3f( h,  h, zc); glVertex3f(-h, -h, zc);
    glVertex3f(-h, -h, zc); glVertex3f( h, -h, zc);
    glEnd();

    glLineWidth(1.0f);
    glPointSize(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glColor3f(1,1,1);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glViewport(vp[0], vp[1], vp[2], vp[3]);
}
//-----------------------------------------------------------------------------------------------
void C3dView::wheelEvent(QWheelEvent * event) {
    event->angleDelta().y() > 0 ? scale += scale*0.1f : scale -= scale*0.1f;
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::mousePressEvent(QMouseEvent * event) {
    lastPos = event->pos();
}
//-----------------------------------------------------------------------------------------------
void C3dView::mouseMoveEvent(QMouseEvent * event) {
    int dx = event->x() - lastPos.x();
    int dy = event->y() - lastPos.y();

    if (event->buttons() & Qt::LeftButton) {
        rotx += 0.5f * dy;
        roty += 0.5f * dx;
    } else if (event->buttons() & Qt::RightButton) {
        rotx += 0.5f * dy;
        rotz += 0.5f * dx;
    }

    updateGL();

    lastPos = event->pos();
}
//-----------------------------------------------------------------------------------------------
bool C3dView::loadMesh(const QString &path) {
    if (!m_mesh.loadSTL(path)) {
        m_hasMesh = false;
        return false;
    }
    computeFit();
    m_hasMesh = true;
    scale = 1.0f;
    updateGL();
    qDebug() << "Mesh charge :" << m_mesh.triangles().size() << "triangles";
    return true;
}
//-----------------------------------------------------------------------------------------------
void C3dView::computeFit(void) {
    m_fitCenter = m_mesh.center();
    SVec3 sz = m_mesh.size();
    float maxDim = std::max(sz.x, std::max(sz.y, sz.z));
    m_fitScale = maxDim > 1e-6f ? 3.0f / maxDim : 1.0f;
}
//-----------------------------------------------------------------------------------------------
void C3dView::setSlices(const std::vector<CSlice> &slices, CSlicer::Axis axis,
                        float thickness, float gap) {
    m_slices = slices;
    m_sliceAxis = axis;
    m_sliceThickness = thickness;
    m_sliceGap = gap;
    m_sliceMode = !slices.empty();
    if (m_sliceMode)
        computeSliceFit();
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::clearSlices(void) {
    m_slices.clear();
    m_sliceMode = false;
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::setBoard(bool generated, bool plateVisible, bool smooth,
                       float scaleMmPerUnit, float thicknessMm) {
    m_boardEnabled = generated;
    m_boardVisible = plateVisible;
    m_boardSmooth  = smooth;
    m_boardScale   = scaleMmPerUnit > 0 ? scaleMmPerUnit : 1.0f;
    m_boardThickMm = thicknessMm > 0 ? thicknessMm : 1.0f;
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::setSlicesVisible(bool visible) {
    m_showSlices = visible;
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::setBoardColor(const QColor &c) {
    m_boardColor[0] = float(c.redF());
    m_boardColor[1] = float(c.greenF());
    m_boardColor[2] = float(c.blueF());
    updateGL();
}
//-----------------------------------------------------------------------------------------------
void C3dView::setMinIslandArea(float areaModelUnits) {
    m_minIslandArea = areaModelUnits > 0.0f ? areaModelUnits : 0.0f;
    updateGL();
}
//-----------------------------------------------------------------------------------------------
std::vector<Contour> C3dView::visibleContours(const CSlice &sl) const {
    std::vector<Contour> c = sl.contours;
    filterSmallIslands(c, m_minIslandArea);
    return c;
}
//-----------------------------------------------------------------------------------------------
// Fit du mode tranches : l'echelle est basee sur l'etendue reelle de la pile assemblee.
// Axe de coupe = n * pitch (spread), axes perpendiculaires = dimensions originales du mesh.
// Ainsi le stack tient toujours dans le frustum et les proportions refletent l'objet assemble.
void C3dView::computeSliceFit(void) {
    const int n  = int(m_slices.size());
    const int ai = int(m_sliceAxis);
    const SVec3 sz = m_mesh.size();
    const float pitch = m_sliceThickness + m_sliceGap;

    // Etendue du stack le long de l'axe de coupe.
    const float spreadAxis = n > 0 ? float(n) * pitch : 1.0f;

    // Max sur les 3 dimensions assemblees : coupe=spread, croix=dims originales.
    float dims[3] = { sz.x, sz.y, sz.z };
    dims[ai] = spreadAxis;
    const float maxDim = std::max(dims[0], std::max(dims[1], dims[2]));
    m_sliceFitScale = maxDim > 1e-6f ? 3.0f / maxDim : 1.0f;

    // Centre geometrique de la pile etalee.
    const float lo = axisValue(m_mesh.bboxMin(), ai);
    float cArr[3] = { m_mesh.center().x, m_mesh.center().y, m_mesh.center().z };
    cArr[ai] = lo + spreadAxis * 0.5f;
    m_sliceFitCenter = SVec3{cArr[0], cArr[1], cArr[2]};
}
//-----------------------------------------------------------------------------------------------
void C3dView::draw(void) {
    if (m_sliceMode && !m_slices.empty())
        drawSlices();
    else if (m_hasMesh)
        drawMesh();
}
//-----------------------------------------------------------------------------------------------
// Rendu des lamelles : pour chaque tranche, faces avant/arriere remplies (tesselateur GLU, regle
// d'enroulement IMPAIR -> gere concavites + trous), extrudees de thickness le long de l'axe, +
// parois laterales. Les tranches sont espacees de thickness+gap -> effet "cote a cote".
void C3dView::drawSlices(void) {
    glScalef(m_sliceFitScale, m_sliceFitScale, m_sliceFitScale);
    glTranslatef(-m_sliceFitCenter.x, -m_sliceFitCenter.y, -m_sliceFitCenter.z);

    const int ai = int(m_sliceAxis);
    const float lo = axisValue(m_mesh.bboxMin(), ai);
    const float pitch = m_sliceThickness + m_sliceGap;    // pas = lamelle + vide
    const float halfT = m_sliceThickness * 0.5f;          // demi-epaisseur de la lamelle
    const SVec3 mn = m_mesh.bboxMin();
    const SVec3 sz = m_mesh.size();
    const float u0Joint = m_boardEnabled ? backPlaneU() : 0.0f;   // plan arriere (rabotage planche)

    if (m_showSlices) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    GLUtesselator *tess = gluNewTess();
    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA,   (_GLUfuncptr)tessBegin);
    gluTessCallback(tess, GLU_TESS_END_DATA,     (_GLUfuncptr)tessEnd);
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA,  (_GLUfuncptr)tessVertex);
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)tessCombine);

    TessCtx ctx;
    ctx.mnx = mn.x; ctx.mny = mn.y; ctx.szx = sz.x; ctx.szy = sz.y;

    // Vecteur normal/axe selon ai (+1 pour la face avant).
    GLdouble nrm[3] = { 0, 0, 0 };

    for (size_t s = 0; s < m_slices.size(); s++) {
        const CSlice &sl = m_slices[s];
        if (sl.contours.empty())
            continue;

        // Planche generee -> dos rabote + tenons ; sinon contours (filtres des petits ilots).
        std::vector<Contour> base = visibleContours(sl);
        if (base.empty()) continue;
        std::vector<Contour> joint;
        if (m_boardEnabled) joint = jointContours(sl, u0Joint);
        const std::vector<Contour> &contours = m_boardEnabled ? joint : base;

        const float center = lo + (sl.index + 0.5f) * pitch;
        const float front = center + halfT;
        const float back  = center - halfT;

        // --- Face avant (normale +axe) ---
        nrm[0] = nrm[1] = nrm[2] = 0; nrm[ai] = 1.0;
        glNormal3d(nrm[0], nrm[1], nrm[2]);
        gluTessNormal(tess, nrm[0], nrm[1], nrm[2]);
        {
            size_t total = 0;
            for (size_t c = 0; c < contours.size(); c++) total += contours[c].size();
            std::vector<GLdouble> buf(3 * (total ? total : 1));
            size_t idx = 0;
            gluTessBeginPolygon(tess, &ctx);
            for (size_t c = 0; c < contours.size(); c++) {
                const Contour &ct = contours[c];
                if (ct.size() < 3) continue;
                gluTessBeginContour(tess);
                for (size_t k = 0; k < ct.size(); k++) {
                    uvTo3D(ai, ct[k].x, ct[k].y, front, &buf[3 * idx]);
                    gluTessVertex(tess, &buf[3 * idx], &buf[3 * idx]);
                    idx++;
                }
                gluTessEndContour(tess);
            }
            gluTessEndPolygon(tess);
        }

        // --- Face arriere (normale -axe) ---
        nrm[0] = nrm[1] = nrm[2] = 0; nrm[ai] = -1.0;
        glNormal3d(nrm[0], nrm[1], nrm[2]);
        gluTessNormal(tess, nrm[0], nrm[1], nrm[2]);
        {
            size_t total = 0;
            for (size_t c = 0; c < contours.size(); c++) total += contours[c].size();
            std::vector<GLdouble> buf(3 * (total ? total : 1));
            size_t idx = 0;
            gluTessBeginPolygon(tess, &ctx);
            for (size_t c = 0; c < contours.size(); c++) {
                const Contour &ct = contours[c];
                if (ct.size() < 3) continue;
                gluTessBeginContour(tess);
                for (size_t k = 0; k < ct.size(); k++) {
                    uvTo3D(ai, ct[k].x, ct[k].y, back, &buf[3 * idx]);
                    gluTessVertex(tess, &buf[3 * idx], &buf[3 * idx]);
                    idx++;
                }
                gluTessEndContour(tess);
            }
            gluTessEndPolygon(tess);
        }

        // --- Parois laterales (une bande quad par contour) ---
        for (size_t c = 0; c < contours.size(); c++) {
            const Contour &ct = contours[c];
            const size_t n = ct.size();
            if (n < 3) continue;
            glBegin(GL_QUAD_STRIP);
            for (size_t k = 0; k <= n; k++) {
                const SPoint2 &p  = ct[k % n];
                const SPoint2 &pn = ct[(k + 1) % n];
                const SPoint2 &pp = ct[(k + n - 1) % n];
                // Normale en plan (perpendiculaire a la tangente), composante axe nulle.
                float tx = pn.x - pp.x, ty = pn.y - pp.y;
                float nu = ty, nv = -tx;
                float len = std::sqrt(nu * nu + nv * nv);
                if (len > 1e-9f) { nu /= len; nv /= len; }
                GLdouble nn[3];
                uvTo3D(ai, nu, nv, 0.0f, nn);   // place (nu,nv) dans le plan (u,v), axe=0
                glNormal3d(nn[0], nn[1], nn[2]);

                GLdouble vf[3], vb[3];
                uvTo3D(ai, p.x, p.y, front, vf);
                uvTo3D(ai, p.x, p.y, back, vb);
                const float texU = sz.x > 1e-6f ? (float(vf[0]) - mn.x) / sz.x : 0.0f;
                const float texV = sz.y > 1e-6f ? (float(vf[1]) - mn.y) / sz.y : 0.0f;
                glTexCoord2f(texU, texV); glVertex3dv(vf);
                glTexCoord2f(texU, texV); glVertex3dv(vb);
            }
            glEnd();
        }
    }

    gluDeleteTess(tess);
    for (size_t i = 0; i < ctx.combined.size(); i++)
        delete[] ctx.combined[i];

    glDisable(GL_TEXTURE_2D);
    }   // m_showSlices

    if (m_boardEnabled && m_boardVisible)
        drawBoard();   // meme transformation modele (fitScale + recentrage) deja appliquee
}
//-----------------------------------------------------------------------------------------------
// Plan arriere (u mini) commun a toutes les lamelles, en unites modele.
float C3dView::backPlaneU(void) const {
    float u0 = 1e30f;
    for (size_t s = 0; s < m_slices.size(); s++) {
        const std::vector<Contour> cs = visibleContours(m_slices[s]);
        for (size_t c = 0; c < cs.size(); c++)
            for (size_t k = 0; k < cs[c].size(); k++)
                u0 = std::min(u0, cs[c][k].x);
    }
    return u0;
}
//-----------------------------------------------------------------------------------------------
// Centres (en v) des tenons d'une tranche : 0 si flottante (dos hors contact) ou sans bande de
// contact exploitable. Mutualise par le rendu des lamelles et celui des mortaises de la planche.
std::vector<float> C3dView::sliceTabCenters(const CSlice &sl, float u0) const {
    std::vector<float> all;
    std::vector<Contour> cs = visibleContours(sl);
    if (cs.empty()) return all;
    const float scale = m_boardScale > 0 ? m_boardScale : 1.0f;
    const float t     = m_sliceThickness;   // dos rabote de l'epaisseur (vue) d'une lamelle
    const float tol   = t;
    const float halfW = (BoardJoint::kTabWidth / scale) * 0.5f;

    // Chaque contour EXTERIEUR (corps, anse, bec...) qui touche le fond recoit ses tenons.
    std::vector<char> outer = outerContourMask(cs);
    for (size_t c = 0; c < cs.size(); c++) {
        if (!outer[c]) continue;
        const std::vector<float> placed = BoardJoint::integrateContourBack(cs[c], u0, t, tol, halfW);
        all.insert(all.end(), placed.begin(), placed.end());
    }
    return all;
}
//-----------------------------------------------------------------------------------------------
// Intervalles v reellement couverts par une tranche : un par contour EXTERIEUR (meme orientation
// que le plus grand contour), fusionnes. Les trous (orientation opposee) ne reduisent pas la
// couverture ; les parties disjointes ne sont PAS comblees -> la planche epouse les creux.
std::vector<std::pair<float, float> > C3dView::boardVSpans(const CSlice &sl) const {
    std::vector<std::pair<float, float> > spans;
    const std::vector<Contour> cs = visibleContours(sl);
    if (cs.empty()) return spans;

    // Un intervalle v par contour EXTERIEUR (containment, pas le signe d'aire) -> n'oublie aucun
    // morceau disjoint (anse, bec) et ne comble pas les trous.
    const std::vector<char> outer = outerContourMask(cs);
    for (size_t c = 0; c < cs.size(); c++) {
        if (!outer[c] || cs[c].size() < 3) continue;
        float vmin = 1e30f, vmax = -1e30f;
        for (size_t k = 0; k < cs[c].size(); k++) {
            vmin = std::min(vmin, cs[c][k].y);
            vmax = std::max(vmax, cs[c][k].y);
        }
        if (vmax > vmin) spans.push_back(std::make_pair(vmin, vmax));
    }

    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<float, float> > merged;
    for (size_t i = 0; i < spans.size(); i++) {
        if (!merged.empty() && spans[i].first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, spans[i].second);
        else
            merged.push_back(spans[i]);
    }
    return merged;
}
//-----------------------------------------------------------------------------------------------
// Contours d'une lamelle, dos rabote de l'epaisseur planche + tenons jusqu'au plan arriere u0
// (planche integree dans la profondeur). Lamelle flottante (sans contact) -> contours inchanges.
std::vector<Contour> C3dView::jointContours(const CSlice &sl, float u0) const {
    std::vector<Contour> out = visibleContours(sl);
    if (out.empty()) return out;

    const float scale = m_boardScale > 0 ? m_boardScale : 1.0f;
    const float t     = m_sliceThickness;   // dos rabote de l'epaisseur (vue) d'une lamelle
    const float tol   = t;
    const float halfW = (BoardJoint::kTabWidth / scale) * 0.5f;

    // Chaque contour exterieur (corps, anse, bec...) atteignant le fond est rabote + tenonne.
    const std::vector<char> outer = outerContourMask(out);
    for (size_t c = 0; c < out.size(); c++)
        if (outer[c])
            BoardJoint::integrateContourBack(out[c], u0, t, tol, halfW);
    return out;
}
//-----------------------------------------------------------------------------------------------
// Planche de fond : plaque unie le long de l'axe de coupe, integree dans la profondeur du modele
// (u ∈ [u0, u0+t]). Tesselee comme une vraie plaque trouee = silhouette (intervalles v reels, sans
// combler les creux) percee des MORTAISES la ou passent les tenons -> les fentes sont visibles
// quand on affiche la planche seule. Couleur unie (pas de texture bois).
void C3dView::drawBoard(void) {
    if (m_slices.empty())
        return;

    const int ai = int(m_sliceAxis);
    const SVec3 mn = m_mesh.bboxMin();
    const SVec3 sz = m_mesh.size();
    const float lo    = axisValue(mn, ai);
    const float pitch = m_sliceThickness + m_sliceGap;
    const float halfT = m_sliceThickness * 0.5f;

    const float scale  = m_boardScale > 0 ? m_boardScale : 1.0f;
    const float tModel = m_sliceThickness;   // meme epaisseur (vue) qu'une lamelle, cf. ci-dessous
    const float halfW  = (BoardJoint::kTabWidth / scale) * 0.5f;

    const float u0 = backPlaneU();
    if (u0 > 1e29f)
        return;
    const float uFront = u0 + tModel;

    glDisable(GL_TEXTURE_2D);
    GLfloat boardCol[4] = { m_boardColor[0], m_boardColor[1], m_boardColor[2], 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, boardCol);

    // Direction unitaire de l'axe u (normale des faces de la plaque) en 3D.
    GLdouble uDir[3]; uvTo3D(ai, 1.0f, 0.0f, 0.0f, uDir);

    GLUtesselator *tess = gluNewTess();
    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA,   (_GLUfuncptr)tessBegin);
    gluTessCallback(tess, GLU_TESS_END_DATA,     (_GLUfuncptr)tessEnd);
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA,  (_GLUfuncptr)tessVertex);
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)tessCombine);
    TessCtx ctx; ctx.mnx = mn.x; ctx.mny = mn.y; ctx.szx = sz.x; ctx.szy = sz.y;

    // Lobes LISSES de la silhouette (corps / anse / bec separes, creux non comblés) + toutes les
    // MORTAISES (trous), construits en plan (axe, v) puis tesselles/extrudes en profondeur (u).
    std::vector<std::pair<float, std::vector<std::pair<float, float> > > > spansBySlice;
    std::vector<Contour> holes;
    for (size_t s = 0; s < m_slices.size(); s++) {
        const CSlice &sl = m_slices[s];
        if (sl.contours.empty()) continue;
        const std::vector<std::pair<float, float> > spans = boardVSpans(sl);
        if (spans.empty()) continue;
        const float center = lo + (sl.index + 0.5f) * pitch;
        spansBySlice.push_back(std::make_pair(center, spans));
        const std::vector<float> centers = sliceTabCenters(sl, u0);
        for (size_t ci = 0; ci < centers.size(); ci++) {
            Contour h;
            h.push_back(SPoint2(center - halfT, centers[ci] - halfW));
            h.push_back(SPoint2(center + halfT, centers[ci] - halfW));
            h.push_back(SPoint2(center + halfT, centers[ci] + halfW));
            h.push_back(SPoint2(center - halfT, centers[ci] + halfW));
            holes.push_back(h);
        }
    }

    // Fond connexe (enveloppe + trous de creux). Lisse = diagonale ; escalier = marches larges de
    // la lamelle (halfT) -> decrochés. Le trou de l'anse est preserve (bandes gaps separees).
    std::vector<std::pair<float, std::vector<std::pair<float, float> > > > env, gaps;
    boardEnvelopeAndGaps(spansBySlice, env, gaps);
    const float treadHW = m_boardSmooth ? 0.0f : halfT;
    const float maxStep = pitch * 1.5f;                // au-dela = tranche sautee -> on ne ponte pas
    std::vector<Contour> cols          = buildSmoothLobes(env,  pitch * 0.5f, treadHW, maxStep, false);
    const std::vector<Contour> gapHoles = buildSmoothLobes(gaps, pitch * 0.5f, treadHW, maxStep, true);
    cols.insert(cols.end(), gapHoles.begin(), gapHoles.end());
    const size_t nOuter = cols.size();                 // les suivants sont des trous (mortaises)
    cols.insert(cols.end(), holes.begin(), holes.end());
    if (cols.empty()) { gluDeleteTess(tess); return; }

    // --- Faces avant (u+) et arriere (u-), trouees par les mortaises (regle IMPAIR) ---
    for (int face = 0; face < 2; face++) {
        const float depth = face == 0 ? uFront : u0;
        const double sgn  = face == 0 ? 1.0 : -1.0;
        glNormal3d(sgn * uDir[0], sgn * uDir[1], sgn * uDir[2]);
        gluTessNormal(tess, sgn * uDir[0], sgn * uDir[1], sgn * uDir[2]);
        size_t total = 0;
        for (size_t c = 0; c < cols.size(); c++) total += cols[c].size();
        std::vector<GLdouble> buf(3 * (total ? total : 1));
        size_t idx = 0;
        gluTessBeginPolygon(tess, &ctx);
        for (size_t c = 0; c < cols.size(); c++) {
            if (cols[c].size() < 3) continue;
            gluTessBeginContour(tess);
            for (size_t k = 0; k < cols[c].size(); k++) {
                uvTo3D(ai, depth, cols[c][k].y, cols[c][k].x, &buf[3 * idx]);
                gluTessVertex(tess, &buf[3 * idx], &buf[3 * idx]);
                idx++;
            }
            gluTessEndContour(tess);
        }
        gluTessEndPolygon(tess);
    }

    // --- Parois (silhouette + bords des mortaises) le long de u ---
    for (size_t c = 0; c < cols.size(); c++) {
        const Contour &ct = cols[c];
        const size_t n = ct.size();
        if (n < 3) continue;
        glBegin(GL_QUAD_STRIP);
        for (size_t k = 0; k <= n; k++) {
            const SPoint2 &p  = ct[k % n];
            const SPoint2 &pn = ct[(k + 1) % n];
            const SPoint2 &pp = ct[(k + n - 1) % n];
            float tAxis = pn.x - pp.x, tV = pn.y - pp.y;   // tangente en (axe,v)
            float nAxis = tV, nV = -tAxis;
            const float len = std::sqrt(nAxis * nAxis + nV * nV);
            if (len > 1e-9f) { nAxis /= len; nV /= len; }
            const float dir = (c < nOuter) ? 1.0f : -1.0f;  // trous : normale inversee
            GLdouble nn[3];
            uvTo3D(ai, 0.0f, dir * nV, dir * nAxis, nn);
            glNormal3d(nn[0], nn[1], nn[2]);

            GLdouble vf[3], vb[3];
            uvTo3D(ai, uFront, p.y, p.x, vf);
            uvTo3D(ai, u0,     p.y, p.x, vb);
            glVertex3dv(vf);
            glVertex3dv(vb);
        }
        glEnd();
    }

    gluDeleteTess(tess);
    for (size_t i = 0; i < ctx.combined.size(); i++)
        delete[] ctx.combined[i];

    // Restaure le materiau blanc (texture bois) pour le reste de la scene.
    GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white);
}
//-----------------------------------------------------------------------------------------------
void C3dView::drawMesh(void) {
    // Normalisation : centrer le mesh sur l'origine et le mettre a l'echelle du frustum.
    glScalef(m_fitScale, m_fitScale, m_fitScale);
    glTranslatef(-m_fitCenter.x, -m_fitCenter.y, -m_fitCenter.z);

    const SVec3 mn = m_mesh.bboxMin();
    const SVec3 sz = m_mesh.size();
    const float ix = sz.x > 1e-6f ? 1.0f / sz.x : 1.0f;
    const float iy = sz.y > 1e-6f ? 1.0f / sz.y : 1.0f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_TRIANGLES);
    const std::vector<CMesh::STriangle> &tris = m_mesh.triangles();
    for (size_t i = 0; i < tris.size(); i++) {
        const CMesh::STriangle &t = tris[i];
        glNormal3f(t.normal.x, t.normal.y, t.normal.z);
        for (int k = 0; k < 3; k++) {
            float u = (t.v[k].x - mn.x) * ix;   // projection planaire simple (UV bois)
            float v = (t.v[k].y - mn.y) * iy;
            glTexCoord2f(u, v);
            glVertex3f(t.v[k].x, t.v[k].y, t.v[k].z);
        }
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);
}
//-----------------------------------------------------------------------------------------------
void C3dView::loadTexture(QString textureName, GLuint *texture) {
    QImage im(textureName);
    QImage tex = QGLWidget::convertToGLFormat(im);

    glEnable(GL_TEXTURE_2D);

    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // MODULATE : la texture est modulee par l'eclairage -> le relief du modele reste lisible.
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width(), tex.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.bits());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glDisable(GL_TEXTURE_2D);
}
//-----------------------------------------------------------------------------------------------
