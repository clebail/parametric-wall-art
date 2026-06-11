//-----------------------------------------------------------------------------------------------
#ifndef CMESH_H
#define CMESH_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <QString>
#include "geometry.h"
//-----------------------------------------------------------------------------------------------
// Chargement d'un maillage STL (binaire ou ASCII, auto-detecte) + boite englobante.
//-----------------------------------------------------------------------------------------------
class CMesh
{
public:
    typedef struct _STriangle {
        SVec3 v[3];
        SVec3 normal;
    } STriangle;

    CMesh(void);

    bool loadSTL(const QString &path);

    const std::vector<STriangle> & triangles(void) const { return m_tris; }
    bool empty(void) const { return m_tris.empty(); }

    SVec3 bboxMin(void) const { return m_bboxMin; }
    SVec3 bboxMax(void) const { return m_bboxMax; }
    SVec3 size(void) const;
    SVec3 center(void) const;

private:
    std::vector<STriangle> m_tris;
    SVec3 m_bboxMin, m_bboxMax;

    bool loadBinary(const QString &path);
    bool loadAscii(const QString &path);
    void computeBounds(void);
};
//-----------------------------------------------------------------------------------------------
#endif // CMESH_H
//-----------------------------------------------------------------------------------------------
