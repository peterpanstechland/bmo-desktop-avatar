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
| 模块附加键 2 | P25 | 短按：系统信息浮层（IP / 信号 / 内存 / 版本），再按一次关闭，10 秒自动消失；长按 5 秒：重置网络，见下节 |
| 红大圆（对话） | P12 | `ai_chat_button`：单击唤醒/打断/结束对话，长按对讲（listening 时改为结束对话），双击切模式，见下节 |
| 蓝三角 | P26 | 返回：游戏中退回游戏选择页，其余页面回表情页 |
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

## 换 WiFi

设备只存一组 WiFi 凭据，换网络有三种办法。

**长按 SW2 五秒。** 按住 1.2 秒后屏幕开始倒计时（`重置网络 3 / 松开取消`），松手随时可以取消，数到 0 才真的执行。执行的是 `tuya_iot_reset()`，WiFi 凭据和云端绑定一起清掉，设备立即重启进入配网状态，然后用涂鸦 App 重新配（蓝牙和 AP 两种都支持，见 `tuya_main.c` 里的 `NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP`）。

**连续快速断电重启三次。** 不依赖固件改动的兜底办法，`src/reset_netcfg.c` 实现的：开机时 `reset_netconfig_start()` 把 KV 里的 `rst_cnt` 加一，同时起一个 5 秒定时器清零，所以只有**没撑过 5 秒就断电**的重启才算数。累计 3 次后 `reset_netconfig_check()` 调同样的 `tuya_iot_reset()`。

**出门的偷懒办法：把手机热点的 SSID 和密码设成跟家里路由器一模一样。** 设备分不出来，开机直接连上，一次都不用重配。

前两种都会解绑设备，重新配网后要在涂鸦 App 里重新绑定，智能体配置本身在云端产品上，不用重设。

## 红键与对话模式

单击不是固定动作，取决于当前对话状态（这套逻辑在 `src/ai_mode_combo.c`）：

| 状态 | 单击 |
|------|------|
| IDLE | 唤醒，进入 listening |
| LISTEN | **结束对话**，回 IDLE，屏幕弹 `Chat ended` |
| THINK / SPEAK | 打断当前思考或播报，重新进 listening |

长按是对讲（按住说，松手上传），但 **listening 时长按也是结束对话**——已经在收音了，再开 PTT 没有意义，这时候按下去只可能是想喊停。

**长按阈值原来只有 400ms，太短。** SDK 的 `ai_chat_main.c` 给 `ai_chat_button` 配的是 400，而其他所有按键都是 3000。面板上这种带弹簧的大圆键，"点一下"很容易超过 0.4 秒，状态机一判成长按就进 `BTN_FSM_LONG_HOLD`，松手时**不会补发单击事件**，所以单击和双击这两条路永远走不到——这就是「单击退不出对话」和「双击切不了模式」的共同原因。现在改成 700ms，见 `patches/ai_chat_button_long_press.patch`，重装 SDK 后要重新打。

单击还有约 300ms 延迟才生效，这是按钮框架在等，要先确认不是双击——`tdl_button` 的状态机里单击和双击互斥，双击不会先触发一次单击。

**双击事件在 `tdl_button` 里根本发不出来，这是 SDK 的 bug。** 状态机的 `ticks` 由扫描循环统一累加，每次状态迁移都会清零它——唯独 `BTN_FSM_WAIT_REPEAT → BTN_FSM_REPEAT_PRESSED`（第二次按下）这条路漏了。于是 `BTN_FSM_REPEAT_PRESSED` 松手时拿来跟 `button_repeat_valid_time`（300ms）比的 `ticks`，量的不是「第二次按了多久」，而是「第一次松手到现在多久」。人的双击是间隔 150~250ms 加按住 80~100ms，总和必然超 300ms，状态机就判成超时直接回 IDLE，`TDL_BUTTON_PRESS_DOUBLE_CLICK` 一次都没发过。串口上的特征是：`released (repeat 2)` 打出来了，后面却没有任何模式切换的日志。补上那行清零即可，见 `patches/tdl_button_double_click.patch`。

**双击切模式，这里有个坑。** 双击不归 combo 模式管，是 SDK 的 `ai_chat_main.c` 直接拦下来调 `ai_mode_switch_next()`，在**已注册的模式链表**里轮转。链表有 5 个：

```
combo -> hold -> oneshot -> wakeup -> free -> 回到 combo
```

后四个是 SDK 内置的，`ENABLE_COMP_AI_MODE_*` 默认全开。**每个模式有自己的按键处理**，所以一旦双击切走：

- 上面那张单击行为表**不再适用**（那是 combo 独有的）
- hold 模式下唤醒词和单击都不响应，只能长按，看起来很像整机哑了
- 要连双击 4 次才能转回 combo

排查「红键怎么不好使了」时，先确认当前在哪个模式。现在切换时屏幕会弹出模式名和用法（比如 `hold / hold red to talk`），串口也会打 `chat mode -> hold (0)`。开机时 `app_chat_bot_init()` 会强制切回 combo，所以重启一定是 combo。

切回 combo 时原本**没有提示音**：SDK 播的是 `AI_AUDIO_ALERT_LONG_KEY_TALK + 模式号`，而 combo 的模式号是 `AI_CHAT_MODE_CUSTOM_START`（0x100），加出来远超枚举范围，落进 `__player_local_alert()` 的 default 分支被静默丢弃。现在由 app 层监听 `AI_USER_EVT_MODE_SWITCH` 自己补一个。

