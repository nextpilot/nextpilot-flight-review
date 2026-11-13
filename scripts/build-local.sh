#!/usr/bin/env bash
# 用 deps/ 下的 Qt/Lua 构建并跑测试，不读系统 Qt。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT="$ROOT/deps/Qt/6.2.4/gcc_64"
LUA="$ROOT/deps/lua"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$QT" \
  -DQt6UiTools_DIR="$QT/lib/cmake/Qt6UiTools" \
  -DLUA_LIBRARY="$LUA/lib/liblua.a" \
  -DLUA_INCLUDE_DIR="$LUA/include"
cmake --build "$ROOT/build" --parallel
ctest --test-dir "$ROOT/build" --output-on-failure
