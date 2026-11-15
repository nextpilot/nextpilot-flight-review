#include "ScriptComponent.h"
#include "MainWindow.h"
#include "PlotModule.h"

#include <QComboBox>
#include <QDockWidget>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ScriptEngine.h"

namespace ps::components
{
namespace
{
struct ScriptRegistrar
{
    ScriptRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("script.runner"), QStringLiteral("脚本运行"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<ScriptComponent>(); });
    }
};
static ScriptRegistrar g_scriptRegistrar;
} // namespace

ComponentManifest ScriptComponent::manifest() const
{
    return {QStringLiteral("script.runner"), QStringLiteral("脚本运行"), QStringLiteral("1.0"),
            {QStringLiteral("script")}};
}

void ScriptComponent::contribute(Registry &registry, const HostContext &host)
{
    registry.addDock(DockContribution{QStringLiteral("dock.script"), QStringLiteral("脚本 · Lua 5.4"),
                                      Qt::RightDockWidgetArea, true, 4,
                                      QStringLiteral(":/icons/script.svg"),
                                      [](QWidget *parent) -> QWidget * {
                                          auto *main = qobject_cast<MainWindow *>(parent);
                                          if (!main)
                                              return nullptr;
                                          auto *dock = new QDockWidget(QStringLiteral("脚本 · Lua 5.4"), parent);
                                          dock->setObjectName(QStringLiteral("dock.script"));
                                          dock->setProperty("psIcon", QStringLiteral(":/icons/script.svg"));
                                          auto *host = new QWidget(dock);
                                          auto *lay = new QVBoxLayout(host);
                                          lay->setContentsMargins(0, 0, 0, 0);
                                          auto *script = new QPlainTextEdit(host);
                                          QFont font(QStringLiteral("monospace"));
                                          font.setStyleHint(QFont::Monospace);
                                          font.setPixelSize(12);
                                          script->setFont(font);
                                          script->setTabStopDistance(28);
                                          script->setPlainText(QStringLiteral(
                                              "-- 绑定面板可选 1–4 个输入，v1..v4 或 ps.input(n)\n"
                                              "rate = ps.derivative(ps.input(1), ps.time())\n"
                                              "out = ps.movingAvg(rate, 5)\n"
                                              "ps.plot(\"roll_rate\", out)\n"));
                                          auto *bindRow = new QHBoxLayout;
                                          bindRow->addWidget(new QLabel(QStringLiteral("绑定"), host));
                                          QComboBox *inputs[4]{};
                                          for (int i = 0; i < 4; ++i)
                                          {
                                              auto *cb = new QComboBox(host);
                                              cb->addItem(QStringLiteral("（未绑定）"), QString());
                                              cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                                              cb->setMinimumContentsLength(8);
                                              bindRow->addWidget(cb, 1);
                                              inputs[i] = cb;
                                          }
                                          lay->addLayout(bindRow);
                                          auto *paramHead = new QHBoxLayout;
                                          paramHead->addWidget(new QLabel(QStringLiteral("参数"), host));
                                          auto *addParam = new QToolButton(host);
                                          addParam->setText(QStringLiteral("+"));
                                          addParam->setToolTip(QStringLiteral("添加参数"));
                                          auto *delParam = new QToolButton(host);
                                          delParam->setText(QStringLiteral("-"));
                                          delParam->setToolTip(QStringLiteral("删除选中参数"));
                                          paramHead->addStretch(1);
                                          paramHead->addWidget(addParam);
                                          paramHead->addWidget(delParam);
                                          lay->addLayout(paramHead);
                                          auto *params = new QTableWidget(0, 2, host);
                                          params->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("值")});
                                          params->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
                                          params->verticalHeader()->setVisible(false);
                                          params->setMaximumHeight(110);
                                          lay->addWidget(params);
                                          QObject::connect(addParam, &QToolButton::clicked, params, [params] {
                                              const int row = params->rowCount();
                                              params->insertRow(row);
                                              params->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0")));
                                              params->editItem(params->item(row, 0));
                                          });
                                          QObject::connect(delParam, &QToolButton::clicked, params, [params] {
                                              const int row = params->currentRow();
                                              if (row >= 0)
                                                  params->removeRow(row);
                                          });
                                          auto *runRow = new QHBoxLayout;
                                          auto *preview = new QPushButton(QStringLiteral("预览"), host);
                                          auto *apply = new QPushButton(QStringLiteral("应用"), host);
                                          runRow->addWidget(preview);
                                          runRow->addWidget(apply);
                                          lay->addWidget(script, 1);
                                          lay->addLayout(runRow);
                                          dock->setWidget(host);
                                          QObject::connect(preview, &QPushButton::clicked, main, [main] {
                                              ps::commands::CommandContext c;
                                              c.arg = QStringLiteral("false");
                                              main->commands().invoke(QStringLiteral("script.run"), c);
                                          });
                                          QObject::connect(apply, &QPushButton::clicked, main, [main] {
                                              ps::commands::CommandContext c;
                                              c.arg = QStringLiteral("true");
                                              main->commands().invoke(QStringLiteral("script.run"), c);
                                          });
                                          ps::plot::bindScriptPanel(main, script, inputs, params);
                                          return dock;
                                      }});
    registry.addRibbon(RibbonContribution{
        QStringLiteral("脚本"), {}, {},
        {{QStringLiteral("Lua"),
          {{QStringLiteral("script.run"), QStringLiteral("运行"), {}, true, QStringLiteral("run")},
           {QStringLiteral("script.format"), QStringLiteral("格式化"), {}, false, QStringLiteral("format")}}}},
        3});
    registry.addAction(QStringLiteral("script.format"), [host](const ActionContext &) {
        if (host.log) host.log(QStringLiteral("脚本格式化（stylua 规则，M2 接）"));
    });
    registry.addScriptRunner(ScriptRunnerContribution{
        [](const QString &source, const QVector<ScriptSeries> &inputs,
           const QMap<QString, double> &params, const QString &language) {
            return ScriptEngine::run(source, inputs, params, language);
        }});
}

} // namespace ps::components
