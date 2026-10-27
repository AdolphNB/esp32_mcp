# ESP32-S3-Touch-LCD-4.3B 显示平台

本文记录 `esp32-mcp-lcd43b` 分支的设计、引脚、构建方式和 MCP 接口。目标是让 AI 客户端（Cherry Studio / Hermes / WorkBuddy）通过 MCP 把日历、待办、格言、星座运势等卡片推到 4.3 寸屏上，同时保留 GPIO / I2C / RS485 / CAN / 隔离 IO 的对外控制能力。

## 1. 当前状态

已完成（不依赖屏幕硬件，可在现有 YD 板上用 mock 后端验证）：

- 板级 profile：`waveshare_43b` + `yd_s3`
- CH422G IO 扩展驱动：背光、触摸复位、LCD 复位、SD CS、DI、DO
- PCF85063 RTC 驱动：I2C 0x51，INT = GPIO6
- UI 场景模型：JSON 校验、坐标裁剪、页面管理、SPIFFS 持久化
- Mock 显示后端：把场景写入日志，便于联调 MCP
- MCP 显示工具：`display_info` / `display_render` / `display_page` / `display_clear` / `display_notify` / `display_brightness` / `display_calendar` / `display_todo` / `display_quote` / `display_horoscope` / `display_image`
- 时钟服务：NTP + 时区 + PCF85063 + 农历/干支/生肖/节日
- 独立构建配置：`sdkconfig.defaults.display`、`sdkconfig.defaults.display-mock`

待硬件到货：

- LVGL 9 + RGB LCD 后端（`ui_backend_lvgl.c` 目前是占位）
- GT911 触摸、背光、RTC 实机验证
- 字体子集生成与中文渲染
- 图片解码、二维码
- I2C / RS485 / CAN / 隔离 IO 的 MCP 工具

## 2. 硬件

Waveshare **ESP32-S3-Touch-LCD-4.3B**（带外壳版 SKU 28141）：

| 项目 | 参数 |
| --- | --- |
| MCU | ESP32-S3-WROOM-1-N16R8 |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| 屏幕 | 4.3" IPS 800×480，RGB565，ST7262 |
| 触摸 | GT911，I2C，5 点 |
| 亮度 | 550 cd/m²，仅开/关（无 PWM） |
| RTC | PCF85063 + CR927 电池 |
| 对外接口 | I2C、RS485、CAN、隔离 DI×2 / DO×4、USB-C |
| 供电 | USB-C 5V / DC 7–36V / 3.7V 锂电池 |

### 2.1 引脚

| 功能 | GPIO / 接口 | 备注 |
| --- | --- | --- |
| LCD VSYNC / HSYNC / DE / PCLK | 3 / 46 / 5 / 7 | |
| LCD B0–B15 | 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40 | GPIO0=B6，GPIO48=B8 |
| I2C SDA / SCL | 8 / 9 | 触摸、CH422G、RTC、外部 I2C 共用 |
| 触摸 INT | 4 | GT911 |
| 触摸 RST | CH422G IO1 | |
| 背光 | CH422G IO2 | 仅开/关 |
| LCD RST | CH422G IO3 | |
| SD CS | CH422G IO4 | |
| 隔离 DI0 / DI1 | CH422G IO0 / IO5 | 5–36V |
| 隔离 DO0–DO3 | CH422G OC0–OC3 | 开漏，450mA |
| RTC | PCF85063，0x51 | INT = GPIO6 |
| RS485 | UART0 TX44 / RX43 | 控制台必须走 USB Serial/JTAG |
| CAN | TWAI TX15 / RX16 | |
| 控制台 / 烧录 | USB Serial/JTAG | `/dev/ttyACM0` |

### 2.2 CH422G

| 地址 | 作用 |
| --- | --- |
| `0x24` | 模式：IO 输出 `0x01`、OC 输出 `0x04`、输入 `0x00` |
| `0x38` | IO 输出（背光、触摸复位、LCD 复位、SD CS、DI） |
| `0x23` | OC 输出（DO0–DO3） |
| `0x26` | IO 输入（DI0/DI1） |

驱动内部维护 `io_shadow` 和 `oc_shadow`，避免写 DO 时把背光/触摸复位/LCD 复位一起写掉。

### 2.3 注意事项

- **GPIO0 是 LCD B6**，同时是 BOOT 键。运行时不要用 BOOT 翻页或恢复出厂；恢复出厂走网页或 MCP。
- **GPIO48 是 LCD B8**，WS2812 状态灯在显示板上禁用，状态显示在屏幕右上角。
- 4.3B 没有独立 USB 转串口（CH343P），控制台是原生 USB；UART0 让给 RS485。
- ESP32 启动时 ROM 日志会从 UART0 输出，RS485 总线上可能看到一小段启动日志。
- DI/DO 是 5–36V 隔离接口，不要直接接 3.3V 逻辑；DO 是开漏，驱动继电器时加续流二极管。

## 3. 构建

旧节点（无屏，保持原行为）：

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py -B build-node build
```

Mock 显示（在现有 YD 板上验证 MCP/UI/农历，不点屏）：

```bash
idf.py -B build-mock \
  -D SDKCONFIG=build-mock/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display-mock" build
idf.py -B build-mock -p /dev/ttyUSB0 flash monitor
```

4.3B 显示配置（当前后端仍是 mock，LVGL 后端待实机）：

```bash
idf.py -B build-43b \
  -D SDKCONFIG=build-43b/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display" build
