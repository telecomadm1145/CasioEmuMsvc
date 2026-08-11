# SVG/JSON Model Configuration Guide

本文说明 CasioEmuMsvc 对 SVG board 模型、JSON 配置、传统 sprite 模型、各平台构建和运行时行为的最终适配结果。

适用范围：

- Windows/Linux/macOS/Android 完整 GUI。
- 普通 Web GUI 导出。
- `CASIOEMU_CORE_WEB` core API 导出中与完整 GUI 的差异。
- 传统 `config.bin` 模型和 JSON 模型的加载。

## 模型目录

一个模型目录通常包含：

- `config.json`：JSON 模型配置。
- `config.bin`：二进制配置缓存，可自动生成。
- ROM 文件：文件名由 `config.json.rom_path` 指定。
- 可选 flash 文件：文件名由 `config.json.flash_path` 指定。
- interface 图片：文件名由 `config.json.interface_path` 指定，可以是 SVG、PNG、JPEG 等 SDL_image 支持的格式。
- board SVG：文件名由 `config.json.board_path` 指定，仅 SVG board 模型需要。

所有资源文件名都来自配置字段。代码不会固定要求 `core.dat`、`rom.bin`、`face.svg`、`skin.png` 或 `board.svg`。

## 配置加载

模型目录通过 `LoadModelInfoFromFolder(model_path, model, loaded_from, error)` 加载。

加载顺序：

1. 如果模型目录中存在 `config.bin`，直接读取 `config.bin`。
2. 如果不存在 `config.bin`，读取 `config.json`。
3. 成功读取 `config.json` 后写出新的 `config.bin` 缓存。
4. `config.bin` 和 `config.json` 都不存在时，模型加载失败。

修改 `config.json` 后，需要删除同目录下的 `config.bin` 才会重新解析 JSON。

## 模型类型

模型类型由 `board_path` 决定：

- `board_path` 非空：SVG board 模型。
- `board_path` 为空：传统 sprite 模型。

`interface_path` 的文件后缀不决定模型类型。SVG board 模型可以使用 SVG interface，也可以使用 PNG/JPEG interface。

## HardwareId

`hardware_id` 必须写 CasioEmuMsvc 实际使用的数字枚举值。

| 值 | 枚举               | 说明                  |
|---|------------------|---------------------|
| 3 | `HW_ES_PLUS`     | ES/ES PLUS 类        |
| 4 | `HW_CLASSWIZ`    | ClassWiz、CY、EG、EY 类 |
| 5 | `HW_CLASSWIZ_II` | ClassWiz II         |
| 6 | `HW_FX_5800P`    | fx-5800P            |
| 7 | `HW_TI`          | TI                  |
| 8 | `HW_SOLARII`     | SOLAR II            |
| 9 | `HW_EPS6800`     | EPS6800             |

常见像素屏幕机型组合：

| 类型        | hardware_id | screen_width | screen_height | u16_mode | large_model | ml620_mirroring |
|-----------|------------:|-------------:|--------------:|----------|-------------|-----------------|
| ES/ESP/FC |           3 |           96 |            31 | false    | true        | true            |
| CY/EG/EY  |           4 |          192 |            63 | true     | true        | false           |
| 5800P     |           6 |           96 |            31 | true     | true        | true            |

SOLAR II 是段码/状态图形类显示，不涉及矩阵像素屏幕，不应按 `screen_width`、`screen_height` 理解为 LCD 像素尺寸。

## 通用 JSON 字段

所有 JSON 模型都需要以下字段。

