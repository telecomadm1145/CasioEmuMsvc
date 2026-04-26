<div align="center">

<img src="../CasioEmuMsvc/icon.ico" width="96" alt="CasioEmuMsvc Logo"/>

# CasioEmuMsvc

**Trình giả lập hiệu suất cao và bộ công cụ phát triển cho dòng MCU nX-U8/100 & nX-U16/100.**

[![License](https://img.shields.io/github/license/telecomadm1145/CasioEmuMsvc?style=flat-square)](../LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Android-blue?style=flat-square)](#-h%E1%BB%97-tr%E1%BB%A3-n%E1%BB%81n-t%E1%BA%A3ng)
[![Stars](https://img.shields.io/github/stars/telecomadm1145/CasioEmuMsvc?style=flat-square)](https://github.com/telecomadm1145/CasioEmuMsvc/stargazers)
[![Discord](https://img.shields.io/discord/NM39VPdJTf?label=Discord&logo=discord&style=flat-square)](https://discord.gg/NM39VPdJTf)

🌐 [English](README.en.md) · [中文](README.zh-CN.md)

</div>

---

## Tổng quan

**CasioEmuMsvc** là trình giả lập hiệu suất cao kiêm bộ công cụ kỹ thuật đảo ngược dành cho máy tính Casio sử dụng dòng vi điều khiển **nX-U8/100** và **nX-U16/100**. Ngoài khả năng mô phỏng đầy đủ, nó còn cung cấp debugger tương tác, disassembler, quản lý snapshot và giao diện plugin hỗ trợ AI.

---

## ✨ Tính năng nổi bật

### 🖥️ Mô phỏng lõi
| Tính năng | Mô tả |
|---|---|
| **Hỗ trợ đa kiến trúc** | Hỗ trợ đầy đủ tập lệnh nX-U16/100 và nX-U8/100 |
| **Ngoại vi chính xác** | Mô phỏng bàn phím, LCD, timer, RTC, UART và nhiều hơn |
| **Kết xuất SDL2** | Đồ họa tăng tốc phần cứng, đa nền tảng |

### 🔬 Công cụ phát triển & gỡ lỗi
| Tính năng | Mô tả |
|---|---|
| **Debugger tương tác** | Breakpoint, chạy từng bước, xem thanh ghi & bộ nhớ |
| **Disassembler / Code Viewer** | Dịch ngược thời gian thực với hỗ trợ nhãn và ký hiệu |
| **Trình soạn thảo Hex** | Xem và chỉnh sửa bộ nhớ trực tiếp |
| **Memory Breakpoints** | Dừng khi đọc/ghi tại bất kỳ vùng địa chỉ nào |
| **Cửa sổ Watch** | Theo dõi biểu thức và địa chỉ bộ nhớ tùy ý |
| **Phân tích lời gọi hàm** | Trực quan hóa đồ thị cuộc gọi và luồng thực thi |
| **Cửa sổ biến** | Kiểm tra và chỉnh sửa biến đặt tên theo thời gian thực |

### 📸 Hệ thống Snapshot
| Tính năng | Mô tả |
|---|---|
| **Lưu / Tải trạng thái** | Lưu và khôi phục toàn bộ trạng thái giả lập tức thì |
| **Snapshot phân nhánh** | Quản lý lưu phi tuyến tính dạng cây |
| **Xem trước màn hình** | Ảnh thu nhỏ trạng thái LCD nhúng trong mỗi snapshot |
| **Định dạng nén** | Dùng `miniz` (zlib) tạo file `.snapshot` nhỏ gọn |

### 🔌 Hệ thống Plugin
| Plugin | Mô tả |
|---|---|
| **MCP Plugin** | Debugger điều khiển bởi AI qua SSE HTTP server (Model Context Protocol) |
| **Python Plugin** | Điều khiển trình giả lập bằng Python |
| **Cw2tools Plugin** | Tích hợp chuỗi công cụ phát triển Casio |
| **Example Plugin** | Template để xây dựng plugin tùy chỉnh |

### 🚀 Giao diện khởi động & UX
| Tính năng | Mô tả |
|---|---|
| **Startup UI** | Bộ chọn mô hình trực quan và quản lý thư viện ROM |
| **Phím tắt Desktop** | Tạo shortcut hệ điều hành cho từng mô hình |
| **Kiểm tra cập nhật tự động** | Kiểm tra bản phát hành GitHub mới và thông báo trong ứng dụng |
| **Quản lý Theme** | Tùy chỉnh màu sắc, font và tỷ lệ |
| **Đa ngôn ngữ** | Giao diện hỗ trợ tiếng Anh, Trung, Việt và nhiều hơn |
| **Discord Rich Presence** | Hiển thị mô hình đang mô phỏng trên trạng thái Discord |

---

## 📦 Hỗ trợ nền tảng

| Nền tảng | Trạng thái | Ghi chú |
|---|---|---|
| **Windows** | ✅ Đầy đủ | Nền tảng chính, Visual Studio 2022 |
| **Linux** | ✅ Hỗ trợ | CMake + Clang/Ninja qua `build-linux.sh` |
| **Android** | ✅ Hỗ trợ | Gradle + NDK qua Android Studio |

---

## 🚀 Bắt đầu nhanh

### Yêu cầu hệ thống

- **Windows**: Visual Studio 2022 (Community trở lên) với workload **"Desktop development with C++"**
- **Linux**: `cmake`, `clang`, `ninja-build`, `libx11-dev`, `libgl1-mesa-dev`, `libcurl4-openssl-dev`, `zlib1g-dev`
- **Android**: Android Studio + NDK

### Build trên Windows

**Cách A — Visual Studio GUI**

1. Clone repository (bao gồm submodule):
   ```bash
   git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
   ```
2. Mở `CasioEmuMsvc.sln` bằng Visual Studio 2022.
3. Chọn cấu hình **Release / x64**, nhấn **Build → Rebuild Solution**.

**Cách B — MSBuild (dòng lệnh)**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
msbuild CasioEmuMsvc.sln /p:Configuration=Release /p:Platform=x64
```

**Cách C — CMake**

```bat
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXECUTABLE=ON
cmake --build build --config Release
```

> **Lưu ý:** `BUILD_EXECUTABLE=ON` liên kết tĩnh toàn bộ phụ thuộc, tạo ra file `.exe` di động duy nhất.

### Build trên Linux

Dùng script tự động cài đặt dependencies và build:

```bash
git clone --recurse-submodules https://github.com/telecomadm1145/CasioEmuMsvc.git
cd CasioEmuMsvc
chmod +x build-linux.sh
./build-linux.sh        # chế độ tương tác (hỏi trước khi cài)
./build-linux.sh -y     # chế độ tự động (tự cài packages)
```

Build thủ công với CMake:

```bash
# Cài dependencies (Debian/Ubuntu)
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

### Build cho Android

Mở thư mục gốc dự án bằng **Android Studio** và chạy build Gradle, hoặc dùng lệnh:

```bash
./gradlew assembleRelease
```

---

## 🛠️ Khắc phục sự cố

### Sự cố GPU / Hiển thị

Buộc dùng renderer OpenGL:

```bat
set SDL_RENDER_DRIVER=opengl
CasioEmuMsvc.exe
```

> **Lưu ý**: Khuyến nghị cập nhật driver GPU trước khi thử giải pháp trên.

### Thiếu ROM / Không tìm thấy mô hình

Đặt file ROM cùng thư mục với file thực thi, hoặc dùng Startup UI để đăng ký thủ công.

### CMake cấu hình thất bại

Đảm bảo tất cả submodule đã được khởi tạo:

```bash
git submodule update --init --recursive
```

---

## 💬 Cộng đồng & Phản hồi

- 💬 **Discord**: [discord.gg/NM39VPdJTf](https://discord.gg/NM39VPdJTf)
- 📧 **Email**: [telecomadm1919@gmail.com](mailto:telecomadm1919@gmail.com)
- 🐛 **Issues**: [GitHub Issues](https://github.com/telecomadm1145/CasioEmuMsvc/issues)
