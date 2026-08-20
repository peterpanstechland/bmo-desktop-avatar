# ST7305 漫反射屏 与 T5AI-Core 接线指南

## 屏幕信息

- 驱动 IC：ST7305（单色反射式 LCD）
- 面板：与微雪 ESP32-S3-RLCD-4.2 同款 4.2 寸面板（已确认）
- 分辨率：300 x 400（1-bit 黑白），列地址窗口 0x12–0x2A，行地址窗口 0x00–0xC7
- 接口：SPI（屏幕标注 SCL/SDA 实际为 SPI 时钟和数据）
- 供电：3.3V（禁止接 5V）
- 无背光，环境光越亮画面越清晰

## 引脚定义（屏幕侧）

| PIN | 丝印 | 功能 |
|-----|-------|------|
| 1 | GND | 电源接地 |
| 2 | VCC | 电源正极（3.3V） |
| 3 | SCL | SPI 时钟（SCK） |
| 4 | SDA | SPI 数据（MOSI） |
| 5 | RST | 显示屏复位 |
| 6 | DC | 数据/命令选择 |
| 7 | CS | 片选信号 |
| 8 | TE | 帧同步（第一版悬空） |

## 接线表

**操作前务必断开 USB 电源！**

| ST7305 屏幕 | T5AI-Core 引脚 | TuyaOpen 软件定义 | 说明 |
|-------------|---------------|------------------|------|
| GND | GND | - | 先接地线 |
| VCC | 3.3V | - | 绝不可接 5V |
| SCL | P14 | TUYA_SPI0_CLK / TUYA_SPI_NUM_0 | SPI0 时钟 |
| SDA | P16 | TUYA_SPI0_MOSI / TUYA_SPI_NUM_0 | SPI0 数据输出 |
| RST | P6 | TUYA_GPIO_NUM_6 | 复位脚（低有效） |
| DC | P17 | TUYA_GPIO_NUM_17 | 高=数据 / 低=命令 |
| CS | P15 | TUYA_SPI0_CS / TUYA_SPI_NUM_0 | 片选（低有效） |
| TE | 悬空 | - | 帧同步，第一版不接 |

## 接线示意

```
ST7305 屏幕          T5AI-Core 开发板
 ┌──────────┐        ┌──────────────┐
 │ GND  (1) │───────▶│ GND          │
 │ VCC  (2) │───────▶│ 3.3V         │
 │ SCL  (3) │───────▶│ P14 (SCK)    │
 │ SDA  (4) │───────▶│ P16 (MOSI)   │
 │ RST  (5) │───────▶│ P6           │
 │ DC   (6) │───────▶│ P17          │
 │ CS   (7) │───────▶│ P15 (CS)     │
 │ TE   (8) │  悬空  │              │
 └──────────┘        └──────────────┘
```

## 注意事项

1. **电压**：一定用 3.3V，T5AI-Core 的 GPIO 不耐 5V。
2. **杜邦线**：尽量短（10 cm 以内），减少 SPI 信号干扰。
3. **SPI 频率**：首次调试先用 1-10 MHz，调通后再逐步提高到 48 MHz。
4. **接线顺序**：先接 GND，再接其他线，最后接 VCC。
5. **TE 引脚**：仅用于同步刷新，第一版不需要，悬空即可。
6. **反射屏特性**：没有背光，靠环境光反射显示；室内灯光下即可看清，阳光下效果最好。

## 软件配置（板级代码中的宏定义）

对应 TuyaOpen 板级驱动中需要配置的参数：

