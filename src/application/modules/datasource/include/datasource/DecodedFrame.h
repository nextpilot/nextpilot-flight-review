#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector>

struct PsDecodedField
{
    QString name;
    QVariant raw;
    QVariant value;
    QString unit;
    int offset = -1;
    int size = 0;
};

struct PsDecodedFrame
{
    QString protocol;
    QString frameName;
    QString frameKey;
    double timestamp = 0.0;
    QByteArray rawBytes;
    QVector<PsDecodedField> fields;
    bool checksumOk = true;
    bool unknown = false;
};
