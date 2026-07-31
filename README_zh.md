# BMO 桌面搭子

[English](./README.md) | 简体中文

基于 **涂鸦 T5AI-Core** 开发板 + **微雪 4.2 寸 ST7305 反射式 LCD**（300×400，横屏逻辑分辨率 400×300）的 **BMO 风格桌面 AI 搭子**固件。集成语音对话（涂鸦 AI）、单色 BMO 表情、时钟/天气/日历页面、双舵机手臂和 10 键控制面板。

> **声明：**「BMO」为《探险活宝》角色。本项目为粉丝自制，与 Cartoon Network 及其版权方无关。

## 功能概览

| 模块 | 说明 |
|------|------|
| 屏幕 | ST7305 SPI 反射屏，横屏 400×300 UI |
| 表情 | 13 种 BMO 风格表情，眨眼、眼珠转动、呼吸动画 |
| 页面 | 表情页、时钟、天气、日历（飞书同步） |
| 语音 | 涂鸦 AI 组合模式：唤醒词 + 单击 + 长按对讲 |
| 按键 | 十字键（音量/翻页）、中键刷新、SW1 静音、SW2 系统信息、三角回主页、绿键彩蛋、红键对话 |
| 舵机 | 双臂 SG90/MG90S，动作序列 + MCP 云端控制 |
| 云端 | MCP 工具：切换表情、换页、手臂姿态、播放动作 |

## 硬件

- **主控：** [涂鸦 T5AI-Core](https://developer.tuya.com/cn/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)
- **屏幕：** 微雪 ESP32-S3-RLCD-4.2 同款 ST7305 面板（仅 3.3V）
- **舵机：** 2× SG90/MG90S，左 P18 / 右 P24，外接 5V 供电，与板子共地
- **按键：** 10 键面板 — 见 [docs/bmo-pins.md](./docs/bmo-pins.md)
- **外壳：** 可选 3D 打印件见 [`models/`](./models/)

完整接线：[docs/st7305-wiring.md](./docs/st7305-wiring.md)

## 快速开始

### 1. 安装 TuyaOpen SDK

```bash
git clone https://github.com/tuya/TuyaOpen.git
cd TuyaOpen
# 按官方文档配置 T5AI 工具链和 Python 环境
```

### 2. 安装本应用

将本仓库链接或复制到 SDK 应用目录：

```bash
# Linux / macOS
ln -s /path/to/bmo-desktop-avatar TuyaOpen/apps/tuya.ai/desktop_avatar

# Windows（PowerShell，管理员）
New-Item -ItemType Junction -Path "C:\TuyaOpen\apps\tuya.ai\desktop_avatar" `
  -Target "C:\path\to\bmo-desktop-avatar"
```

### 3. 打显示方向补丁

本外壳安装方式需要将 LVGL 坐标映射翻转 180°（补丁在 SDK 层，不在应用内）：

```bash
cd TuyaOpen
git apply /path/to/bmo-desktop-avatar/patches/lv_port_disp_landscape_180.patch
```

目标文件：`src/liblvgl/v9/port/lv_port_disp_full_frame.c`。若屏幕安装方向相反，可跳过此补丁。

### 4. 配置密钥

先从示例各复制一份。`app_default.config` 和 `config/TUYA_T5AI_CORE.config` 都已加入 `.gitignore`，是仅有的两个允许存放密钥的地方：

```bash
cp app_default.config.example app_default.config
cp config/TUYA_T5AI_CORE.config.example config/TUYA_T5AI_CORE.config
```

`tos.py build` 默认回落到 `app_default.config`，`tos.py config choice` 用的是 `config/` 下的板级模板。按你实际用的那套填：

1. **涂鸦产品 ID：** 设置 `CONFIG_TUYA_PRODUCT_ID` 为 [涂鸦 IoT 平台](https://platform.tuya.com/) 上的产品 ID。
2. **Open SDK 授权：** 设置 `CONFIG_TUYA_OPENSDK_UUID` 和 `CONFIG_TUYA_OPENSDK_AUTHKEY`，授权码在[涂鸦采购页](https://platform.tuya.com/purchase/index?type=6)领取。也可以两项都留空，改用 `tos.py auth` 把授权码写进设备 flash——固件优先读取 flash 里的授权，这样编译期完全不接触密钥。
3. **飞书日历（可选）：** 编辑 `src/ui/feishu_cal.h` 中的 `FEISHU_APP_ID` / `FEISHU_APP_SECRET`。

切勿把授权码写进任何 `*.example` 文件或 `include/tuya_config.h`，它们都会被 git 跟踪。

### 5. 编译烧录

```bash
cd TuyaOpen/apps/tuya.ai/desktop_avatar
tos.py config choice    # 选择 TUYA_T5AI_CORE
tos.py build
tos.py flash -p COM6    # 下载口（本板日志口为 COM8）
tos.py monitor -p COM8
```

**配置陷阱：** `tos.py build` 读的是 `.build/cache/using.config`，不是 `config/` 里的模板。改完 Kconfig 后务必重新 `tos.py config choice`，或核对：

```powershell
rg SERVO_RIGHT_PWM .build\include\tuya_kconfig.h
```

启动日志应显示 `[servo] dual arm init ok (L=PWM0/P18 R=PWM1/P24, positive, duty=750)`。

## 目录结构

```
├── config/           板级 Kconfig 模板
├── docs/             接线、引脚、AI 提示词
├── include/          应用头文件
├── models/           3D 打印外壳 (.3mf)
├── patches/          TuyaOpen SDK 补丁（屏幕旋转）
├── src/
│   ├── ui/           BMO 脸、页面、按键、天气、日历
│   ├── motion/       舵机 PWM、动作引擎、MCP、诊断工具
│   └── display2/     旧版聊天 UI 资源（头像模式未使用）
├── CMakeLists.txt
├── Kconfig
└── LICENSE           Apache 2.0
```

## 文档索引

| 文件 | 内容 |
|------|------|
| [docs/bmo-pins.md](./docs/bmo-pins.md) | 引脚表、PWM 通道映射、配置生效检查 |
| [docs/st7305-wiring.md](./docs/st7305-wiring.md) | ST7305 与 T5AI-Core 接线 |
| [docs/agent-system-prompt.md](./docs/agent-system-prompt.md) | 云端 AI Agent 工具调用提示词 |

## 舵机排查

开启内置诊断：在 config 中加 `CONFIG_ENABLE_SERVO_TEST=y`，然后 `tos.py config choice` 并重新编译。详见 [docs/bmo-pins.md](./docs/bmo-pins.md#舵机诊断工具)。

本项目已修复的常见问题：

- 右臂 PWM 必须是 `SERVO_RIGHT_PWM=1`（P24），**不是** 4（P36）
- 极性必须是 `TUYA_PWM_POSITIVE`（负极性会把脉宽反相）
- 初始 duty 不能为 0（BK 驱动在 duty=0 时会进入 flip mode 且无法恢复）

## 开源协议

[Apache License 2.0](./LICENSE) — 与 [TuyaOpen](https://github.com/tuya/TuyaOpen) 一致。第三方依赖与商标说明见 [NOTICE](./NOTICE)。

## 贡献

欢迎 Issue 和 PR。请勿提交产品 ID、UUID/AuthKey 或飞书密钥。
