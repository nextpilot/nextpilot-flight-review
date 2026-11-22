#!/usr/bin/env bash
# 用 ~/.ps-dps 下的 Qt 构建并跑测试，不读系统 Qt。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT="$HOME/.ps-dps/Qt/6.2.4/gcc_64"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$QT" \
  -DQt6UiTools_DIR="$QT/lib/cmake/Qt6UiTools"
cmake --build "$ROOT/build" --parallel
ctest --test-dir "$ROOT/build" --output-on-failure
