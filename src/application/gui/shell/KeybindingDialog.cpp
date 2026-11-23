#include "KeybindingDialog.h"


#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

KeybindingDialog::KeybindingDialog(CommandRegistry &commands, KeybindingRegistry &bindings,
                                   QWidget *parent)
    : QDialog(parent), m_commands(commands), m_bindings(bindings)
{
    setObjectName(QStringLiteral("keybindingDialog"));
    setWindowTitle(QStringLiteral("快捷键设置"));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral("留空恢复默认；两段 chord 用逗号分隔，例如 Ctrl+K, Ctrl+P。"),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("keybindingDialog.table"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("命令"), QStringLiteral("默认"), QStringLiteral("当前")});
    m_table->setRowCount(m_bindings.commandIds().size());
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    const QStringList ids = m_bindings.commandIds();
    for (int row = 0; row < ids.size(); ++row)
    {
        const QString id = ids.at(row);
        const CommandDescriptor *command = m_commands.find(id);
        const QString title = command ? command->title : id;
        m_table->setItem(row, 0, new QTableWidgetItem(
                                   QStringLiteral("%1  (%2)").arg(title, id)));
        m_table->setItem(row, 1, new QTableWidgetItem(m_bindings.defaultSequenceFor(id)));
        auto *edit = new QLineEdit(m_bindings.sequenceFor(id), m_table);
        edit->setObjectName(QStringLiteral("keybindingDialog.sequence"));
        edit->setProperty("commandId", id);
        m_table->setCellWidget(row, 2, edit);
    }

    layout->addWidget(m_table, 1);
    m_conflicts = new QLabel(this);
    m_conflicts->setObjectName(QStringLiteral("keybindingDialog.conflicts"));
    m_conflicts->setWordWrap(true);
    m_conflicts->setStyleSheet(QStringLiteral("QLabel{color:#c0392b;}"));
    layout->addWidget(m_conflicts);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QStringList KeybindingDialog::editedSequences() const
{
    QStringList out;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        if (auto *edit = qobject_cast<QLineEdit *>(m_table->cellWidget(row, 2)))
            out << edit->text().trimmed();
    }
    return out;
}

QStringList KeybindingDialog::conflictMessages() const
{
    QStringList out;
    for (const KeybindingConflict &conflict : m_bindings.conflicts())
        out << QStringLiteral("%1 被以下命令同时占用：%2")
                   .arg(conflict.sequence, conflict.commandIds.join(QStringLiteral(", ")));
    return out;
}

void KeybindingDialog::save()
{
    const QStringList ids = m_bindings.commandIds();
    const QStringList edits = editedSequences();
    if (edits.size() != ids.size())
        return;

    for (int i = 0; i < edits.size(); ++i)
    {
        const QString sequence = edits.at(i);
        if (!sequence.isEmpty() && KeybindingRegistry::normalize(sequence).isEmpty())
        {
            m_conflicts->setText(QStringLiteral("键位无效：%1（%2）").arg(sequence, ids.at(i)));
            return;
        }
    }

    const QHash<QString, QString> previous = m_bindings.overrides();
    for (int i = 0; i < ids.size(); ++i)
    {
        const QString sequence = edits.at(i);
        if (sequence.isEmpty() || sequence == m_bindings.defaultSequenceFor(ids.at(i)))
            m_bindings.clearOverride(ids.at(i));
        else
            m_bindings.setOverride(ids.at(i), sequence);
    }

    const QStringList conflicts = conflictMessages();
    if (!conflicts.isEmpty())
    {
        m_bindings.setOverrides(previous);
        m_conflicts->setText(conflicts.join(QStringLiteral("\n")));
        return;
    }

    if (m_saveHandler)
        m_saveHandler();
    accept();
}

} // namespace nextpilot::plotscope::gui
