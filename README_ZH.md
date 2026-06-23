# adb-files-tui
一个基于 adb 工具的 TUI 文件管理器。

语言：[English](README.md) | 中文

## 预览

![adb-files-tui 中文预览](images/preview_zh.png)

## 构建

需要安装 FTXUI，可以通过 Homebrew 安装：

```sh
brew install ftxui
```

```sh
cmake -S . -B build
cmake --build build
```

## 运行

```sh
./build/adb-files-tui
```

可执行文件接受三个可选参数：

```sh
./build/adb-files-tui [输出目录] [adb设备序列号] [adb路径]
```

- `输出目录`：用于保存导出文件的目录。如果省略，则使用当前工作目录。
- `adb设备序列号`：目标 adb 设备序列号。如果省略，则使用 `adb devices` 中第一个状态为 `device` 的设备。
- `adb路径`：adb 可执行文件路径，或包含 `adb` 的目录。如果省略，则从系统 `PATH` 中解析 `adb`。

例如，在当前机器上：

```sh
./build/adb-files-tui . "" /Users/devq-mini/Library/Android/sdk/platform-tools
```

## 控制

- `Up` / `W`：向上移动光标。
- `Down` / `S`：向下移动光标。
- `Right` / `D`：进入目录。
- `Left` / `A`：返回父目录。远端根目录 `/` 无法继续向上返回。
- `Space`：选择或取消选择当前文件或目录。
- `E`：导出选中的文件或目录。
- `I`：向当前远端目录导入一个本地文件。
- `L`：在中文和英文之间切换语言。
- `Esc`：在主页退出，或取消/关闭对话框。
