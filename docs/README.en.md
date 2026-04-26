<div align="center">

<img src="../CasioEmuMsvc/icon.ico" width="96" alt="CasioEmuMsvc Logo"/>

# CasioEmuMsvc

**A high-performance emulator and developer toolkit for the nX-U8/100 & nX-U16/100 MCU series.**

[![License](https://img.shields.io/github/license/telecomadm1145/CasioEmuMsvc?style=flat-square)](../LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Android-blue?style=flat-square)](#-platform-support)
[![Stars](https://img.shields.io/github/stars/telecomadm1145/CasioEmuMsvc?style=flat-square)](https://github.com/telecomadm1145/CasioEmuMsvc/stargazers)
[![Discord](https://img.shields.io/discord/NM39VPdJTf?label=Discord&logo=discord&style=flat-square)](https://discord.gg/NM39VPdJTf)

🌐 [中文](README.zh-CN.md) · [Tiếng Việt](README.vi.md)

</div>

---

## Overview

**CasioEmuMsvc** is a full-featured emulator and reverse-engineering workbench for Casio calculators built on the **nX-U8/100** and **nX-U16/100** microcontroller architectures. It goes far beyond simple emulation—offering a rich suite of developer tools including an interactive debugger, disassembler, snapshot manager, and an AI-powered plugin interface.

---

## ✨ Features

### 🖥️ Core Emulation
| Feature | Description |
|---|---|
| **Full Emulation** | Full instruction-set support for both nX-U16/100 and nX-U8/100 |
| **Accurate Peripherals** | Emulates keyboard, LCD, timers, RTC, UART, and more |
| **SDL2 Rendering** | Hardware-accelerated display with cross-platform graphics |

### 🔬 Developer & Debugging Tools
| Feature | Description |
|---|---|
| **Interactive Debugger** | Breakpoints, single-stepping, register & memory inspection |
| **Disassembler / Code Viewer** | Real-time disassembly with label and symbol support |
| **Memory Hex Editor** | Live hex view with in-place editing |
| **Memory Breakpoints** | Break on read/write to any address range |
| **Watch Window** | Monitor arbitrary expressions and memory locations |
| **Call Analysis** | Visualize call graphs and execution flow |
| **Variable Window** | Inspect and modify named variables in real-time |

### 📸 Snapshot System
| Feature | Description |
|---|---|
| **Save / Load States** | Instant save/restore of full emulator state |
| **Snapshot Branches** | Tree-based branching for non-linear save management |
| **Screen Previews** | Thumbnail of the LCD state embedded in each snapshot |
| **Compressed Format** | Uses `miniz` (zlib) for compact `.snapshot` files |

### 🔌 Plugin System
| Plugin | Description |
|---|---|
| **MCP Plugin** | AI-driven debugger via SSE HTTP server (Model Context Protocol) |
| **Python Plugin** | Script the emulator with Python |
| **Cw2tools Plugin** | Toolchain integration for Casio development workflows |
| **Example Plugin** | Template for building your own plugins |

### 🚀 Startup & UX
| Feature | Description |
|---|---|
| **Startup UI** | Visual model selector with ROM library management |
| **Desktop Shortcuts** | Create per-model OS shortcuts (Windows COM / Linux `.desktop`) |
| **Auto-Update Checker** | Background GitHub release check with in-app notification |
| **Theme Manager** | Custom color/font/scale settings |
| **Localization** | Multi-language UI (English, Chinese, Vietnamese, and more) |
| **Discord Rich Presence** | Shows current model in Discord status |

---

## 📦 Platform Support

| Platform | Status | Notes |
|---|---|---|
| **Windows** | ✅ Full | Primary platform, Visual Studio 2022 |
| **Linux** | ✅ Supported | CMake + Clang/Ninja via `build-linux.sh` |
| **Android** | ✅ Supported | Gradle + NDK via Android Studio |

---

## 🚀 Quick Start

### Prerequisites

- **Windows**: Visual Studio 2022 (Community or higher) with the **"Desktop development with C++"** workload
- **Linux**: `cmake`, `clang`, `ninja-build`, `libx11-dev`, `libgl1-mesa-dev`, `libcurl4-openssl-dev`, `zlib1g-dev`
- **Android**: Android Studio with NDK

### Build on Windows

**Option A — Visual Studio GUI**

1. Clone the repository (with submodules):
   ```bash
   git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
   ```
2. Open `CasioEmuMsvc.sln` in Visual Studio 2022.
3. Select the **Release / x64** configuration.
4. Click **Build → Rebuild Solution**.

**Option B — MSBuild (command line)**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64
```

**Option C — CMake (Windows)**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXECUTABLE=ON
cmake --build build --config Release
```

> **Note:** The CMake option `BUILD_EXECUTABLE=ON` links all dependencies statically, producing a single portable `.exe`.

### Build on Linux

The provided script automatically installs missing dependencies (requires `apt`/`sudo`) and builds with Clang + Ninja:

```bash
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
chmod +x build-linux.sh
./build-linux.sh          # interactive (prompts before installing packages)
# or
./build-linux.sh -y       # non-interactive (auto-installs packages)
```

To build manually with CMake:

```bash
# Install dependencies (Debian/Ubuntu)
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

### Build for Android

Open the project root in **Android Studio** and run the standard Gradle build, or use the command line:

```bash
./gradlew assembleRelease
```

---

## 🛠️ Troubleshooting

### GPU / Rendering Issues

If you experience crashes or display glitches, force the OpenGL renderer:

```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe
```

> **Tip:** Updating your GPU drivers should be your first step before trying this workaround.

### Missing ROM / Model Not Found

Place the ROM file in the same directory as the executable, or use the Startup UI to browse and register it manually.

### CMake Configuration Fails

Ensure all submodules are initialized:

```bash
git submodule update --init --recursive
```

---

## 💬 Community & Feedback

- 💬 **Discord**: [discord.gg/NM39VPdJTf](https://discord.gg/NM39VPdJTf)
- 📧 **Email**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
- 🐛 **Issues**: [GitHub Issues](https://github.com/telecomadm1145/CasioEmuMsvc/issues)
