#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ps::components
{
struct ExtensionEntry;
}

class DiagnosticsDialog : public QDialog
{
public:
    DiagnosticsDialog(QWidget *parent, const QString &kernelInfo,
                      const QVector<ps::components::ExtensionEntry> &extensions,
                      const QStringList &errors);
};
