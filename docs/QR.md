# QR 导出功能说明

本文记录 CasioEmuMsvc 当前 QR 导出功能的实现方式、支持范围和调试结论。目标是在计算器 ROM 显示 QR Code 时，不依赖屏幕识别、按键路径识别或 URL 特征识别，而是从模拟器内部状态中恢复 QR payload，并提供给桌面 QR Code 窗口、MCP 调试接口以及 `CASIOEMU_CORE_WEB` 的数据导出接口。

## 支持范围

当前实现面向具备 QR 功能的 CLASSWIZ / CLASSWIZ II 系列：

- CLASSWIZ：例如 `fx-JP900_emu`、`fx-JP900 VF`
- CLASSWIZ II：例如 `fx-JP900CW_emu`、`fx-JP900CW`

模拟器 ROM 和真机 ROM 均已接入。模拟器 ROM 主要依赖官方模拟器风格的停止事件和 payload buffer；真机 ROM 没有这类显式事件，因此需要轮询 ROM 运行时 RAM 中的 QR context 和 QR 编码数据。

## 对外能力

### 桌面 QR Code 窗口

桌面端新增 QR Code 窗口，用于展示当前捕获状态和本次运行期间的历史记录。窗口会显示：

- 当前是否检测到 QR 显示状态；
- 当前 QR payload 是否已经完整捕获；
- QR Version；
- payload 长度；
- 真机 ROM 多页 QR 的当前页 / 总页数；
- 当前完整 payload 或当前页 payload；
- 本次运行期间已经捕获的历史记录。

对于 URL payload，桌面窗口提供“打开 URL”操作；所有 payload 都可以复制到剪贴板。历史记录只保存在本次 CasioEmuMsvc 进程内，退出 QR 显示界面不会清空历史。

### MCP 调试接口

MCP 插件通过 `get_qr_code` 暴露 QR 状态，主要字段包括：

- `active`：当前是否检测到 QR 显示状态；
- `complete`：payload 是否已经完整捕获；
- `version`：QR Version；
- `data` / `length`：完整 payload 及长度；
- `real_current_page_data`：真机 ROM 多页捕获时的当前页 payload；
- `real_current_page` / `real_total_pages`：真机 ROM QR 页码；
- `history` / `history_count`：本次运行中已捕获的历史记录。

### CASIOEMU_CORE_WEB

`CASIOEMU_CORE_WEB` 导出 QR 数据读取能力，供 WebCalcEmu 侧 JavaScript 读取。Web 端不包含桌面 QR Code GUI，只需要通过 core API 获得当前 QR 状态和 payload。

## 模拟器 ROM 的实现

模拟器 ROM 的 QR 功能可以通过官方模拟器相关事件获得：

- `ES_STOP_QRCODE_IN`
- `ES_STOP_QRCODE_IN3`
- `ES_STOP_QRCODE_OUT`

CLASSWIZ 和 CLASSWIZ II 的模拟器 ROM 会在生成或退出 QR 显示时触发这些状态。CasioEmuMsvc 在 CPU 停止事件处理路径中识别这些事件，并从对应 buffer 读取 payload。

模拟器 ROM 通常会一次性生成完整 payload。因此对于多页 QR，模拟器 ROM 路径可以直接导出完整内容，不需要逐页翻页收集。

## 真机 ROM 的实现

真机 ROM 不会触发模拟器 ROM 的 QR 停止事件，因此实现采用轮询方式检测 QR 相关运行状态和 RAM 中的 QR context。

### QR 相关 UI 状态

这里使用的状态地址是通过 MCP/debugger 写断点确认的 ROM RAM 状态字节，不应理解为硬件 SFR 或通用 MMIO 寄存器。

- CLASSWIZ 使用 `0xD113` 作为粗略 UI 状态门槛，目前接受 `1`、`2`、`3`。其中 `2` 可覆盖 SETUP 菜单中生成 QR 的场景。
- CLASSWIZ II 使用 `0x91A3` 作为粗略 UI 状态门槛，目前接受 `1`、`2`、`3`、`4`。断点观察到直接 QR 场景会进入 `1/2` 这一组状态，SETUP QR 场景会进入 `3/4` 这一组状态。

