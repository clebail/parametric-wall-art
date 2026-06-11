//-----------------------------------------------------------------------------------------------
#ifndef CMAINWINDOW_H
#define CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
#include <QMainWindow>
#include "ui_CMainWindow.h"
//-----------------------------------------------------------------------------------------------
class CMainWindow : public QMainWindow, private Ui::CMainWindow
{
    Q_OBJECT
public:
    explicit CMainWindow(QWidget *parent = nullptr);
    ~CMainWindow();
private slots:
    void onImport(void);
    void onBrightnessChanged(int value);
};
//-----------------------------------------------------------------------------------------------
#endif // CMAINWINDOW_H
//-----------------------------------------------------------------------------------------------
