#pragma once

#include <QString>

// 应用路径的唯一出口：配置、日志、默认布局。
class ShellPaths
{
public:
    static QString configDir();
    static QString logDir();
    static QString logPath();
    static QString defaultLayoutPath();
};
