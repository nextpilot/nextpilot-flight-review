#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::gui
{

struct KeybindingConflict
{
    QString sequence;
    QStringList commandIds;
};

// 默认键位 + 用户覆盖。一个 sequence 可以是单键（Ctrl+B），
// 也可以是两段 chord（Ctrl+K, Ctrl+P）。空覆盖串表示恢复默认。
class KeybindingRegistry
{
public:
    void setDefault(const QString &commandId, const QString &sequence);
    bool setOverride(const QString &commandId, const QString &sequence);
    bool clearOverride(const QString &commandId);
    bool hasOverride(const QString &commandId) const;
    QHash<QString, QString> overrides() const { return m_overrides; }
    void setOverrides(const QHash<QString, QString> &overrides);

    QString sequenceFor(const QString &commandId) const;
    QString defaultSequenceFor(const QString &commandId) const { return m_defaults.value(commandId); }
    QHash<QString, QString> bindings() const;
    QStringList commandIds() const;

    QList<KeybindingConflict> conflicts() const;

    // 规范化后的展示串；无效键位返回空串。
    static QString normalize(const QString &sequence);
    static bool isChord(const QString &sequence);
    static QStringList chordSteps(const QString &sequence);

private:
    QHash<QString, QString> m_defaults;
    QHash<QString, QString> m_overrides;
};

} // namespace nextpilot::plotscope::gui
