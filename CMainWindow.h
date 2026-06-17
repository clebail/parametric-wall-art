//-----------------------------------------------------------------------------------------------
#ifndef CMAINWINDOW_H
#define CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
#include <vector>
#include <QMainWindow>
#include <QColor>
#include "ui_CMainWindow.h"
#include "CSlicer.h"
#include "CCutPlan.h"
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
    void onNbOrThickChanged(void);
    void onGapChanged(void);
    void onViewChanged(void);
    void onBoardColorPick(void);
private:
    void reslice(void);
    void updateInfo(void);
    CCutPlan::Params currentParams(void) const;
    float axisModelSize(void) const;
    float currentScale(void) const;
    void  sliceViewSplit(float &thickView, float &gapView) const;
    void  applyBoardPreview(void);
    void  updateBoardColorSwatch(void);

    CSlicer m_slicer;
    std::vector<CSlice> m_slices;
    CCutPlan m_plan;
    bool m_hasMesh;
    QColor m_boardColor;
};
//-----------------------------------------------------------------------------------------------
#endif // CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
