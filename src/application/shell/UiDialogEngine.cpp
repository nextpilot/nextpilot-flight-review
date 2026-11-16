#include "UiDialogEngine.h"

#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#if PS_HAVE_UITOOLS
#include <QDialogButtonBox>
#include <QFile>
#include <QUiLoader>
#endif

UiDialogEngine::UiDialogEngine(const UiDialogSpec &spec, const QString &path, QWidget *parent)
    : QDialog(parent), m_spec(spec), m_sourcePath(path)
{
    setWindowTitle(m_spec.title);
    if (!m_sourcePath.isEmpty())
        setToolTip(QStringLiteral("数据文件：%1").arg(m_sourcePath));

#if PS_HAVE_UITOOLS
    QUiLoader loader;
    QFile f(m_spec.uiPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        auto *lay = new QVBoxLayout(this);
        lay->addWidget(new QLabel(QStringLiteral("扩展 UI 打不开：%1").arg(m_spec.uiPath), this));
        return;
    }
    QWidget *w = loader.load(&f, this);
    f.close();
    if (!w)
    {
        auto *lay = new QVBoxLayout(this);
        lay->addWidget(new QLabel(QStringLiteral("扩展 UI 加载失败：%1").arg(m_spec.uiPath), this));
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
    connect(box, &QDialogButtonBox::accepted, this, [this, w] {
        collectOptions(w);
        if (m_options.isEmpty())
            m_options = QStringLiteral("{}");
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
#else
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral("当前构建缺少 Qt6 UiTools，扩展 .ui 无法加载"), this));
#endif
}

void UiDialogEngine::collectOptions(QWidget *root)
{
    QJsonObject out;
    auto consume = [&out](const QString &name, const QString &raw) {
        if (raw.isEmpty())
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isObject())
        {
            const QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                out.insert(it.key(), it.value());
            return;
        }
        out.insert(name.isEmpty() ? QStringLiteral("options") : name, raw);
    };

    if (root && m_spec.data)
    {
        for (QWidget *child : root->findChildren<QWidget *>())
        {
            const QString name = child->objectName();
            if (!name.isEmpty())
                consume(name, m_spec.data(name));
        }
    }
    if (out.isEmpty() && m_spec.data)
        consume(QString(), m_spec.data(QString()));

    m_options = out.isEmpty()
                    ? QStringLiteral("{}")
                    : QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}