idf.py -B build-43b -p /dev/ttyACM0 flash monitor
```

> 官方 4.3B 示例建议 ESP-IDF 5.5.2；当前工程用 6.1 构建通过。实机 bring-up 时如果 LVGL/触摸组件在 6.1 下有问题，再统一降到 5.5.2 LTS。

## 4. UI 场景

`ui_service` 每页保存一个 scene JSON，最多 4 页（Kconfig 可调）。场景经过校验后写入 `/spiffs/ui/pageN.json`，重启恢复。

### 4.1 Scene 结构

```json
{
  "v": 1,
  "theme": "dark",
  "screen": {"w": 800, "h": 480},
  "widgets": [
    {
      "id": "clock",
      "type": "clock",
      "x": 24, "y": 16, "w": 360, "h": 90,
      "size": 64,
      "format": "%H:%M"
    },
    {
      "id": "calendar",
      "type": "calendar",
      "x": 24, "y": 120, "w": 360, "h": 320,
      "solar": "10/27",
      "weekday": "周二",
      "lunar": "九月十八",
      "ganzhi": "丙午",
      "festival": "重阳节",
      "yi": "会友",
      "ji": "远行",
      "events": ["10:30 项目评审", "14:00 交付固件"]
    }
  ]
}
```

### 4.2 组件类型

`text`、`big_text`、`clock`、`calendar`、`list`、`quote`、`horoscope`、`image`、`qr`、`divider`、`progress`。

校验规则：

- 场景 ≤ 16 KB，组件 ≤ 24 个
- `x/y/w/h` 必填，自动裁剪到屏幕范围
- 文本长度限制：普通文本 512，列表项 128，格言 256
- `list.items` ≤ 16，`calendar.events` ≤ 8
- 未知组件类型直接拒绝

## 5. MCP 工具

| 工具 | 说明 |
| --- | --- |
| `display_info` | 当前页、亮度、后端、页面状态、时钟/农历、板型 |
| `display_render` | 渲染自定义 scene JSON |
| `display_patch` | 按 `id` 更新单个组件 |
| `display_page` | `action=next/prev` 或指定页 |
| `display_clear` | 清空当前页/指定页 |
| `display_notify` | 3 秒悬浮提示 |
| `display_brightness` | 0–100；0 关闭背光 |
| `display_calendar` | 日历模板，AI 只填数据 |
| `display_todo` | 待办模板，`items` 支持 `prio/done/due` |
| `display_quote` | 大字格言，`size` 32/48/64 |
| `display_horoscope` | 星座运势卡片 |
| `display_image` | 允许前缀内的 LAN 图片（后端待实现） |
| `io_status` | 隔离 DI/DO 状态与 I2C/RS485/CAN 引脚 |
| `io_read` | 读隔离 DI0/DI1（5–36V） |
| `io_write` | 写隔离 DO0–DO3（默认关闭，需 `confirm="IO"`） |
| `i2c_scan` | 扫描外部 I2C 总线，跳过 CH422G/GT911/RTC |

### 5.1 示例

```json
{"name":"display_quote","arguments":{"text":"慢慢来，比较快。","author":"佚名","size":48}}
```

```json
{"name":"display_todo","arguments":{
  "title":"今天",
  "items":[
    {"text":"项目评审","prio":"high","due":"10:30"},
    {"text":"交付固件","prio":"normal"},
    {"text":"买电池","prio":"low","done":true}
  ],
  "footer":"按轻重缓急排序"
}}
```

```json
{"name":"display_calendar","arguments":{
  "solar":"10/27",
  "weekday":"周二",
  "lunar":"九月十八",
  "ganzhi":"丙午",
  "festival":"重阳节",
  "events":["10:30 项目评审","14:00 交付固件"]
}}
```

### 5.2 客户端配置

Cherry Studio：

```json
{
  "mcpServers": {
    "esp32-display": {
      "type": "streamableHttp",
      "url": "http://192.168.3.50:8080/mcp",
      "headers": { "Authorization": "Bearer <token>" }
    }
  }
}
```

Hermes：

```yaml
mcp_servers:
  esp32_display:
    url: "http://192.168.3.50:8080/mcp"
    headers:
      Authorization: "Bearer <token>"
    tools:
      include:
        - display_info
        - display_render
        - display_calendar
        - display_todo
        - display_quote
        - display_horoscope
```

## 6. 源码结构

```
components/app_board/
  include/board.h
  board.c                     # 板型分发
  board_waveshare_43b.c       # 引脚、CH422G、背光、RTC、DI/DO
  board_yd_s3.c               # 旧板 profile
  io_expander_ch422g.c
  rtc_pcf85063.c

components/app_ui/
  include/ui_scene.h
  include/ui_service.h
  include/ui_backend.h
  ui_scene.c                  # JSON 校验/归一化/默认首页
  ui_service.c                # 页面、SPIFFS、NVS、亮度
  ui_backend_mock.c           # 日志后端
  ui_backend_lvgl.c           # 占位，待实机
  ui_backend.c

main/
  clock_service.c             # NTP + RTC + 农历
  lunar.c                     # 1900–2099 农历表
  mcp_tools_display.c         # 11 个显示工具
  mcp_tools_io.c              # 待实现
```

## 7. 下一步

1. 实机验证 4.3B：LCD、触摸、背光、RTC、DI/DO
2. 实现 `ui_backend_lvgl.c`：RGB + GT911 + LVGL 9
3. 字体子集（26/32/48）与中文折行
4. 图片/二维码后端
5. I2C / RS485 / CAN / 隔离 IO 工具
6. 配网页“显示”页与屏幕预览
