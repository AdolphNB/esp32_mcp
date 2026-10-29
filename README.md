# ESP32-S3 MCP 桌面显示（4.3"）

> **当前分支：`esp32-mcp-lcd43b`** — Waveshare ESP32-S3-Touch-LCD-4.3B 显示版。
> 基础 MCP 节点（YD-ESP32-S3，无屏）请切换到 **`main`** 分支查看它的 README。

一块 4.3 寸、800×480 的桌面屏，运行标准 **MCP Streamable HTTP** 服务端。AI 客户端（Cherry Studio / Hermes / OpenClaw）可以：

- 推送日历、农历、待办、格言、星座运势、进度条等卡片
- 在屏上完成 Wi-Fi 配网，并直接看到 MCP Endpoint / Token
- 点按勾选待办、长按拖动排序
- 通过 MCP 控制 GPIO、隔离 DI/DO、I2C、RS485、CAN
- 通过局域网 OTA 升级并自动回滚

详细设计、引脚、场景 schema 与客户端配置见 [`docs/DISPLAY.md`](docs/DISPLAY.md)。

---

## 本分支已实现

- ✅ Waveshare ESP32-S3-Touch-LCD-4.3B 板级支持（800×480 RGB565、GT911 触摸、CH422G、PCF85063 RTC）
- ✅ LVGL 9 显示后端：场景 JSON + 4 个页面 + SPIFFS 持久化
- ✅ 屏上配网：扫描 SSID、屏上键盘、连接状态；SoftAP 仅作备用
- ✅ 配网成功后自动显示 MCP 配置页（Endpoint / Token / mDNS / 网页入口）
- ✅ 待办交互：点按勾选（删除线）、长按拖动排序，状态写回场景
- ✅ 品牌开机 Logo、完整 GB2312 字体（6763 汉字 + 中英混排 + 标点）
- ✅ MCP 显示工具 11 个，设备 / GPIO / 隔离 IO / OTA 工具保留
- ✅ 双 OTA 分区 + 健康检查 + 自动回滚
- ✅ 三种构建：`build-43b` / YD 节点 / 无屏 mock

---

## 硬件

### 主目标板：Waveshare ESP32-S3-Touch-LCD-4.3B (SKU 28141)

| 项目 | 参数 |
| --- | --- |
| 屏幕 | 4.3" IPS，800×480，RGB565 |
| 触摸 | GT911（I2C） |
| RTC | PCF85063（I2C 0x51） |
| IO 扩展 | CH422G（mode `0x24` / OC `0x23` / IO `0x38` / 输入 `0x26`） |
| 隔离 IO | DI0–DI1（5–36V）、DO0–DO3 |
| 现场总线 | RS485（TX44/RX43）、CAN（TX15/RX16） |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| 控制台 | 原生 USB Serial/JTAG（`/dev/ttyACM0`） |

> **GPIO0 和 GPIO48 是 LCD 数据线**：运行期间 BOOT 键与 WS2812 不可用，恢复出厂请走网页或屏上 UI。

### 兼容开发板：YD-ESP32-S3 N16R8

无屏节点仍可在本分支构建（mock 显示后端），用于联调 MCP/UI/农历。

---

## 快速开始（4.3B）

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
cd /home/alan/myPrj/esp32_mcp

./tools/build_43b.sh
./tools/flash_43b.sh /dev/ttyACM0 --erase   # 首次建议整片擦除
idf.py -B build-43b -p /dev/ttyACM0 monitor
```

脚本会校验板级配置和固件大小，并整片写入 app + `storage.bin`（字体和页面资源在 storage 分区）。

> 不要直接 `idf.py flash`：默认 `build/` 可能是 YD 节点固件。

### VS Code

工作区已默认使用 `build-43b`。若“小火苗”烧错固件，确认：

- `idf.buildPath = build-43b`
- `idf.sdkconfigFilePath = build-43b/sdkconfig`
- 或直接用上面的脚本烧录

### 其他构建

```bash
# YD 无屏节点
idf.py -B build -D SDKCONFIG=build/sdkconfig build

# 无屏 mock（验证 UI/农历/MCP）
idf.py -B build-mock \
  -D SDKCONFIG=build-mock/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display-mock" build
```

---

## 首次开机

1. 屏幕先显示“奇点临近 / SINGULARITY NEAR”开机 Logo（约 1 秒）
2. 无 Wi-Fi 凭证时自动进入“连接 Wi-Fi”：扫描 → 选 SSID → 屏上键盘输入密码 → 连接
3. 连接成功后自动显示 **MCP 配置页**：
   - `http://<ip>:8080/mcp`
   - 32 位 Bearer Token
   - `http://<hostname>.local:8080/mcp`
   - 浏览器打开 `http://<ip>/` 可复制完整客户端配置
4. 点“进入桌面”回到仪表盘；仪表盘右下角“设置”可再次打开配网页

### 找回 Token

Token 存在 NVS，每台设备独立。若丢失：

```bash
# 已连接时（需要旧 Token 才能读敏感字段）
curl -s http://<ip>/api/status -H "Authorization: Bearer <token>"

# 或直接读 NVS（需要串口未被占用）
esptool.py -p /dev/ttyACM0 read-flash 0x9000 0x6000 /tmp/nvs.bin
python3 $IDF_PATH/components/nvs_flash/nvs_partition_tool/nvs_tool.py \
        -d minimal /tmp/nvs.bin | grep mcp_token
```