```c
#define BOARD_LCD_BL_TYPE        TUYA_DISP_BL_TP_NONE   // 无背光
#define BOARD_LCD_WIDTH          300                      // 源极方向（竖屏宽）
#define BOARD_LCD_HEIGHT         400                      // 栅极方向（竖屏高）
#define BOARD_LCD_X_OFFSET       0x12                     // 列窗口起点（微雪面板固定 0x12）
#define BOARD_LCD_Y_OFFSET       0                        // 行窗口起点
#define BOARD_LCD_ROTATION       TUYA_DISPLAY_ROTATION_0  // 按需调整

#define BOARD_LCD_SPI_PORT       TUYA_SPI_NUM_0
#define BOARD_LCD_SPI_CLK        10000000                 // 10 MHz（官方例程同值）
#define BOARD_LCD_SPI_CS_PIN     TUYA_GPIO_NUM_15
#define BOARD_LCD_SPI_DC_PIN     TUYA_GPIO_NUM_17
#define BOARD_LCD_SPI_RST_PIN    TUYA_GPIO_NUM_6

#define BOARD_LCD_POWER_PIN      TUYA_GPIO_NUM_MAX        // 无独立电源控制
```

注册驱动（板级文件已改好，此处仅说明）：

```c
#include "tdd_disp_st7305.h"

// 板级文件中已定义 sg_st7305_ws42_init_seq（微雪 4.2 寸参考驱动的初始化序列），
// 注册前替换掉内置的 T5AI Pocket 168x384 序列：
tdd_disp_spi_mono_st7305_set_init_seq(sg_st7305_ws42_init_seq);
TUYA_CALL_ERR_RETURN(tdd_disp_spi_mono_st7305_register(DISPLAY_NAME, &display_cfg));
```

微雪序列与内置序列的主要差异：电源电压组（0xC0–0xC5）、帧率（0xB2）、栅极 EQ（0xB3）、
NVM 载入（0xD6）。反显位保留 0x20（关）：TuyaOpen 约定位 1=黑，微雪驱动是 0x21（开）+位 1=白
（官方 `ColorWhite=0xFF`），两者光学效果一致；若实际黑白颠倒，把序列里的 0x20 改成 0x21 即可。

以上序列已与微雪官方仓库
[`display_bsp.cpp`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/main/02_Example/Arduino/09_LVGL_V9_Test/display_bsp.cpp)
逐字节核对一致（2026-07-23）；像素打包公式 `bit = 7 - (x%4*2 + y%2)` 也与官方竖屏模式相同。
官方例程 SPI 时钟为 **10 MHz**，点屏成功后可把 `BOARD_LCD_SPI_CLK` 提到该值。

## 帧缓冲

300 x 400 的 1-bit 单色帧缓冲 = 300 * 400 / 8 = 15,000 字节（约 15 KB），非常节省内存。

## 点屏验证（黑白闪烁测试）

板级代码已接入 ST7305 驱动并载入微雪 4.2 寸初始化序列
（`C:\TuyaOpen\boards\T5AI\TUYA_T5AI_CORE\tuya_t5ai_core.c`）。
`fill_color` 示例已改为黑白交替（随机颜色在单色屏上几乎不变化），屏幕应每秒黑白切换一次。

### 1. 接线完成后上电

按上文接线表接好，再插 USB。

### 2. 激活环境并编译

```powershell
Set-ExecutionPolicy -Scope Process Bypass
Set-Location C:\TuyaOpen
. .\export.ps1

Set-Location C:\TuyaOpen\examples\peripherals\display\fill_color
tos.py config choice -c TUYA_T5AI_CORE.config
tos.py build
```

看到 `BUILD SUCCESS` 即可。

### 3. 烧录

T5AI-Core 插上电脑后通常出现两个串口：

- 编号 **A**：下载口（烧录用）
- 编号 **B**：日志口（460800 波特率）

```powershell
tos.py flash
```

按提示选择下载口（A）。烧录完成后复位板子，屏幕应开始黑白交替刷新。

### 4. 首次点屏问题回顾（已修复）

第一次烧录后屏幕出现「约 1/3 均匀灰 + 2/3 雪花噪点」，原因有三，均已修复（2026-07-23）：

1. **列窗口偏移错误**：微雪面板列地址从 0x12 开始，此前写 0 导致大部分数据写到了
   可视区外，只有约 7/25 的列落在屏内（对应照片里那 1/3 均匀区）；没被写到的区域
   保持上电随机内容（雪花区）。已把 `BOARD_LCD_X_OFFSET` 改为 `0x12`。
