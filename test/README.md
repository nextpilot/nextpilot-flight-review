# test

- `unit/`：C++ 侧（Qt Test，M1 起）
- `kernel/`：`cargo test`（当前由 ctest 的 `kernel_cargo_test` 驱动）
- `golden/`：解析结果哈希清单，回归比对
- `perf/`：22 ms 端到端预算的基准用例
- `fixture/`：每个样例 ≤2 MB