| 字段                  | 类型      | 必填 | 说明                                |
|---------------------|---------|----|-----------------------------------|
| `format`            | string  | 建议 | 建议写 `CasioEmuMsvc.ModelInfo`。     |
| `version`           | integer | 建议 | 当前格式版本建议写 `1`。                    |
| `model_name`        | string  | 是  | 启动界面显示名称。                         |
| `hardware_id`       | integer | 是  | CasioEmuMsvc 的硬件枚举数值。             |
| `csr_mask`          | integer | 是  | CSR mask。                         |
| `real_hardware`     | boolean | 是  | 是否使用真实硬件键盘矩阵行为。                   |
| `pd_value`          | integer | 是  | PD 初始值。                           |
| `interface_path`    | string  | 是  | interface 图片路径。                   |
| `rom_path`          | string  | 是  | ROM 文件路径。                         |
| `is_unpacked_nibbles` | boolean | 否 | ROM 是否为每个 nibble 各占一个字节的格式；缺省为 `false`。当前用于 EPS6800。 |
| `flash_path`        | string  | 是  | flash 文件路径；没有独立 flash 文件时写空字符串。   |
| `ink_color`         | object  | 是  | LCD 和 status 绘制颜色，包含 `r`、`g`、`b`。 |
| `enable_new_screen` | boolean | 是  | 屏幕渲染模式开关。                         |
| `is_sample_rom`     | boolean | 是  | 是否样例 ROM。                         |
| `legacy_ko`         | boolean | 是  | 是否使用 legacy KO 行为。                |
| `u16_mode`          | boolean | 是  | 是否 U16 模式。                        |
| `large_model`       | boolean | 是  | 是否 large model。                   |
| `ml620_mirroring`   | boolean | 是  | ML620/ML610 镜像行为选择。               |

路径字段支持相对路径和绝对路径。相对路径以模型目录为基准。

`ink_color` 格式：

```json
"ink_color": {
"r": 0,
"g": 0,
"b": 0
}
```

矩形字段统一格式：

```json
{
  "x": 0,
  "y": 0,
  "w": 375,
  "h": 635
}
```

## SVG Board 模型字段

当 `board_path` 非空时，模型按 SVG board 模型加载，并要求以下字段。

| 字段                          | 类型             | 必填 | 说明                                    |
|-----------------------------|----------------|----|---------------------------------------|
| `board_path`                | string         | 是  | board SVG 路径，也是 SVG board 模型开关。       |
| `screen_width`              | integer        | 是  | 矩阵像素屏幕机型的 LCD 逻辑宽度。                 |
| `screen_height`             | integer        | 是  | 矩阵像素屏幕机型的 LCD 逻辑高度。                 |
| `screen_scale_y`            | number         | 是  | 矩阵像素屏幕机型的 LCD 高度修正系数，通常为 `1.0`。       |
| `sprites.rsd_interface.src` | rect           | 是  | interface 原图尺寸或 SVG 源渲染尺寸。             |
| `sprites.rsd_interface.dest` | rect           | 是  | 模型坐标尺寸，也是默认窗口尺寸；`x/y` 必须为 `0`。     |
| `sprites.rsd_* .index`      | integer        | 是  | `rsd_*` 状态精灵与 SVG status 子节点的映射。      |
| `buttons`                   | array          | 否  | 按键映射覆盖项，只包含 `index`、`keyname`、`kiko`。 |

### `sprites.rsd_interface.src` / `dest`

SVG board 模型必须写明 interface 源区域和目标模型尺寸：

```json
"sprites": {
"rsd_interface": {
"src": {"x": 0, "y": 0, "w": 375, "h": 635},
"dest": {"x": 0, "y": 0, "w": 750, "h": 1270}
}
}
```

`src` 含义：

- `x`、`y`：源图裁剪起点，通常为 `0`。
- `w`、`h`：interface 图片实际尺寸。SVG interface 使用源渲染尺寸。
- PNG 后缀但内容为 JPEG 的文件也按这里的尺寸处理。

`dest` 含义：

- `x`、`y` 必须为 `0`。
- `w`、`h` 是 SVG board 模型坐标尺寸，也是模拟器初始窗口默认尺寸。
- board SVG 推导出的按键、LCD、status 几何会按根坐标系到 `dest` 的比例映射。

SVG board 模型的其它 sprite 几何来自 board SVG，并映射到 `dest` 坐标系。

### `screen_width`、`screen_height`、`screen_scale_y`

对矩阵像素屏幕机型，board SVG 中的 `screenSlot` 提供 LCD 区域坐标。LCD 逻辑尺寸由配置指定。

