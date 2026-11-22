# ESP32-S3 语音助手

> 一块 ¥104 的开发板，自己做的一台能听、能想、能说的小设备。
>
> 说话 → 本地 ASR 转文字 → 大模型回答 → TTS 合成语音 → 喇叭播出来。
>
> 全程 7–8 秒，硬件和固件全部自己调试。

---

## 它能干什么

```
你：这个...快吓我一跳
它：怎么啦，什么东西吓到你了？          ← 从喇叭里说出来的
```

- **听**：INMP441 数字麦克风，I2S 采集 3 秒
- **懂**：本地 faster-whisper 把语音转成文字
- **想**：DeepSeek 大模型生成回答
- **说**：Edge TTS 合成语音，MAX98357 功放推喇叭
- **看**：1.28" 圆屏显示状态（白=在听，绿=在说）

## 硬件

| 器件 | 型号 | 价格 |
|---|---|---|
| 主控 | ESP32-S3 DevKit N16R8（16MB Flash / 8MB Octal PSRAM） | ¥32.18 |
| 屏幕 | 1.28" 圆屏 GC9A01 240×240 SPI | ¥20.30 |
| 麦克风 | INMP441 I2S 数字麦克风 | ¥9.50 |
| 功放 | MAX98357A I2S 数字功放 | ¥8.80 |
| 喇叭 | 8Ω 3W | ¥12.16 |
| 杜邦线 / 面包板 / 数据线 | — | ¥21.27 |
| **合计** | | **¥104.21** |

## 接线

### 屏幕 GC9A01（SPI）

| 屏引脚 | ESP32-S3 |
|---|---|
| VCC | **3V3**（接 5V 会烧屏） |
| GND | GND |
| SCL | GPIO12 |
| SDA | GPIO11 |
| DC | GPIO9 |
| CS | GPIO10 |
| RST | GPIO14 |

### 麦克风 INMP441（I2S RX）

| 麦引脚 | ESP32-S3 |
|---|---|
| VDD | 3V3 |
| GND | GND |
| **L/R** | **GND（必须接地，悬空会录出全 0）** |
| SCK | GPIO4 |
| WS | GPIO5 |
| SD | GPIO6 |

### 功放 MAX98357（I2S TX）

| 功放引脚 | 接到 |
|---|---|
| VIN | **5V 直接从板子引，不要走面包板电源轨** |
| GND | GND |
| **SD** | **3V3（悬空 = 静音）** |
| BCLK | GPIO15 |
| LRC | GPIO16 |
| DIN | GPIO17 |
| `+` / `−` | 喇叭（**BTL 输出，两端都不能接地**） |
| GAIN | 悬空 = 9dB |

> ⚠️ **三个会烧板子的点**：
> 1. 屏是 3.3V，接 5V 立刻烧
> 2. 功放是 BTL 桥接输出，`+`/`−` 任一端接 GND 就短路烧输出级
> 3. INMP441 的 L/R 悬空会录出全 0 或乱码

## 软件架构

```
┌────────────── ESP32-S3 ──────────────┐
│                                      │
│  INMP441 ─I2S_NUM_1 RX→ 录音 3 秒     │
│              8000Hz / GAIN 30         │
│              100Hz 高通 / 丢前 0.5s   │
│                    ↓                 │
│            POST :8080/asr ───────────┼──→ 电脑 asr_server.py
│                    ←── 文字 ──────────┤     faster-whisper small
│                    ↓                 │     1.5 秒
│            HTTPS api.deepseek.com ───┼──→ deepseek-chat
│                    ←── 回答 ──────────┤     4 秒
│                    ↓                 │
│            POST :8081/tts ───────────┼──→ 电脑 tts_server.py
│                    ←── wav ──────────┤     Edge TTS 晓晓
│                    ↓                 │     3 秒
│  MAX98357 ←I2S_NUM_0 TX─ 22050Hz     │
│                                      │
│  GC9A01 屏幕：白=听 / 绿=说           │
└──────────────────────────────────────┘
```

### 两种接入方式

项目里有两条跑通的链路，二选一：

**A. 直连大模型（`asrtest/`）** —— 自己调 ASR / 大模型 / TTS 三个接口，适合学原理，每一段都看得见。

