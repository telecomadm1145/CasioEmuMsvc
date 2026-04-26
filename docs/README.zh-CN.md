<div align="center">

<img src="../CasioEmuMsvc/icon.ico" width="96" alt="CasioEmuMsvc Logo"/>

# CasioEmuMsvc

**面向 nX-U8/100 & nX-U16/100 MCU 系列的高性能模拟器与开发工具平台。**

[![License](https://img.shields.io/github/license/telecomadm1145/CasioEmuMsvc?style=flat-square)](../LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Android-blue?style=flat-square)](#-平台支持)
[![Stars](https://img.shields.io/github/stars/telecomadm1145/CasioEmuMsvc?style=flat-square)](https://github.com/telecomadm1145/CasioEmuMsvc/stargazers)
[![Discord](https://img.shields.io/discord/NM39VPdJTf?label=Discord&logo=discord&style=flat-square)](https://discord.gg/NM39VPdJTf)

🌐 [English](README.en.md) · [Tiếng Việt](README.vi.md)

</div>

---

## 项目简介

**CasioEmuMsvc** 是一款面向卡西欧计算器 **nX-U8/100** 与 **nX-U16/100** 系列 MCU 的高性能模拟器与逆向工程工作台。它不仅能完整运行固件，更提供了调试器、反汇编器、快照管理器以及 AI 驱动的插件接口，是固件开发与分析的利器。

---

## ✨ 功能特性

### 🖥️ 核心模拟
| 功能 | 说明 |
|---|---|
| **完整模拟** | 完整支持 nX-U16/100 和 nX-U8/100 指令集 |
| **精确外设模拟** | 键盘、LCD、定时器、RTC、UART 等一应俱全 |
| **SDL2 渲染** | 硬件加速显示，跨平台图形支持 |

### 🔬 开发与调试工具
| 功能 | 说明 |
|---|---|
| **交互式调试器** | 断点、单步执行、寄存器与内存查看 |
| **反汇编器 / 代码视图** | 实时反汇编，支持标签与符号 |
| **内存十六进制编辑器** | 实时查看与在线编辑内存 |
| **内存断点** | 对任意地址范围的读/写操作设置断点 |
| **监视窗口** | 监视任意表达式和内存地址 |
| **调用分析** | 可视化调用图与执行流程 |
| **变量窗口** | 实时查看和修改命名变量 |

### 📸 快照系统
| 功能 | 说明 |
|---|---|
| **存档 / 读档** | 一键保存和恢复完整模拟器状态 |
| **快照分支** | 基于树结构的非线性存档管理 |
| **屏幕预览** | 快照内嵌 LCD 状态缩略图 |
| **压缩格式** | 使用 `miniz`（zlib）生成紧凑的 `.snapshot` 文件 |

### 🔌 插件系统
| 插件 | 说明 |
|---|---|
| **MCP 插件** | 通过 SSE HTTP 服务器实现 AI 驱动调试（模型上下文协议） |
| **Python 插件** | 使用 Python 脚本控制模拟器 |
| **Cw2tools 插件** | 集成卡西欧开发工具链 |
| **示例插件** | 用于自定义插件开发的模板 |

### 🚀 启动界面与用户体验
| 功能 | 说明 |
|---|---|
| **启动选择器 UI** | 可视化型号选择器与 ROM 库管理 |
| **桌面快捷方式** | 为每个型号创建系统快捷方式（Windows COM / Linux `.desktop`） |
| **自动更新检测** | 后台检查 GitHub 最新版本并在应用内提示 |
| **主题管理器** | 支持自定义颜色、字体与缩放 |
| **多语言支持** | 界面支持英文、中文、越南语等多种语言 |
| **Discord 状态** | 在 Discord 中展示当前正在模拟的型号 |

---

## 📦 平台支持

| 平台 | 状态 | 说明 |
|---|---|---|
| **Windows** | ✅ 完整支持 | 主要平台，Visual Studio 2022 |
| **Linux** | ✅ 已支持 | CMake + Clang/Ninja，使用 `build-linux.sh` |
| **Android** | ✅ 已支持 | Gradle + NDK，通过 Android Studio 构建 |

---

## 🚀 快速入门

### 环境准备

- **Windows**：Visual Studio 2022（Community 或更高版本），需安装 **"使用 C++ 的桌面开发"** 工作负载
- **Linux**：`cmake`、`clang`、`ninja-build`、`libx11-dev`、`libgl1-mesa-dev`、`libcurl4-openssl-dev`、`zlib1g-dev`
- **Android**：Android Studio + NDK

### Windows 构建

**方式 A — Visual Studio 图形界面**

1. 克隆仓库（含子模块）：
   ```bash
   git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
   ```
2. 用 Visual Studio 2022 打开 `CasioEmuMsvc.sln`。
3. 选择 **Release / x64** 配置，点击 **生成 → 重新生成解决方案**。

**方式 B — MSBuild 命令行**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64
```

**方式 C — CMake**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXECUTABLE=ON
cmake --build build --config Release
```

> **说明：** `BUILD_EXECUTABLE=ON` 将所有依赖静态链接，生成单一可移植的 `.exe` 文件。

### Linux 构建

使用脚本自动安装依赖并构建（需要 `apt`/`sudo`）：

```bash
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
chmod +x build-linux.sh
./build-linux.sh        # 交互模式（安装前询问确认）
./build-linux.sh -y     # 非交互模式（自动安装）
```

手动 CMake 构建：

```bash
# 安装依赖（Debian/Ubuntu）
sudo apt-get install build-essential cmake clang ninja-build \
    libx11-dev libxext-dev libgl1-mesa-dev libcurl4-openssl-dev zlib1g-dev

cmake -S . -B build \
    -G Ninja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXECUTABLE=ON

cmake --build build -- -j$(nproc)
```

### Android 构建

用 **Android Studio** 打开项目根目录执行 Gradle 构建，或命令行：

```bash
./gradlew assembleRelease
```

---

## 🛠️ 故障排除

### GPU / 渲染问题

强制使用 OpenGL 渲染：

```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe
```

> **提示**：建议优先更新 GPU 驱动程序。

### ROM 缺失 / 找不到型号

将 ROM 文件放在可执行文件同目录下，或在启动界面手动浏览注册。

### CMake 配置失败

确保所有子模块已初始化：

```bash
git submodule update --init --recursive
```

---

## 💬 交流与反馈

- 💬 **Discord 社区**: [discord.gg/NM39VPdJTf](https://discord.gg/NM39VPdJTf)
- 📧 **邮件**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
- 🐛 **Issues**: [GitHub Issues](https://github.com/telecomadm1145/CasioEmuMsvc/issues)
