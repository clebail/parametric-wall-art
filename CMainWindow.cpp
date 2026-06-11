//-----------------------------------------------------------------------------------------------
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include "CMainWindow.h"
//-----------------------------------------------------------------------------------------------
CMainWindow::CMainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi(this);

    // menu Fichier / action Importer et sliderBrightness viennent du .ui.
    connect(actionImporter, SIGNAL(triggered()), this, SLOT(onImport()));
    connect(sliderBrightness, SIGNAL(valueChanged(int)), this, SLOT(onBrightnessChanged(int)));
}
//-----------------------------------------------------------------------------------------------
CMainWindow::~CMainWindow() {
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onImport(void) {
    QString path = QFileDialog::getOpenFileName(this, tr("Importer un modele STL"),
                                                QString(), tr("Fichiers STL (*.stl)"));
    if (path.isEmpty())
        return;

    if (w3d->loadMesh(path))
        setWindowTitle(tr("Parametric Wall Art — %1").arg(QFileInfo(path).fileName()));
    else
        QMessageBox::warning(this, tr("Erreur"), tr("Chargement impossible : %1").arg(path));
}
//-----------------------------------------------------------------------------------------------
void CMainWindow::onBrightnessChanged(int value) {
    w3d->setBrightness(value / 100.0f);
}
//-----------------------------------------------------------------------------------------------
