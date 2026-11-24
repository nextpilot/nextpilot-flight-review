#include "PreferencesDialog.h"

#include "AppContext.h"
#include "Intent.h"
#include "Result.h"
#include "SettingsStore.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QSizePolicy>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

namespace
{
// 分组：左列表一行 = 右堆叠一页，索引必须一一对应。
constexpr int kSectionAppearance = 0;
constexpr int kSectionTimeline = 1;
constexpr int kSectionData = 2;
} // namespace

PreferencesDialog::PreferencesDialog(AppContext &appCtx, IIntentExecutor &executor,
                                     const QString &initialSection, QWidget *parent)
    : QDialog(parent), m_appCtx(appCtx), m_executor(executor)
{
    setWindowTitle(QStringLiteral("偏好设置"));
    setMinimumSize(660, 470);
    buildUi();
    loadValues();
    selectSection(initialSection);
}

void PreferencesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // 左分类 + 右内容：分类列表固定宽度，内容吃余量（VS Code 式布局）。
    auto *body = new QHBoxLayout();
    m_sections = new QListWidget(this);
    m_sections->setFixedWidth(150);
    m_sections->addItem(QStringLiteral("外观"));
    m_sections->addItem(QStringLiteral("时间轴"));
    m_sections->addItem(QStringLiteral("数据"));

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildAppearancePage());
    m_pages->addWidget(buildTimelinePage());
    m_pages->addWidget(buildDataPage());

    // 扩展包设置页（SettingsPageSpec）：只有模块知道自己有哪些设置项，所以页由
    // 模块建（壳只给 parent）；壳负责按 order 追加左栏条目并塞进 stack。
    // create 为空或返回空指针视为未实现——不占位，免得左栏出现点不开的空白页。
    for (const nextpilot::plotscope::registry::SettingsPageSpec &spec :
         m_appCtx.toolbox().settingsPagesSorted())
    {
        if (!spec.create)
            continue;
        QWidget *page = spec.create(m_pages);
        if (!page)
            continue;
        m_sections->addItem(spec.title.isEmpty() ? spec.id : spec.title);
        m_pages->addWidget(page);
    }

    body->addWidget(m_sections);
    body->addWidget(m_pages, 1);
    root->addLayout(body, 1);

    // 设置文件的实际落盘位置：让人一眼看到"改的东西存在哪"，
    // 也避免内存模式（不落盘）被误当成"已保存到磁盘"。
    m_pathLabel = new QLabel(this);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    const QString path = m_appCtx.settings().filePath();
    m_pathLabel->setText(path.isEmpty()
        ? QStringLiteral("设置文件：内存模式（本次运行有效，不落盘）")
        : QStringLiteral("设置文件：%1").arg(QDir::toNativeSeparators(path)));
    root->addWidget(m_pathLabel);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_status->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);

    root->addWidget(buttons);

    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(reset, &QPushButton::clicked, this, &PreferencesDialog::resetToDefaults);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget *PreferencesDialog::buildAppearancePage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    auto *box = new QGroupBox(QStringLiteral("外观"), page);
    auto *form = new QFormLayout(box);
    m_theme = new QComboBox(box);
    m_theme->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    m_theme->setMinimumHeight(26);
    m_theme->setMaximumWidth(180);
    form->addRow(QStringLiteral("主题"), m_theme);
    lay->addWidget(box);

    auto *hint = new QLabel(QStringLiteral("主题立即作用于全局配色、ribbon 与 dock 样式；"
                                           "保存后写入设置文件，下次启动沿用。"),
                            page);
    hint->setWordWrap(true);
    // Ignored 水平策略：让布局能压窄它，否则 sizeHint 会把提示撑出窗口右边界。
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    lay->addWidget(hint);
    lay->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildTimelinePage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    auto *box = new QGroupBox(QStringLiteral("时间轴"), page);
    auto *form = new QFormLayout(box);
    m_timeFormat = new QComboBox(box);
    m_timeFormat->addItem(QStringLiteral("相对秒（12.345）"), QStringLiteral("relative"));
    m_timeFormat->addItem(QStringLiteral("时分秒（0:12:20）"), QStringLiteral("hms"));
    m_timeFormat->addItem(QStringLiteral("UTC（无 epoch 时同 hms）"), QStringLiteral("utc"));
    m_timeFormat->setMinimumHeight(26);
    m_timeFormat->setMaximumWidth(220);
    form->addRow(QStringLiteral("X 轴刻度"), m_timeFormat);
    lay->addWidget(box);

    auto *hint = new QLabel(QStringLiteral("只改刻度文本，不改刻度位置；"
                                           "保存后立即重绘所有页所有格。"),
                            page);
    hint->setWordWrap(true);
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    lay->addWidget(hint);
    lay->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildDataPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    auto *box = new QGroupBox(QStringLiteral("数据"), page);
    auto *form = new QFormLayout(box);

    m_exportMax = new QSpinBox(box);
    m_exportMax->setRange(0, 100000000);
    m_exportMax->setSingleStep(1000);
    m_exportMax->setSpecialValueText(QStringLiteral("不限"));
    m_exportMax->setSuffix(QStringLiteral(" 点"));
    m_exportMax->setMinimumHeight(26);
    m_exportMax->setMaximumWidth(180);
    m_exportMax->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    form->addRow(QStringLiteral("导出 CSV 采样上限"), m_exportMax);

    m_deriveMax = new QSpinBox(box);
    m_deriveMax->setRange(0, 100000000);
    m_deriveMax->setSingleStep(10000);
    m_deriveMax->setSpecialValueText(QStringLiteral("不限"));
    m_deriveMax->setSuffix(QStringLiteral(" 点"));
    m_deriveMax->setMinimumHeight(26);
    m_deriveMax->setMaximumWidth(180);
    m_deriveMax->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    form->addRow(QStringLiteral("派生通道重算上限"), m_deriveMax);

    auto *cacheRow = new QWidget(box);
    auto *cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    m_cacheLabel = new QLabel(cacheRow);
    m_clearCache = new QPushButton(QStringLiteral("清除"), cacheRow);
    cacheLayout->addWidget(m_cacheLabel, 1);
    cacheLayout->addWidget(m_clearCache);
    form->addRow(QStringLiteral("解析缓存"), cacheRow);

    lay->addWidget(box);

    auto *hint = new QLabel(QStringLiteral("缓存是运行时产物，清除不影响已载入的数据源。"), page);
    hint->setWordWrap(true);
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    lay->addWidget(hint);
    // 启动恢复：三档的差别是"恢复多少"。off 什么都不恢复；layout 只还原
    // 源条目与画布布局（不读数据，启动不被大文件拖住）；full 连数据一起加载。
    auto *restoreBox = new QGroupBox(QStringLiteral("启动恢复"), page);
    auto *restoreForm = new QFormLayout(restoreBox);
    m_startupRecovery = new QComboBox(restoreBox);
    m_startupRecovery->addItem(QStringLiteral("仅布局（推荐）"), QStringLiteral("layout"));
    m_startupRecovery->addItem(QStringLiteral("完整（含数据）"), QStringLiteral("full"));
    m_startupRecovery->addItem(QStringLiteral("关闭"), QStringLiteral("off"));
    m_startupRecovery->setMinimumHeight(26);
    m_startupRecovery->setMaximumWidth(220);
    restoreForm->addRow(QStringLiteral("上次会话"), m_startupRecovery);
    lay->addWidget(restoreBox);

    auto *restoreHint = new QLabel(QStringLiteral("布局档只还原源条目与画布，曲线等你手动加载数据后自动重绑；"
                                                  "完整档会在启动时读取数据，大文件会慢。"
                                                  "流式源在任何档位都只保留条目，不自动连接。"),
                                   page);
    restoreHint->setWordWrap(true);
    restoreHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    restoreHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    lay->addWidget(restoreHint);

    lay->addStretch(1);

    connect(m_clearCache, &QPushButton::clicked, this, [this]() {
        Intent cc;
        cc.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(cc);
        m_status->setText(r ? QStringLiteral("已清除 %1 个缓存文件").arg(r.value().payload.toInt())
                            : QStringLiteral("清除失败：%1").arg(r.error().message));
        refreshCacheLabel();
    });
    return page;
}

