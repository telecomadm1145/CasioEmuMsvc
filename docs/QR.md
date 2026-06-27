# QR 导出功能说明

本文记录当前 CasioEmuMsvc 中 QR 导出功能的实现方式、支持范围和调试结论。该功能的目标是让模拟器在计算器 ROM 显示 QR Code 时，从模拟器内部状态中捕获 QR payload，并将其提供给桌面 QR Code 窗口、MCP 调试接口以及 `CASIOEMU_CORE_WEB` 的导出接口。

## 支持范围

当前实现面向具备 QR 功能的 CLASSWIZ / CLASSWIZ II 系列：

- CLASSWIZ：例如 `fx-JP900_emu`、`fx-JP900 VF`
- CLASSWIZ II：例如 `fx-JP900CW_emu`、`fx-JP900CW`

模拟器 ROM 和真机 ROM 都已接入。模拟器 ROM 的 QR 导出主要依赖官方模拟器相关的停止事件和 payload buffer；真机 ROM 没有这类显式事件，因此需要从运行中的 RAM 结构和 QR 编码中恢复 payload。

## 对外能力

### 桌面 QR Code 窗口

桌面端新增了 QR Code 窗口，用于展示当前捕获状态和历史记录。窗口会显示：

- 当前 QR 是否处于显示状态；
- 当前 QR payload 是否已完整捕获；
- QR Version；
- payload 长度；
- 本次运行中的历史记录数量；
- 真机 ROM 多页 QR 的捕获进度；
- 当前完整 payload 或当前页 payload；
- 本次运行中已捕获的历史记录。

对于 URL payload，桌面端窗口会提供“打开 URL”操作；所有 payload 都可以复制到剪贴板。历史记录在本次 CasioEmuMsvc 运行期间保留，即使计算器退出 QR 显示界面，也不会清空历史。

### MCP 调试接口

MCP 插件通过 `get_qr_code` 暴露 QR 状态，主要字段包括：

- `active`：当前是否检测到 QR 显示状态；
- `complete`：payload 是否已完整捕获；
- `version`：QR Version；
- `data` / `length`：完整 payload 及长度；
- `current_page_data`：真机 ROM 多页捕获时当前页 payload；
- `real_current_page` / `real_total_pages`：真机 ROM QR 页码；
- `history` / `history_count`：本次运行中已捕获的历史。

这使得外部调试脚本可以不依赖屏幕识别、按键路径识别或 URL 特征识别，而是直接读取模拟器内部 QR 捕获状态。

### CASIOEMU_CORE_WEB

`CASIOEMU_CORE_WEB` 导出了 QR 数据读取能力，供 WebCalcEmu 侧 JavaScript 读取。Web 端不需要桌面 QR Code GUI，只需要能从 core API 获得当前 QR 状态和 payload。

## 模拟器 ROM 的实现

模拟器 ROM 的 QR 功能可以通过官方模拟器相关事件获得：

- `ES_STOP_QRCODE_IN`
- `ES_STOP_QRCODE_IN3`
- `ES_STOP_QRCODE_OUT`

CLASSWIZ 和 CLASSWIZ II 的模拟器 ROM 会在生成或退出 QR 显示时触发这些状态。CasioEmuMsvc 在 CPU 停止事件处理路径中识别这些事件，并从对应 buffer 读取 payload。

模拟器 ROM 通常会一次性生成完整 payload。因此对于多页 QR，模拟器 ROM 路径可以直接导出完整内容，不需要通过翻页逐页收集。

## 真机 ROM 的实现

真机 ROM 不会触发模拟器 ROM 的 QR 停止事件，因此实现采用轮询方式检测 QR 相关运行状态和 RAM 中的 QR context。

### QR 显示状态

不同硬件家族的状态位不完全相同：

- CLASSWIZ 通过 `0xD113` 判断 QR 相关 UI 状态，目前接受 `1`、`2`、`3`。其中 `2` 覆盖 SETUP 菜单中生成 QR 的场景。
- CLASSWIZ II 通过 `0xF000 == 5` 判断 QR 相关 UI 状态。

仅有这些状态还不足以证明正在显示 QR；代码还会继续查找有效 QR context，并成功解码 payload 后才认为捕获到 QR。

### QR context

真机 ROM 在 `0xEF00..0xF000` 附近保存 QR context。context 中包含：