这些状态只能说明 ROM 处在可能与 QR 相关的 UI 区域，并不能单独证明正在显示 QR。代码还会继续扫描并验证 QR context；只有找到有效 context 且成功解码 payload 后，才认为捕获到了 QR。

早期曾用过 `0xF000 == 5` 作为 CLASSWIZ II 的粗门槛，但断点确认它并不是可靠的 QR UI 状态来源：它在菜单、退出后等非 QR 场景也可能保持相同值，并且在相关 UI 切换时没有稳定的 ROM 写入。因此当前实现不再依赖 `0xF000` 判断 QR 状态。

### QR context

真机 ROM 在 `0xEF00..0xF000` 附近保存 QR context。context 中包含：

- 源字符串 buffer 地址；
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

解析时会按 QR Version 选择字符计数字段长度，并恢复 payload 字符串。对于多页 QR，会利用 Structured Append 中的当前页和总页数信息，收集各页 payload；当 `1..N` 页全部捕获后，再拼接为完整 payload 并加入历史记录。

### encoded 指针和连续 stream 的关系

调试中确认，context 内的 encoded 指针并不总是直接指向可完整解码的连续 packed stream。部分场景下它指向后续渲染或展开用的中间表；真正连续的 packed stream 可能位于该指针之前的临时 buffer。

已确认的例子：

- `fx-JP900 VF` 的 SETUP QR：连续 packed stream 位于 `0xEA40`。
- `fx-JP900CW` 的 SETUP QR：连续 packed stream 位于 `0xE9C6`。

因此实现不会只读取 context encoded 指针本身，而是在 encoded 指针附近向前搜索连续 packed stream。候选必须能按 QR segment bitstream 成功解码。

### 源字符串 buffer 校验

真机 ROM 的源字符串 buffer 不再作为 payload 导出来源，也不存在通用 NUL 终止字符串 fallback。导出的 QR 数据来自 packed QR segment stream 的解码结果。

不过，对于单页 QR，代码仍会用 context 中的源字符串 buffer 做一致性校验：解码出的 payload 必须与源字符串 buffer 的前缀一致。这个校验用于降低误判风险，因为调试中确实观察到展开表、中间表或随机 RAM 也可能“语法上”被 QR segment 解析器解成某个看似合法的字符串。没有这个校验时，可能把中间表误判为 QR payload。

多页 QR 不使用这个校验作为完整性依据，因为真机 ROM 不一定在一个稳定源 buffer 中保存完整多页 payload；多页完整性主要由 Structured Append 页码和已捕获页集合保证。

## 多页真机 QR

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
- 重新生成 QR 时可以再次追加；
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
- 当前真机 ROM 的 RAM 地址范围和状态字节来自 CLASSWIZ / CLASSWIZ II 观察结果；其它硬件家族不应直接套用。
- packed QR segment stream 搜索范围是根据当前两类真机 ROM 的 RAM 布局确定的。如果后续 ROM 版本改变临时 buffer 区域，可能需要重新用 MCP/debugger 断点确认。
- 单页 QR 的源字符串 buffer 校验是当前搜索式实现的防误判措施。若未来能通过断点完全确定 ROM 中“context 到连续 packed stream”的稳定元数据关系，可以考虑进一步收窄搜索范围，并减少对该校验的依赖。

## 相关源码位置

- QR 捕获核心：`CasioEmuMsvc/src/QrCode.cpp`
- QR 状态结构：`CasioEmuMsvc/src/QrCode.h`
- 桌面 QR Code 窗口：`CasioEmuMsvc/src/Gui/QrCodeWindow.cpp`
- MCP 导出：`McpPlugin/dllmain.cpp`
- Web core 导出：`CasioEmuMsvc/src/Web/core_api.cpp`
