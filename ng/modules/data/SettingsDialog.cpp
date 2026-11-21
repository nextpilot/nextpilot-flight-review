#include "SettingsDialog.h"

#include "Intent.h"
#include "Result.h"

#include <QAbstractSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariantMap>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;

namespace nextpilot::plotscope::gui
{

SettingsDialog::SettingsDialog(IIntentExecutor &executor, QWidget *parent)
    : QDialog(parent), m_executor(executor)
{
    setWindowTitle(QStringLiteral("数据设置"));
    setMinimumWidth(420);
    buildUi();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // --- 数据组：三个**真实消费**的键 ---
    auto *dataBox = new QGroupBox(QStringLiteral("数据"), this);
    auto *form = new QFormLayout(dataBox);

    m_exportMax = new QSpinBox(dataBox);
    m_exportMax->setRange(0, 100000000);
    m_exportMax->setSingleStep(1000);
    m_exportMax->setSpecialValueText(QStringLiteral("不限"));
    m_exportMax->setSuffix(QStringLiteral(" 点"));
    m_exportMax->setMinimumHeight(26); // 默认高度下上下箭头会被压成一条线
    m_exportMax->setMaximumWidth(180); // 表单右列不要铺满整行，留白比拉长好看
    m_exportMax->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    form->addRow(QStringLiteral("导出 CSV 采样上限"), m_exportMax);

    m_deriveMax = new QSpinBox(dataBox);
    m_deriveMax->setRange(0, 100000000);
    m_deriveMax->setSingleStep(10000);
    m_deriveMax->setSpecialValueText(QStringLiteral("不限"));
    m_deriveMax->setSuffix(QStringLiteral(" 点"));
    m_deriveMax->setMinimumHeight(26);
    m_deriveMax->setMaximumWidth(180);
    m_deriveMax->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    form->addRow(QStringLiteral("派生通道重算上限"), m_deriveMax);

    auto *cacheRow = new QWidget(dataBox);
    auto *cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    m_cacheLabel = new QLabel(cacheRow);
    m_clearCache = new QPushButton(QStringLiteral("清除"), cacheRow);
    cacheLayout->addWidget(m_cacheLabel, 1);
    cacheLayout->addWidget(m_clearCache);
    form->addRow(QStringLiteral("解析缓存"), cacheRow);

    root->addWidget(dataBox);

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);

    // 读当前值（走 settings.get，与脚本读到的是同一份）。
    auto readInt = [this](const QString &key, QSpinBox *box) {
        Intent g;
        g.op = QStringLiteral("settings.get");
        g.params[QStringLiteral("key")] = key;
        const auto r = m_executor.execute(g);
        if (r)
            box->setValue(r.value().payload.toInt());
    };
    readInt(QStringLiteral("export.csv.maxPoints"), m_exportMax);
    readInt(QStringLiteral("derive.maxPoints"), m_deriveMax);
    refreshCacheLabel();

    connect(m_clearCache, &QPushButton::clicked, this, [this]() {
        Intent cc;
        cc.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(cc);
        m_status->setText(r ? QStringLiteral("已清除 %1 个缓存文件").arg(r.value().payload.toInt())
                            : QStringLiteral("清除失败：%1").arg(r.error().message));
        refreshCacheLabel();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::refreshCacheLabel()
{
    Intent st;
    st.op = QStringLiteral("data.cache.stats");
    const auto r = m_executor.execute(st);
    if (!r)
    {
        m_cacheLabel->setText(QStringLiteral("不可用"));
        return;
    }
    const QVariantMap m = r.value().payload.toMap();
    const int entries = m.value(QStringLiteral("entries")).toInt();
    const qlonglong bytes = m.value(QStringLiteral("bytes")).toLongLong();
    const qlonglong hits = m.value(QStringLiteral("hits")).toLongLong();
    const qlonglong misses = m.value(QStringLiteral("misses")).toLongLong();
    m_cacheLabel->setText(entries > 0
        ? QStringLiteral("%1 项 · %2 MB · 命中 %3 / 未命中 %4")
              .arg(entries)
              .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
              .arg(hits)
              .arg(misses)
        : QStringLiteral("无缓存（命中 %1 / 未命中 %2）").arg(hits).arg(misses));
    m_clearCache->setEnabled(entries > 0);
}

void SettingsDialog::save()
{
    auto writeInt = [this](const QString &key, QSpinBox *box) {
        Intent s;
        s.op = QStringLiteral("settings.set");
        s.params[QStringLiteral("key")] = key;
        s.params[QStringLiteral("value")] = box->value();
        m_executor.execute(s);
    };
    writeInt(QStringLiteral("export.csv.maxPoints"), m_exportMax);
    writeInt(QStringLiteral("derive.maxPoints"), m_deriveMax);
}

} // namespace nextpilot::plotscope::gui
