//-----------------------------------------------------------------------------------------------
#ifndef CMAINWINDOW_H
#define CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <QMainWindow>
#include "ui_CMainWindow.h"
#include "CSlicer.h"
#include "CCutPlan.h"
//-----------------------------------------------------------------------------------------------
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QWidget;
//-----------------------------------------------------------------------------------------------
class CMainWindow : public QMainWindow, private Ui::CMainWindow
{
    Q_OBJECT
public:
    explicit CMainWindow(QWidget *parent = nullptr);
    ~CMainWindow();
private slots:
    void onImport(void);
    void onParamsChanged(void);
    void onExport(void);
    void onBrightnessChanged(int value);
private:
    QWidget * buildControlPanel(void);
    void reslice(void);                 // re-tranche + rebuild plan + maj vue + infos
    void updateInfo(void);
    CCutPlan::Params currentParams(void) const;

    // Controles du panneau (construits en code).
    QPushButton    *m_importBtn;
    QComboBox      *m_axisCombo;
    QSpinBox       *m_nbSlices;
    QDoubleSpinBox *m_scale;
    QDoubleSpinBox *m_material;
    QDoubleSpinBox *m_sheetW;
    QDoubleSpinBox *m_sheetH;
    QDoubleSpinBox *m_margin;
    QDoubleSpinBox *m_spacing;
    QPushButton    *m_exportBtn;
    QLabel         *m_info;

    // Etat.
    CSlicer m_slicer;
    std::vector<CSlice> m_slices;
    CCutPlan m_plan;
    bool m_hasMesh;
};
//-----------------------------------------------------------------------------------------------
#endif // CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