LCD 目标高度按以下公式计算：

```text
lcd_height = screenSlot.width * screen_height / screen_width * screen_scale_y
```

这使 LCD 区域以逻辑像素宽高比铺满 board 中的屏幕区域。SOLAR II 这类段码/状态图形类显示不按这一节理解为矩阵像素屏幕。

### `sprites` 状态项

除 `rsd_interface` 外，SVG board 模型在 `sprites` 下用 `rsd_*` key 指定状态精灵名对应 status 容器的第几个直接子节点。

格式：

```json
"sprites": {
  "rsd_interface": {
    "src": {"x": 0, "y": 0, "w": 375, "h": 635},
    "dest": {"x": 0, "y": 0, "w": 750, "h": 1270}
  },
  "rsd_s": {"index": 0},
  "rsd_a": {"index": 1},
  "rsd_m": {"index": 2},
  "rsd_sto": {"index": 3},
  "rsd_math": {"index": 4},
  "rsd_d": {"index": 5}
}
```

规则：

- index 从 0 开始。
- index 对应 status 容器直接子元素顺序。
- `defs` 和 `style` 子元素不计入顺序。
- index 超出范围会导致模型加载失败。
- `rsd_*` 名称需要匹配当前硬件屏幕实现会读取的 sprite 名称。
- 不同机型的 status 数量不同，配置应只写该机型实际使用的 status 项。

### `buttons`

SVG board 模型的按键几何由 board SVG 决定。JSON 中的 `buttons` 只用于保存或覆盖键盘映射：

```json
"buttons": [
{"index": 0, "keyname": "Escape", "kiko": 255},
{"index": 1, "keyname": "A", "kiko": 17}
]
```

字段：

- `index`：board SVG 中解析出的按键序号。不写时使用数组序号。
- `keyname`：SDL 键名。
- `kiko`：按键矩阵编码，整数格式为 `(ko << 4) | ki`。

保存配置时，所有按键的 `kiko` 必须唯一。

## Board SVG 约定

SVG board 文件是模型几何信息来源。

### 根坐标系

根 `<svg>` 应提供可用的 `viewBox`：

```xml

<svg viewBox="0 0 375 635">
```

如果没有 `viewBox`，则需要提供可解析的 `width` 和 `height`。

### 屏幕区域

必须存在 `id="screenSlot"` 的矩形元素：

```xml

<rect id="screenSlot" x="62" y="91" width="251" height="82"/>
```

当前读取 `x`、`y`、`width`、`height`。

### status 区域

status frame 查找规则：

- 优先查找 class 包含 `board-status` 的元素。
- 未找到时查找 `id="status"`。

status frame 尺寸来源：

- 优先读取 `x`、`y`、`width`、`height`。
- 否则读取 `viewBox`。

status 容器查找规则：

- 优先在 status frame 内查找 `id="status"`。
- 未找到时使用 status frame 本身。

状态图形按 status 容器直接子节点顺序提取，并保留祖先 `transform`。

### `board-display` 变换

如果 SVG 中存在 class 包含 `board-display` 的元素，status 坐标会应用该元素 `transform` 中的变换。

支持的 transform：

- `translate(x y)`
- `translate(x, y)`
- `scale(s)`
- `scale(sx sy)`
- `scale(sx, sy)`

### 按键元素

按键元素通过 `data-ki` 和 `data-ko` 标记：

```xml

<path data-ki="1" data-ko="2" d="..."/>
```

规则：

- `data-ki` 和 `data-ko` 是 bit mask。
- mask 必须是单 bit 值：`1`、`2`、`4`、`8`、`16`、`32`、`64`、`128`。
- 解析后生成 `kiko = (ko_index << 4) | ki_index`。
- `data-ki="0"` 且 `data-ko="0"` 会生成 `kiko = 0xff`。
- 按键热区通过 SVG rasterize 后的 alpha 区域计算。
- `<use href="#...">`、`<use xlink:href="#...">` 和 `<symbol>` 会展开。
- `defs` 会带入按键独立 SVG，用于形状绘制和命中测试。

