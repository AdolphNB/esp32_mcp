# ESP32-S3 MCP Node

基于 **YD-ESP32-S3 N16R8** 的 Wi-Fi MCP 节点：通过 SoftAP 网页配网接入局域网，并以 **MCP Streamable HTTP** 对外提供设备状态、GPIO、WS2812 和 OTA 能力。

目标客户端：**Hermes Agent**、**OpenClaw**，以及任意标准 MCP 客户端。

---

## 当前状态

- ✅ ESP-IDF v6.1 工程骨架
- ✅ 16 MB Flash / 8 MB OPI PSRAM 配置
- ✅ 双 OTA 分区 + 回滚
- ✅ SoftAP + Captive Portal 配网页面
- ✅ MCP Streamable HTTP（`/mcp`，端口 8080）
- ✅ 11 个 MCP 工具 + 2 个资源
- ✅ WS2812（GPIO48）
- ✅ `idf.py build` 通过，固件约 1.3 MB
- ✅ 实机烧录 / 配网 / MCP 握手验证通过

详细设计见 [`docs/DESIGN.md`](docs/DESIGN.md)。

---

## 4.3 寸桌面显示平台

`esp32-mcp-lcd43b` 分支在现有 MCP 节点上增加 **Waveshare ESP32-S3-Touch-LCD-4.3B** 支持：800×480 屏、GT911 触摸、PCF85063 RTC、隔离 DI/DO、RS485/CAN，目标是把 AI 推送的日历/待办/格言/星座卡片显示到桌面屏上。

4.3B 默认使用 **屏上配网**：首次启动直接在屏幕上扫描/选择 Wi-Fi、输入密码，不再需要先连接 `ESP32-MCP-XXXX` 热点；SoftAP 仅作为屏幕里的备用入口。

- 设计、引脚、场景 schema、MCP 工具和 Cherry Studio 配置：[`docs/DISPLAY.md`](docs/DISPLAY.md)
- 无屏 mock 构建（现有 YD 板可跑）：

```bash
idf.py -B build-mock \
  -D SDKCONFIG=build-mock/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display-mock" build
```

- 4.3B 构建：

```bash
idf.py -B build-43b \
  -D SDKCONFIG=build-43b/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display" build
```

> 4.3B 当前使用 mock 显示后端；LVGL/RGB 后端在硬件到货后接入。

---

## 硬件

| 项目 | 参数 |
| --- | --- |
| 开发板 | YD-ESP32-S3 N16R8 |
| Flash | 16 MB |
| PSRAM | 8 MB OPI |
| LED | WS2812，GPIO48 |
| USB | CH343P（`/dev/ttyUSB0`）+ 原生 USB（`/dev/ttyACM0`） |
| 按键 | BOOT（GPIO0）、RST |

> GPIO35/36/37 被 OPI PSRAM 占用，不要外接。
> 默认 GPIO 白名单：`4,5,6,7,8,15,16,17,18,21,38`。

---

## 构建

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
cd /home/alan/myPrj/esp32_mcp
idf.py set-target esp32s3
idf.py build
```

首次烧录（会写入新的双 OTA 分区表；建议先整片擦除，避免旧 otadata 指向旧分区）：

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash monitor
# 或原生 USB：
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

如果提示串口权限不足：

```bash
sudo usermod -aG dialout "$USER"   # 重新登录后生效
# 或临时：
sudo chmod 666 /dev/ttyUSB0
```

---

## 首次配网

1. 上电后连接热点：`ESP32-MCP-XXXX`，默认密码 `esp32mcp`。
2. 浏览器打开 `http://192.168.4.1`（或等待 Captive Portal 自动弹出）。
3. 扫描并选择 Wi-Fi，输入密码，点击“连接”。
4. 成功后页面显示：
   - MCP Endpoint：`http://<ip>:8080/mcp`
   - mDNS 地址：`http://<hostname>.local:8080/mcp`
   - Bearer Token（每台设备独立，存 NVS）
   - 可直接复制的 Hermes / OpenClaw / 通用 JSON / curl 配置块

“MCP 接入信息”卡片会一次性列出全部接入参数，每块都有复制按钮，点“复制全部接入信息”可把 Endpoint、mDNS、Token 和各客户端配置一起拷走。

AP 会在 STA 连接成功 2 分钟后自动关闭。Wi-Fi 凭证和 Token 掉电保存。配网成功页会显示 Token，请及时复制；之后也可从串口日志或重新配网获取。

> 配网页（`192.168.4.1`）和 STA 状态页（设备局域网 IP）是两个不同的浏览器 origin，localStorage 不会共享。切换到 STA 页面后，把 Token 粘贴到“MCP 接入信息 → Bearer Token”输入框（或“高级 → MCP Token”）即可；粘贴后下方所有配置会实时更新，Token 只保存在本浏览器。

长按 BOOT 5 秒会恢复出厂：清除 Wi-Fi 配置**并重新生成 Token**，之后需要重新配网。

### 找回 Token

Token 只在生成时完整打印到配网页面，串口日志仅有后 4 位。若已丢失：

```bash
# 方式一：AP 模式（热点开启时无需鉴权）
curl -s http://192.168.4.1/api/status | python3 -c "import sys,json;print(json.load(sys.stdin)['mcp_token'])"

# 方式二：已知 Token 时
curl -s http://<ip>/api/status -H "Authorization: Bearer <token>"

# 方式三：直接读 NVS 分区（不破坏配置）
esptool.py -p /dev/ttyACM0 read-flash 0x9000 0x6000 /tmp/nvs.bin
python3 $IDF_PATH/components/nvs_flash/nvs_partition_tool/nvs_tool.py \
        -d minimal /tmp/nvs.bin | grep mcp_token
```

