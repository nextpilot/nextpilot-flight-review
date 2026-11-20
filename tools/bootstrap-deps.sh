#!/usr/bin/env bash
# 把 Qt 6.2.4 / Lua 5.4.4 装进仓库 deps/，不依赖 /usr 包。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_VERSION=6.2.4
QT_DIR="$ROOT/deps/Qt/$QT_VERSION/gcc_64"

if [ ! -f "$QT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
  echo "安装 Qt $QT_VERSION 到 $QT_DIR"
  python3 -m pip install --user aqtinstall >/dev/null
  python3 -m aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
    -O "$ROOT/deps/Qt" --archives qtbase qtsvg qttools icu
fi

# ng（PlotScopeNG）已改用仓库内的 src/3rdparty/lua-5.4 源码编库，不再吃 deps/lua；
# 这一段只保留给根 CMakeLists（旧 PlotScope src/）用，ng 构建不需要它。
if [ ! -f "$ROOT/deps/lua/lib/liblua.a" ]; then
  echo "构建 Lua 5.4.4 静态库"
  mkdir -p "$ROOT/deps/lua-src" "$ROOT/deps/lua/lib" "$ROOT/deps/lua/include"
  curl -fsSL https://www.lua.org/ftp/lua-5.4.4.tar.gz -o /tmp/lua-5.4.4.tar.gz
  tar -xzf /tmp/lua-5.4.4.tar.gz -C "$ROOT/deps/lua-src" --strip-components=1
  make -C "$ROOT/deps/lua-src/src" -j"$(nproc)" generic
  cp "$ROOT/deps/lua-src/src/"*.h "$ROOT/deps/lua/include/"
  cp "$ROOT/deps/lua-src/src/liblua.a" "$ROOT/deps/lua/lib/"
fi

echo "依赖就绪：$QT_DIR 与 $ROOT/deps/lua"
