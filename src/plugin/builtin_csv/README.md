# builtin_csv

最小 CSV Loader（M1 首个落地案例）：列名 → 通道，第一列时间（自动识别 s / ms / us）。
用来说明 M0 的 ABI 约定：宿主预分配 PsBatchOut，插件零拷贝写回。
