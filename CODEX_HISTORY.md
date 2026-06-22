# Codex History

本文件用于按批次记录本仓库中的 Codex 协作历史。

## 记录规则

- 每一轮新的对话批次都按时间顺序追加记录。
- 批次编号递增：`Batch 001`、`Batch 002`，依次类推。
- 记录保持简洁，但应足够还原用户请求、Codex 回复，以及涉及的文件/系统操作。
- 除非明确需要修正历史记录，否则不改写已有批次。

## Batch 001

- Time: 2026-06-22 20:21:12 CST (+0800)
- 用户请求：创建一个 Codex History 的 Markdown 文件，并确保后续每一个对话都按批次有序记录到其中。
- Codex 回复：创建 `CODEX_HISTORY.md`，写入记录规则，并记录第一个批次。
- 文件变更：
  - 新增 `CODEX_HISTORY.md`

## Batch 002

- Time: 2026-06-22 20:23:37 CST (+0800)
- 用户请求：提交 `CODEX_HISTORY.md` 文件到 git。
- Codex 回复：先将本次提交请求追加到历史文件，再暂存并提交该文件。
- 文件变更：
  - 更新 `CODEX_HISTORY.md`

## Batch 003

- Time: 2026-06-22 20:33:27 CST (+0800)
- 用户请求：基于 CMake 和 C++ 创建一个基本 C++ 工程，能够完成编译并输出字符串到终端。
- Codex 回复：新增最小 CMake/C++ 工程文件，补充构建和运行说明，并执行构建运行验证。
- 文件变更：
  - 新增 `CMakeLists.txt`
  - 新增 `src/main.cpp`
  - 更新 `README.md`
  - 更新 `CODEX_HISTORY.md`

## Batch 004

- Time: 2026-06-22 20:35:15 CST (+0800)
- 用户请求：提交当前 CMake/C++ 工程改动到 git。
- Codex 回复：追加本次提交请求到历史文件，暂存并提交相关工程文件和文档。
- 文件变更：
  - 更新 `CODEX_HISTORY.md`

## Batch 005

- Time: 2026-06-22 20:36:34 CST (+0800)
- 用户请求：在 CMake 中配置已通过 Homebrew 安装的 FTXUI 框架，确保可以编译并运行后提交 git。
- Codex 回复：配置 CMake 查找并链接 FTXUI，更新示例程序使用 FTXUI 输出终端内容，验证构建运行后提交改动。
- 文件变更：
  - 更新 `CMakeLists.txt`
  - 更新 `src/main.cpp`
  - 更新 `README.md`
  - 更新 `CODEX_HISTORY.md`

## Batch 006

- Time: 2026-06-22 20:51:07 CST (+0800)
- 用户请求：按照已确认计划实现基于 FTXUI 的简易 ADB 文件工具，支持参数解析、设备文件浏览、导入导出、进度弹窗、取消、语言切换，并在验证后提交 git。
- Codex 回复：实现 ADB 命令封装和 FTXUI 主界面，补充运行说明，完成编译及无 adb 错误路径验证后提交改动。
- 文件变更：
  - 更新 `CMakeLists.txt`
  - 更新 `src/main.cpp`
  - 新增 `src/adb_client.h`
  - 新增 `src/adb_client.cpp`
  - 新增 `src/app.h`
  - 新增 `src/app.cpp`
  - 更新 `README.md`
  - 更新 `CODEX_HISTORY.md`

### Batch 006 使用提示词与方案

用户用于触发实现的完整提示词如下：

````markdown
PLEASE IMPLEMENT THIS PLAN:
# FTXUI ADB 文件工具实现计划

## Summary
实现一个基于 FTXUI 的简易 ADB TUI 文件工具：启动时解析输出目录和设备 serial，浏览设备文件系统，支持选择、导出、导入、取消传输、语言切换和 Esc 退出。实现完成后追加 `CODEX_HISTORY.md`，编译运行验证，并提交 git。

已锁定默认行为：
- 设备浏览根目录：`/`
- 默认语言：中文，按 `L` 在 `zh/en` 间切换
- 导出同名处理：覆盖
- 进度条：按总任务数量推进
- 导入检查：检查本地源文件是否存在
- 平台范围：Linux/macOS

## Key Changes
- CMake 保持 `find_package(ftxui CONFIG REQUIRED)` 和 `ftxui::ftxui` 链接，新增必要源文件组织，例如 ADB 命令封装、TUI 状态、路径工具。
- 启动参数：
  - `argv[1]` 为输出文件夹；未传入则使用当前工作目录。
  - `argv[2]` 为目标设备 serial；未传入则解析 `adb devices` 中第一个状态为 `device` 的设备。
  - 若 `adb` 不可用、无可用设备、输出目录不存在/不可创建，则在 TUI 或启动错误中清晰提示。
- 主界面三行布局：
  - 顶部固定 1 行：左侧显示 `zh/en` 状态，中间显示 `adb-files-tui`，右侧显示 CMake 项目版本。
  - 中间动态高度：设备文件/文件夹列表，包含复选框、类型标识、文件名；上下移动，右键进入文件夹，左键返回上一级，根目录 `/` 不再返回。
  - 底部固定 1 行：显示当前设备路径；路径过长时按定时 tick 横向滚动。
