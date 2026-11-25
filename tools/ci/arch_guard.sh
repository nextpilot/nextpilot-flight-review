#!/usr/bin/env bash
# 架构铁律反证（A4a）：把"裸壳零业务、模块可插拔"从纸面变成可重复执行的门禁。
#
# 三项检查：
#   1. configure 期源码守卫（LibBoundary/LayerContract/ArchitectureGuard）——configure 即验证；
#   2. ps_shell_bare 独立 configure + build + run smoke（零模块第二产品）；
#   3. 逐个剔除任一 ps_module_* 后，完整产品 plotscope-gui 仍能 configure + build。
#
# 用法：
#   tools/ci/arch_guard.sh                 # 全跑（较慢，逐模块 configure+build）
#   tools/ci/arch_guard.sh --quick         # 只做 1+2（快，日常提交前）
#   RUSTUP_TOOLCHAIN=stable tools/ci/arch_guard.sh   # 环境 rust 工具链钉死时的绕过
#
# 退出码：0 全过；非 0 表示某项反证失败（脚本会打印具体阶段）。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${ROOT}/build-arch-guard"
QUICK=0
[[ "${1:-}" == "--quick" ]] && QUICK=1

# Qt 路径：优先环境变量 PS_QT_DIR，否则用 setup 脚本的统一下载位置。
QT_PREFIX="${PS_QT_DIR:-/root/.ps-dps/toolchain/qt/6.2.4/gcc_64}"
CMAKE_ARGS=(-S . -B "${WORK}" -DPS_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH="${QT_PREFIX}")

cd "${ROOT}"
echo "=== [1/3] configure 期守卫（含三条铁律源码级检查）==="
cmake "${CMAKE_ARGS[@]}" > /tmp/ps_arch_cfg.log 2>&1 || {
    echo "FAIL: configure/守卫未通过"; tail -30 /tmp/ps_arch_cfg.log; exit 1; }
grep -E '守卫通过|架构守卫' /tmp/ps_arch_cfg.log | tail -5 || true
echo "OK: configure + 守卫通过"

echo "=== [2/3] ps_shell_bare 独立构建 + 冒烟 ==="
cmake --build "${WORK}" -j4 --target ps_shell_bare > /tmp/ps_arch_bare.log 2>&1 || {
    echo "FAIL: ps_shell_bare 构建失败"; tail -30 /tmp/ps_arch_bare.log; exit 1; }
QT_QPA_PLATFORM=offscreen timeout 15 "${WORK}/src/application/bare/ps_shell_bare" --smoke \
    > /tmp/ps_arch_bare_run.log 2>&1 || {
    echo "FAIL: ps_shell_bare 运行失败"; tail -20 /tmp/ps_arch_bare_run.log; exit 1; }
echo "OK: ps_shell_bare 独立 build + run"

if [[ "${QUICK}" -eq 1 ]]; then
    echo "=== --quick：跳过逐模块剔除 ==="
    exit 0
fi

echo "=== [3/3] 逐个剔除任一功能模块后完整产品仍能构建 ==="
# 按「功能模块」分组：headless 器官(<name>) + 呈现层(<name>_gui) 是配对的同一模块，
# 呈现层合法依赖本功能器官的序列化等，剔除时整组去才是"删一个模块"的语义。
MODULE_GROUPS=(
    "ps_module_data;ps_module_data_gui"
    "ps_module_plot;ps_module_plot_gui"
    "ps_module_script;ps_module_script_gui"
    "ps_module_protocol;ps_module_protocol_gui"
    "ps_module_report;ps_module_report_gui"
    "ps_module_analysis"
    "ps_module_settings"
    "ps_module_workspace"
)

FAIL_COUNT=0
for mod in "${MODULE_GROUPS[@]}"; do
    printf '  剔除 %-50s ... ' "${mod}"
    if cmake "${CMAKE_ARGS[@]}" -DPS_DISABLE_MODULE="${mod}" \
        > /tmp/ps_arch_drop.log 2>&1 \
       && cmake --build "${WORK}" -j4 --target plotscope-gui \
        >> /tmp/ps_arch_drop.log 2>&1; then
        echo "OK"
    else
        echo "FAIL"
        tail -25 /tmp/ps_arch_drop.log
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 恢复无剔除配置
cmake "${CMAKE_ARGS[@]}" -U PS_DISABLE_MODULE > /dev/null 2>&1 || true

if [[ "${FAIL_COUNT}" -gt 0 ]]; then
    echo "FAIL: ${FAIL_COUNT} 个模块剔除后无法构建——存在模块间横向依赖或壳依赖模块"
    exit 1
fi
echo "OK: 全部 ${#MODULE_GROUPS[@]} 个功能模块逐个剔除后 plotscope-gui 仍可构建"
echo "=== 架构反证全部通过 ==="
