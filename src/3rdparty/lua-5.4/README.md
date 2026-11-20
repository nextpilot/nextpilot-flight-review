# lua-5.4 —— Lua 5.4.4（MIT）

- 来源：https://www.lua.org/ftp/lua-5.4.4.tar.gz
- `src/` 是从上游 `src/` 原样拷过来的 C 源码与头文件，**不做本地修改**
  （上游版权与许可见 `README-upstream.md`）。
- 构建：`CMakeLists.txt` 把库本体（除 `lua.c` / `luac.c` 两个命令行入口）编成静态库
  目标 `lua::lua`，ng 侧只链接这个目标，不再引用 `deps/lua` 里的 Linux 预编译库。

升级步骤：换掉 `src/`（保持上游原样）、同步 `README-upstream.md`，不需要改 CMake。
