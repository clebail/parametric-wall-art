//-----------------------------------------------------------------------------------------------
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include "CMainWindow.h"
//-----------------------------------------------------------------------------------------------
CMainWindow::CMainWindow(QWidget *parent) : QMainWindow(parent), m_hasMesh(false) {
    setupUi(this);

    connect(actionImporter,  SIGNAL(triggered()),              this, SLOT(onImport()));
    connect(sliderBrightness,SIGNAL(valueChanged(int)),        this, SLOT(onBrightnessChanged(int)));
    connect(m_importBtn,     SIGNAL(clicked()),                this, SLOT(onImport()));
    connect(m_exportBtn,     SIGNAL(clicked()),                this, SLOT(onExport()));

    connect(m_axisCombo,  SIGNAL(currentIndexChanged(int)), this, SLOT(onNbOrThickChanged()));
    connect(m_nbSlices,   SIGNAL(valueChanged(int)),        this, SLOT(onNbOrThickChanged()));
    connect(m_sliceThick, SIGNAL(valueChanged(double)),     this, SLOT(onNbOrThickChanged()));

    connect(m_gapThick, SIGNAL(valueChanged(double)), this, SLOT(onGapChanged()));

    connect(m_sheetW,  SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_sheetH,  SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_margin,  SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_spacing, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));

    connect(m_genBoard, SIGNAL(toggled(bool)), this, SLOT(onParamsChanged()));
}
//-----------------------------------------------------------------------------------------------
CMainWindow::~CMainWindow() {
}
//-----------------------------------------------------------------------------------------------
float CMainWindow::axisModelSize(void) const {
    const SVec3 sz = w3d->mesh().size();
    switch (m_axisCombo->currentIndex()) {
        case 0:  return sz.x;
        case 1:  return sz.y;
        default: return sz.z;
    }
}
//-----------------------------------------------------------------------------------------------
float CMainWindow::currentScale(void) const {
    if (!m_hasMesh) return 1.0f;
    const float axisSize = axisModelSize();
    if (axisSize <= 0.0f) return 1.0f;
    return float(m_sliceThick->value()) * float(m_nbSlices->value()) / axisSize;
}
//-----------------------------------------------------------------------------------------------
// Repartit le pas de tranche (constant, en unites modele) entre lamelle pleine et vide,
// selon le ratio mm lamelle:vide. Le pas total reste m_slicer.thickness() -> la pile ne
// s'allonge jamais quand on change le vide : la lamelle s'amincit, le vide grandit a la place.
void CMainWindow::sliceViewSplit(float &thickView, float &gapView) const {
    const float pitch = m_slicer.thickness();
    const float t     = float(m_sliceThick->value());
    const float g     = float(m_gapThick->value());
    const float denom = t + g;
    const float frac  = denom > 0.0f ? t / denom : 1.0f;
    thickView = pitch * frac;
    gapView   = pitch * (1.0f - frac);
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onImport(void) {
    QString path = QFileDialog::getOpenFileName(this, tr("Importer un modele STL"),
                                                QString(), tr("Fichiers STL (*.stl)"));
    if (path.isEmpty())
        return;

    if (w3d->loadMesh(path)) {
        m_hasMesh = true;
        setWindowTitle(tr("Parametric Wall Art — %1").arg(QFileInfo(path).fileName()));
        onNbOrThickChanged();
    } else {
        QMessageBox::warning(this, tr("Erreur"), tr("Chargement impossible : %1").arg(path));
    }
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onNbOrThickChanged(void) {
    if (!m_hasMesh) return;
    reslice();
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onGapChanged(void) {
    if (!m_hasMesh) return;
    float thickView, gapView;
    sliceViewSplit(thickView, gapView);
    w3d->setSlices(m_slices, CSlicer::Axis(m_axisCombo->currentIndex()),
                   thickView, gapView);
    m_plan.build(m_slices, currentParams());           // le vide change l'espacement du socle
    m_exportBtn->setEnabled(m_plan.pieceCount() > 0);
    applyBoardPreview();
    updateInfo();
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::applyBoardPreview(void) {
    w3d->setBoard(m_genBoard->isChecked(), currentScale(), float(m_sliceThick->value()));
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onParamsChanged(void) {
    if (!m_hasMesh) return;
    m_plan.build(m_slices, currentParams());
    m_exportBtn->setEnabled(m_plan.pieceCount() > 0);
    applyBoardPreview();
    updateInfo();
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::reslice(void) {
    const CSlicer::Axis axis = CSlicer::Axis(m_axisCombo->currentIndex());
    m_slices = m_slicer.slice(w3d->mesh(), axis, m_nbSlices->value());

    float thickView, gapView;
    sliceViewSplit(thickView, gapView);
    w3d->setSlices(m_slices, axis, thickView, gapView);
    applyBoardPreview();

    m_plan.build(m_slices, currentParams());
    m_exportBtn->setEnabled(m_plan.pieceCount() > 0);

    m_sliceThick->setEnabled(true);
    m_gapThick->setEnabled(true);

    updateInfo();
}
//-----------------------------------------------------------------------------------------------
CCutPlan::Params CMainWindow::currentParams(void) const {
    CCutPlan::Params p;
    p.scale          = currentScale();
    p.sheetW         = float(m_sheetW->value());
    p.sheetH         = float(m_sheetH->value());
    p.margin         = float(m_margin->value());
    p.spacing        = float(m_spacing->value());
    p.generateBoard  = m_genBoard->isChecked();
    p.sliceThickness = float(m_sliceThick->value());
    p.gapThickness   = float(m_gapThick->value());
    return p;
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::updateInfo(void) {
    if (!m_hasMesh) {
        m_scaleLabel->setText(tr("—"));
        m_sizeLabel->setText(tr("—"));
        m_info->setText(tr("Aucun modèle importé."));
        return;
    }
    const float s   = currentScale();
    const SVec3 sz  = w3d->mesh().size();
    const int    n  = m_nbSlices->value();
    const float gap = float(m_gapThick->value());
    // Le preview garde les proportions, mais l'objet physique monte au mur inclut les vides :
    // sur l'axe de coupe, profondeur reelle = n lamelles + (n-1) vides.
    const float assembledAxis = float(n) * float(m_sliceThick->value())
                                + (n > 1 ? n - 1 : 0) * gap;
    float dim[3] = { sz.x * s, sz.y * s, sz.z * s };
    dim[m_axisCombo->currentIndex()] = assembledAxis;
    m_scaleLabel->setText(tr("%1 mm/u").arg(s, 0, 'f', 3));
    m_sizeLabel->setText(tr("%1 × %2 × %3 mm")
                         .arg(dim[0], 0, 'f', 1)
                         .arg(dim[1], 0, 'f', 1)
                         .arg(dim[2], 0, 'f', 1));
    QString info = tr("Lamelle : %1 mm\nPièces : %2 — Feuilles : %3")
                    .arg(m_slicer.thickness() * s, 0, 'f', 2)
                    .arg(m_plan.pieceCount())
                    .arg(m_plan.sheetCount());

    // Alerte : lamelles ne touchant pas le socle (impossible a accrocher).
    if (m_genBoard->isChecked()) {
        const std::vector<int> &fl = m_plan.floatingSlices();
        if (!fl.empty()) {
            QStringList nums;
            for (size_t i = 0; i < fl.size() && i < 12; i++) nums << QString::number(fl[i]);
            QString list = nums.join(", ");
            if (fl.size() > 12) list += "…";
            info += tr("\n⚠ %1 lamelle(s) sans contact avec le fond : %2")
                        .arg(int(fl.size())).arg(list);
        }
    }
    m_info->setText(info);
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onExport(void) {
    if (m_plan.pieceCount() == 0) {
        QMessageBox::warning(this, tr("Export"), tr("Rien à exporter : importez d'abord un modèle."));
        return;
    }

    QString base = QFileDialog::getSaveFileName(this, tr("Exporter le plan de découpe"),
                                                QString(), tr("Plan (base de nom)"));
    if (base.isEmpty())
        return;

    QFileInfo fi(base);
    if (!fi.suffix().isEmpty())
        base = base.left(base.length() - fi.suffix().length() - 1);

    const bool okSvg = m_plan.exportSVG(base);
    const bool okDxf = m_plan.exportDXF(base);

    if (okSvg && okDxf)
        QMessageBox::information(this, tr("Export"),
            tr("Plan exporté : %1 feuille(s), %2 pièce(s).\nFichiers : %3_sheetN.svg / .dxf")
            .arg(m_plan.sheetCount()).arg(m_plan.pieceCount()).arg(QFileInfo(base).fileName()));
    else
        QMessageBox::warning(this, tr("Export"), tr("Échec de l'écriture d'un ou plusieurs fichiers."));
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onBrightnessChanged(int value) {
    w3d->setBrightness(value / 100.0f);
}
//-----------------------------------------------------------------------------------------------
