# ESP32-S3-Touch-LCD-4.3B 显示平台

本文记录 `esp32-mcp-lcd43b` 分支的设计、引脚、构建方式和 MCP 接口。目标是让 AI 客户端（Cherry Studio / Hermes / WorkBuddy）通过 MCP 把日历、待办、格言、星座运势等卡片推到 4.3 寸屏上，同时保留 GPIO / I2C / RS485 / CAN / 隔离 IO 的对外控制能力。

## 1. 当前状态

已完成（不依赖屏幕硬件，可在现有 YD 板上用 mock 后端验证）：

- 板级 profile：`waveshare_43b` + `yd_s3`
- CH422G IO 扩展驱动：背光、触摸复位、LCD 复位、SD CS、DI、DO
- PCF85063 RTC 驱动：I2C 0x51，INT = GPIO6
- UI 场景模型：JSON 校验、坐标裁剪、页面管理、SPIFFS 持久化
- Mock 显示后端：把场景写入日志，便于联调 MCP
- LVGL 9 显示后端：RGB565 + GT911 + 页面渲染（text/clock/calendar/list/quote/horoscope/divider/progress）
- 24px GB2312 中文字体（SPIFFS binfont，加载到 PSRAM）
- 屏上 Wi-Fi 配网：扫描、选择 SSID、密码键盘、连接状态、Token 展示、SoftAP 备用
- MCP 显示工具：`display_info` / `display_render` / `display_patch` / `display_page` / `display_clear` / `display_notify` / `display_brightness` / `display_calendar` / `display_todo` / `display_quote` / `display_horoscope` / `display_image`
- 时钟服务：NTP + 时区 + PCF85063 + 农历/干支/生肖/节日
- 独立构建配置：`sdkconfig.defaults.display`、`sdkconfig.defaults.display-mock`

待硬件到货：

- 实机验证 RGB/GT911/背光/RTC/DI-DO
- 32px/48px 大字字体子集
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

4.3B 显示配置（推荐用脚本，避免误用 `build/` 里的 YD 固件）：

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
./tools/build_43b.sh
./tools/flash_43b.sh /dev/ttyACM0 --erase
```

`--erase` 会清掉 NVS（Wi-Fi/Token），首次 bring-up 建议加。  
`flash_43b.sh` 会检查 `build-43b/sdkconfig` 的板型，以及 app 大小（约 2 MB），防止把 YD 固件烧进 4.3B。

等价的手动命令：

```bash
idf.py -B build-43b \
  -D SDKCONFIG=build-43b/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display" build
idf.py -B build-43b -p /dev/ttyACM0 flash monitor
```

> `idf.py flash` 会同时写入 `storage.bin`（字体在 storage 分区）。如果只执行 `app-flash`，字体不会更新。

> 官方 4.3B 示例建议 ESP-IDF 5.5.2；当前工程用 6.1 构建通过。实机 bring-up 时如果 LVGL/触摸组件在 6.1 下有问题，再统一降到 5.5.2 LTS。

### 3.1 屏上配网

显示构建默认使用 **屏上配网**，不再自动开 SoftAP：

- 首次启动无 Wi-Fi 凭证时，屏幕直接进入“连接 Wi-Fi”页面
- 点“重新扫描”列出 2.4 GHz 网络（按 RSSI 排序）
- 点某个 SSID → 弹出密码页 + LVGL 键盘 → 点“连接”
- 连接成功后自动弹出 **MCP 配置页**：Endpoint、Bearer Token、mDNS 地址，以及
  `http://<ip>/` 网页入口（网页里可一键复制 Hermes / OpenClaw / 通用 JSON / curl 配置）
- MCP 配置页点“进入桌面”回到仪表盘；点“返回”回配网页
- 配网页在已连接时多出“MCP 配置”按钮，可随时重新查看
- 失败后停留配网页，可重新扫描/换网络
- 点“手机配网”可临时启动 SoftAP + 网页作为备用（`APP_PROVISIONING_ALLOW_SOFTAP_FALLBACK`）
- 点“清除配置”清除 Wi-Fi 并重启，回到配网页
- 仪表盘右下角“设置”按钮可随时重新打开配网页

相关 Kconfig：

```ini
CONFIG_APP_PROVISIONING_SCREEN=y
CONFIG_APP_PROVISIONING_ALLOW_SOFTAP_FALLBACK=y
CONFIG_APP_WIFI_SCAN_MAX=16
```

网页门户仍保留在端口 80，用于 STA 连接后的状态查看/OTA；它不再作为唯一的配网入口。

> 屏上配网依赖 LVGL + GT911，实机验证前请先按 P0 清单确认屏幕和触摸正常。

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

#### list 触摸交互（待办）

`list` 支持两个可选布尔字段：