2. **初始化序列不匹配**：内置序列面向 T5AI Pocket 168×384 面板，电压/帧率/EQ 参数
   与 4.2 寸面板不同。已在板级文件加入微雪参考驱动的完整序列。
3. **驱动字节对齐 bug**：TuyaOpen 的 ST7305 转换函数按 `width/8` 计算行距，300 不是
   8 的倍数（300/8=37.5），逐行累积错位。已改为向上取整（38 字节/行），并同步修复
   `tdl_display_draw.c` 中单色画点的同类问题。

修完上述三项后仍剩「顶部约 37.5% 正常黑白交替、下方仍是噪点」，根因是第四个 bug：

4. **SPI 发送信号量超时截断**：`tdd_display_spi.c` 的 `__disp_spi_send` 一次 DMA 发完
   整帧后等待完成信号量，超时仅 100ms；1 MHz 时钟下一帧 15000 字节需 120ms，超时后
   代码提前拉高 CS 截断本帧，且信号量从此错位，后续每帧只有开头一小段有效。
   已把超时加大到 1000ms，并把 SPI 时钟提到 10 MHz（官方例程同值，一帧仅 12ms）。

### 5. 仍异常时的排查

1. 再核对接线（尤其 3.3V、SCL=P14、SDA=P16、CS=P15、DC=P17、RST=P6）。
2. 黑白颠倒：把板级初始化序列中的 `0x20`（反显关）改成 `0x21`。
3. 当前 SPI 为 10 MHz（官方例程同值）；若杜邦线过长出现花屏，把 `BOARD_LCD_SPI_CLK`
   降到 4–8 MHz 试试。
4. 查日志口（波特率 460800）是否出现 `spi tx wait timeout`：出现说明单帧传输时间
   超过了 `__disp_spi_send` 的信号量超时，需降低分辨率带宽或加大超时。

## BMO 按键与舵机（横屏后）

详见 [bmo-pins.md](bmo-pins.md)。屏幕物理竖装、逻辑横屏 400×300（`CONFIG_UI_LANDSCAPE=y`）。

首次横装可在 `menuconfig` 打开 `UI_LANDSCAPE_TEST`，上电 2.5 秒显示四角数字 1–4 确认旋转方向；方向反了则在 `lv_port_disp_full_frame.c` 换映射公式。

| 功能 | GPIO |
|------|------|
| D-pad 上/下/左/右 | P4 / P5 / P7 / P8 |
| D-pad 中键（刷新数据） | P22 |
| 模块附加键 1/2（静音 / 系统信息） | P23 / P25 |
| 红圆（对话） | P12 |
| 蓝三角（返回 / 退出游戏） | P26 |
| 绿圆（刷新/挥手） | P19 |
| 左/右臂舵机 | P18(TUYA_PWM_NUM_0) / P24(TUYA_PWM_NUM_1) |

EC11 编码器已移除；翻页与音量改由十字键承担。

舵机不动时先看启动日志的 `[servo] dual arm init ok (L=PWM0 R=PWM1)`，通道号不对就是配置没生效而不是硬件问题，排查步骤见 `bmo-pins.md`。

## 参考资料

- [微雪官方仓库 ESP32-S3-RLCD-4.2](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)（同款面板：Arduino/ESP-IDF/ESPHome/小智示例，`display_bsp.cpp` 为权威初始化参考）
- [TuyaOpen 显示驱动文档](https://tuyaopen.ai/docs/peripheral/display)
- [T5AI 外设引脚映射](https://tuyaopen.ai/docs/hardware/tuya-t5/t5ai-peripheral-mapping)
- [T5AI-Core Chatbot 接线参考](https://developer.tuya.com/en/docs/developer/t5-chatbot?id=Kevq9zjcak848)
- [ESPHome ST7305 RLCD 参考](https://github.com/kylehase/ESPHome-ST7305-RLCD)
- [Waveshare RLCD 参考配置](https://docs.waveshare.net/ESP32-ESHome-Tutorials/Example-RLCD-Voice/)
- [官方烧录说明](https://tuyaopen.ai/zh/docs/quick-start/firmware-burning)
