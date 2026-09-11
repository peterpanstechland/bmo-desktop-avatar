# 飞书日历接入

日历页右半边的「今日日程」和月历上的事件圈来自飞书。不配也能用，月历和今天的高亮照常显示，只是日程区一直是「暂无数据」。

配好之后还能反过来写：跟 BMO 说「明天下午三点开会」，它会调 `pet.calendar.add_event` 把日程记进飞书，见下面的[语音记日程](#语音记日程)。

## 要填的三个值

| 值 | 必填 | 从哪来 |
|----|------|--------|
| `CONFIG_FEISHU_APP_ID` | 是 | 开发者后台 → 凭证与基础信息，形如 `cli_a1b2c3d4e5f6` |
| `CONFIG_FEISHU_APP_SECRET` | 是 | 同上，注意别提交到仓库 |
| `CONFIG_FEISHU_CAL_ID` | 否 | 只有自动选错日历时才需要，见下文排错 |

这三项是 Kconfig 选项，和涂鸦 PID / 授权码放在同一个地方：`app_default.config` 和 `config/TUYA_T5AI_CORE.config`（两个文件都已 gitignore，`*.example` 里保持为空）。两个文件要**同时**填，然后重新 `tos.py config choice` 再 build：

```
CONFIG_FEISHU_APP_ID="cli_xxxxxxxxxx"
CONFIG_FEISHU_APP_SECRET="xxxxxxxxxxxxxxxxxxxxxxxx"
CONFIG_FEISHU_CAL_ID=""
```

如果用私有的 `bmo-desktop-avatar-secrets` 仓库管理这两个文件，填在那边再同步过来即可。`app_id` 留空就关闭日历同步，日历页只显示月历。

编译后可以核对一下是否真的带进了固件：

```bash
rg FEISHU .build/include/tuya_kconfig.h
```

> 早期版本靠环境变量 `FEISHU_APP_ID` / `FEISHU_APP_SECRET` 注入，换一台机器或忘了 `export` 就静默退回占位符——现在不再支持，统一走 Kconfig。

## 配置步骤

### 1. 创建自建应用

[飞书开发者后台](https://open.feishu.cn/app) → 创建企业自建应用，填名称和图标。

### 2. 开启机器人能力

**这一步不能跳过。** 应用能力 → 添加应用能力 → 机器人 → 添加。

用 `tenant_access_token`（应用身份）调日历接口时，飞书要求应用有机器人身份，否则直接报 `app bot_id not found`，串口上会看到 `[feishu] list calendars rejected`。

### 3. 开权限

开发配置 → 权限管理，把下面这行粘进搜索框批量开通：

```
calendar:calendar:readonly,calendar:calendar
```

**注意飞书把每个权限拆成了「应用身份」和「用户身份」两套，要分别开通。** 设备运行时用的是应用身份，但下一步给机器人授权日历时要用用户身份，所以两个都得开。

只开了应用身份的话，症状很有迷惑性：同一个接口用 `tenant_access_token` 返回 `code: 0`，换成 `user_access_token` 就报 99991679，看起来像是 token 坏了，其实是权限没开全。

### 4. 发布版本

自建应用改了权限要**创建版本并发布**，等管理员审核通过后权限才生效。自己是管理员的话可以秒过。

开发阶段可以用「测试企业和人员」建一个测试企业，权限变更即时生效不用审核，调通了再同步到正式版。

### 5. 让应用能看见你的日程

这是最容易漏的一步，也是「凭证明明填对了但日程永远是空的」的原因。

`GET /open-apis/calendar/v4/calendars` 返回的是**应用自己订阅的日历**，不是你的。一个新建的自建应用只有一个属于它自己的空主日历，里面永远没有日程。你的个人日历默认对它不可见，而且应用**没法自己给自己授权**——加日历成员的接口要求调用方已经是该日历的 owner。

**不要在飞书客户端里试着共享。** 日历的「分享」面板只接受人和群，机器人能被搜到但复选框是灰的，选不了。授权只能走 API。

不用写代码，在[飞书 API 调试台](https://open.feishu.cn/api-explorer)调三个接口就行。注意身份的切换，这是最容易错的地方。

**1. 拿机器人的 open_id** — 接口是 `GET /open-apis/bot/v3/info`，但它是 v3 的老接口，**调试台的列表里搜不到**（搜「机器人」只会出来 v4 的「搜索机器人」）。接口本身还在，用 PowerShell 直接调即可。

一行一条粘进去，不要用反引号折行——从网页复制多行命令时换行常常会被压掉，反引号后面跟空格续行就失效了：

```powershell
$id = "cli_xxxxxxxxxx"
```

```powershell
$secret = "从后台复制按钮拷来的 secret"
```

```powershell
$r = Invoke-RestMethod -Method Post -Uri "https://open.feishu.cn/open-apis/auth/v3/tenant_access_token/internal" -ContentType "application/json; charset=utf-8" -Body (@{app_id=$id; app_secret=$secret} | ConvertTo-Json); $r
```

```powershell
(Invoke-RestMethod -Uri "https://open.feishu.cn/open-apis/bot/v3/info" -Headers @{Authorization="Bearer $($r.tenant_access_token)"}).bot | Format-List
```

输出里的 `open_id : ou_xxxxxxxx` 就是第 3 步要填的值。

凭证一定要用后台的复制按钮拷，别照着截图敲：`a`/`4`、`c`/`e`、`0`/`o`、`1`/`l`、`6`/`8` 在截图上几乎分不出来，错一位就是 `app id not exists`。换 token 失败时飞书返回的是 HTTP 200 加错误码，PowerShell 不会报错，`$r.tenant_access_token` 只是悄悄变成空，下一条命令才报 `Missing access token`——所以要先把 `$r` 整个打出来看 `code` 和 `msg`。

**2. 拿你主日历的 ID** — `POST /open-apis/calendar/v4/calendars/primary`，**切换成 `user_access_token`**，用自己的账号授权。返回里 `calendars[0].calendar.calendar_id` 就是。

这一步务必核对返回里的 `summary` 是**你的名字**。这个接口用 `tenant_access_token` 也返回 `code: 0`，但给的是「应用自己的主日历」——每个开了机器人能力的应用都有一个，永远是空的。拿那个 ID 去做第 3 步，等于让机器人给自己的空日历授权，接口一路返回成功，设备上却什么都读不到。认准 `summary`：是你的名字才对，是应用名就说明身份没切过来。

报 99991679 就是上一步的「用户身份权限」没开，或者授权是在开通之前做的——回去补开并发布，然后点「获取 Token」重新授权，旧 token 不会自动带上新 scope。

**3. 把机器人加成日历成员** — `POST /open-apis/calendar/v4/calendars/:calendar_id/acls`，**继续用 `user_access_token`**。这一步必须是你的身份：接口要求调用方拥有该日历的 owner 权限，而应用对你的主日历什么权限都没有，没法自己给自己授权。

`user_id_type` 选 `open_id`，请求体：

```json
{
  "role": "writer",
  "scope": {
    "type": "user",
    "user_id": "ou_第一步的机器人open_id"
  }
}
```

`writer` 是「可增删日程」。只读日历页的话 `reader` 就够，但语音记日程要写权限，一步到位省得回头改。别用 `free_busy_reader`，那档只能读到忙闲，拿不到日程标题，设备上会显示一排没名字的日程。

返回 `code: 0` 即成功。如果报 `191002 no calendar access_role`，是身份用错了，检查是不是还停在 `tenant_access_token`。

**已经配成 reader 了**：ACL 不能改，只能删了重建。用 `user_access_token` 调 `GET /open-apis/calendar/v4/calendars/:calendar_id/acls` 找到 `scope.user_id` 是机器人 open_id 的那条，记下 `acl_id`，`DELETE .../acls/:acl_id` 删掉，再按上面的请求体重新建一条 `writer`。

### 另一种选择：让应用建一个共享日历

如果不想让应用碰主日历，可以用 `tenant_access_token` 调 `POST /open-apis/calendar/v4/calendars` 新建一个日历——应用天然是它的 owner（读写都有，语音记日程直接可用），再调同一个 acls 接口把你自己加成 `writer` 就行，全程不需要 `user_access_token`。代价是日程得单独记在这个日历里。

### 6. 编译烧录

填好 config 后重新编译烧录。开机后等时间同步完成会自动拉一次，失败（还没联网、NTP 没到）每 60 秒重试，成功后每 30 分钟随天气一起刷新；按 D-pad 中键或在日历页按绿键可以随时手动刷新。

## 语音记日程 / 读日程

固件注册了 MCP 工具：

- `pet.calendar.add_event`：跟它说「明天下午三点开会」即可写入飞书；成功后跳到日历页并 toast「日程已添加」。
- `pet.calendar.get_events`：问「今天有什么安排」时读取设备已缓存的飞书日程并口头播报；`day_offset` 为 `-1` 看近 4 天，`0` 今天，`1` 明天。

日期传的是**相对天数**（`day_offset`），不是绝对日期。云端智能体不一定知道今天几号，但设备有 NTP 校准过的时钟，所以让设备自己换算更稳。时间按本地时区解释，没说时长默认一小时。

提示词里已经写好了对应的用法说明，见 [agent-system-prompt.md](agent-system-prompt.md)「工作梳理 / 记日程」。

几点注意：

- 写日程不是设闹钟，到点提醒由飞书 App 发。
- 机器人对日历只有 `reader` 时写入会失败，串口报 `add event rejected`，按上面的方法升到 `writer`；只读播报有 `reader` 就够。
- 写入走 HTTPS 同步等待，网络差的时候智能体那边会多等一两秒才回话。
- 读日程用的是设备缓存（后台约 30 分钟刷新，绿键可手动刷）；缓存未就绪时工具会提示稍等。

## 排错

代码在关键节点都打了日志，串口上 `grep feishu` 基本能直接定位：

| 日志 | 含义 | 怎么办 |
|------|------|--------|
| `CONFIG_FEISHU_APP_ID/SECRET not set, calendar sync disabled` | 固件里没有凭证 | 两个 config 文件都填上，`tos.py config choice` 后重新 build，用 `rg FEISHU .build/include/tuya_kconfig.h` 核对 |
| `clock not synced yet, retry later` | NTP 还没同步，60 秒后自动重试 | 正常，刚开机会出现一两次；一直出现说明设备没联网 |
| `http ... failed, client_rt=N` | TLS/TCP 层就失败了 | 网络不通或 DNS 失败，看 WiFi 状态 |
| `token rejected: code=10014` | 看 `msg`：`app id not exists` 是 app_id 错，`invalid app_secret` 是 secret 错 | 用后台复制按钮重拷，别照截图敲 |
| `list calendars rejected` | 权限或机器人能力缺失 | 回第 2、3、4 步，注意版本要发布 |
| `no calendar visible to the app` | 应用一个日历都看不到 | 第 5 步没做，或 acls 那步没返回 code:0 |
| `calendar type=... role=... summary=...` | 列出了应用能看到的每个日历 | 正常，用来确认选中的是哪个 |
| `N event(s) in the next 4 days` | 拉取成功 | N 为 0 就是这几天真没安排 |
| `add event rejected: code=99991672` | 语音记日程时机器人只有读权限 | 把 acls 里的 `reader` 换成 `writer` |
| `event added: ... at MM-DD HH:MM` | 写入成功 | 核对一下时间对不对，不对多半是设备时区没同步 |

### 选错日历了

代码会打印出所有能看到的日历。选择规则是**优先选 role 不是 owner 的那个**——应用自己的空主日历 role 是 owner，你共享过来的是 reader/writer，所以正常情况下会自动选对。选中的 ID 会缓存到重启，所以这几行日志每次开机只出现一次。

如果你共享了多个日历导致选错，从日志里找到对应的那条，把它的 `calendar_id` 填进 `CONFIG_FEISHU_CAL_ID`，就会跳过自动选择直接用它。

### 为什么不能全程用应用身份

试过了，走不通，链条是闭合的：应用要读你的日历就得先成为日历成员，加成员的接口要求调用方拥有该日历的 owner 权限，而你主日历的 owner 只有你自己。所以第 3 步必须是用户身份。

绕路也堵死了：搜索日历接口写明「应用身份不支持搜索用户主日历」，订阅接口虽然支持应用身份，但要求先有 calendar_id，而拿 ID 又回到用户身份。飞书这么设计是对的，否则任何一个自建应用都能用自己的身份翻遍全公司的私人日程。

### 关于 feishu.cn 和 larksuite.com

实测同一套凭证在 `open.feishu.cn` 和 `open.larksuite.com` 上都能换到 token，而且换来的 token 拿去调另一个域名的接口也认。所以固件里写死 `open.feishu.cn` 没问题，遇到 `app id not exists` 先怀疑凭证抄错，不要急着换域名。

### 已知限制

- 一次只拉**今天起 4 天**的日程，所以月历上的事件圈也只覆盖这 4 天，再往后的日子不会有标记。
- 最多保留 10 条日程（`FEISHU_CAL_MAX_EVENTS`）。
- 日程标题超过 63 字节会被截断。
- token 有效期内会缓存，过期前 5 分钟自动续。
