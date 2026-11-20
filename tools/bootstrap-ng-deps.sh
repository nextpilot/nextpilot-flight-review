#!/usr/bin/env bash
# 把 ng 需要的开发依赖（Qt6 + Lua）装进一个不会被系统镜像恢复掉的前缀目录。
# 用法：sudo NG_DEPS_PREFIX=/opt/ng-deps bash tools/bootstrap-ng-deps.sh
# 产物：$PREFIX/usr/...  之后用 NG_DEPS_PREFIX=$PREFIX 配置 cmake 即可。
set -euo pipefail
PREFIX="${NG_DEPS_PREFIX:-/opt/ng-deps}"
PKGS=(
  qt6-base-dev qt6-base-dev-tools qt6-base-private-dev
  libqt6core6 libqt6gui6 libqt6widgets6 libqt6network6 libqt6dbus6
  libqt6opengl6 libqt6openglwidgets6 libqt6svg6 libqt6test6
  libqt6concurrent6 libqt6printsupport6 libqt6sql6 libqt6xml6
  liblua5.4-dev liblua5.4-0
  libgl-dev libgl1 libglx-dev libglvnd0 libglx0 libegl1
  libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4
  libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-render-util0
  libdouble-conversion3 libpcre2-16-0 libzstd1 libmd4c0
  qt6-qpa-plugins libqt6svg6-dev
)

echo "[1/3] 下载 deb 包 → $PREFIX"
mkdir -p "$PREFIX" /tmp/ng-debs
cd /tmp/ng-debs
# 清掉上一轮的 deb，避免把旧版本混进来
rm -f ./*.deb || true
apt-get download "${PKGS[@]}" >/dev/null 2>&1 || apt-get download "${PKGS[@]}"

echo "[2/3] 解包到前缀（不动 /usr）"
for f in ./*.deb; do dpkg -x "$f" "$PREFIX"; done

echo "[3/3] 重写 Qt CMake 配置里的绝对路径"
# Ubuntu 的 Qt6*.cmake 里 IMPORTED_LOCATION 是 /usr/lib/x86_64-linux-gnu/...，
# 解包到前缀后必须把这些绝对路径改成前缀内路径，否则链接的还是系统库。
LIBCMAKE="$PREFIX/usr/lib/x86_64-linux-gnu/cmake"
if [ -d "$LIBCMAKE" ]; then
  find "$LIBCMAKE" -name '*.cmake' -print0 | xargs -0 sed -i \
    -e "s|/usr/lib/x86_64-linux-gnu|$PREFIX/usr/lib/x86_64-linux-gnu|g" \
    -e "s|/usr/include/x86_64-linux-gnu|$PREFIX/usr/include/x86_64-linux-gnu|g" \
    -e "s|/usr/lib/qt6|$PREFIX/usr/lib/qt6|g" \
    -e "s|/usr/share/qt6|$PREFIX/usr/share/qt6|g"
fi
# qmake/Lua 的 .pc 也顺手改掉，避免 pkg-config 找系统路径
find "$PREFIX/usr/lib/x86_64-linux-gnu/pkgconfig" -name '*.pc' -print0 2>/dev/null | xargs -0 -r sed -i \
  -e "s|^prefix=/usr|prefix=$PREFIX/usr|" -e "s|/usr/include|$PREFIX/usr/include|g" || true

echo "完成：$PREFIX"
echo "Qt CMake 目录：$PREFIX/usr/lib/x86_64-linux-gnu/cmake"
