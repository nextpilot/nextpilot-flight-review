#!/usr/bin/env bash
# 把 Qt 6.2.4 装进用户目录 ~/.ps-dps，不依赖 /usr 包、不污染仓库目录。
# Lua 5.4 已内置于 src/3rdparty/lua-5.4，随源码编译，无需外部安装。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS_ROOT="${HOME}/.ps-dps"
QT_VERSION=6.2.4
QT_DIR="$DEPS_ROOT/Qt/$QT_VERSION/gcc_64"

if [ ! -f "$QT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
  echo "安装 Qt $QT_VERSION 到 $QT_DIR"
  python3 -m pip install --user aqtinstall >/dev/null
  python3 -m aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
    -O "$DEPS_ROOT/Qt" --archives qtbase qtsvg qttools icu
fi

echo "依赖就绪：$QT_DIR"
