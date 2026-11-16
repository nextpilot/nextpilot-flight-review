#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ExtensionManager.h"


class DiagnosticsDialog : public QDialog
{
public:
    DiagnosticsDialog(QWidget *parent, const QString &kernelInfo,
                      const QVector<ExtensionManager::ExtensionInfo> &extensions,
                      const QStringList &errors, ExtensionManager *manager);
};
