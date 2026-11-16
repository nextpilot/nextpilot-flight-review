#pragma once

#include <QHash>
#include <QString>
#include <QVector>

struct PsFieldDescriptor
{
    QString name;
    QString type;
    QString dataType;
    int length = 0;
    int arrayLength = 0;
    QString enumId;
    QString description;
};

struct PsMessageDescriptor
{
    QString protocol;
    QString name;
    int messageId = -1;
    QString variant;
    QVector<PsFieldDescriptor> fields;
};

class ProtocolCatalog
{
public:
    bool load(const QString &path, const QString &protocol, QString *error = nullptr);
    const PsMessageDescriptor *find(const QString &protocol, int messageId,
                                    const QString &variant = QString()) const;
    const PsMessageDescriptor *findByName(const QString &protocol, const QString &name,
                                          const QString &variant = QString()) const;

private:
    QHash<QString, QVector<PsMessageDescriptor>> m_messages;
    QHash<QString, QVector<PsMessageDescriptor>> m_byName;
};
