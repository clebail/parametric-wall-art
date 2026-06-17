//-----------------------------------------------------------------------------------------------
#ifndef CSLICER_H
#define CSLICER_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include "geometry.h"
#include "CMesh.h"
//-----------------------------------------------------------------------------------------------
// Une tranche : section plane du maillage, projetee en 2D (u,v), sous forme de boucles fermees.
//-----------------------------------------------------------------------------------------------
class CSlice
{
public:
    CSlice(void) : index(0), position(0) {}

    int index;                        // ordre d'assemblage (0..n-1)
    float position;                   // coordonnee le long de l'axe de coupe (au centre du slab)
    std::vector<Contour> contours;    // boucles fermees dans le plan (u,v)
};
//-----------------------------------------------------------------------------------------------
// Moteur de tranchage : coupe le maillage par une pile de plans perpendiculaires a un axe X/Y/Z.
//-----------------------------------------------------------------------------------------------
class CSlicer
{
public:
    enum Axis { AxisX = 0, AxisY = 1, AxisZ = 2 };

    CSlicer(void) : m_thickness(0) {}

    // Tranche le maillage en nbSlices lamelles le long de l'axe. Chaque plan coupe AU CENTRE
    // de son slab (evite les hits degeneres sur sommets/aretes et represente la section mediane).
    std::vector<CSlice> slice(const CMesh &mesh, Axis axis, int nbSlices);

    // Epaisseur d'un slab du dernier appel a slice() (= taille_axe / nbSlices).
    float thickness(void) const { return m_thickness; }

    // Projection d'un point 3D dans le plan 2D (u,v) selon l'axe de coupe. u = profondeur (normale
    // a la planche de fond) = TOUJOURS Z -> la planche est toujours sur le plan X,Y.
    //   AxisX -> (u=Z, v=Y) ; AxisY -> (u=Z, v=X).
    static SPoint2 project(const SVec3 &p, Axis axis);

    // Aire signee d'un contour (>0 : sens trigo / CCW, <0 : sens horaire / CW).
    static float signedArea(const Contour &c);

private:
    float m_thickness;

    // Intersection d'un triangle par le plan {axisValue == pos} : ajoute 0 ou 1 segment.
    static void intersectTriangle(const CMesh::STriangle &t, Axis axis, float pos,
                                  std::vector<SPoint2> &outPts);

    // Couture des segments (paires de points consecutifs) en boucles fermees.
    static std::vector<Contour> stitch(const std::vector<SPoint2> &segPts, float eps);
};
//-----------------------------------------------------------------------------------------------
#endif // CSLICER_H
//-----------------------------------------------------------------------------------------------