**B. 接 AI 角色（`terminal/`）** —— 板子只负责录音和播放，说什么由电脑上的角色服务决定（HTTP 3102）。适合做「有性格的实体终端」。

```
语音 → ESP32 → POST /utter?token=xxx → 电脑角色服务
                                        ↓
                              大模型 + 人设 + 分层记忆
                                        ↓
    喇叭 ← ESP32 ← WAV ← GET /say?token=xxx&wait=15000
```

`terminal/` 这版**不依赖任何第三方库**（只用 WiFi + WiFiClient + I2S）—— 因为库管理器里的 WebSockets / ArduinoJson 在国内经常拉不下来。
## 项目结构

```
arduino/
├── asrtest/              ← 完整语音助手（主项目）
│   └── asrtest.ino       录音 → ASR → 大模型 → TTS → 播放
├── plantest/             ← 屏幕 + LittleFS 音频播放器
│   ├── plantest.ino
│   ├── image.h           （生成物，见下）
│   └── audio.h           （生成物，见下）
├── terminal/             ← ★ 接 AI 角色的终端版（HTTP 3102）
│   ├── terminal.ino      录音 → 上传 → 取音频 → 播放 + 圆屏显示
│   ├── config.h          （不入库）WiFi / IP / 令牌，模板见 config.example.h
│   ├── make_images.py    角色图 → 240×240 RGB565 → images.h
│   └── pics/             images.h + 原始 jpg
├── mictest/              ← 麦克风单独调试
├── aitest/               ← 大模型 API 单独调试
├── wifitest/             ← WiFi 连接调试
├── idle/                 ← 待机程序
│
├── asr_server.py         ← 电脑端 ASR 服务（:8080）
├── tts_server.py         ← 电脑端 TTS 服务（:8081，Edge TTS）
│
├── img2h.ps1             ← 图片 → RGB565 数组
├── text2h.ps1            ← 文字 → 渲染成图 → RGB565 数组
├── wav2h.py              ← WAV → int16 数组
├── pick_audio.ps1        ← 弹窗选音频 → 转 22050 mono → audio.h
├── song2esp.ps1          ← 选歌 → LittleFS 镜像 → 烧录
├── pick.ps1              ← 弹窗选图 → image.h
└── 换图.bat / 换音频.bat / 换歌.bat / 文字.bat
```

## 快速开始

### 1. 环境

```powershell
# Arduino IDE 2.3.11+
# 开发板包：esp32 by Espressif Systems 3.2.1-cn（国内镜像）
# 附加开发板管理器网址：
#   https://arduino.me/packages/esp32.json
```

**开发板设置（一项都不能错）**：

| 项 | 值 |
|---|---|
| Board | ESP32S3 Dev Module |
| **USB CDC On Boot** | **Disabled**（用 CH343 右口时） |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3MB APP/12.8MB LittleFS) |
| **PSRAM** | **OPI PSRAM** |
| Upload Speed | 921600 |

### 2. 电脑端服务

```powershell
# 装依赖
pip install faster-whisper edge-tts opencc-python-reimplemented -i https://mirrors.aliyun.com/pypi/simple/
pip install imageio-ffmpeg matplotlib -i https://mirrors.aliyun.com/pypi/simple/

# 起 ASR（首次会下 small 模型，约 480MB）
$env:HF_ENDPOINT = "https://hf-mirror.com"
python asr_server.py       # :8080

# 起 TTS
python tts_server.py       # :8081
```

### 3. 配置（必做，否则编译报 secrets.h: No such file）

`ash
# 每份配置都有对应模板，复制后填自己的值（真文件已在 .gitignore 里）
cp terminal/config.example.h   terminal/config.h
cp asrtest/secrets.example.h   asrtest/secrets.h
cp aitest/secrets.example.h    aitest/secrets.h
cp wifitest/secrets.example.h  wifitest/secrets.h
`

需要填的项：

| 文件 | 变量 | 说明 |
|---|---|---|
| luefish/config.h | WIFI_SSID / WIFI_PASS | 2.4GHz 热点（5GHz 连不上） |
| luefish/config.h | SERVER_HOST | 电脑在同一局域网内的 IP |
| luefish/config.h | DEVICE_TOKEN | 自定字符串，与电脑端 config.json 里的一致 |
| srtest/secrets.h 等 | SSID / PASS | 同上 |
| srtest/secrets.h 等 | DEEPSEEK_API_KEY | https://platform.deepseek.com 申请 |

