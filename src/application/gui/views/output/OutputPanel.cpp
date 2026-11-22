#include "OutputPanel.h"

#include "AppContext.h"
#include "MessageLog.h"
#include "PanelFactoryHub.h"
#include "ThemeManager.h"

#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::context::Message;

namespace
{

// 级别 → 语义 token。颜色一律走 ThemeManager（§12.1：视图不认色值）。
QString levelColor(const QString &level)
{
    if (level == QStringLiteral("error"))
        return ThemeManager::themeColor(QStringLiteral("stateError")).name();
    if (level == QStringLiteral("warn"))
        return ThemeManager::themeColor(QStringLiteral("stateWarn")).name();
    if (level == QStringLiteral("debug"))
        return ThemeManager::themeColor(QStringLiteral("textMuted")).name();
    return ThemeManager::themeColor(QStringLiteral("textDim")).name();
}

QString renderLine(const QString &level, const QString &source, const QString &text,
                   const QString &locator, qint64 ts)
{
    const QString stamp =
        QDateTime::fromMSecsSinceEpoch(ts).toString(QStringLiteral("HH:mm:ss"));
    const QString color = levelColor(level);
    // 正文可能多行：换行转 <br>，否则整段会挤成一行。
    QString body = text.toHtmlEscaped();
    body.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    const QString tail = locator.isEmpty()
                             ? QString()
                             : QStringLiteral(" <span style=\"color:%1\">[%2]</span>")
                                   .arg(color, locator.toHtmlEscaped());
    return QStringLiteral("<span style=\"color:%1\">[%2]</span> "
                          "<span style=\"color:%3\">%4</span> "
                          "<span style=\"color:%1\">%5</span>%6")
        .arg(ThemeManager::themeColor(QStringLiteral("textMuted")).name(), stamp, color,
             source.toHtmlEscaped(), body, tail);
}

QWidget *createOutputPanel(AppContext &appCtx,
                           nextpilot::plotscope::intent::IIntentExecutor &,
                           QWidget *parent)
{
    return new OutputPanel(appCtx, parent);
}

} // namespace

OutputPanel::OutputPanel(AppContext &appCtx, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(6);
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    clearBtn->setToolTip(QStringLiteral("清空输出（消息流同步清空，问题面板一并清掉）"));
    connect(clearBtn, &QPushButton::clicked, this, &OutputPanel::clearLog);
    bar->addStretch(1);
    bar->addWidget(clearBtn);
    layout->addLayout(bar);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setMaximumBlockCount(context::MessageLog::kMaxMessages);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_view->setFont(mono);
    layout->addWidget(m_view, 1);

    // 增量渲染：谁 append 都走同一条信号，面板不轮询、不持有别人的 widget。
    connect(&m_appCtx, &AppContext::messageAppended, this,
            [this](const QString &level, const QString &source, const QString &text,
                   const QString &locator) {
                appendMessage(level, source, text, locator);
            });

    reloadFromLog();
}

void OutputPanel::reloadFromLog()
{
    m_view->clear();
    for (const Message &m : m_appCtx.messages().all())
        m_view->appendHtml(renderLine(m.level, m.source, m.text, m.locator, m.ts));
}

void OutputPanel::appendMessage(const QString &level, const QString &source, const QString &text,
                                const QString &locator)
{
    m_view->appendHtml(renderLine(level, source, text, locator,
                                 QDateTime::currentMSecsSinceEpoch()));
}

void OutputPanel::clearLog()
{
    // 清的是**共享消息流**，不是本视图的副本：问题/调试控制台看到的是同一份数据，
    // 各自留一份副本就会出现"输出清空了、问题里还挂着旧错误"的错位。
    m_appCtx.messages().clear();
    m_view->clear();
}

} // namespace nextpilot::plotscope::gui

PS_REGISTER_PANEL_TI("output-console", outputConsole,
                     ::nextpilot::plotscope::gui::createOutputPanel, "输出", "output")
