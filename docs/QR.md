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

`CASIOEMU_CORE_WEB` 导出 QR 数据读取能力。Web 端不包含桌面 QR Code GUI，只需要通过 core API 获得当前 QR 状态和 payload。

## 模拟器 ROM 的实现

模拟器 ROM 的 QR 功能可以通过官方模拟器相关事件获得：

- `ES_STOP_QRCODE_IN`
- `ES_STOP_QRCODE_IN3`
- `ES_STOP_QRCODE_OUT`

CLASSWIZ 和 CLASSWIZ II 的模拟器 ROM 会在生成或退出 QR 显示时触发这些状态。CasioEmuMsvc 在 CPU 停止事件处理路径中识别这些事件，并从对应 buffer 读取 payload。

模拟器 ROM 通常会一次性生成完整 payload。因此对于多页 QR，模拟器 ROM 路径可以直接导出完整内容，不需要逐页翻页收集。

## 真机 ROM 的实现

真机 ROM 不会触发模拟器 ROM 的 QR 停止事件，因此实现采用轮询方式检测 QR 相关运行状态、QR context 和 RAM 中的 QR 编码数据。核心入口是 `QrCodeCapture::PollRealHardware()`，它会在模拟器运行过程中反复执行，但每次轮询都只做只读检查，不主动按键、不读取屏幕、不根据 URL 特征猜测。

当前算法可以分成以下阶段：

1. 按机型确认是否支持真机 QR 捕获；
2. 用 ROM UI 状态字节做粗门槛过滤；
3. 在 `0xEF00..0xF000` 扫描 QR context；
4. 根据 context 中的信息寻找并解码 packed QR segment stream；
5. 对单页 QR 做 source buffer 一致性校验；
6. 对多页 QR 按页收集；
7. 连续两次观察到同一页数据后才更新当前状态；
8. 所有页收集完成后写入完整 payload 和历史记录；
9. QR 消失后做短暂去抖，再清空当前状态，但保留历史记录。

### 机型入口和固定地址

真机 QR 捕获只在 `real_hardware` 机型上启用，目前覆盖 CLASSWIZ 和 CLASSWIZ II。

代码中保留了两类地址信息：

- 优先 context 地址：用于给已知机型提供一个参考位置；
- 数据源地址：用于识别 context 中的源字符串 buffer，也用于部分页码读取的兼容逻辑。

当前已知地址如下：

| 机型家族 | 代表机型 | 优先 context 地址 | 源字符串 buffer 地址 |
| --- | --- | ---: | ---: |
| CLASSWIZ | `fx-JP900 VF` | `0xEFB0` | `0xED8A` |
| CLASSWIZ II | `fx-JP900CW` | `0xEF96` | `0xED9E` |

实际捕获时并不会只信任优先 context 地址。代码会扫描整个 `0xEF00..0xF000` 区间寻找候选 context，因此 SETUP、普通 QR 入口或不同 ROM 状态下 context 略有移动时仍可捕获。

### QR 相关 UI 状态

这里使用的状态地址是通过 MCP/debugger 写断点确认的 ROM RAM 状态字节，不应理解为硬件 SFR 或通用 MMIO 寄存器。

- CLASSWIZ 使用 `0xD113` 作为粗略 UI 状态门槛，目前接受 `1`、`2`、`3`。其中 `2` 可覆盖 SETUP 菜单中生成 QR 的场景。
- CLASSWIZ II 使用 `0x91A3` 作为粗略 UI 状态门槛，目前接受 `1`、`2`、`3`、`4`。断点观察到直接 QR 场景会进入 `1/2` 这一组状态，SETUP QR 场景会进入 `3/4` 这一组状态。

这些状态只能说明 ROM 处在可能与 QR 相关的 UI 区域，并不能单独证明正在显示 QR。代码还会继续扫描并验证 QR context；只有找到有效 context 且成功解码 payload 后，才认为捕获到了 QR。

早期曾用过 `0xF000 == 5` 作为 CLASSWIZ II 的粗门槛，但断点确认它并不是可靠的 QR UI 状态来源：它在菜单、退出后等非 QR 场景也可能保持相同值，并且在相关 UI 切换时没有稳定的 ROM 写入。因此当前实现不再依赖 `0xF000` 判断 QR 状态。

### QR context 扫描

真机 ROM 在 `0xEF00..0xF000` 附近保存 QR context。扫描范围由以下常量定义：

- 起始：`kRealQrContextScanStart = 0xEF00`
- 结束：`kRealQrContextScanEnd = 0xF000`

扫描时每个地址都被视为一个可能的 context 起点，至少需要能读取到 `address + 0x11`，因此循环条件是 `address + 0x12 <= 0xF000`。

