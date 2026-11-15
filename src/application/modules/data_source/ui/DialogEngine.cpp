#include "DialogEngine.h"

#include "DataSourceEntry.h"

#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>

#if PS_HAVE_UITOOLS
#include <QDialogButtonBox>
#include <QFile>
#include <QUiLoader>
#endif

ExtensionDialogEngine::ExtensionDialogEngine(const DataSourceEntry &entry, const QString &path, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(entry.label);

#if PS_HAVE_UITOOLS
    Q_UNUSED(path);
    QUiLoader loader;
    QFile f(entry.uiDialog);
    if (!f.open(QIODevice::ReadOnly))
    {
        auto *lay = new QVBoxLayout(this);
        lay->addWidget(new QLabel(QStringLiteral("扩展 UI 打不开：%1").arg(entry.uiDialog), this));
        return;
    }
    QWidget *w = loader.load(&f, this);
    f.close();
    if (!w)
    {
        auto *lay = new QVBoxLayout(this);
        lay->addWidget(new QLabel(QStringLiteral("扩展 UI 加载失败：%1").arg(entry.uiDialog), this));
        return;
    }

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(w);
    const QPalette pal = QApplication::palette();
    const QString css = QStringLiteral(
                            "QDialog,QWidget{background:%1;}"
                            "QLabel{color:%2;background:transparent;}"
                            "QPushButton{color:%2;}"
                            "QLineEdit,QComboBox,QSpinBox{background:%3;color:%2;border:1px solid %4;}")
                            .arg(pal.color(QPalette::Window).name(),
                                 pal.color(QPalette::WindowText).name(),
                                 pal.color(QPalette::Base).name(),
                                 pal.color(QPalette::Mid).name());
    setStyleSheet(css);
    w->setStyleSheet(css);
    for (QLabel *l : w->findChildren<QLabel *>())
        l->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                             .arg(pal.color(QPalette::WindowText).name()));

    QDialogButtonBox *box = w->findChild<QDialogButtonBox *>();
    if (!box)
    {
        box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        lay->addWidget(box);
    }
    const DataSourceEntry *p = &entry;
    connect(box, &QDialogButtonBox::accepted, this, [this, p] {
        if (p->uiData)
            m_options = p->uiData(QString());
        if (m_options.isEmpty())
            m_options = QStringLiteral("{}");
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
#else
    Q_UNUSED(entry);
    Q_UNUSED(path);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral("当前构建缺少 Qt6 UiTools，扩展 .ui 无法加载"), this));
#endif
}