- 源字符串地址；
- 当前页；
- 总页数；
- QR Version；
- encoded 数据相关指针。

实现会扫描该区域，寻找满足地址范围、页码范围和版本范围的候选 context。

### packed QR segment stream

当前真机 ROM 的主导出方式是解析 RAM 中的 packed QR segment stream。该 stream 使用 QR 标准 segment bitstream 格式，支持：

- Numeric mode；
- Alphanumeric mode；
- Byte mode；
- Structured Append。

解析时会按 QR Version 选择字符计数字段长度，并恢复 payload 字符串。对于多页 QR，会利用 Structured Append 的当前页和总页数信息，将各页 payload 收集齐后再视为完整 QR。

### encoded 指针和连续 stream 的关系

调试中确认，context 内的 encoded 指针并不总是直接指向连续 packed stream。部分场景下它指向后续转换/展开用的中间表；连续 packed stream 可能位于该指针之前。

已确认的例子：

- `fx-JP900 VF` 的 SETUP QR：连续 packed stream 位于 `0xEA40`。
- `fx-JP900CW` 的 SETUP QR：连续 packed stream 位于 `0xE9C6`。

因此实现不会只读取 context encoded 指针本身，而是在 encoded 指针附近向前搜索连续 packed stream。搜索候选必须能按 QR segment bitstream 成功解码，并且对于单页 QR，还会用 context 中的源字符串 buffer 做一致性校验，以避免把展开表或随机 RAM 误判成有效 payload。

这里的源字符串 buffer 只作为校验依据；导出的 QR 数据仍来自 packed QR segment stream 解码结果。

### 多页真机 QR

真机 ROM 的多页 QR 不是一次性导出完整 payload。当前实现会在用户或测试脚本翻页时捕获各页：

1. 发现 QR context；
2. 读取当前页 packed QR segment stream；
3. 解析 Structured Append 中的页码和总页数；
4. 保存当前页 payload；
5. 当 `1..N` 页全部捕获后，拼接为完整 payload；
6. 将完整 payload 加入当前状态和历史记录。

由于计算器界面的翻页可以循环，历史记录逻辑会尽量区分“同一次 QR 显示中的重复采样”和“退出后重新生成的同内容 QR”。对于一次完整显示，完成捕获后只记录一次；退出 QR 后再次生成，即使内容相同，也应作为新的历史记录。

## 历史记录语义

QR 历史记录是本次 CasioEmuMsvc 运行内的内存状态，不写入模型存档，也不跨进程持久化。

历史记录的设计目标：

- 捕获到完整 QR 后追加历史；
- 退出当前 QR 显示不清空历史；
- 重新生成 QR 时可再次追加；
- 对真机 ROM 多页 QR，只有页数收集完整后才追加历史；
- 避免因为轮询同一页或循环翻页导致历史记录无限增长。

## 不采用的方案

实现刻意避免以下方式：

- 不通过按键路径判断“用户将要生成 QR”；
- 不通过屏幕像素或 OCR 判断 QR 内容；
- 不通过 URL 前缀或 payload 特征猜测是否为 QR；
- 不把普通 NUL 终止字符串读取作为通用 fallback。

这些方案容易在不同菜单路径、不同 ROM 版本或非 URL QR 中失效，也容易掩盖真实捕获算法的问题。

## 已知限制

- 真机 ROM 的完整多页 payload 需要所有页都实际出现过；如果用户没有翻到某些页，当前实现无法凭空恢复未出现页。
- 当前真机 ROM 的 RAM 地址范围和状态位来自 CLASSWIZ / CLASSWIZ II 观察结果；其它硬件家族不应直接套用。
- packed QR segment stream 搜索范围是根据当前两类真机 ROM 的 RAM 布局确定的。如果后续 ROM 版本改变临时 buffer 区域，可能需要重新用 MCP / debugger 断点确认。

## 相关源码位置

- QR 捕获核心：`CasioEmuMsvc/src/QrCode.cpp`
- QR 状态结构：`CasioEmuMsvc/src/QrCode.h`
- 桌面 QR Code 窗口：`CasioEmuMsvc/src/Gui/QrCodeWindow.cpp`
- MCP 导出：`McpPlugin/dllmain.cpp`
- Web core 导出：`CasioEmuMsvc/src/Web/core_api.cpp`
