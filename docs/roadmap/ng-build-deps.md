# ng 构建依赖：装到「不会被恢复掉」的位置

## 为什么

云主机每次重置只保留用户目录（`/home/user`），`apt install` 装进 `/usr` 的
Qt / Lua 会随镜像一起消失，表现为：

```
Could not find a package configuration file provided by "Qt6"
```

每次重装几分钟，且要反复确认版本。解决办法是把开发依赖铺进用户目录下的
一个独立前缀，构建时显式指向它。

## 前缀位置

```
/home/user/Doubao/tools/ng-deps/           # 整个前缀，重装系统也在
  usr/include/x86_64-linux-gnu/qt6/
  usr/lib/x86_64-linux-gnu/                # libQt6*.so + cmake/Qt6 + pkgconfig
  usr/lib/x86_64-linux-gnu/cmake/Qt6/
  usr/lib/qt6/{bin,libexec,plugins}        # moc/rcc/uic/qmake + 平台插件
  usr/include/lua5.4/, usr/lib/…/liblua5.4.so
```

一次性铺设脚本：`/home/user/Doubao/tools/bootstrap-ng-deps.sh`
（`sudo bash …`，走 `apt-get download` + `dpkg -x`，不动系统 `/usr`）。
脚本会做三件必须的事：

1. 把 deb 解包到前缀；
2. 把 Qt 各 `*.cmake` 里的绝对路径 `/usr/...` 改写成前缀内路径
   （否则 IMPORTED_LOCATION 仍指向系统库，白铺）；
3. 把 `libexec/moc|rcc|uic` 链到 `bin/` 下（Qt 的 CMake 工具链按 `bin` 找工具）。

## 构建怎么用

`ng/CMakeLists.txt` 会自动探测：

- 设了 `NG_DEPS_PREFIX` 环境变量 → 用它；
- 否则若 `/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu/cmake/Qt6` 存在 → 用它；
- 都没有 → 回退仓库 `deps/Qt/6.2.4/gcc_64`（Windows 本地开发常用）。

```bash
cmake -S ng -B build-ng -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ng -j$(nproc)
QT_QPA_PLATFORM=offscreen ctest --test-dir build-ng
```

运行时动态库从同一前缀加载（`CMAKE_BUILD_RPATH` 已设），不依赖系统有没有 Qt。
需要 GUI 时再补：

```bash
export QT_PLUGIN_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/qt6/plugins
export LD_LIBRARY_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu
```

## 边界

- 这套只解决「Qt/Lua 被镜像恢复掉」；`gcc` / `cmake` / `cargo` 属基础镜像，不在范围内。
- 前缀里是发行版 6.2.4 的 Qt，和仓库 `deps/Qt/6.2.4` 同版本，接口一致。
- 前缀体积约 300 MB，位于用户目录，不随系统快照回滚。