```json
{
  "id": "todo",
  "type": "list",
  "x": 24, "y": 104, "w": 752, "h": 352,
  "checkable": true,
  "reorder": true,
  "items": [
    {"text": "项目评审", "prio": "high", "done": false, "due": "10:30"},
    {"text": "买电池", "prio": "low", "done": true}
  ]
}
```

- `checkable`：每行左侧显示勾选框；**点按整行**切换 `done`，完成项文字变灰并加删除线
- `reorder`：**长按约 0.4 秒后上下拖动**调整顺序，松手保存
- 触摸修改直接写回当前页的 scene JSON（`/spiffs/ui/pageN.json`），重启/切页后保持
- 修改只更新 `done` 和顺序，不重新渲染整页；写盘由独立 worker 完成，不会卡住 LVGL
- `display_todo` 模板默认开启 `checkable` 和 `reorder`，并在卡片底部显示操作提示

> AI 再次调用 `display_todo` 会用新数据覆盖整页，包括屏上改过的勾选和顺序。需要保留时请让 AI 只更新未完成项，或改用 `display_patch`。

### 4.3 字体

- `data/fonts/ui24.bin`：GB2312 一级 + 二级字库（6763 汉字）+ ASCII/Latin-1 + 中英标点，24px，约 1.75 MB
- ASCII/Latin 字形来自 Noto Sans，汉字与中文标点来自 Droid Sans Fallback，合并同一个 24px 字体，混排字号一致
- U+3000、U+00A0 等 Unicode 空格在渲染前统一转成普通空格，避免缺字方框
- 字体位图总量 > 1 MB，必须开启 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`；否则 LVGL 默认的 20-bit `bitmap_index` 会溢出，后半段汉字会渲染成错误字形（乱码）
- 启动时从 `/spiffs/fonts/ui24.bin` 读入 PSRAM，用 `lv_binfont_create_from_buffer()` 加载
- 加载失败时回退到 LVGL 内置 16px CJK 字体
- 重新生成：

```bash
./tools/gen_fonts.sh
# 或指定字体：
FONT=/path/to/NotoSansCJKsc-Regular.otf ./tools/gen_fonts.sh
```

> 字体在 `storage` 分区，必须用 `idf.py flash`（会写 `storage.bin`）；只烧 app 不会更新字体。

后续可以再生成 32px/48px 子集，用于标题和“大字格言”。

校验规则：

- 场景 ≤ 16 KB，组件 ≤ 24 个
- `x/y/w/h` 必填，自动裁剪到屏幕范围
- 文本长度限制：普通文本 512，列表项 128，格言 256
- `list.items` ≤ 16，`calendar.events` ≤ 8
- 未知组件类型直接拒绝

### 4.4 开机 Logo

开机后先显示“奇点临近 / SINGULARITY NEAR”Logo，约 1 秒后进入配网页或仪表盘。

- 源文件：`data/brand/brand-mark.svg`（官网 brand mark）
- 预览：`data/brand/logo_boot.png`
- 固件资源：`components/app_ui/assets/logo_boot.bin`（460×150，RGB565，背景 `#0B141B`），由 `EMBED_FILES` 编进 app
- 重新生成：

```bash
./tools/gen_boot_logo.py
```

依赖 Pillow、node/npx（`sharp-cli` 把 SVG 转 PNG）和 Noto Sans CJK 字体；字体路径可用 `CJK_FONT` / `LATIN_FONT` 覆盖。