> NVS dump 同时包含 Wi-Fi 密码，属于敏感文件，不要外传。

---

## 接入 MCP 客户端

统一名称：`esp32_desk`。传输方式：**Streamable HTTP**，认证头：

```text
Authorization: Bearer <屏幕上的 Token>
```

### Cherry Studio

```json
{
  "mcpServers": {
    "esp32_desk": {
      "type": "streamableHttp",
      "url": "http://<ip>:8080/mcp",
      "headers": { "Authorization": "Bearer <token>" }
    }
  }
}
```

### Hermes

```yaml
mcp_servers:
  esp32_desk:
    url: "http://<ip>:8080/mcp"
    headers:
      Authorization: "Bearer <token>"
    resources: true
    prompts: false
```

```bash
hermes mcp test esp32_desk
```

### OpenClaw

```bash
openclaw mcp set esp32_desk '{
  "url": "http://<ip>:8080/mcp",
  "transport": "streamable-http",
  "headers": {"Authorization": "Bearer <token>"},
  "connectionTimeoutMs": 5000,
  "requestTimeoutMs": 15000
}'
openclaw mcp probe esp32_desk --json
```

> 如果客户端收到 401 并去探测 `/.well-known/...`，说明 Authorization 头缺失或 Token 过期；重新从屏上 MCP 配置页复制即可。

---

## MCP 工具

### 显示

| 工具 | 说明 |
| --- | --- |
| `display_info` | 当前页、亮度、后端、页面状态 |
| `display_render` | 渲染自定义 scene JSON |
| `display_patch` | 按 `id` 更新单个组件 |
| `display_page` | 切换页面 |
| `display_clear` | 清空页面 |
| `display_notify` | 悬浮提示 |
| `display_brightness` | 亮度 0–100 |
| `display_calendar` | 日历/农历模板 |
| `display_todo` | 待办模板；屏上可点按勾选、长按拖动排序 |
| `display_quote` | 格言卡片（文字垂直居中） |
| `display_horoscope` | 星座运势卡片 |

### 设备 / IO / 系统

| 工具 | 说明 |
| --- | --- |
| `device_info` / `sys_status` / `wifi_status` | 设备、堆、Wi-Fi 状态 |
| `gpio_list` / `gpio_read` / `gpio_write` / `gpio_pulse` / `led_set` | GPIO 与 WS2812 |
| `io_status` / `io_read` / `io_write` | 隔离 DI/DO、I2C/RS485/CAN 引脚 |
| `i2c_scan` | 扫描外部 I2C 总线 |
| `ota_status` / `ota_apply` / `ota_abort` | 局域网 OTA |

完整参数和场景 schema：[`docs/DISPLAY.md`](docs/DISPLAY.md)。

---

## OTA

网页：打开 `http://<ip>/`，上传 `build-43b/esp32_mcp.bin`。

Agent：先启动局域网固件服务，再调用 `ota_apply`：

```bash
./tools/serve_firmware.sh 0.2.0
```

```json
{
  "name": "ota_apply",
  "arguments": {
    "url": "http://192.168.3.110:8000/esp32_mcp-0.2.0.bin",
    "sha256": "<64 hex>",
    "version": "0.2.0",
    "confirm": "OTA"
  }
}
```

- 写入备用 4 MB 分区，校验 SHA256
- 切换分区重启，30 秒健康检查，失败自动回滚
- URL 必须在 `CONFIG_APP_OTA_ALLOWED_URL_PREFIX` 白名单内

---

## 目录

```text
esp32_mcp/
├── components/
│   ├── app_board/            # 4.3B / YD 板级、CH422G、RTC
│   ├── app_ui/               # 场景服务、LVGL 后端、配网 UI、开机 Logo
│   └── mcp-c-sdk/            # 内置 MCP SDK（含运行时 Token 等补丁）
├── main/                     # Wi-Fi、HTTP、MCP 工具、OTA、时钟
├── data/                     # 字体、品牌资源、SPIFFS 内容
├── tools/
│   ├── build_43b.sh
│   ├── flash_43b.sh
│   ├── gen_fonts.sh
│   └── gen_boot_logo.py
├── docs/
│   ├── DISPLAY.md            # 4.3B 设计、场景、工具、P0 清单
│   ├── DESIGN.md             # 基础 MCP 节点设计
│   └── README-BRANCHES.md    # 分支 README 约定
└── sdkconfig.defaults*
```

---

## 已知限制

- 4.3B 的 GPIO0 / GPIO48 被 LCD 占用，不能当普通 IO 或 WS2812 使用。
- OTA 仅限局域网，不做 Secure Boot / Flash Encryption。
- 字体覆盖 GB2312 全部 6763 汉字；生僻字、emoji 可能仍缺字。
- 内置 MCP SDK 为 vendored 副本，升级时需重新核对补丁。

---

## 分支 README

各分支的 README 内容不同，GitHub 会显示当前所选分支的版本：

| 分支 | README 主题 |
| --- | --- |
| `main` | YD-ESP32-S3 基础 MCP 节点 |
| `mcp-base` | MCP 基础固件 |
| `esp32-mcp-lcd43b` | 4.3" 桌面显示平台（本文） |

维护方式见 [`docs/README-BRANCHES.md`](docs/README-BRANCHES.md)。