## 传统 Sprite 模型字段

当 `board_path` 为空时，模型按传统 sprite 配置加载。此类模型必须包含 `buttons` 和 `sprites`。

### `buttons`

```json
"buttons": [
{
"rect": {"x": 12, "y": 34, "w": 50, "h": 24},
"kiko": 17,
"keyname": "A"
}
]
```

字段：

- `rect`：按键矩形区域。
- `kiko`：按键矩阵编码。
- `keyname`：SDL 键名。
- `svg_shape`：可选 SVG shape。
- `svg_defs`：可选 SVG defs。

### `sprites`

```json
"sprites": {
"rsd_interface": {
"src": {"x": 0, "y": 0, "w": 405, "h": 813},
"dest": {"x": 0, "y": 0, "w": 405, "h": 813}
},
"rsd_pixel": {
"src": {"x": 0, "y": 0, "w": 1, "h": 1},
"dest": {"x": 62, "y": 92, "w": 2, "h": 2}
}
}
```

字段：

- 每个 sprite 必须包含 `src` 和 `dest`。
- `src` 是 interface 图中的源矩形。
- `dest` 是模型坐标中的目标矩形。
- `svg_shape`、`svg_defs` 可用于独立 SVG sprite。

## SVG Board 模型示例

```json
{
  "format": "CasioEmuMsvc.ModelInfo",
  "version": 1,
  "model_name": "svg-test-CY",
  "hardware_id": 4,
  "csr_mask": 15,
  "board_path": "board.svg",
  "interface_path": "face.svg",
  "rom_path": "core.dat",
  "flash_path": "",
  "real_hardware": false,
  "is_sample_rom": false,
  "pd_value": 0,
  "legacy_ko": false,
  "ink_color": {
    "r": 0,
    "g": 0,
    "b": 0
  },
  "enable_new_screen": false,
  "u16_mode": true,
  "large_model": true,
  "ml620_mirroring": false,
  "screen_width": 192,
  "screen_height": 63,
  "screen_scale_y": 1.0,
  "sprites": {
    "rsd_interface": {
      "src": {
        "x": 0,
        "y": 0,
        "w": 375,
        "h": 635
      },
      "dest": {
        "x": 0,
        "y": 0,
        "w": 750,
        "h": 1270
      }
    },
    "rsd_s": {"index": 0},
    "rsd_a": {"index": 1},
    "rsd_m": {"index": 2},
    "rsd_sto": {"index": 3},
    "rsd_math": {"index": 4},
    "rsd_d": {"index": 5},
    "rsd_r": {"index": 6},
    "rsd_g": {"index": 7},
    "rsd_fix": {"index": 8},
    "rsd_sci": {"index": 9},
    "rsd_e": {"index": 10},
    "rsd_cmplx": {"index": 11},
    "rsd_angle": {"index": 12},
    "rsd_wdown": {"index": 13},
    "rsd_left": {"index": 14},
    "rsd_down": {"index": 15},
    "rsd_up": {"index": 16},
    "rsd_right": {"index": 17},
    "rsd_pause": {"index": 18},
    "rsd_sun": {"index": 19}
  },
  "buttons": [
    {
      "index": 0,
      "keyname": "Escape",
      "kiko": 255
    }
  ]
}
```

`buttons` 可以省略。省略时按 board SVG 的 `data-ki`、`data-ko` 生成映射。通过编辑器保存后会写出当前映射。

## 编辑器

编辑器支持 JSON 和 `config.bin` 模型。

SVG board 模型：

- 预览使用 `interface_path` 和 `sprites.rsd_interface.src`。
- SVG interface 会按 `rsd_interface.src.w/h` rasterize。
- 按键几何由 board SVG 管理，编辑器只修改 `keyname` 和 `kiko`。
- Status 页可修改 `sprites.rsd_* .index`。
- 保存时写出 `config.json` 并更新 `config.bin`。
- 保存前校验所有按键 `kiko` 唯一。

传统 sprite 模型：