候选 context 的字段布局如下：

| 偏移 | 含义 | 当前校验 |
| ---: | --- | --- |
| `+0x00..+0x01` | 源字符串 buffer 指针，小端 | 必须落在 `0xED00..0xEFFF` |
| `+0x0B` | 总页数 | `1..16`，且必须大于等于当前页 |
| `+0x0C` | 当前页 | `1..16` |
| `+0x0E` | QR Version | `1..40` |
| `+0x10..+0x11` | encoded 数据相关指针，小端 | 后续用于寻找 packed stream |

只有字段全部通过范围校验后，才会继续尝试读取 encoded 数据。这个阶段仍然只是“候选 context”，不是最终 QR 结果。

### packed QR segment stream 解码

当前真机 ROM 的主导出方式是解析 RAM 中的 packed QR segment stream。该 stream 使用 QR 标准 segment bitstream 格式。

读取 encoded 数据时最多读取 `0x300` 字节，并且只在 `0xE900..0xEF00` 范围内读取，避免扫到 QR context 区或未映射区域。

解析器按 bit 读取 QR segment：

1. 每个 segment 先读 4 bit mode；
2. `mode == 0` 表示 terminator，且必须已经解析出非空 payload；
3. `mode == 3` 表示 Structured Append；
4. `mode == 1` 表示 Numeric；
5. `mode == 2` 表示 Alphanumeric；
6. `mode == 4` 表示 Byte；
7. 其它 mode 当前视为不支持，候选失败。

当前支持的模式如下：

| QR mode | 名称 | 解析规则 |
| ---: | --- | --- |
| `0` | Terminator | payload 非空时结束 |
| `1` | Numeric | 每 3 位十进制数字用 10 bit，剩余 2 位用 7 bit，剩余 1 位用 4 bit |
| `2` | Alphanumeric | 使用 QR 标准 45 字符表，每 2 字符用 11 bit，剩余 1 字符用 6 bit |
| `3` | Structured Append | 读取 sequence、total、parity，用于多页 QR |
| `4` | Byte | 按字节读取，目前要求字符在可打印 ASCII `0x20..0x7E` |

字符计数字段长度按 QR Version 决定：

| mode | Version `1..9` | Version `10..26` | Version `27..40` |
| ---: | ---: | ---: | ---: |
| Numeric | 10 bit | 12 bit | 14 bit |
| Alphanumeric | 9 bit | 11 bit | 13 bit |
| Byte | 8 bit | 16 bit | 16 bit |

解析过程中还有以下保护：

- 单个 segment 的字符数不能超过 `0x300`；
- 累计 payload 长度不能超过 `0x300`；
- Numeric 数值必须落在对应位宽可表示的十进制范围内；
- Alphanumeric 索引必须落在 45 字符表范围内；
- Byte mode 暂只接受可打印 ASCII；
- Structured Append 只能出现一次；
- Structured Append 的 sequence 不能大于 total。

### encoded 指针和连续 stream 的关系

调试中确认，context 内的 encoded 指针并不总是直接指向可完整解码的连续 packed stream。部分场景下它指向后续渲染或展开用的中间表；真正连续的 packed stream 可能位于该指针之前的临时 buffer。

已确认的例子：

- `fx-JP900 VF` 的 SETUP QR：连续 packed stream 位于 `0xEA40`。
- `fx-JP900CW` 的 SETUP QR：连续 packed stream 位于 `0xE9C6`。

因此 `FindRealQrEncodedPayload()` 的查找顺序是：

1. 先尝试直接从 context encoded 指针处读取连续 packed stream；
2. 如果直接读取失败，或单页 QR 的 source buffer 校验失败，则在 encoded 指针附近搜索连续 packed stream；
3. 搜索范围大致是 `[encoded - 0x300, encoded + 0x20)`，并夹在 `0xE900..0xEF00` 内；
4. 如果连续读取仍失败，再尝试 stride 读取路径；
5. 所有路径都失败时，该 context 被丢弃，扫描下一个 context。

连续搜索时，如果同一范围内找到多个能解码的候选，必须满足解码出的 payload 一致；如果出现多个不同 payload，则认为该范围不可信并失败。

### stride encoded 路径

`ReadStrideEncodedPayload()` 是为已观察到的非连续布局保留的读取路径。它从 encoded 指针开始，每隔 5 字节取 1 字节，最多读取 `0x300` 字节，然后仍然使用同一个 QR segment parser 解码。

这个路径不是旧的 source-buffer fallback，也不是按 URL 或字符串特征猜测。它仍然要求：

- encoded 指针在 `0xEA00..0xEF00`；
- 解码结果是合法 QR segment stream；
- 多页 QR 的 Structured Append 页码与 context 一致；
- 单页 QR 仍需通过 source buffer 一致性校验。

