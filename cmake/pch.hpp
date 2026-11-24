// 预编译头（PCH）实验用公共头集合。仅在 PS_USE_PCH=ON 时被 target 引用。
//
// 取舍原则：只放高频、稳定、跨 TU 重复包含的头。PCH 一旦被某个 TU 包含
// 而未包含这些头，多出来的部分就是纯负担（PCH 越大，加载越慢）；反过来
// 少放一个高频头，收益立刻缩水。因此这里选 Qt 侧最通用的核心类型，
// 不放项目自身头文件——项目头的改动频率远高于 Qt，PCH 命中率会崩。
//
// 注意：PCH 与 ccache 有交互，见 docs/develop/build-performance.md。

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QHash>
#include <QtCore/QVariant>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTimer>

#include <QtGui/QAction>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QTransform>

#include <QtWidgets/QApplication>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QWidget>
