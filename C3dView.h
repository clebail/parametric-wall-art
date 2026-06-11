//-----------------------------------------------------------------------------------------------
#ifndef C3DVIEW_H
#define C3DVIEW_H
//-----------------------------------------------------------------------------------------------
#include <QGLWidget>
#include <QOpenGLTexture>
#include <QString>
#include "CMesh.h"
//-----------------------------------------------------------------------------------------------
class C3dView : public QGLWidget {
    Q_OBJECT
public:
    explicit C3dView(QWidget *parent = nullptr);
    ~C3dView(void);
    void initializeGL();
    void resizeGL(int width, int height);
    void paintGL();

    bool loadMesh(const QString &path);
    void setBrightness(float b);    // facteur multiplicatif de luminosite (1.0 = defaut)

protected:
    virtual void wheelEvent(QWheelEvent * event);
    virtual void mousePressEvent(QMouseEvent * event);
    virtual void mouseMoveEvent(QMouseEvent * event);
private:
    float scale;
    float roty, rotx, rotz;
    QPoint lastPos;
    GLuint texture;

    CMesh m_mesh;
    bool m_hasMesh;
    SVec3 m_fitCenter;          // centre du mesh (recentrage)
    float m_fitScale;           // facteur de normalisation pour tenir dans le frustum
    float m_brightness;         // luminosite reglable (slider)

    void draw(void);
    void drawMesh(void);
    void computeFit(void);
    void applyLighting(void);   // (re)applique les intensites lumineuses * m_brightness
    void loadTexture(QString textureName, GLuint *texture);
};
//-----------------------------------------------------------------------------------------------
#endif // C3DVIEW_H
//-----------------------------------------------------------------------------------------------
