#include "PlottingSettings.h"
#include "ModuleContext.h"
#include "CommandManager.h"
#include "SettingsManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

PlottingSettingsPage::PlottingSettingsPage(ps::runtime::ModuleContext &ctx, QWidget *parent)
    : QWidget(parent), m_ctx(&ctx)
{
    auto &st = SettingsManager::instance();

    auto *lay = new QVBoxLayout(this);
    auto *canvasGroup = new QGroupBox(QStringLiteral("画布"), this);
    auto *canvasForm = new QFormLayout(canvasGroup);
    m_grid = new QCheckBox(QStringLiteral("新建画布默认显示网格"), canvasGroup);
    m_grid->setChecked(st.get(ps::settings::kGridDefault));
    m_legend = new QCheckBox(QStringLiteral("新建画布默认显示图例"), canvasGroup);
    m_legend->setChecked(st.get(ps::settings::kLegendDefault));
    canvasForm->addRow(m_grid);
    canvasForm->addRow(m_legend);
    lay->addWidget(canvasGroup);

    auto *timeGroup = new QGroupBox(QStringLiteral("时间轴"), this);
    auto *timeForm = new QFormLayout(timeGroup);
    m_timeFormat = new QComboBox(timeGroup);
    m_timeFormat->addItem(QStringLiteral("相对秒"), QStringLiteral("relative"));
    m_timeFormat->addItem(QStringLiteral("时分秒"), QStringLiteral("clock"));
    m_timeFormat->addItem(QStringLiteral("UTC（Unix 秒时间戳）"), QStringLiteral("utc"));
    const QString savedFmt = st.get(ps::settings::kTimeFormat);
    m_timeFormat->setCurrentIndex(qMax(0, m_timeFormat->findData(savedFmt)));
    timeForm->addRow(QStringLiteral("时间轴格式"), m_timeFormat);
    lay->addWidget(timeGroup);

    auto *dataGroup = new QGroupBox(QStringLiteral("数据"), this);
    auto *dataForm = new QFormLayout(dataGroup);
    m_exportCap = new QSpinBox(dataGroup);
    m_exportCap->setRange(0, 100000000);
    m_exportCap->setSingleStep(1000);
    m_exportCap->setSuffix(QStringLiteral(" 点/通道（0=不限）"));
    m_exportCap->setValue(st.get(ps::settings::kExportSampleLimit));
    m_derivedCap = new QSpinBox(dataGroup);
    m_derivedCap->setRange(0, 100000000);
    m_derivedCap->setSingleStep(100000);
    m_derivedCap->setSuffix(QStringLiteral(" 点（0=不限）"));
    m_derivedCap->setValue(st.get(ps::settings::kDerivedMaxPoints));
    dataForm->addRow(QStringLiteral("CSV 导出采样上限"), m_exportCap);
    dataForm->addRow(QStringLiteral("派生通道重算上限"), m_derivedCap);
    lay->addWidget(dataGroup);

    auto *clearCache = new QPushButton(QStringLiteral("清空解析缓存"), this);
    connect(clearCache, &QPushButton::clicked, this, [this] {
        if (m_ctx)
            m_ctx->commands().invoke(QStringLiteral("data.clearCache"), {});
    });
    lay->addWidget(clearCache);
    lay->addStretch(1);
}

void PlottingSettingsPage::apply()
{
    auto &st = SettingsManager::instance();
    st.set(ps::settings::kGridDefault, m_grid->isChecked());
    st.set(ps::settings::kLegendDefault, m_legend->isChecked());
    st.set(ps::settings::kTimeFormat, m_timeFormat->currentData().toString());
    st.set(ps::settings::kExportSampleLimit, m_exportCap->value());
    st.set(ps::settings::kDerivedMaxPoints, m_derivedCap->value());
    if (m_ctx)
        m_ctx->commands().invoke(QStringLiteral("plot.reloadSettings"), {});
}
