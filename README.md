# BMO Desktop Avatar

English | [简体中文](./README_zh.md)

A fan-made **BMO-style desktop companion** firmware for the **Tuya T5AI-Core** board with a **Waveshare 4.2" ST7305 reflective LCD** (300×400, landscape UI 400×300). It combines voice AI chat (Tuya AI), a monochrome BMO face, weather/calendar pages, dual servo arms, and a 10-button control panel.

> **Disclaimer:** "BMO" is a character from *Adventure Time*. This is an independent fan project, not affiliated with Cartoon Network or its rights holders.

## Features

| Area | Details |
|------|---------|
| Display | ST7305 SPI reflective panel, landscape 400×300 logical UI |
| Face | 13 BMO-style expressions with blink, eye dart, breathing |
| Pages | Avatar, split-flap clock, weather, calendar (Feishu sync), games (Snake, Tetris) |
| Voice | Tuya AI combo mode — wake word, click, long-press PTT |
| Buttons | D-pad (volume / page nav), centre refresh, SW1 mute, SW2 sysinfo (hold 5 s to re-provision WiFi), triangle back, green easter-egg, red talk |
| Arms | Dual SG90/MG90S servos with motion sequences + MCP cloud tools |
| Cloud | MCP tools for expression, page switch, arm pose, motion play, adding calendar events by voice |

## Hardware