void PreferencesDialog::loadValues()
{
    // 读值一律走意图层：与脚本 `settings.get` 读到的是同一份。
    auto readString = [this](const QString &key, const QString &fallback) {
        Intent g;
        g.op = QStringLiteral("settings.get");
        g.params[QStringLiteral("key")] = key;
        const auto r = m_executor.execute(g);
        if (!r)
            return fallback;
        const QString s = r.value().payload.toString();
        return s.isEmpty() ? fallback : s;
    };
    auto readInt = [this](const QString &key) {
        Intent g;
        g.op = QStringLiteral("settings.get");
        g.params[QStringLiteral("key")] = key;
        const auto r = m_executor.execute(g);
        return r ? r.value().payload.toInt() : 0;
    };

    // 启动恢复档位：默认 layout——恢复布局但不读数据，启动速度与现场续接折中。
    if (m_startupRecovery != nullptr)
    {
        const QString mode = readString(QStringLiteral("app/startup.recovery"),
                                        QStringLiteral("layout"));
        const int idx = m_startupRecovery->findData(mode);
        m_startupRecovery->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    // 主题走 theme.get（ThemeManager 的当前值），不硬编码 app/theme 键。
    {
        Intent g;
        g.op = QStringLiteral("theme.get");
        const auto r = m_executor.execute(g);
        const QString cur = r ? r.value().payload.toString() : QStringLiteral("light");
        const int idx = m_theme->findData(cur);
        m_theme->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    m_exportMax->setValue(readInt(QStringLiteral("export.csv.maxPoints")));
    m_deriveMax->setValue(readInt(QStringLiteral("derive.maxPoints")));

    const QString tf = readString(QStringLiteral("plot.time.format"), QStringLiteral("relative"));
    const int idx = m_timeFormat->findData(tf);
    m_timeFormat->setCurrentIndex(idx < 0 ? 0 : idx);

    refreshCacheLabel();
}

void PreferencesDialog::save()
{
    auto writeValue = [this](const QString &key, const QVariant &value) {
        Intent s;
        s.op = QStringLiteral("settings.set");
        s.params[QStringLiteral("key")] = key;
        s.params[QStringLiteral("value")] = value;
        const auto r = m_executor.execute(s);
        return r ? QString() : r.error().message;
    };

    QString err = writeValue(QStringLiteral("export.csv.maxPoints"), m_exportMax->value());
    if (err.isEmpty())
        err = writeValue(QStringLiteral("derive.maxPoints"), m_deriveMax->value());
    if (err.isEmpty())
        err = writeValue(QStringLiteral("plot.time.format"), m_timeFormat->currentData().toString());
    if (err.isEmpty() && m_startupRecovery != nullptr)
        err = writeValue(QStringLiteral("app/startup.recovery"),
                         m_startupRecovery->currentData().toString());

    if (err.isEmpty())
    {
        // 主题走 theme.set：ThemeManager 内部 persist 到 app/theme 并立即应用，
        // 不用这里再手工写键（手写会绕过"换肤"的单一出口）。
        Intent t;
        t.op = QStringLiteral("theme.set");
        t.params[QStringLiteral("theme")] = m_theme->currentData().toString();
        const auto r = m_executor.execute(t);
        if (!r)
            err = r.error().message;
    }

    // 落盘：settings.set 只改内存，flush 才有磁盘效果（App 无 settingsPath 时
    // flush 是成功的空操作，路径标签已明确标"内存模式"）。
    if (err.isEmpty())
    {
        Intent f;
        f.op = QStringLiteral("settings.flush");
        const auto r = m_executor.execute(f);
        if (!r)
            err = r.error().message;
    }

    if (!err.isEmpty())
    {
        m_status->setText(QStringLiteral("保存失败：%1").arg(err));
        return; // 失败不关窗：值还在界面上，改完可再存
    }

    applyTimeline();
    accept();
}

void PreferencesDialog::resetToDefaults()
{
    // 只重置界面值，不立刻写：用户还能「取消」反悔。
    const int light = m_theme->findData(QStringLiteral("light"));
    m_theme->setCurrentIndex(light < 0 ? 0 : light);
    const int rel = m_timeFormat->findData(QStringLiteral("relative"));
    m_timeFormat->setCurrentIndex(rel < 0 ? 0 : rel);
    m_exportMax->setValue(0);
    m_deriveMax->setValue(0);
    m_status->setText(QStringLiteral("已恢复默认值，点「保存」后生效。"));
}

void PreferencesDialog::refreshCacheLabel()
{
    Intent st;
    st.op = QStringLiteral("data.cache.stats");
    const auto r = m_executor.execute(st);
    if (!r)
    {
        m_cacheLabel->setText(QStringLiteral("不可用"));
        m_clearCache->setEnabled(false);
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

void PreferencesDialog::applyTimeline()
{
    // 时间格式换了要立刻让画布换刻度文本：走 view action，不直接依赖画布模块
    // （与数据源面板保存后的派发完全同一条路）。无画布时静默失败。
    QVariantMap params;
    params[QStringLiteral("mode")] = m_timeFormat->currentData().toString();
    Result<QVariant> out{ QVariant() };
    m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.timeFormat"), params, out);
}

void PreferencesDialog::selectSection(const QString &initialSection)
{
    int row = kSectionAppearance;
    if (initialSection.compare(QStringLiteral("data"), Qt::CaseInsensitive) == 0
        || initialSection == QStringLiteral("数据"))
        row = kSectionData;
    else if (initialSection.compare(QStringLiteral("timeline"), Qt::CaseInsensitive) == 0
             || initialSection == QStringLiteral("时间轴"))
        row = kSectionTimeline;
    m_sections->setCurrentRow(row);
    m_pages->setCurrentIndex(row);
}

} // namespace nextpilot::plotscope::gui
