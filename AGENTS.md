# PlotScope 架构边界

本文件是仓库级硬约束，不是建议。新增代码、扩展、文档和评审都先按这里判断归属；与历史架构文档冲突时，以本文件为准。

## 强制工作纪律（最高优先级）

- **未经用户明确同意，禁止写代码**——源码、构建脚本、迁移、脚本一律在内。动手前先给方案（改哪些文件、影响面、验证方式），等用户明确表态（"同意/执行/做吧/拍板"）后才可施工。
- **先复述后动手**——收到修改类指令后，先用执行者自己的话复述对指令的理解（改什么、怎么改、含糊点），等用户对复述明确同意后才可动手；理解有误则纠正后再次确认。见 `.claude/skills/ai-develop-workflow/references/change-confirmation.md`。
- **方案没有讨论清楚前，禁止写代码**——存在歧义、未裁定的分叉、半成品讨论时，继续讨论或补齐事实，不得以"先做起来看看"为名动手。
- "明确同意"的判据：用户的肯定表态才算；沉默、未回复、话题转移、仅提问都不算同意。
- 例外仅限：用户明确指令的文档更新、只读操作（读代码、盘现状、跑测试验证既有行为）。

## Skill 管理约定

- 本仓库的 agent skill 一律存放于 `.claude/skills/`，随 git 走，执行 agent 拉取仓库即生效；新 skill 默认写到这里，不写外部私有目录。

## 1. 工程归属

仓库为**单一工程**：`src/` 即 PlotScope 本体；更早的历史在 legacy-v1/v2/v3 存档分支（冻结）。

- 架构边界与协作要求：`src/AGENTS.md`（权威来源）
- 机制设计与决策记录：`src/ARCHITECTURE.md`、`src/LAYERS.md`
- 构建：仓库顶层 `CMakeLists.txt`（project PlotScope），构建目录 `build-debug` / `build-release`
- 测试：根 `tests/`（`ctest`，fixture 在 `tests/fixture/`）
- Rust 内核：`src/kernel`（cargo 编 `ps-abi`，构建入口在顶层 CMake）
- 第三方锁版本库：`src/3rdparty`（lua-5.4 / saribbon / ads / mavlink）
- 本地依赖前缀：`~/.ps-deps`（仓库外；由 `tools/ubuntu-setup.sh --install --qt` 生成）

## 2. 文档边界

- `src/AGENTS.md`：src 工程的协作要求与硬线；`src/ARCHITECTURE.md`：架构机制与决策记录。二者是工程边界的权威来源，本文件不重复其内容。
- `docs/roadmap/`：长期规划与里程碑设计文档（方案/思路）；**任务 plan 不写仓库文件**，一律建 gitee issue 跟踪，内容规范与开发流程统一按 `.claude/skills/ai-develop-workflow/`。
- `docs/manual/`：用户手册；`docs/develop/`：开发指南；给人看的文档去 AI 腔、只写事实。
- 任何架构边界变化先改文档，再改代码；不能只改文档不改边界，也不能只改代码不改文档。

## 3. 分支约定（GitHub Flow）

- `main`：唯一长期分支，始终可发布；所有改动经短命特性分支 + PR 合入，规则见 `.claude/skills/ai-develop-workflow/`。
- `legacy-v1` / `legacy-v2` / `legacy-v3`：**历史存档，冻结，禁止开发**；考古切这三个分支查，不合并、不 rebase、不推送。

## 4. 提交前检查

推之前必须过门禁，权威口径见 [`docs/develop/push-gate.md`](docs/develop/push-gate.md)。一条命令：

```bash
tools/ci/local_preflight.sh          # quick：格式 / 脚本 / workflow / mypy / 重排告警
tools/ci/local_preflight.sh --full   # 改过 C++，或推分支前最后一遍：加 构建 + ctest + tidy + arch_guard
```

- **基准统一 `origin/main`**：先 `git fetch origin && git rebase origin/main`。不允许在旧 base 上推送，否则改动行口径与 CI 不一致。
- **启用 hook（每个 clone 一次）**：`git config core.hooksPath .githooks`。`pre-commit` 只修格式，`pre-push` 阻断未过门禁的推送。
- 改动符合 `src/AGENTS.md` 的目录、依赖与层级边界；新增 include 不跨越依赖矩阵；UI 遵守主题硬约束。
- 新模块/扩展有对应测试；改 UI 做 GUI 冒烟。
- **三条硬禁令**：不整文件重排；不放宽门禁换绿（不改断言、删测试、加 `#ifdef` 绕守卫、调 `.clang-tidy` 白名单）；不用 `--no-verify`、不 force-push `main`。
- **CI 绿 ≠ 可合**：主线合并前还要本地 `--full`（含合并后状态）+ PR 最后一轮 CI 绿 + 四步门禁 + owner 点头。
- 文档归位正确：成熟指南在 `docs/develop/`，长期规划在 `docs/roadmap/`；任务跟踪在 gitee issue。
