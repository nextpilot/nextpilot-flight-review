#pragma once

#include <QWidget>

namespace ps::runtime { struct ModuleContext; }
class QCheckBox;
class QComboBox;
class QSpinBox;

class PlottingSettingsPage : public QWidget
{
public:
    explicit PlottingSettingsPage(ps::runtime::ModuleContext &ctx, QWidget *parent = nullptr);
    void apply();

private:
    ps::runtime::ModuleContext *m_ctx = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_legend = nullptr;
    QComboBox *m_timeFormat = nullptr;
    QSpinBox *m_exportCap = nullptr;
    QSpinBox *m_derivedCap = nullptr;
};