- 支持编辑按键矩形。
- 支持编辑 sprite 的 `src` 和 `dest`。
- 保存时写出 `config.json` 并更新 `config.bin`。

## 渲染

### Renderer

默认 renderer hint：

- Windows/Linux/macOS：`opengl`。
- Android/Emscripten：`opengles2`。

主模拟器 renderer 需要支持 target texture。创建 renderer 时会优先使用当前 hint，然后尝试 SDL 默认 renderer 和 software
renderer。

### SVG interface

SVG board 模型使用 SVG interface 时：

- 启动时读取 SVG 文本。
- 窗口目标尺寸变化时按显示尺寸 rasterize SVG。
- interface 和 status 在同一 board 坐标缩放下绘制；矩阵像素屏幕机型的 LCD 像素也使用同一缩放。
- `sprites.rsd_interface.dest` 可大于 `src`，例如使用 `src` 的 2 倍作为默认窗口尺寸。

### PNG/JPEG interface

SVG board 模型使用位图 interface 时：

- 使用 `sprites.rsd_interface.src` 指定的尺寸作为 offscreen render target。
- board 坐标按 interface 源尺寸映射到位图像素坐标。
- 窗口显示时整体等比缩放。

### LCD 像素

矩阵像素屏幕机型的 LCD 像素由 `screen_ink_alpha` 生成几何矩形：

- SVG board 模型下，矩阵 LCD 区域铺满 `rsd_pixel.dest`。
- 每个逻辑像素单元大小为 `lcd_dest.w / screen_width` 和 `lcd_dest.h / screen_height`。
- 使用 `SDL_RenderGeometry` 批量绘制。
- alpha 大于 255 时按对比度/残影逻辑加深颜色。

窗口实时显示受窗口尺寸和 GPU rasterization 影响。截图使用固定 3x3 输出规则。SOLAR II 不涉及这一矩阵像素屏幕绘制路径。

### Status

status 精灵来源：

- SVG board 模型：从 board SVG status 子节点生成独立 SVG sprite。
- 传统 sprite 模型：从 interface texture 的 `rsd_*` 源区域裁剪。

status 透明度由屏幕逻辑提供。

### 按键命中和高亮

SVG board 模型按键会保存 SVG shape 和 defs：

- 初始化键盘时 rasterize 为 alpha mask。
- 鼠标和触摸命中测试使用 alpha mask。
- 按下和右键按住状态使用 SVG 形状叠加高亮。

传统 sprite 模型使用矩形命中和矩形高亮。

## 截屏

截图逻辑独立生成图片，不直接保存当前窗口 framebuffer。

规则：

- 背景为白色。
- 对矩阵像素屏幕机型，每个 LCD 逻辑像素固定输出为 3x3 像素。
- 截图区域为矩阵 LCD 区域和 status 区域的 union。
- status 会合成到截图中。
- 矩阵 LCD 像素颜色由 `screen_ink_alpha` 和 `ink_color` 计算。
- SVG status 使用 SVG texture 绘制。
- 传统 status 使用 interface texture 裁剪绘制。

## 弹出屏幕

弹出屏幕使用独立 SDL window、renderer 和 streaming texture。

行为：

- 使用平台默认 accelerated renderer hint。
- 保持捕获区域宽高比。
- 监听自己的窗口关闭和大小变化事件。
- 不消费主窗口事件队列。
- 内容来自当前渲染结果中的屏幕/status 捕获区域。

## 普通 Web GUI

普通 Web GUI 支持：

- 读取模型目录中的 `config.bin` 或 `config.json`。
- 在只有 `config.json` 时生成 `config.bin`。
- SVG board 模型。
- SVG interface。
- PNG/JPEG interface。

Emscripten SDL_image 格式包含：

```text
png, svg
```

## Core Web

`CASIOEMU_CORE_WEB` 是 core API 导出，不包含完整 GUI 的以下功能入口：

- 截屏。
- 弹出屏幕。
- 开始录像。

Core Web 的 SDL_image 格式保持：

```text
png
```

## 平台

### Windows