- 文件操作：
  - 目录切换后清空全部选中状态。
  - 空格切换当前项选中状态。
  - `E` 导出选中项：在输出目录下创建 `YYYYMMDD_HHMMSS` 子目录，逐项执行 `adb -s <serial> pull <remote> <timestamp_dir>`，进度按完成项数推进，Esc 取消当前 adb 子进程并关闭弹窗。
  - `I` 导入文件：弹出输入框，输入本机绝对路径；回车后检查本地文件存在且为普通文件，再执行 `adb -s <serial> push <local> <current_remote_dir>`；进度条显示 0/1 到 1/1，Esc 可取消。
  - 首页 Esc 直接退出；弹窗中 Esc 优先取消/关闭弹窗。
- ADB 文件列表：
  - 用 `adb shell` 在当前目录枚举条目，返回类型和名称；目录优先或按名称排序保持稳定。
  - 路径拼接和 shell 参数必须统一转义，支持空格、括号等常见文件名字符。
  - 权限失败、目录不可读、pull/push 失败时在中间区域或弹窗状态中显示错误，不崩溃。

## Test Plan
- 构建验证：`cmake -S . -B build && cmake --build build`
- 基础运行：
  - 无 `adb` 或无设备时，程序给出明确错误状态。
  - 有设备时，不传参数可默认使用当前工作目录和第一个 `device` 状态设备。
  - 传入输出目录和 serial 时使用指定值。
- TUI 交互：
  - 顶部三项显示正确，`L` 切换中英文。
  - `/` 根目录下左键无效；进入/返回目录后选中状态被清空。
  - 底部长路径可以滚动显示。
  - Esc 在主页退出，在传输弹窗中取消任务。
- ADB 操作：
  - 选中文件/文件夹后 `E` 创建时间戳目录并导出。
  - `I` 输入不存在本地文件时提示错误；输入存在普通文件时导入当前设备目录。
  - 多选导出进度按项目数推进，失败项显示结果。
- 提交验证：
  - 更新 `CODEX_HISTORY.md` 新增本批次。
  - 提交前确认 `git status --short` 只包含本功能相关改动。
  - 提交信息建议：`Add FTXUI adb file manager`

## Assumptions
- `adb` 由用户环境提供并在运行时 PATH 中可见；当前规划中不捆绑 Android platform-tools。
- 导入只支持本机普通文件，不支持导入本机目录。
- 导出和导入遇到同名目标时允许覆盖，依赖 `adb pull/push` 默认行为。
- 由于当前环境未检测到 `adb`，实现时可完成编译和无设备错误路径验证；真实设备导入/导出需要在装有 `adb` 且连接设备的环境中验收。
````

实现时采用的执行方案：

- 先确认仓库干净、读取当前 `CMakeLists.txt`、`src/main.cpp`、FTXUI 组件头文件，以及验证当前环境没有可用 `adb`。
- 保持现有 CMake/FTXUI 集成，新增 `src/adb_client.*` 负责：
  - 检查 `adb version`。
  - 解析 `adb devices` 的首个 `device` 状态 serial。
  - 通过 `adb -s <serial> shell <script>` 枚举远端目录。
  - 通过 POSIX `fork/execvp/waitpid` 执行可取消的 `adb pull` 和 `adb push`。
  - 提供远端路径拼接、父路径计算、目录条目解析和排序。
- 新增 `src/app.*` 负责 FTXUI 主界面：
  - 顶部固定信息栏、中央文件列表、底部路径栏。
  - 上下左右、空格、`E`、`I`、`L`、`Esc` 等事件处理。
  - 导入输入弹窗、导入/导出进度弹窗。
  - 后台传输线程通过 FTXUI `Post` 回写 UI 状态，并用 `SIGTERM` 取消当前 adb 进程组。
  - 底部路径按终端宽度定时滚动。
- 更新 `src/main.cpp` 负责：
  - 解析可选输出目录和设备 serial。
  - 默认输出目录为当前工作目录。
  - 默认设备为 `adb devices` 的第一个 `device`。
  - `adb` 不存在、无设备、输出目录不可用时输出明确错误并退出。
- 更新 `README.md` 写入构建、运行参数和按键说明。
- 更新 `CODEX_HISTORY.md` 记录本批次。
- 执行验证：
  - `cmake -S . -B build && cmake --build build` 通过。
  - 当前环境无 `adb`，运行 `./build/adb-files-tui` 输出 `adb is not available in PATH.`。
- 提交 git：
  - 提交信息：`49db7b9 Add FTXUI adb file manager`

## Batch 007

- Time: 2026-06-22 21:03:17 CST (+0800)
- 用户请求：将这次使用到的提示词和方案完整补充到 `CODEX_HISTORY.md` 中并提交 git。
- Codex 回复：补充 Batch 006 的完整实现提示词、方案和验证/提交信息，并提交本次历史记录更新。
- 文件变更：
  - 更新 `CODEX_HISTORY.md`