> NVS dump 同时包含 Wi-Fi 密码，属于敏感文件，不要外传。


---

## 接入 Hermes

`~/.hermes/config.yaml`：

```yaml
mcp_servers:
  esp32_mcp:
    url: "http://192.168.3.50:8080/mcp"
    headers:
      Authorization: "Bearer <token>"
    tools:
      include:
        - device_info
        - sys_status
        - wifi_status
        - gpio_list
        - gpio_read
        - ota_status
      resources: true
      prompts: false
```

```bash
hermes mcp test esp32_mcp
```

---

## 接入 OpenClaw

必须显式使用 `streamable-http`：

```bash
openclaw mcp set esp32_mcp '{
  "url": "http://192.168.3.50:8080/mcp",
  "transport": "streamable-http",
  "headers": {"Authorization": "Bearer <token>"},
  "connectionTimeoutMs": 5000,
  "requestTimeoutMs": 15000
}'

openclaw mcp probe esp32_mcp --json
openclaw mcp doctor esp32_mcp --probe
```

> `openclaw mcp set/add/configure` 等写操作要求本地 Gateway 已停止；只读的 `status/list/show` 不受影响。

---

## MCP 工具

| 工具 | 说明 |
| --- | --- |
| `device_info` | 芯片、MAC、固件、Flash、运行时间 |
| `sys_status` | 堆、任务数、CPU、复位原因 |
| `wifi_status` | SSID、IP、RSSI、信道、mDNS |
| `gpio_list` | 白名单引脚与电平 |
| `gpio_read` | 读引脚 |
| `gpio_write` | 写引脚（`level`: `high`/`low`，可选 `hold_ms`） |
| `gpio_pulse` | 非阻塞脉冲（10–5000 ms） |
| `led_set` | 板载 WS2812（颜色/亮度） |
| `ota_status` | OTA 状态与进度 |
| `ota_apply` | 从局域网 URL 升级 |
| `ota_abort` | 取消 OTA |

资源：`device://info`、`device://gpio`。

---

## OTA

### 方式一：网页上传

打开 `http://<ip>/`，在“OTA 固件升级”中选择 `build/esp32_mcp.bin`，填写可选 SHA256/版本，上传。

- 写入备用 4 MB 分区；
- 校验镜像与 SHA256；
- 切换启动分区并重启；
- 新固件 30 秒健康检查通过后标记有效；
- 失败/崩溃自动回滚。

### 发布一个可 OTA 的新版本

```bash
# 1. 修改 CMakeLists.txt 里的 PROJECT_VER，例如 0.2.0
sed -i 's/set(PROJECT_VER "0.1.0")/set(PROJECT_VER "0.2.0")/' CMakeLists.txt

# 2. 重新构建
idf.py build

# 3. 启动局域网固件服务
./tools/serve_firmware.sh 0.2.0
```

Agent OTA 只接受与镜像 `esp_app_desc.version` 一致的 `version` 参数，并且默认拒绝同版本/降级。

### 方式二：Agent 触发

在局域网运行静态固件服务：

```bash
./tools/serve_firmware.sh 0.2.0
```

脚本会复制固件、计算 SHA256，并启动 `python3 -m http.server 8000`。

然后让 Agent 调用：

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

设备立即返回 `accepted`，后台下载；用 `ota_status` 轮询。

> 设备侧只允许 `CONFIG_APP_OTA_ALLOWED_URL_PREFIX` 开头的 URL。当前默认按本机
> `192.168.3.110:8000` 配置；如果固件服务器 IP 变化，请修改：

```bash
idf.py menuconfig
# ESP32 MCP Node -> Allowed OTA URL prefix
```

第一版不做 Secure Boot；SHA256 只防损坏，不防恶意替换。固件服务器必须放在可信机器上，且不要暴露到公网。

---

## 目录

```text
esp32_mcp/
├── CMakeLists.txt
├── partitions.csv              # 16 MB 双 OTA 分区
├── sdkconfig.defaults
├── components/mcp-c-sdk/       # 内置并打了两个小补丁（NVS token、默认参数）
├── main/
│   ├── app_main.c
│   ├── wifi_manager.c
│   ├── captive_portal.c
│   ├── mcp_server.c
│   ├── mcp_tools_*.c
│   ├── gpio_guard.c
│   ├── led_status.c
│   ├── ota.c
│   ├── mdns_service.c
│   └── web/index.html
├── tools/
│   ├── serve_firmware.sh
│   └── test_mcp.py
└── docs/DESIGN.md
```

---

## 内置 SDK 补丁

`components/mcp-c-sdk` 是从 `espressif/mcp-c-sdk` v2.0.1 内置的副本，包含两个小补丁：

1. **运行时 Token**：增加弱符号 `esp_mcp_http_get_bearer_token()`，应用侧从 NVS 返回每台设备独立令牌。
2. **默认参数**：`tools/call` 缺少带默认值的参数时，自动使用 schema 默认值（例如 `hold_ms=0`、`brightness=20`）。

另外，内置 HTTP transport 的 `httpd` 在 `esp_mcp_mgr_start()` 中才创建，因此注册顺序是
`init → start → register_endpoint`。

---

## 已知限制

- 第一版不支持 Secure Boot / Flash Encryption。
- OTA 页面和 MCP OTA 都依赖局域网，不支持公网升级。
- 配网热点默认密码 `esp32mcp`，生产环境建议改成每台随机密码。
- `gpio_write` 是物理动作，Agent 接入时建议先用只读工具白名单。
- Windows 默认可能不解析 `.local`，可改用 IP。