Logo 资源在 app 分区，普通 `idf.py flash` 或 `idf.py app-flash` 都会更新。

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
| `display_todo` | 待办模板，`items` 支持 `prio/done/due`；屏上可点按勾选、长按拖动排序 |
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
    "esp32_desk": {
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
  esp32_desk:
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

## 8. 内存预算

### 8.1 当前 mock 构建（未接 LVGL）

`idf.py -B build-43b size` 的典型结果：

| 区域 | 已用 | 余量 | 说明 |
| --- | --- | --- | --- |
| DIRAM | ~100 KB | ~241 KB | 内部 SRAM，真正的运行时堆来源 |
| Flash Code | ~900 KB | 4 MB 分区内 | LVGL 会再增加 ~700–900 KB |
| Flash Data | ~287 KB | | 字体不放 app，放 storage |

> `size` 里的 `IRAM 16384 / 100%` 是 esp-idf-size 的**统计桶**，不是整个 IRAM。
> 实际 map 中 `iram0_0_seg` 为 358 KB，当前只用了约 68 KB（19%），
> 链接器的 IRAM 溢出断言没有触发。LVGL 本身不占 IRAM，只有少量 `IRAM_ATTR` 回调
> （例如 RGB vsync）会用到，余量足够。

### 8.2 接入 LVGL 后的预算

PSRAM（8 MB）：

| 项目 | 估算 |
| --- | --- |
| RGB 双 framebuffer（800×480×2×2） | 1.5 MB |
| 中文字体（24px，GB2312 全字库） | 1.8 MB |
| LVGL 堆 / 对象 / 图片解码 | 0.8–1.2 MB |
| 场景 JSON、页面、队列 | 0.2 MB |
| 余量 | ≥ 1.5 MB |

内部 SRAM（DIRAM 余量 ~241 KB 起）：

| 项目 | 估算 |
| --- | --- |
| Wi-Fi + lwIP | 70–100 KB |
| MCP / HTTP / mDNS | 40–60 KB |
| LVGL task stack | 8–16 KB |
| RGB bounce buffer（10 行） | 16 KB |
| 触摸 / I2C / CH422G | 4 KB |
| UI / 时钟 / JSON 解析 | 12–20 KB |
| 余量 | 30–60 KB |

结论：**PSRAM 充足，内部 SRAM 偏紧但可满足**。关键措施：

- LVGL 使用 RGB 双 framebuffer + **full refresh**（avoid tearing），framebuffer 全部在 PSRAM，不额外分配内部 draw buffer。
- LVGL/UI 任务栈优先放 PSRAM（`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`）。
- 字体从 SPIFFS 加载到 PSRAM；场景 JSON 也放 PSRAM。
- LVGL 只编译用到的 widget，关闭 demo、动画、多余字体。
- 显示构建已关闭 `ESP_WIFI_IRAM_OPT` / `ESP_WIFI_RX_IRAM_OPT` / `ESP_TIMER_IN_IRAM`，把约 18 KB 从 IRAM 挪回可用空间；如 Wi-Fi 吞吐仍够用就保持关闭。
- 如果内部堆低于 40 KB，再考虑 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`，把 Wi-Fi/lwIP 缓冲移到 PSRAM。

### 8.3 运行时观测

- 启动日志会打印：
  `boot heap: internal=... KB, psram=... KB`
  `runtime heap: internal=... KB, psram=... KB`
- MCP `sys_status` 返回 `free_internal_kb`、`free_psram_kb`、`largest_psram_block_kb`。
- 目标：LVGL 跑起来后 `free_internal_kb ≥ 40`，`largest_psram_block_kb ≥ 300`。

## 9. P0 实机检查清单

板子到货后按顺序做，先排除硬件问题，再排查软件：

1. **官方 demo**：用 ESP-IDF 5.5.2 烧 `08_lvgl_Porting` 或 `09_lvgl_v9_demo`
   - 屏亮、颜色正常
   - 触摸四角坐标准确
   - 背光可开/关
2. **RTC**：装 CR927，烧 `04_RTC_Test`
   - 读写时间正常
   - 断电 30 秒再上电，时间继续走
3. **隔离 IO**：烧 `05_IO_Test`
   - DO 接 5V LED + 电阻，能亮/灭
   - DI 接 5V 开关，状态能读回
4. **RS485**：烧 `02_RS485_Test`，USB-RS485 回环
5. **CAN**：烧 `06/07_TWAI`，USB-CAN-A 收发
6. **我们的固件**：
   ```bash
   idf.py -B build-43b -p /dev/ttyACM0 flash monitor
   ```
   检查日志：`board=waveshare_43b`、CH422G、`rtc=yes`、`SPIFFS ready`、`loaded CJK font`、`MCP Streamable HTTP ready`
7. **屏上配网**：
   - 首次启动应直接出现“连接 Wi-Fi”
   - 点“重新扫描”，选择 2.4 GHz SSID
   - 输入密码并连接，成功后自动出现 MCP 配置页
   - 核对 Endpoint (`http://<ip>:8080/mcp`)、32 位 Token、mDNS 地址
   - 点“返回”回到配网页，点“MCP 配置”可再次查看
   - 点“进入桌面”回到仪表盘
   - 点右下角“设置”能再次打开配网页
   - 点“手机配网”能临时启动 SoftAP（备用）
8. **MCP 端到端**：
   ```bash
   python3 tools/test_display.py --url http://<ip>:8080/mcp --token <token>
   ```
   检查 `display_quote` / `display_todo` / `display_calendar` 是否返回 `ok`，以及屏幕是否出现对应卡片。
9. **性能**：Wi-Fi + MCP + UI 同时跑，记录 free heap、PSRAM、FPS。

> 如果屏幕没有出现配网页，先看串口日志：
> - `ui_lvgl: RGB+LVGL ready` 说明显示初始化成功
> - `prov_backend: on-screen provisioning backend ready` 说明配网 UI 已注册
> - 如果 `display UI init failed`，固件会自动回退到 SoftAP 配网