如果不想要这些内置模式，把 `CONFIG_ENABLE_COMP_AI_MODE_HOLD/ONESHOT/WAKEUP/FREE` 设成 n，链表里就只剩 combo，双击不再切走。

## 舵机供电：不要从开发板的 5V 引脚取电

从板子 5V 引脚取电时，出现过「音量 100 + 双臂同时动 = 整机掉电重启」。那个引脚基本是 USB 5V 直通，USB 口本身只给 500mA（USB3 900mA），而两个 SG90 启动瞬间各自要几百 mA 到 1A，再叠上喇叭满功率的峰值，电压塌下去就复位了。

正确接法：舵机走**独立 5V 电源**（能给 2A 的充电头或 DC-DC），电源负极与开发板 GND 接在一起（共地是必须的，否则 PWM 信号没有参考电平）。再在舵机电源端就近并一个 **1000µF 电解 + 0.1µF 陶瓷**，吸收启动电流尖峰。

固件侧做了两层削峰，但它们只是让问题更晚出现，**替代不了独立供电**：

- **动作关键帧之间线性插值**。以前每个 tick 直接把角度写成当前关键帧的目标值，等于命令舵机以最大速度冲过去，两臂的启动电流精确叠在同一瞬间。改成插值后，`cheer_both` 的 40° 行程摊到 250ms 走完，约 160°/s，只有 SG90 空载速度的四分之一。进场也从当前角度 ramp 过去，避免从 droop 的 45° 直接跳到 90°。
- **音量上限 `CONFIG_BMO_MAX_VOLUME`（默认 85）**。按键音量+、云端 DP、以及开机时从 KV 读回的历史音量，全部经 `app_volume_set()` 钳位。供电充足的话可以调回 100。

## 小游戏页（第 5 页 games）

从日历页再按 **RIGHT** 进入，或语音「打开游戏」。

| 状态 | 按键 |
|------|------|
| 选游戏 | UP/DOWN 选 Snake / Tetris，**十字中**确认进入；LEFT/RIGHT 仍是翻页，TRI 回表情页 |
| 游戏中 | 十字上下左右操控；**蓝三角**退出回选择页 |
| 游戏结束 | 十字中重开 |

- **Snake**：上下左右改方向
- **Tetris**：左右移动，UP 旋转，DOWN 软降

**退出用三角，不用十字中。** 三角本来就是「返回」语义，在这里正好分两级：游戏中退回选择页，选择页再按一次回表情页。原来是长按十字中退出，但十字中在游戏里一直压在拇指底下，误触退出太频繁；现在十字中在游戏中只剩「结束画面重开一局」这一个作用，误触没有代价。

去掉长按退出之后，`games_btn_held()` 整个删了，十字中也恢复成按下即响应（原来为了区分长按得等松手）。顺带修好一件事：以前游戏页在长按分支里被 games 抢走，SW2 长按 5 秒重置网络在游戏页是不生效的，现在生效了。

游戏页只吃掉自己用得上的键：选择页放行 LEFT/RIGHT/TRI，游戏中放行 SW1/SW2，所以翻页、静音、系统信息随时可用。

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

### 改配置要改**三个**地方，漏掉 app_default.config 会复发

同一个问题后来又犯了一次：右臂好好的忽然又不动，一查 `tuya_kconfig.h` 里的 `SERVO_RIGHT_PWM` 自己变回了 4。原因是仓库根目录还有一个 `app_default.config`，里面留着旧值，而缓存重新生成时是从它来的——`config/TUYA_T5AI_CORE.config` 和 `Kconfig` 的 default 当时都已经是 1 了，唯一还写着 4 的就是它。

所以配置有三份，必须一起改，任何一份掉队都会在下次重建缓存时把旧值捞回来：

| 文件 | 作用 |
|------|------|
| `app_default.config` | 应用默认配置，缓存重建时的来源，**最容易漏** |
| `config/TUYA_T5AI_CORE.config` | 板级模板，`tos.py config choice` 读它 |
| `Kconfig` 的 `default` | 前两者都没写该项时的兜底 |

改完对一遍，三份一致才算数：

```powershell
Compare-Object (Get-Content app_default.config) (Get-Content config\TUYA_T5AI_CORE.config)
```

## 舵机诊断工具

`src/motion/servo_test.c` 是右臂上电自检，默认关闭。要用就在 `config/TUYA_T5AI_CORE.config` 加 `CONFIG_ENABLE_SERVO_TEST=y` 和 `CONFIG_SERVO_TEST_PIN=24`，然后按上一节确认配置生效。开机 4 秒后开始，约一分钟跑完，结束后自动把控制权交还给正常驱动。

四个阶段依次是：P24 静态高低电平（配万用表量）、软件打拍 50Hz 舵机帧（绕过 PWM 外设，用来区分引脚坏还是 PWM 配错）、逐个扫描 `TUYA_PWM_NUM_0..10`（胳膊在哪个通道动，那个就是正确通道号）、正负极性对比。

阶段 1 的电平回读**不可信**：SDK 的 GPIO 表给这些脚设了 `GPIO_IO_DISABLE`，输入锁存是关的，输出时回读可能恒为 1。判断引脚好坏要靠万用表和阶段 2。
