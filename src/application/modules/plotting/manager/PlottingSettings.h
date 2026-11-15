#pragma once

#include <QWidget>

class MainWindow;
class QCheckBox;
class QComboBox;
class QSpinBox;

class PlottingSettingsPage : public QWidget
{
public:
    explicit PlottingSettingsPage(MainWindow *mw, QWidget *parent = nullptr);
    void apply();

private:
    MainWindow *m_mw = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_legend = nullptr;
    QComboBox *m_timeFormat = nullptr;
    QSpinBox *m_exportCap = nullptr;
    QSpinBox *m_derivedCap = nullptr;
};
