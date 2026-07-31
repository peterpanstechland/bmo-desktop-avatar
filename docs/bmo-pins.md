# BMO 桌面搭子引脚表

T5AI-Core + ST7305 横屏 + 10 键 + 双舵机。经核对，下列 GPIO 与 LCD(SPI0 P14-P17/P6)、LED(P9)、喇叭使能(P39) 无冲突。

| 功能 | GPIO | 说明 |
|------|------|------|
| 十字上 | P4 | 音量 +10 |
| 十字下 | P5 | 音量 -10 |
| 十字左 | P7 | 上一页 |
| 十字右 | P8 | 下一页 |
| 十字中 | P22 | 重新拉取天气 + 飞书日历 |
| 模块附加键 1 | P23 | 静音 / 恢复音量 |
| 模块附加键 2 | P25 | 系统信息浮层（IP / 信号 / 内存 / 版本），再按一次关闭，10 秒自动消失 |
| 红大圆（对话） | P12 | `ai_chat_button`：单击唤醒/打断，长按对讲，双击切模式 |
| 蓝三角 | P26 | 回到表情页 |
| 绿小圆 | P19 | 刷新/彩蛋动作 |
| 左臂舵机 | P18 | `TUYA_PWM_NUM_0`（硬件 PWM0），50Hz |
| 右臂舵机 | P24 | `TUYA_PWM_NUM_1`（硬件 PWM4，**不是** NUM_4），50Hz |

**勿用引脚**：P13（板级 LCD / SD 的 LDO 控制），P0/P1（日志串口），P10/P11（UART0，驱动层已屏蔽）。P5 在 BK7258 SDK 里默认还是音频 PA 控制脚（`AUD_DAC_PA_CTRL_GPIO`），本板喇叭使能实际走 P39，所以可以用，但若以后音量键出现异常先怀疑这里。

**按键接线**：COM/另一脚接 **GND** 或 **3.3V** 均可，方向脚接对应 GPIO，不需要外接电阻。

固件不使用固定的有效电平，而是每 40ms 用内部上拉、内部下拉各采样一次，靠这一对读数判断引脚状态：上拉读高且下拉读低 = **float**（悬空，即按键松开）；两次都读高 = **high**（被短接到 3.3V）；两次都读低 = **low**（被短接到 GND）。启动时测到的状态作为静止基准，之后任何偏离基准的状态都算按下。所以裸按键接 GND、裸按键接 3.3V、带板载上拉或下拉电阻的模块，四种情况都能自适应。若某键上电时恰好被按住导致基准取错，持续 10 秒后固件会自动重新校准。

串口日志可用来核对。启动时每个键一行：

```
[bmo-btn] UP on P4, idle=float
```

之后每 5 秒打印一次所有键的当前状态，按下时会打印边沿事件：

```
[bmo-btn] UP=float DOWN=float LEFT=float RIGHT=float MID=float SW1=float SW2=float TRI=float GREEN=float
[bmo-btn] LEFT P7 DOWN (high)
```

松开时应稳定停在 idle 那个状态，按下的那个键会变成 `high`（COM 接 3.3V）或 `low`（COM 接 GND）。如果某个键按下时状态纹丝不动，就是虚焊或模块内部没导通。

舵机：**外接 5V ≥1A，与开发板共 GND**，信号线接对应 PWM 脚。

**注意**：BK7258 上 P24 对应的是硬件 PWM4，但在 TuyaOpen 里要配置成 `TUYA_PWM_NUM_1`，不是 `NUM_4`（`NUM_4` 会输出到 P36）。烧录后日志应显示 `[servo] dual arm init ok (L=PWM0 R=PWM1)`。

完整映射（`tkl_pwm.c` 的 `ty_to_bk_pwm()` 叠加 `GPIO_PWM_MAP_TABLE` 推出，配错就是往空脚输出）：

| `TUYA_PWM_NUM_` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 实际引脚 | P18 | **P24** | P32 | P34 | P36 | P19 | P8 | P9 | P25 | P33 | P35 |

## 改 Kconfig 后必须确认配置真的生效

`tos.py build` **不读** `config/TUYA_T5AI_CORE.config`。那只是最小配置模板，只在执行 `tos.py config choice` 时被展开成 `.build/cache/using.config`，再由它生成 `.build/include/tuya_kconfig.h`，而后者才是真正产出 C 宏的文件。所以只改模板然后 build，改动会被静默忽略，编译照样成功，行为完全是旧的。

右臂舵机曾因此排查了很久：模板里 `CONFIG_SERVO_RIGHT_PWM` 早已改成 1，但 `tuya_kconfig.h` 停留在昨天生成的 `#define SERVO_RIGHT_PWM 4`，固件一直把 PWM 打到 P36，而舵机接在 P24 上，看起来就像舵机坏了。

改完 Kconfig 相关配置后，用日志或头文件核对一遍：

```powershell
rg "SERVO_RIGHT_PWM" .build\include\tuya_kconfig.h
```

值不对就说明配置没进去，需要重新 `tos.py config choice`，或者直接同步 `using.config` 与 `tuya_kconfig.h`（改了 `tuya_kconfig.h` 会触发全量重编）。启动日志里的 `[servo] dual arm init ok (L=PWM0 R=PWM1)` 也能直接看出右臂通道号。

## 舵机诊断工具

`src/motion/servo_test.c` 是右臂上电自检，默认关闭。要用就在 `config/TUYA_T5AI_CORE.config` 加 `CONFIG_ENABLE_SERVO_TEST=y` 和 `CONFIG_SERVO_TEST_PIN=24`，然后按上一节确认配置生效。开机 4 秒后开始，约一分钟跑完，结束后自动把控制权交还给正常驱动。

四个阶段依次是：P24 静态高低电平（配万用表量）、软件打拍 50Hz 舵机帧（绕过 PWM 外设，用来区分引脚坏还是 PWM 配错）、逐个扫描 `TUYA_PWM_NUM_0..10`（胳膊在哪个通道动，那个就是正确通道号）、正负极性对比。

阶段 1 的电平回读**不可信**：SDK 的 GPIO 表给这些脚设了 `GPIO_IO_DISABLE`，输入锁存是关的，输出时回读可能恒为 1。判断引脚好坏要靠万用表和阶段 2。
