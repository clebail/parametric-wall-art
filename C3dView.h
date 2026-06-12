//-----------------------------------------------------------------------------------------------
#ifndef C3DVIEW_H
#define C3DVIEW_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <QGLWidget>
#include <QOpenGLTexture>
#include <QString>
#include "CMesh.h"
#include "CSlicer.h"
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
    const CMesh & mesh(void) const { return m_mesh; }
    void setBrightness(float b);    // facteur multiplicatif de luminosite (1.0 = defaut)

    // Previsualisation des tranches : lamelles extrudees espacees le long de l'axe (cote a cote).
    void setSlices(const std::vector<CSlice> &slices, CSlicer::Axis axis,
                   float thickness, float gap);
    void clearSlices(void);         // revient a l'affichage du mesh brut

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

    // Mode tranches.
    std::vector<CSlice> m_slices;
    CSlicer::Axis m_sliceAxis;
    float m_sliceThickness, m_sliceGap;
    bool m_sliceMode;
    SVec3 m_sliceFitCenter;
    float m_sliceFitScale;

    void draw(void);
    void drawMesh(void);
    void drawSlices(void);
    void computeFit(void);
    void computeSliceFit(void);
    void applyLighting(void);   // (re)applique les intensites lumineuses * m_brightness
    void loadTexture(QString textureName, GLuint *texture);
};
//-----------------------------------------------------------------------------------------------
#endif // C3DVIEW_H
//-----------------------------------------------------------------------------------------------
