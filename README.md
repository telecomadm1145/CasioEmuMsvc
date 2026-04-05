## 🇺🇸 English Version

# CasioEmuMsvc

**CasioEmuMsvc** is a high-performance(?) emulator designed for the **nX-U16/100** and **nX-U8/100** MCU series. Beyond simple emulation, it serves as a comprehensive development tool featuring a real-time debugger and a built-in disassembler.

## 🌟 Key Features

* **Multi-Architecture Support**: Full support for both nX-U16/100 and nX-U8/100 instruction sets.
* **Built-in Debugger**: Includes breakpoints, single-stepping, and memory inspection for in-depth firmware analysis.
* **Graphics Rendering**: Powered by SDL for performance and cross-platform compatibility.
* **Native Integration**: Optimized for Windows, seamlessly integrating with the Visual Studio workflow.

## 🚀 Quick Start

### Prerequisites

Before you begin, ensure you have the following installed:

* **Visual Studio 2022** (Community or higher) or **MSVC Build Tools**.
* **Desktop development with C++** workload.

### Build Instructions

1. **Clone the repository**
```bash
git clone https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc

```


2. **Compile the project**
* **Via Visual Studio**: Open the `.sln` file, select `Release/x64`, and click "Rebuild Solution".
* **Via Command Line**:
```bash
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64

```





## 🛠️ Troubleshooting (GPU Compatibility)

If you experience crashes or rendering glitches on specific hardware, it is often due to driver incompatibility. You can force the OpenGL backend to resolve this:

1. Open a terminal (CMD or PowerShell).
2. Run the following commands:
```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe

```



> **Note**: Updating your GPU drivers is recommended before trying this workaround.

## 💬 Community & Feedback

We value your feedback. Please reach out via the following channels for suggestions or bug reports:

* **Discord Server**: [Join us here](https://discord.gg/NM39VPdJTf)
* **Email Support**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
* **Issues**: Feel free to submit an issue on GitHub.

---

## 🇨🇳 Chinese Version (中文版)

# CasioEmuMsvc

**CasioEmuMsvc** 是一个针对 **nX-U16/100** 及 **nX-U8/100** 系列 MCU（微控制器）的高性能(?)模拟器。它不仅是一个运行环境，更是一个集成了实时调试器和反汇编器的开发辅助工具。

## 🌟 主要特性

* **多架构支持**：完整支持 nX-U16/100 和 nX-U8/100 指令集。
* **内置调试器**：支持断点设置、单步执行及内存查看，方便固件分析。
* **图形化渲染**：基于 SDL 驱动，兼顾性能与跨平台。
* **原生集成**：专为 Windows 环境优化，完美契合 Visual Studio 开发流。

## 🚀 快速入门

### 环境准备

在开始之前，请确保你的系统已安装：

* **Visual Studio 2022** (推荐 Community 或更高版本) 或 **MSVC Build Tools**。
* 已配置好 **C++ 桌面开发** 工作负载。

### 构建步骤

1. **克隆仓库**
```bash
git clone https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc

```


2. **编译项目**
* **通过 VS 界面**：打开 `.sln` 文件，选择 `Release/x64` 配置并点击“重新生成解决方案”。
* **通过命令行**：
```bash
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64

```





## 🛠️ 故障排除 (GPU 兼容性)

如果在特定硬件上遇到启动崩溃或渲染异常，通常是由于默认渲染驱动不兼容导致的。可以通过强制使用 OpenGL 模式来解决：

1. 打开命令行（CMD 或 PowerShell）。
2. 执行以下指令启动：
```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe

```



> **提示**：建议优先尝试更新 GPU 驱动程序。

## 💬 交流与反馈

我们非常重视用户的反馈，欢迎通过以下渠道提交建议或报告 Bug：

* **Discord 社区**: [加入我们的服务器](https://discord.gg/NM39VPdJTf)
* **邮件支持**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
* **Issues**: 欢迎在 GitHub 提交 Issue。

---

## 🇻🇳 Vietnamese Version (Tiếng Việt)

# CasioEmuMsvc

**CasioEmuMsvc** là trình giả lập hiệu suất cao được thiết kế cho dòng vi điều khiển (MCU) **nX-U16/100** và **nX-U8/100**. Không chỉ đơn thuần là mô phỏng, nó còn là một công cụ phát triển toàn diện tích hợp trình gỡ lỗi (debugger) thời gian thực và trình phân rã mã (disassembler).

## 🌟 Tính năng nổi bật

* **Hỗ trợ đa kiến trúc**: Hỗ trợ đầy đủ tập lệnh cho cả nX-U16/100 và nX-U8/100.
* **Trình gỡ lỗi tích hợp**: Bao gồm tính năng đặt điểm ngắt (breakpoint), chạy từng bước (single-stepping) và kiểm tra bộ nhớ để phân tích firmware chuyên sâu.
* **Kết xuất đồ họa**: Sử dụng SDL để tối ưu hóa hiệu suất và khả năng tương thích đa nền tảng.
* **Tích hợp tối ưu**: Được tối ưu hóa cho Windows, tích hợp mượt mà với quy trình làm việc của Visual Studio.

## 🚀 Bắt đầu nhanh

### Yêu cầu hệ thống

Trước khi bắt đầu, hãy đảm bảo bạn đã cài đặt:

* **Visual Studio 2022** (Phiên bản Community hoặc cao hơn) hoặc **MSVC Build Tools**.
* Gói công việc **Desktop development with C++**.

### Hướng dẫn xây dựng (Build)

1. **Sao chép kho lưu trữ (Clone repo)**
```bash
git clone https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc

```


2. **Biên dịch dự án**
* **Qua giao diện Visual Studio**: Mở file `.sln`, chọn cấu hình `Release/x64`, và nhấn "Rebuild Solution".
* **Qua dòng lệnh (Command Line)**:
```bash
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64

```





## 🛠️ Khắc phục sự cố (Tương thích GPU)

Nếu bạn gặp sự cố treo ứng dụng hoặc lỗi hiển thị trên một số phần cứng cụ thể, nguyên nhân thường do không tương thích driver. Bạn có thể buộc ứng dụng sử dụng OpenGL để khắc phục:

1. Mở cửa sổ dòng lệnh (CMD hoặc PowerShell).
2. Chạy các lệnh sau:
```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe

```



> **Lưu ý**: Khuyên bạn nên cập nhật driver GPU trước khi thử giải pháp này.

## 💬 Cộng đồng & Phản hồi

Chúng tôi rất trân trọng ý kiến đóng góp của bạn. Vui lòng liên hệ qua các kênh sau để gửi đề xuất hoặc báo lỗi:

* **Máy chủ Discord**: [Tham gia tại đây](https://discord.gg/NM39VPdJTf)
* **Hỗ trợ qua Email**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
* **Issues**: Đừng ngần ngại gửi báo cáo lỗi trên GitHub.