### 4. 烧固件


```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_little12M,PSRAM=opi,UploadSpeed=921600" --upload -p COM10 asrtest
```

### 5. 生成的资源文件

`image.h` 和 `audio.h` 不在仓库里（生成物），需要自己生成：

```powershell
# 换屏幕图片（输出 plantest\image.h）
.\换图.bat

# 换播放音频（输出 plantest\audio.h）
.\换音频.bat
```

## 关键参数（已被人声验证，改动需重新测试）

```cpp
采样率     8000 Hz
I2S        32bit STEREO，取左声道（L/R 接 GND）
增益       GAIN 30
高通       HP_ALPHA 0.96f（约 100Hz）
丢弃       前 0.5 秒（I2S 启动瞬态）
判据       rms 3400~6700 / 108Hz 基频 + 211Hz 二次谐波
```

## 踩过的坑

**19 个坑的完整记录在 [notes/ESP32-语音助手闭环-20261121.md](../notes/ESP32-语音助手闭环-20261121.md)**，每条都写了「现象 / 原因 / 修法 / 判据」。

三个最值钱的：

**① I2S MONO 模式下 BCLK 只有一半**
`I2S_SLOT_MODE_MONO` + 32bit + 16kHz 只给 32×fs = 512kHz，而 INMP441 是标准 I2S 从机，需要 64×fs = 1024kHz。时钟少一半 → 时序错位 → SD 上读到固定值 `0x5A82`（= 23170 = 32767÷√2，看着像满幅正弦，其实是残片）。

**② 原生 USB 口没有 DTR/RTS → RST/BOOT 电路**
板子左口是 Espressif 原生 USB（`VID_303A`），`Hard resetting via RTS pin` 是假的，复位不生效；且 PID 会在 `4001`（用户程序）和 `1001`（下载模式）之间跳，Windows 每次都分配新 COM 号。

**③ HTTP 响应头后面是二进制时，不能用行读取**
`readStringUntil('\n')` 会把 WAV 数据也吞掉，报 `[tts] no data chunk`。必须逐字节读，手动匹配 `\r\n\r\n`。

2026-11-22 加的另外三个：

**④ 上电一直"bibi"：自检音残留在 I2S DMA 环形缓冲**
setup 里放过一段 1kHz 自检音确认喇叭通路，播完后波形还留在 DMA 缓冲里被反复重播。**喇叭是环形播放，不喂新数据就一直放旧数据**。修法：删掉自检音，并且**任何不播放音频的时刻都要喂静音**——原来的静音填充条件是 `!recording`，录音期间不喂，DMA 空转又开始重播。

**⑤ 上传 `write()` 返回 0 被当成断线**
`WiFiClient::write()` 在发送缓冲区满时**天然返回 0**（背压，不是错误），原代码 `if (w == 0) { 报错; net.stop(); return; }` 直接放弃，表现为"录了音她没反应"。修法：`w == 0` 时 `delay()` 重试，靠 `net.connected()` 判断真断线；分块从 4096 降到 1024、每块 `delay(1)` 后一次通过（日志 `[net] 上传完成 227072 字节 744ms`）。

**⑥ 播放完尾部 100ms 残音被循环重播成"嗡"声**
`[喇叭] 放完` 时 DMA 里还剩约 100ms 数据，loop 立刻切静音 → 那 100ms 被反复重播。修法：放完后连写 6 次静音（约 96ms）冲刷干净。

## 已知问题

- [ ] TTS 声音还可优化（换音色 / 调语速）
- [ ] **音色依赖云服务**：复刻音色走 CosyVoice（付费），账户欠费直接 418，会降级到预置音色
- [ ] IP 写死 `SERVER_HOST`（默认 `192.168.1.100`），换网络要改
- [ ] 电脑端服务需手动启动，重启电脑要重新拉
- [ ] 无唤醒词，要按 BOOT 键
- [ ] 不能打断（说话时听不见）
- [ ] 裸板 + 面包板，没有外壳，必须插 USB 供电

## 作者

张正 — 黄河科技学院 电子信息工程 2025 级

2026-11-19 ~ 2026-11-21，三天，从插第一根线到语音对话闭环。