### source buffer 校验

真机 ROM 的源字符串 buffer 不再作为 payload 导出来源，也不存在通用 NUL 终止字符串 fallback。导出的 QR 数据来自 packed QR segment stream 的解码结果。

不过，对于单页 QR，代码仍会用 context 中的源字符串 buffer 做一致性校验：解码出的 payload 必须与源字符串 buffer 的前缀一致。这个校验用于降低误判风险，因为调试中确实观察到展开表、中间表或随机 RAM 也可能“语法上”被 QR segment parser 解成某个看似合法的字符串。没有这个校验时，可能把中间表误判为 QR payload。

当前校验规则很简单：

1. 解码结果不能为空；
2. source buffer 地址必须在 `0xED00..0xEF00`；
3. `source + payload_length` 不能超过 `0xEF00`；
4. source buffer 前 `payload_length` 个字节必须逐字节等于解码结果。

多页 QR 不使用这个校验作为完整性依据，因为真机 ROM 不一定在一个稳定 source buffer 中保存完整多页 payload；多页完整性主要由 Structured Append 页码和已捕获页集合保证。

### 当前页确认去抖

`PollRealHardware()` 不会在第一次读到某页时立刻更新当前 QR 状态。它会先把页码和 payload 存入临时字段：

- `pending_real_page_`
- `pending_real_data_`

只有下一次轮询再次读到同一个页码和同一段 payload 时，才认为该页稳定，并继续更新 `real_current_page_`、`real_current_page_data_` 和页集合。

这个机制用于过滤 ROM 正在生成或更新 QR 数据时的瞬时中间状态，避免捕获半写入的数据。

### 多页真机 QR

真机 ROM 的多页 QR 不是一次性导出完整 payload。当前实现会在用户或测试脚本翻页时捕获各页。

多页捕获的完整流程是：

1. 发现有效 QR context；
2. 读取当前页 packed QR segment stream；
3. 解析 Structured Append 中的页码和总页数；
4. 要求 Structured Append 的当前页、总页数与 context 中的页码一致；
5. 当前页通过两次相同采样确认后，写入 `real_pages_[page - 1]`；
6. 如果还没有从第 1 页开始收集，而当前看到的不是第 1 页，则只更新“当前正在显示 QR”的状态，不加入页集合；
7. 如果页码跳跃超过 `real_pages_.size() + 1`，说明中间缺页，当前采样丢弃；
8. 如果某个非当前页已经保存了完全相同的 payload，则认为这是异常或循环误判，当前采样丢弃；
9. 当 `1..N` 页全部非空后，按页序拼接为完整 payload；
10. 拼接结果写入 `data_`，`complete_ = true`；
11. 本次显示会话首次完成时追加历史记录。

这里的 `N` 来自 context 和 Structured Append 的总页数。实现不尝试猜测未出现页的内容；没有实际翻到的页不会被凭空恢复。

### 新一轮 QR 的识别

计算器界面的翻页可以循环，因此历史记录需要避免“同一次 QR 显示里不断翻页导致无限追加”。当前逻辑大致如下：

- 如果已经完整捕获并记录过当前 QR，而后续采样仍属于同一份 QR，则不追加历史；
- 如果当前页内容或总页数显示它不再属于已完成的 QR，则清空页集合，开始新一轮收集；
- 如果看到第 1 页，且之前已经完成、或已经收集了多页、或第 1 页内容发生变化，也会开始新一轮收集；
- 退出 QR 后再次生成同内容 QR，只要完成捕获，仍应作为新的历史记录追加。

这部分逻辑主要围绕以下状态字段：

- `real_session_recorded_`：本次 QR 显示会话是否已经完成并记录过；
- `real_active_session_recorded_`：当前 active 阶段是否已经写过历史；
- `real_pages_`：当前会话已收集的页；
- `real_current_page_` / `real_total_pages_`：当前显示页和总页数；
- `real_current_page_data_`：当前页 payload。

### 退出 QR 和当前状态清理

当 UI 状态门槛失败、找不到有效 context、页码无效或 payload 为空时，代码不会马上清空当前 QR 状态，而是进入 `markInactive()`。

`markInactive()` 有一个 2 秒去抖：

1. 第一次观察到 inactive 时记录时间；
2. 2 秒内如果仍处于 inactive，才清空当前 QR 状态；
3. 历史记录不会被清空；
4. 如果 QR 又重新变为 active，则取消 pending inactive。

这个设计用于避免 ROM 短暂切换、context 重建或轮询时序造成 Current 面板闪烁。不过它也意味着退出 QR 后，Current 内容可能短暂保留一小段时间。

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
