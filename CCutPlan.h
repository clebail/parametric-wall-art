//-----------------------------------------------------------------------------------------------
#ifndef CCUTPLAN_H
#define CCUTPLAN_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <QString>
#include "geometry.h"
#include "CSlicer.h"
//-----------------------------------------------------------------------------------------------
// Plan de decoupe : place les tranches sur des feuilles (nesting simple par rangees) et exporte
// un fichier SVG + un fichier DXF (R12) par feuille, pret pour laser / CNC.
//-----------------------------------------------------------------------------------------------
class CCutPlan
{
public:
    struct Params {
        float scale;              // mm par unite du modele
        float sheetW, sheetH;     // taille feuille en mm
        float margin;             // marge de bord en mm
        float spacing;            // espacement entre pieces en mm
        bool  generateBoard;      // generer une planche de fond (socle a fentes)
        float sliceThickness;     // epaisseur lamelle/socle en mm
        float gapThickness;       // epaisseur vide en mm (pas d'assemblage = lamelle + vide)
        Params(void) : scale(1.0f),
                       sheetW(600.0f), sheetH(400.0f), margin(10.0f), spacing(5.0f),
                       generateBoard(false), sliceThickness(10.0f), gapThickness(1.0f) {}
    };

    // Une piece placee sur une feuille : contours en mm, normalises (bbox min a l'origine),
    // a decaler de (tx,ty) sur la feuille.
    struct Piece {
        int sliceIndex;                 // numero d'ordre d'assemblage (etiquette ; -1 = planche de fond)
        std::vector<Contour> contours;  // mm, bbox min = (0,0) ; [0] = exterieur, suivants = trous
        float w, h;                     // dimensions bbox en mm
        float tx, ty;                   // position sur la feuille en mm
        int sheet;                      // index de feuille (0-based)
    };

    CCutPlan(void) : m_sheetCount(0) {}

    // Construit le nesting a partir des tranches. Ignore les tranches vides.
    bool build(const std::vector<CSlice> &slices, const Params &params);

    int sheetCount(void) const { return m_sheetCount; }
    int pieceCount(void) const { return int(m_pieces.size()); }
    const std::vector<Piece> & pieces(void) const { return m_pieces; }
    const Params & params(void) const { return m_params; }

    // Tranches non vides qui n'atteignent pas le plan du socle (impossible a accrocher).
    const std::vector<int> & floatingSlices(void) const { return m_floating; }

    // Exporte un fichier par feuille : <base>_sheetN.svg / <base>_sheetN.dxf.
    // base = chemin sans extension. Retourne false si une ecriture echoue.
    bool exportSVG(const QString &basePath) const;
    bool exportDXF(const QString &basePath) const;

private:
    Params m_params;
    std::vector<Piece> m_pieces;
    std::vector<int> m_floating;
    int m_sheetCount;

    bool writeSVGSheet(const QString &path, int sheet) const;
    bool writeDXFSheet(const QString &path, int sheet) const;
};
//-----------------------------------------------------------------------------------------------
#endif // CCUTPLAN_H
//-----------------------------------------------------------------------------------------------