- **MCU board:** [Tuya T5AI-Core](https://developer.tuya.com/en/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)
- **Display:** Waveshare ESP32-S3-RLCD-4.2 class ST7305 panel (3.3 V only)
- **Servos:** 2× SG90/MG90S on P18 (left) and P24 (right), **dedicated 5 V ≥2 A supply**, shared GND (powering them from the board's 5 V pin browns out when both arms move at high volume)
- **Buttons:** 10-key panel — see [docs/bmo-pins.md](./docs/bmo-pins.md)
- **3D enclosure:** STL/3MF parts in [`models/`](./models/) (optional)

Full wiring: [docs/st7305-wiring.md](./docs/st7305-wiring.md)

## Quick start

### 1. Install TuyaOpen

Clone and prepare the SDK (see [TuyaOpen docs](https://github.com/tuya/TuyaOpen)):

```bash
git clone https://github.com/tuya/TuyaOpen.git
cd TuyaOpen
# follow platform setup for T5AI (toolchain, Python venv, etc.)
```

### 2. Install this app

Copy or symlink this repository into the SDK apps tree:

```bash
# Linux / macOS
ln -s /path/to/bmo-desktop-avatar TuyaOpen/apps/tuya.ai/desktop_avatar

# Windows (PowerShell, admin)
New-Item -ItemType Junction -Path "C:\TuyaOpen\apps\tuya.ai\desktop_avatar" `
  -Target "C:\path\to\bmo-desktop-avatar"
```

Or copy the folder contents directly to `TuyaOpen/apps/tuya.ai/desktop_avatar/`.

### 3. Apply the SDK patches

All three patches touch the SDK rather than this app, so they need reapplying after an SDK reinstall:

```bash
cd TuyaOpen
git apply /path/to/bmo-desktop-avatar/patches/lv_port_disp_landscape_180.patch
git apply /path/to/bmo-desktop-avatar/patches/ai_chat_button_long_press.patch
git apply /path/to/bmo-desktop-avatar/patches/tdl_button_double_click.patch
```

- `lv_port_disp_landscape_180.patch` — flips the software coordinate map 180° in `src/liblvgl/v9/port/lv_port_disp_full_frame.c` to match this enclosure. If your panel is mounted the other way, skip it or revert to `px = 299 - ly; py = lx;`.
- `ai_chat_button_long_press.patch` — raises the talk button's long-press threshold from 400 ms to 700 ms. Below that a normal press registers as push-to-talk and single/double click never fire.
- `tdl_button_double_click.patch` — fixes a missing counter reset in the `tdl_button` state machine. Without it `TDL_BUTTON_PRESS_DOUBLE_CLICK` never fires for any button, so double-click mode switching does nothing. See [docs/bmo-pins.md](./docs/bmo-pins.md) for the analysis.

### 4. Configure credentials

Start from the examples. `app_default.config` and `config/TUYA_T5AI_CORE.config` are gitignored, so they are the only places credentials may live:

```bash
cp app_default.config.example app_default.config
cp config/TUYA_T5AI_CORE.config.example config/TUYA_T5AI_CORE.config
```

`tos.py build` falls back to `app_default.config`; `tos.py config choice` uses the board template in `config/`. Fill in whichever one your workflow uses:

1. **Tuya product:** set `CONFIG_TUYA_PRODUCT_ID` to your product ID from [Tuya IoT Platform](https://platform.tuya.com/).
2. **Open SDK license:** set `CONFIG_TUYA_OPENSDK_UUID` and `CONFIG_TUYA_OPENSDK_AUTHKEY` to the license from [Tuya's purchase page](https://platform.tuya.com/purchase/index?type=6). Alternatively leave both empty and flash the license onto the device with `tos.py auth` — the firmware prefers the flashed license and never needs it at build time.
3. **Feishu calendar (optional):** export `FEISHU_APP_ID` and `FEISHU_APP_SECRET` before building so no secret ends up in a tracked file. Grant the app access to a calendar — see [docs/feishu-calendar.md](./docs/feishu-calendar.md).

Never put a license in a `*.example` file or in `include/tuya_config.h`: those are tracked by git.

### 5. Build and flash

```bash
cd TuyaOpen/apps/tuya.ai/desktop_avatar
tos.py config choice    # select TUYA_T5AI_CORE
tos.py build
tos.py flash -p COM6    # download port (COM8 = serial log on this board)
tos.py monitor -p COM8
```

**Config trap:** `tos.py build` reads `.build/cache/using.config`, not the template in `config/` directly. Change Kconfig values in **both** `app_default.config` and `config/TUYA_T5AI_CORE.config` — the cache is regenerated from the former, so a stale value there comes back. Then run `tos.py config choice` again or verify:

```bash
rg SERVO_RIGHT_PWM .build/include/tuya_kconfig.h
```

Boot log should show `[servo] dual arm init ok (L=PWM0/P18 R=PWM1/P24, positive, duty=750)`.

## Project layout

```
├── config/           Board Kconfig templates (TUYA_T5AI_CORE.config = this project)
├── docs/             Wiring, pin map, AI agent prompt
├── include/          App headers
├── models/           3D-printable enclosure parts (.3mf)
├── patches/          TuyaOpen SDK patches (display rotation, button timing)
├── src/
│   ├── ui/           BMO face, pages, buttons, weather, calendar
│   ├── motion/       Servo PWM, motion engine, MCP, diagnostic tool
│   └── display2/     Legacy chat UI assets (unused in avatar mode)
├── CMakeLists.txt
├── Kconfig
└── LICENSE           Apache 2.0
```

## Documentation

| File | Contents |
|------|----------|
| [docs/bmo-pins.md](./docs/bmo-pins.md) | GPIO pin map, PWM channel mapping, config pitfalls |
| [docs/st7305-wiring.md](./docs/st7305-wiring.md) | ST7305 ↔ T5AI-Core wiring |
| [docs/agent-system-prompt.md](./docs/agent-system-prompt.md) | Cloud AI agent tool-calling prompt |
| [docs/feishu-calendar.md](./docs/feishu-calendar.md) | Feishu app setup, calendar sharing, troubleshooting |

## Servo troubleshooting

Enable the built-in diagnostic (`CONFIG_ENABLE_SERVO_TEST=y` in config, then `tos.py config choice` + rebuild). See [docs/bmo-pins.md](./docs/bmo-pins.md#舵机诊断工具).

Common fixes applied in this project:

- Right arm PWM must be `SERVO_RIGHT_PWM=1` (P24), **not** 4 (P36)
- Polarity must be `TUYA_PWM_POSITIVE` (negative inverts the pulse)
- Init duty must be non-zero (BK driver flip-mode bug at duty=0)
- Brownout resets: servos need their own supply. The firmware also interpolates between motion keyframes (a step command makes both servos draw stall current at the same instant) and caps volume via `CONFIG_BMO_MAX_VOLUME` (default 85)

## License

[Apache License 2.0](./LICENSE) — same as [TuyaOpen](https://github.com/tuya/TuyaOpen). See [NOTICE](./NOTICE) for third-party and trademark notes.

## Contributing

Issues and pull requests welcome. Please do not commit product IDs, UUID/authkey pairs, or Feishu secrets.