- CMake 构建包含 JSON/SVG 解析代码。
- MSVC 工程文件包含 `ModelConfig.cpp`、`SvgBoardModelLoader.cpp` 和 `tinyxml2.cpp`。
- 默认 renderer hint 为 `opengl`。

### Linux

- CMake 构建包含 JSON/SVG 解析代码。
- 默认 renderer hint 为 `opengl`。

### macOS

- CMake 构建包含 JSON/SVG 解析代码。
- 默认 renderer hint 为 `opengl`。

### Android

- Gradle/CMake 构建包含 JSON/SVG 解析代码。
- 默认 renderer hint 为 `opengles2`。
- 模型列表扫描使用统一模型目录加载逻辑。
- 只放 `config.json` 时可以生成 `config.bin`。
- interface 尺寸由 `sprites.rsd_interface.src` 提供。

### 普通 Web

- 支持 JSON 模型。
- 支持 SVG board。
- 支持 SVG interface。
- SDL_image 开启 `png,svg`。

### Core Web

- 支持 core API 所需的模型加载。
- SDL_image 使用 `png`。
- 完整 GUI 的截屏、弹出屏幕、录像入口不编译进该目标。

## GitHub Actions

workflow 支持手动选择构建目标：

- `all`
- `windows-x64`
- `windows-x86`
- `android`
- `linux`
- `macos`
- `web`

行为：

- 自动构建：全部 native 平台和 web。
- 手动 `target=all`：全部 native 平台和 web。
- 手动选择 native 单平台：只构建对应 native 平台。
- 手动选择 `web`：只构建 Web 目标。

## 配置检查清单

SVG board 模型目录应满足：

- `config.json` 存在。
- `hardware_id` 是数字。
- `board_path` 指向存在的 SVG 文件。
- `interface_path` 指向存在的 interface 文件。
- `rom_path` 指向存在的 ROM 文件。
- `flash_path` 为空或指向存在的 flash 文件。
- `sprites.rsd_interface.src.w/h` 是正数。
- `sprites.rsd_interface.dest.x/y` 为 `0`，`dest.w/h` 是正数。
- 矩阵像素屏幕机型的 `screen_width`、`screen_height`、`screen_scale_y` 是正数。
- `sprites` 下至少包含一个带 `index` 的状态项。
- `sprites.rsd_* .index` 没有超出 status 子节点数量。
- board SVG 根节点有可用 viewBox 或尺寸。
- board SVG 有 `id="screenSlot"`。
- board SVG 的按键元素有 `data-ki` 和 `data-ko`。
- 所有按钮 `kiko` 唯一。

修改 JSON 后重新加载：

1. 删除模型目录中的 `config.bin`。
2. 启动程序或重新打开模型。
3. 程序读取 `config.json` 并生成新的 `config.bin`。

## 相关文件

- `CasioEmuMsvc/src/ModelConfig.h`
- `CasioEmuMsvc/src/ModelConfig.cpp`
- `CasioEmuMsvc/src/ModelInfo.h`
- `CasioEmuMsvc/src/SvgBoardModelLoader.h`
- `CasioEmuMsvc/src/SvgBoardModelLoader.cpp`
- `CasioEmuMsvc/src/Emulator.cpp`
- `CasioEmuMsvc/src/Peripheral/Screen.cpp`
- `CasioEmuMsvc/src/Peripheral/Keyboard.cpp`
- `CasioEmuMsvc/src/StartupUi/StartupUi.cpp`
- `CasioEmuMsvc/src/Gui/HwController.cpp`
- `CasioEmuMsvc/src/Gui/PopUpDisplay.h`
- `CasioEmuMsvc/src/RendererBackend.h`
- `CasioEmuMsvc/src/Web/web_main.cpp`
- `CasioEmuMsvc/src/Web/core_api.cpp`
- `CasioEmuMsvc/CMakeLists.txt`
- `CasioEmuMsvc/CasioEmuMsvc.vcxproj`
- `.github/workflows/build.yml`
- `CasioEmuMsvc/third_party/tinyxml2/`
