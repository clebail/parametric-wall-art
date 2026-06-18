//-----------------------------------------------------------------------------------------------
#ifndef C3DVIEW_H
#define C3DVIEW_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <utility>
#include <QGLWidget>
#include <QOpenGLTexture>
#include <QString>
#include "CMesh.h"
#include "CSlicer.h"
//-----------------------------------------------------------------------------------------------
class QColor;
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

    // Planche de fond. generated : la planche est creee (les lamelles sont rabotees du fond et
    // recoivent des tenons) ; plateVisible : la plaque est dessinee. scale en mm/unite, epais. mm.
    void setBoard(bool generated, bool plateVisible, bool smooth,
                  float scaleMmPerUnit, float thicknessMm);
    void setSlicesVisible(bool visible);   // affiche/masque les lamelles dans le rendu tranches
    void setBoardColor(const QColor &c);   // couleur unie de la planche (pas de texture bois)
    void setMinIslandArea(float areaModelUnits);   // filtre les petits ilots (aire en unites modele^2)

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
    bool m_showSlices;          // affiche les lamelles (sinon seul le socle est rendu)
    SVec3 m_sliceFitCenter;
    float m_sliceFitScale;

    // Planche de fond (previsualisation).
    bool m_boardEnabled;      // planche generee (raboute les lamelles + tenons)
    bool m_boardVisible;      // plaque dessinee dans l'apercu
    bool m_boardSmooth;       // fond lisse (silhouette) ; false = escalier par lamelle
    float m_boardScale;       // mm/unite
    float m_boardThickMm;     // epaisseur lamelle/socle en mm
    float m_boardColor[3];    // couleur unie de la planche (RGB 0..1)
    float m_minIslandArea;    // aire mini d'un contour (unites modele^2) ; en dessous -> filtre

    void draw(void);
    void drawMesh(void);
    void drawSlices(void);
    void drawBoard(void);
    // Contours d'une lamelle pour le rendu : dos rabote + tenons si une planche est generee.
    std::vector<Contour> jointContours(const CSlice &sl, float u0) const;
    float backPlaneU(void) const;   // plan arriere (u mini) commun a toutes les lamelles
    // Tenons d'une tranche : (centre v, demi-largeur v) pour chaque tenon (un ou deux par contour
    // exterieur qui atteint le fond) -> sert a percer les mortaises a la MEME taille.
    std::vector<std::pair<float, float> > sliceTabCenters(const CSlice &sl, float u0) const;
    // Intervalles v couverts par la tranche (contours exterieurs seulement, creux non combles).
    std::vector<std::pair<float, float> > boardVSpans(const CSlice &sl, float u0) const;
    // Contours d'une tranche apres filtrage des petits ilots (m_minIslandArea).
    std::vector<Contour> visibleContours(const CSlice &sl) const;
    void drawAxisIndicator(void);
    void computeFit(void);
    void computeSliceFit(void);
    void applyLighting(void);
    void loadTexture(QString textureName, GLuint *texture);
};
//-----------------------------------------------------------------------------------------------
#endif // C3DVIEW_H
//-----------------------------------------------------------------------------------------------
