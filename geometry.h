//-----------------------------------------------------------------------------------------------
#ifndef GEOMETRY_H
#define GEOMETRY_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <cmath>
//-----------------------------------------------------------------------------------------------
// Types geometriques legers partages par le loader, le slicer et l'export.
//-----------------------------------------------------------------------------------------------
struct SVec3 {
    float x, y, z;
    SVec3(void) : x(0), y(0), z(0) {}
    SVec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
//-----------------------------------------------------------------------------------------------
struct SPoint2 {
    float x, y;
    SPoint2(void) : x(0), y(0) {}
    SPoint2(float x_, float y_) : x(x_), y(y_) {}
};
//-----------------------------------------------------------------------------------------------
// Contour ferme (fermeture implicite : le dernier point se relie au premier).
typedef std::vector<SPoint2> Contour;
//-----------------------------------------------------------------------------------------------
// Geometrie du joint socle/lamelle (mm), partagee par le plan de decoupe et la previsualisation.
namespace BoardJoint {
const float kTabWidth  = 10.0f;   // largeur d'un tenon/mortaise (mm), constante
const float kTwoTabMin = 40.0f;   // hauteur de contact au-dela de laquelle on met 2 tenons
const float kTabPad    = 4.0f;    // retrait des tenons par rapport aux bords du contact (mm)
}
//-----------------------------------------------------------------------------------------------
inline SVec3 operator-(const SVec3 &a, const SVec3 &b) { return SVec3(a.x-b.x, a.y-b.y, a.z-b.z); }
inline SVec3 operator+(const SVec3 &a, const SVec3 &b) { return SVec3(a.x+b.x, a.y+b.y, a.z+b.z); }
inline SVec3 operator*(const SVec3 &a, float s)        { return SVec3(a.x*s, a.y*s, a.z*s); }
inline float dot(const SVec3 &a, const SVec3 &b)       { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float length(const SVec3 &a)                    { return std::sqrt(dot(a, a)); }
//-----------------------------------------------------------------------------------------------
// Acces a une composante par index d'axe (0=X, 1=Y, 2=Z).
inline float axisValue(const SVec3 &v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}
//-----------------------------------------------------------------------------------------------
#endif // GEOMETRY_H
//-----------------------------------------------------------------------------------------------
