//-----------------------------------------------------------------------------------------------
#include <limits>
#include <algorithm>
#include <QFile>
#include <QDataStream>
#include <QTextStream>
#include <QStringList>
#include <QtDebug>
#include "CMesh.h"
//-----------------------------------------------------------------------------------------------
CMesh::CMesh(void) : m_bboxMin(), m_bboxMax() {
}
//-----------------------------------------------------------------------------------------------
// Auto-detection binaire/ASCII : on ne se fie PAS au mot-cle "solid" (un STL binaire peut
// commencer par "solid"). On compare la taille du fichier a 84 + nbTriangles*50.
bool CMesh::loadSTL(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "CMesh: impossible d'ouvrir" << path;
        return false;
    }

    qint64 fileSize = f.size();
    bool isBinary = false;

    if (fileSize >= 84) {
        f.seek(80);
        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::LittleEndian);
        quint32 count = 0;
        ds >> count;
        if (fileSize == 84 + qint64(count) * 50)
            isBinary = true;
    }
    f.close();

    bool ok = isBinary ? loadBinary(path) : loadAscii(path);
    if (ok)
        computeBounds();
    else
        qWarning() << "CMesh: aucun triangle charge depuis" << path;
    return ok;
}
//-----------------------------------------------------------------------------------------------
bool CMesh::loadBinary(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

    f.seek(80);
    quint32 count = 0;
    ds >> count;

    m_tris.clear();
    m_tris.reserve(count);

    for (quint32 i = 0; i < count; i++) {
        STriangle t;
        ds >> t.normal.x >> t.normal.y >> t.normal.z;
        for (int k = 0; k < 3; k++)
            ds >> t.v[k].x >> t.v[k].y >> t.v[k].z;
        quint16 attr = 0;
        ds >> attr;

        if (ds.status() != QDataStream::Ok) {
            qWarning() << "CMesh: STL binaire tronque au triangle" << i;
            break;
        }
        m_tris.push_back(t);
    }
    return !m_tris.empty();
}
//-----------------------------------------------------------------------------------------------
bool CMesh::loadAscii(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream ts(&f);
    m_tris.clear();

    STriangle t;
    int vi = 0;

    while (!ts.atEnd()) {
        QString line = ts.readLine().simplified();
        if (line.startsWith("facet normal")) {
            QStringList p = line.split(' ');
            if (p.size() >= 5)
                t.normal = SVec3(p[2].toFloat(), p[3].toFloat(), p[4].toFloat());
            vi = 0;
        } else if (line.startsWith("vertex")) {
            QStringList p = line.split(' ');
            if (p.size() >= 4 && vi < 3) {
                t.v[vi] = SVec3(p[1].toFloat(), p[2].toFloat(), p[3].toFloat());
                vi++;
            }
        } else if (line.startsWith("endfacet")) {
            if (vi == 3)
                m_tris.push_back(t);
            vi = 0;
        }
    }
    return !m_tris.empty();
}
//-----------------------------------------------------------------------------------------------
void CMesh::computeBounds(void) {
    if (m_tris.empty()) {
        m_bboxMin = m_bboxMax = SVec3();
        return;
    }

    const float big = std::numeric_limits<float>::max();
    m_bboxMin = SVec3( big,  big,  big);
    m_bboxMax = SVec3(-big, -big, -big);

    for (size_t i = 0; i < m_tris.size(); i++) {
        for (int k = 0; k < 3; k++) {
            const SVec3 &p = m_tris[i].v[k];
            m_bboxMin.x = std::min(m_bboxMin.x, p.x);
            m_bboxMin.y = std::min(m_bboxMin.y, p.y);
            m_bboxMin.z = std::min(m_bboxMin.z, p.z);
            m_bboxMax.x = std::max(m_bboxMax.x, p.x);
            m_bboxMax.y = std::max(m_bboxMax.y, p.y);
            m_bboxMax.z = std::max(m_bboxMax.z, p.z);
        }
    }
}
//-----------------------------------------------------------------------------------------------
SVec3 CMesh::size(void) const {
    return m_bboxMax - m_bboxMin;
}
//-----------------------------------------------------------------------------------------------
SVec3 CMesh::center(void) const {
    return (m_bboxMin + m_bboxMax) * 0.5f;
}
//-----------------------------------------------------------------------------------------------
