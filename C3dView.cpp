//-----------------------------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <QtDebug>
#include <QColor>
#include <QWheelEvent>
#include <QMouseEvent>
#include "C3dView.h"
//-----------------------------------------------------------------------------------------------
C3dView::C3dView(QWidget *parent) : QGLWidget(parent) {
    scale = 1.0;
    rotx = roty = rotz = 0.0;
    roty = 45.0;
    rotx = 30.0;
    m_hasMesh = false;
    m_fitScale = 1.0f;
    m_brightness = 1.0f;
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
}
//-----------------------------------------------------------------------------------------------
void C3dView::wheelEvent(QWheelEvent * event) {
    event->delta() > 0 ? scale += scale*0.1f : scale -= scale*0.1f;
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
void C3dView::draw(void) {
    if (m_hasMesh)
        drawMesh();
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
