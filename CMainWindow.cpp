//-----------------------------------------------------------------------------------------------
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include "CMainWindow.h"
//-----------------------------------------------------------------------------------------------
CMainWindow::CMainWindow(QWidget *parent) : QMainWindow(parent), m_hasMesh(false) {
    setupUi(this);

    // Reorganise le central : panneau de controle a gauche, vue 3D (+ luminosite) a droite.
    QWidget *right = new QWidget;
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->addWidget(w3d, 1);
    QHBoxLayout *bl = new QHBoxLayout;
    bl->addWidget(labelBrightness);
    bl->addWidget(sliderBrightness);
    rv->addLayout(bl);

    QWidget *panel = buildControlPanel();

    QWidget *central = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(central);
    h->addWidget(panel);
    h->addWidget(right, 1);
    setCentralWidget(central);
    resize(960, 600);

    // menu Fichier / action Importer et sliderBrightness viennent du .ui.
    connect(actionImporter, SIGNAL(triggered()), this, SLOT(onImport()));
    connect(sliderBrightness, SIGNAL(valueChanged(int)), this, SLOT(onBrightnessChanged(int)));
}
//-----------------------------------------------------------------------------------------------
CMainWindow::~CMainWindow() {
}
//-----------------------------------------------------------------------------------------------
QWidget * CMainWindow::buildControlPanel(void) {
    QWidget *panel = new QWidget;
    panel->setMaximumWidth(280);
    QVBoxLayout *v = new QVBoxLayout(panel);

    m_importBtn = new QPushButton(tr("Importer STL…"));
    v->addWidget(m_importBtn);

    // --- Decoupe ---
    QGroupBox *gxCut = new QGroupBox(tr("Découpe"));
    QFormLayout *fCut = new QFormLayout(gxCut);
    m_axisCombo = new QComboBox;
    m_axisCombo->addItem(tr("X"));
    m_axisCombo->addItem(tr("Y"));
    m_axisCombo->addItem(tr("Z"));
    fCut->addRow(tr("Axe de coupe"), m_axisCombo);
    m_nbSlices = new QSpinBox;
    m_nbSlices->setRange(1, 999);
    m_nbSlices->setValue(30);
    fCut->addRow(tr("Nb de tranches"), m_nbSlices);
    v->addWidget(gxCut);

    // --- Materiau / feuille ---
    QGroupBox *gxMat = new QGroupBox(tr("Matériau / feuille (mm)"));
    QFormLayout *fMat = new QFormLayout(gxMat);
    m_scale = new QDoubleSpinBox;
    m_scale->setRange(0.001, 10000.0);
    m_scale->setDecimals(3);
    m_scale->setValue(1.0);
    fMat->addRow(tr("Échelle (mm/unité)"), m_scale);
    m_material = new QDoubleSpinBox;
    m_material->setRange(0.1, 100.0);
    m_material->setValue(3.0);
    fMat->addRow(tr("Épaisseur matériau"), m_material);
    m_sheetW = new QDoubleSpinBox;
    m_sheetW->setRange(10.0, 5000.0);
    m_sheetW->setValue(600.0);
    fMat->addRow(tr("Largeur feuille"), m_sheetW);
    m_sheetH = new QDoubleSpinBox;
    m_sheetH->setRange(10.0, 5000.0);
    m_sheetH->setValue(400.0);
    fMat->addRow(tr("Hauteur feuille"), m_sheetH);
    m_margin = new QDoubleSpinBox;
    m_margin->setRange(0.0, 500.0);
    m_margin->setValue(10.0);
    fMat->addRow(tr("Marge bord"), m_margin);
    m_spacing = new QDoubleSpinBox;
    m_spacing->setRange(0.0, 500.0);
    m_spacing->setValue(5.0);
    fMat->addRow(tr("Espacement pièces"), m_spacing);
    v->addWidget(gxMat);

    m_exportBtn = new QPushButton(tr("Exporter plan de découpe…"));
    m_exportBtn->setEnabled(false);
    v->addWidget(m_exportBtn);

    m_info = new QLabel(tr("Aucun modèle importé."));
    m_info->setWordWrap(true);
    v->addWidget(m_info);

    v->addStretch(1);

    connect(m_importBtn, SIGNAL(clicked()), this, SLOT(onImport()));
    connect(m_exportBtn, SIGNAL(clicked()), this, SLOT(onExport()));
    connect(m_axisCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onParamsChanged()));
    connect(m_nbSlices, SIGNAL(valueChanged(int)), this, SLOT(onParamsChanged()));
    connect(m_scale, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_material, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_sheetW, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_sheetH, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_margin, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));
    connect(m_spacing, SIGNAL(valueChanged(double)), this, SLOT(onParamsChanged()));

    return panel;
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
        reslice();
    } else {
        QMessageBox::warning(this, tr("Erreur"), tr("Chargement impossible : %1").arg(path));
    }
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onParamsChanged(void) {
    if (m_hasMesh)
        reslice();
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::reslice(void) {
    const CSlicer::Axis axis = CSlicer::Axis(m_axisCombo->currentIndex());
    m_slices = m_slicer.slice(w3d->mesh(), axis, m_nbSlices->value());

    // Aperçu 3D : petit gap visuel proportionnel a l'epaisseur.
    const float thick = m_slicer.thickness();
    w3d->setSlices(m_slices, axis, thick, thick * 0.15f);

    m_plan.build(m_slices, currentParams());
    m_exportBtn->setEnabled(m_plan.pieceCount() > 0);
    updateInfo();
}
//-----------------------------------------------------------------------------------------------
CCutPlan::Params CMainWindow::currentParams(void) const {
    CCutPlan::Params p;
    p.scale = float(m_scale->value());
    p.materialThickness = float(m_material->value());
    p.sheetW = float(m_sheetW->value());
    p.sheetH = float(m_sheetH->value());
    p.margin = float(m_margin->value());
    p.spacing = float(m_spacing->value());
    return p;
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::updateInfo(void) {
    if (!m_hasMesh) {
        m_info->setText(tr("Aucun modèle importé."));
        return;
    }
    const SVec3 sz = w3d->mesh().size();
    const float s = float(m_scale->value());
    const float slab = m_slicer.thickness() * s;
    m_info->setText(tr("Dimensions : %1 × %2 × %3 mm\n"
                       "Lamelle : %4 mm\n"
                       "Pièces : %5 — Feuilles : %6")
                    .arg(sz.x * s, 0, 'f', 1)
                    .arg(sz.y * s, 0, 'f', 1)
                    .arg(sz.z * s, 0, 'f', 1)
                    .arg(slab, 0, 'f', 2)
                    .arg(m_plan.pieceCount())
                    .arg(m_plan.sheetCount()));
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

    // On retire une eventuelle extension : l'export ajoute _sheetN.svg / _sheetN.dxf.
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
