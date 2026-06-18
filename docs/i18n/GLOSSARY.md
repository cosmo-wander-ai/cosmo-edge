# I18N Glossary

This glossary defines the public UI terminology used by CosmoEdge. It is both a
documentation reference and an input to the frontend i18n validation scripts.
Entries in the `Short` and `Short scope` columns control where compact labels
may be used.

## 原则

1. **默认只维护一份专业英文 EN**。它是产品标准文案,优先用于页面、弹窗、toast、详情、表单等位置。
2. **Short 只是布局例外,不是第二套翻译**。只有 `SHORT-SCOPES.md` 中受控枚举的紧凑场景才允许使用。
3. **Short 必须绑定 Short scope**。Short scope 列只能填 SHORT-SCOPES.md 中的 ID,CI 会校验。
4. **不要为了短牺牲理解**。`DL`、`Ops`、`Def`、`F&B` 这类内部感强或有歧义的缩写不进入 UI 文案。
5. **能通过布局解决的,不通过硬缩写解决**。例如按钮可改 icon + tooltip,表格可给列宽或 tooltip。
6. **实施规则优先于单条术语偏好**:GLOSSARY 不能保护错误的设计。

## 列说明

| 列 | 含义 |
|---|---|
| **中文** | 源语言 |
| **EN** | 默认英文,产品标准文案 |
| **Short** | 仅紧凑 UI 使用的短形式。`-` 表示不维护短形式 |
| **Short scope** | Short 允许出现的场景 ID (来自 `SHORT-SCOPES.md`) |
| **频次** | 术语在当前 Web UI 中的参考出现次数 |
| **风险** | 英文膨胀导致布局问题的风险: 高/中/低 |
| **备注** | 翻译/落地注意事项 |

## 维护规则

1. 新增或修改带 Short 的术语时,必须同时确认 `Short scope` 是否属于 `SHORT-SCOPES.md`。
2. 模板字符串统一使用 `{placeholder}` 风格的命名占位符。
3. 同一中文短语在不同语义下需要不同英文时,必须拆分为不同 i18n key。
4. 表格中的 `EN` 是默认显示文案;`Short` 只用于声明的紧凑 UI scope。

---

## 1. UI 通用动作

> 标记 EP 的项由 Element Plus locale 接管;项目自定义按钮仍需要进入项目字典。

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 取消 | Cancel | - | - | 48 | 低 | EP popconfirm/button 类文案 |
| 确定 | OK | - | - | 31 | 低 | 仅信息弹窗使用 OK,见 §9 |
| 确认 | Confirm | - | - | 6 | 低 | 仅作兜底;危险或业务动作必须使用具体动词,见 §9 |
| 保存 | Save | - | - | 25 | 低 | `action.save` |
| 删除 | Delete | - | - | 20 | 低 | 不用 `Del`;极窄处用 trash icon + tooltip |
| 批量删除 | Bulk Delete | Delete | `btn.compact` | 11 | 中 | 短形式仅在"已选多项"语境下使用,避免 `Del All` |
| 编辑 | Edit | - | - | 8 | 低 | `action.edit` |
| 添加 | Add | - | - | 5 | 低 | 与"新增"统一 |
| 新增 | Add | - | - | - | 低 | 同义项,统一翻 Add |
| 重置 | Reset | - | - | 7 | 低 | `action.reset` |
| 关闭 | Close | - | - | 5 | 低 | EP dialog close |
| 浏览 | Browse | - | - | 9 | 中 | 文件选择按钮;如过窄用 icon + tooltip |
| 查询 | Search | - | - | 5 | 低 | 用户界面优先 Search,不要用 Query |
| 详情 | Details | Info | `inline.action` | 10 | 中 | 默认用 Details;Info 只做紧凑例外 |
| 操作 | Actions | - | - | 10 | 中 | 表格列头保留 Actions,不要用 Ops |
| 清空 | Clear | - | - | 5 | 低 | `action.clear` |
| 下载 | Download | - | - | 4 | 中 | 不用 DL;极窄处用 download icon + tooltip |
| 设置 | Settings | - | - | 4 | 中 | 侧栏/按钮常配 icon |
| 输入 | Input | - | - | 4 | 低 | 多为名词/动词混用,具体语境定 |
| 说明 | Description | Desc. | `table.header` | 7 | 中 | 表单 label 用 Description;表头可用 Desc. + tooltip |
| 测试 | Test | - | - | 4 | 低 | `action.test` |

## 2. UI 通用提示

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 操作成功 | Operation succeeded | - | - | 68 | 低 | 通用成功提示仅作兼容保留;新增文案应绑定具体动作,见 §9 |
| 提示 | Notice | - | - | 44 | 低 | Dialog 标题可接受;也可按语境用 Tip/Warning |
| 错误 | Error | - | - | 6 | 低 | |
| 成功 | Success | - | - | 5 | 低 | badge/toast |
| 失败 | Failed | - | - | 4 | 低 | badge/toast |
| 请输入 | Please enter ... | Enter ... | `placeholder` | 47 | 中 | placeholder 可去掉 Please |
| 请选择 | Please select ... | Select ... | `placeholder` | 39 | 中 | placeholder 可去掉 Please |
| 请上传 | Please upload ... | Upload ... | `placeholder` | 11 | 中 | |
| 点击上传 | Click to upload | Upload | `btn.compact` | 4 | 中 | |
| 暂无数据 | No data | - | - | 4 | 低 | EP empty/table |
| 当前暂无服务 | No service available | - | - | 3 | 低 | empty state |
| 全部 | All | - | - | 43 | 低 | filter/select |
| 默认 | Default | - | - | 26 | 中 | 不用 Def;badge 放不下时改样式或 tooltip |
| 名称 | Name | - | - | 10 | 低 | 字段/列名 |
| 地址 | Address | Addr. | `table.header` | 10 | 中 | 详情/表单仍用 Address |
| 序号 | No. | - | - | 14 | 低 | 表格首列统一用 No. |
| 状态 | Status | - | - | 15 | 中 | 表头通常给足列宽 |
| 范围 | Range | - | - | 14 | 低 | |
| 时间 | Time | - | - | 4 | 低 | |
| 更新时间 | Update Time | Updated | `table.header` | 4 | 中 | 表格列头推荐 Updated;详情用 Update Time |

## 3. 表单校验消息

> 校验消息必须用模板和占位符,不要硬拼中文碎片。这里不维护 Short;校验容器应允许换行或 tooltip。

| 中文/模式 | EN 模板 | 频次 | 备注 |
|---|---|---|---|
| `请输入 {field}` | `Please enter {field}` | 47 | placeholder 可用 `Enter {field}` |
| `请选择 {field}` | `Please select {field}` | 39 | placeholder 可用 `Select {field}` |
| `请上传 {field}` | `Please upload {field}` | 11 | |
| `最长为 {n} 个字符` | `{field} must be no more than {n} characters` | 26 | 避免只翻 `个字符` |
| `请输入 {min}-{max} 的整数` | `Enter an integer between {min} and {max}` | 11 | |
| `{field} 不能超过 {n}` | `{field} must not exceed {n}` | 4 | |
| `仅支持 {types} 格式的文件` | `Only {types} files are supported` | 4 | |
| `已达上限，无法再添加` | `Maximum reached. Cannot add more.` | 4 | |
| `正在重启中，剩余 {n}` | `Restarting. {n} remaining.` | 4 | |
| `确定要删除吗？删除后将不可恢复` | `Delete this item? This cannot be undone.` | 10 | 比 recovered 更自然 |
| `确认要删除吗？删除后将不可恢复` | `Delete this item? This cannot be undone.` | 3 | 合并同一 key |
| `文件获取失败` | `Failed to fetch file` | 7 | toast |
| `文件失效` | `File is no longer available` | 6 | 比 invalid 更像用户文案 |
| `相机不在线` | `Camera is offline` | 4 | |

## 4. 状态/枚举

> 状态必须全工程一致。无法独立确定含义的单字或碎片统一放入 §7。

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 在线 | Online | - | - | 10 | 低 | badge |
| 离线 | Offline | - | - | 8 | 低 | badge |
| 运行中 | Running | - | - | 5 | 低 | badge |
| 已停止 | Stopped | - | - | 4 | 低 | badge |
| 已暂停 | Paused | - | - | 4 | 低 | badge |
| 已上传 | Uploaded | - | - | 9 | 低 | 文件状态 |
| 未上传 | Not uploaded | - | - | 9 | 低 | 与 Pending 是不同状态,不要混用;Pending 单列见补充术语 |
| 等于 | Equals | = | `flow.node` | 5 | 低 | 流程图条件操作符 |
| 或 | Or | - | - | 4 | 低 | 流程图条件操作符 |
| 除外 | Except | - | - | 6 | 低 | 流程图条件操作符 |
| 授权状态 | License Status | License | `table.header` | 4 | 中 | |
| 授权失败 | Authorization failed | Auth failed | `tag.badge` | 3 | 高 | toast/dialog 用完整形式 |
| 模型状态 | Model Status | Status | `table.header` | 7 | 中 | 仅在表格上下文已明确是模型时使用 |

## 5. 领域术语

> Short 只在 scope 指定位置允许使用。

### 5.1 顶层业务概念

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 计数统计 | Counting Analytics | Counting | `sidebar.menu, dashboard.card` | 20 | 高 | 用于分析类模块名 |
| 单机版 | Standalone Edition | Standalone | `tag.badge` | 6 | 高 | 产品版本名 |
| 联网版 | Connected Edition | Connected | `tag.badge` | 6 | 高 | 用于联网/云侧接入版本 |
| 场景任务 | Scenario Task | Scenario | `sidebar.menu, table.header` | 5 | 中 | 表示面向具体场景的任务配置 |
| 算法服务 | Algorithm Service | Algo Service | `table.header, dashboard.card` | 6 | 高 | 不建议短成 Algorithm,会丢失 service |
| 模型 | Model | - | - | 6 | 低 | |
| 模型文件 | Model File | - | - | 5 | 低 | |
| 参数设置 | Parameter Settings | Settings | `sidebar.menu, flow.node` | 4 | 高 | 技术型界面可用 Params,但默认不推荐 |
| 灵敏度计算 | Sensitivity Calculation | Sensitivity | `sidebar.menu, flow.node` | 4 | 高 | 用于规则/流程中的灵敏度计算节点 |

### 5.2 检测/分析能力

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 检测 | Detection | Detect | `flow.node` | 15 | 中 | 菜单/标题优先 Detection |
| 分析 | Analysis | - | - | 14 | 中 | |
| 图片分析 | Image Analysis | Image | `tab.compact` | 6 | 中 | tab/menu 已有 Analysis 上下文 |
| 视频分析 | Video Analysis | Video | `tab.compact` | 5 | 中 | 同上 |
| 车辆分析 | Vehicle Analysis | Vehicle | `tab.compact` | 4 | 中 | 同上 |
| 人脸人体 | Face and Body | Face/Body | `tab.compact` | 8 | 中 | 不用 F&B (英文 = Food & Beverage);**与"人体人脸检测"统一为 Face and Body** |
| 检测区域 | Detection Area | Area | `table.header, tab.compact` | 5 | 中 | 已有 detection 上下文时省略前缀 |
| 新增区域 | Add Area | - | - | 6 | 低 | |
| 区域名称 | Area Name | - | - | 6 | 低 | |
| 阈值 | Threshold | - | - | 9 | 中 | 不建议缩写;必要时靠 tooltip |

### 5.3 通道/设备

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 通道 | Channel | Ch. | `table.header` | - | 中 | 首次出现和详情页用 Channel |
| 通道名称 | Channel Name | Channel | `table.header` | 18 | 中 | |
| 通道编号 | Channel ID | Ch. ID | `table.header` | 4 | 低 | 工程语境用 ID |
| 编辑通道 | Edit Channel | Edit Ch. | `inline.action` | 7 | 中 | 优先用 icon + tooltip |
| 接入类型 | Access Type | Access | `table.header` | 4 | 中 | |
| 帧率 | Frame Rate | FPS | `table.header, dashboard.card` | 4 | 中 | FPS 是行业标准缩写 |
| 端口 | Port | - | - | 5 | 低 | |
| 服务器地址 | Server Address | Server | `table.header` | 5 | 中 | 表单 label 用 Server Address |
| 离线视频 | Offline Video | - | - | 3 | 中 | |
| 视频播放 | Video Playback | Playback | `tab.compact, btn.compact` | 6 | 中 | |
| 音频文件 | Audio File | - | - | 4 | 低 | |
| 播放次数 | Play Count | - | - | 6 | 低 | |
| 播放时长 | Play Duration | Duration | `table.header, dashboard.card` | 4 | 中 | |

### 5.4 告警 / 抓拍 / 事件

> **底库 / 脸库 / Library 概念家族**:
>
> | 中文 | 拟用 EN | 备注 |
> |---|---|---|
> | 底库 (总称) | Reference Library | 顶层概念 |
> | 脸库 / 人脸库 | Face Library | "Face Library" 即"人脸底库",不需要带 Reference 字样 |
> | 工服库 | Uniform Library | "工服" = 工作服 |
> | 底库照 | Reference Photo | 底库中的单张照片 |
> | 人脸底库照 | Face Library Photo | Face Library 中的单张照片 |
>
> §5.4 中所有相关条目按此映射。

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 告警时间 | Alarm Time | Time | `table.header` | 7 | 中 | |
| 告警类型 | Alarm Type | Type | `table.header` | 5 | 低 | |
| 事件类型 | Event Type | Type | `table.header` | 6 | 低 | 与 Alarm Type 保持语义区分 |
| 前端大屏展示告警音 | Dashboard Alarm Sound | Alarm Sound | `dashboard.card` | 4 | 高 | 不用 Wall-screen |
| 抓拍照 | Snapshot | - | - | 10 | 低 | 与 Capture 的边界需统一 |
| 抓拍时间 | Snapshot Time | Time | `table.header` | 6 | 中 | 不用 Snap Time,除非空间极窄且有 tooltip |
| 抓拍添加 | Add from Snapshot | Snapshot | `tab.compact, btn.compact` | 4 | 中 | 与 Manual 对应 |
| 手动添加 | Add Manually | Manual | `tab.compact, btn.compact` | 4 | 中 | |
| 全景照 | Panorama | - | - | 10 | 低 | 如语境强调照片,可使用 Panorama Photo |
| 底库照 | Reference Photo | Reference | `table.header, dashboard.card` | 6 | 高 | 见上方"底库家族" |
| 检测照 | Detection Photo | Detection | `table.header, dashboard.card` | 4 | 高 | 不用 Detected (动词时态错位) |
| 人脸底库照 | Face Library Photo | Face Photo | `table.header, dashboard.card` | 4 | 高 | 与"脸库 = Face Library"保持一致 |
| 相似度 | Similarity | - | - | 8 | 中 | 不用 Sim.;窄表头用 tooltip |
| 所属脸库 | Face Library | Library | `table.header` | 5 | 中 | |
| 所属通道 | Channel | Ch. | `table.header` | 4 | 低 | 英文省略 "所属" |

### 5.5 任务管理

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 编辑任务 | Edit Task | - | - | 6 | 低 | |
| 新建任务 | New Task | - | - | 4 | 低 | |
| 新节点 | New Node | - | - | 4 | 低 | 流程图 |
| 添加组件 | Add Component | Add Comp. | `flow.node, btn.compact` | 4 | 中 | 默认仍用完整形式 |

### 5.6 人员/底库

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 姓名 | Name | - | - | 6 | 低 | |
| 人员编号 | Person ID | ID | `table.header` | 11 | 低 | |
| 人员姓名 | Person Name | Name | `table.header` | 5 | 低 | 与"姓名"统一为 Name |
| 新增人员 | Add Person | - | - | 4 | 低 | |
| 请选择工服库 | Please select uniform library | Select library | `placeholder` | 5 | 中 | "工服库" = Uniform Library |

### 5.7 系统资源

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 使用率 | Usage | - | - | 6 | 低 | 后端 DeviceInfo |
| 内存使用率 | Memory Usage | Mem Usage | `dashboard.card` | 3 | 低 | Mem 在监控面板可接受 |
| 丢包率 | Packet Loss Rate | Loss Rate | `dashboard.card, table.header` | 3 | 高 | |

## 6. LLM 协议字符串 (不译)

这些字符串用于发送给 VLM/LLM 或解析模型响应,属于协议语义,不作为 UI 文案翻译。任何改写都必须经过模型行为验证。

涉及文件:
- `src/flow/alarm/TaskAlarmLlmReview.cc`
- `src/flow/qwen3vl/Qwen3VLWorker.cc`
- `src/flow/qwen3vl/Qwen3VLInference.cc`
- `src/flow/qwen3vl/PQwen3VLWorker.cc`
- `src/flow/common/LlmYesNoJudge.h`

典型字符串: `"是"` `"否"` `"判断图片中是否存在【...】"` `"不要换行,不要其他内容。"` 等。

如需本地化模型 prompt,应作为独立任务处理,与 UI i18n 解耦。

## 7. 不独立翻译的碎片

下列 term 是字符串拼接碎片或单字,不能按字面单独翻译。处理时必须回到完整上下文,并优先改成完整 i18n key。

| 中文 | 频次 | 怀疑 |
|---|---|---|
| 关 | 8 | 可能是 switch Off,也可能是"关闭"碎片 |
| 闭 | 5 | 可能是"关闭"被切碎 |
| 人 | 4 | 可能是单位/量词,如 `{n} people` |
| 秒 | 8 | 量词,通常 `{n} seconds` |
| 文件 | 5 | 可能是单独名词,也可能是 `xxx 文件` |
| 取 / 消 | 3 / 3 | 明显是"取消"被切碎 |
| 限 / 以内 / 到 | 3 / 3 / 3 | 校验范围碎片 |

## 8. 格式与本地化约定

以下格式约定用于 Web UI 的默认显示。除非明确需要本地化格式,否则各 locale 共享同一格式。

| 维度 | 约定 | 说明 |
|---|---|---|
| 日期 | `YYYY-MM-DD HH:mm` | 工程上下文统一,默认 24h 制;不切换为 `Mar 15, 2026 3:00 PM`;避免歧义 (07/05 在 US/EU 含义不同) |
| 数字 | `Intl.NumberFormat(locale)` | 启用千分位:`zh-CN` 用 `,`、`en-US` 用 `,`,效果一致 |
| 文件大小 | `1.23 MB` (二进制单位) | 不用 `MiB`,与产品已有显示保持一致 |
| 时区 | UTC 偏移 `+08:00` | **不写 CST**(与 US Central 时间撞名);不写时区缩写 |
| 百分比 | `87%` | 无空格 |
| 货币 | 当前不定义 | 如新增货币展示,单独定义格式 |

## 9. I18N 实施规则

新增或修改 UI 文案时必须遵守以下规则。

1. **禁止泛化 success toast**:新代码禁止使用 `message.success.operation`、`message.toast.ok` 这类与具体动作无关的 key。每个成功 toast 必须绑定动词:`message.task.created` / `message.user.deleted` 等。**"操作成功"作为兼容 key 保留,不应用于新增文案**。
2. **禁止 Vue 模板直接含 CJK 字面量**:可见文案必须走 i18n key。
3. **禁止 i18n key 含 CJK 或拼音**:全部点分英文小写。
4. **新 Short 必须先进 GLOSSARY**:不能在 PR 中临时发明 Short 形式。
5. **Short scope 必须是受控值**:见 `SHORT-SCOPES.md`;CI 校验。**调用 Short 的唯一 API 是 `tShort(key, scope)`**:scope 必须在 `SHORT-SCOPES.md` 受控集中且与 GLOSSARY 中该 key 的 scope 列匹配。`t(key)` 永远返回当前 locale 全形式,不得通过 `t('key.short')` 之类的 key 路径绕开校验。scope 不匹配时,dev 模式抛错;prod 模式回落当前 locale 全形式 + 上报埋点。
6. **每个 Web UI PR 必须附 zh-CN / en-US / xx-pseudo 三套截图**:截图覆盖该 PR 修改的所有页面/对话框。
7. **API 改 messageKey 必须保留 message 兜底字段** 至少一个 release,前端先迁移再删字段。
8. **同一中文 term 对应多个英文语境必须拆 key**:如果同一个中文短语在不同业务/技术语境下要翻成不同英文,**必须拆成多个 i18n key**,不得用同一 key 在运行时按上下文选词。
   - **示例**:`类别` → `field.category`(业务分组,值为 "Category")+ `model.class`(分类器输出类别,值为 "Class")
   - **示例**:`登录` → `auth.button.signIn`(动作按钮,值为 "Sign in")+ `auth.label.login`(名词/技术上下文,值为 "Login")
   - **理由**:同 key 多义会让 i18n JSON 失去单源真相;翻译人员看 key 无法判断该填哪种英文;CI 也无法校验。**i18n 层不能藏语义模型问题**。
   - **触发判定**:GLOSSARY 或补充术语中,一个中文条目的 `EN` / `建议 EN` 列出现 `A / B` 形式且备注里写明"按 X 语境用 A、按 Y 语境用 B"时,必须拆 key。高风险汇总中的 `EN / Short` 展示不触发本规则。
9. **确认按钮文案三级政策**:
   - **信息弹窗**(纯通知/操作完成提示):按钮用 `OK`(对应中文`确定`),key 统一 `action.ok`
   - **危险/业务动作**(删除、发布、提交、保存、放弃修改等):**必须用具体动词**,严禁泛化为 "Confirm"。如 `Delete this item?` 按钮为 `Delete`、`Discard changes?` 按钮为 `Discard`、`Publish task?` 按钮为 `Publish`。每个动作单独命名 key(`action.delete`/`action.discard`/`action.publish`),不复用 `action.confirm`
   - **兜底**:仅当确实没有合适动词时(如多步向导的中间步骤),才用 `Confirm`,key 统一 `action.confirm`
   - **校验要求**:dialog/confirm 类组件中,若按钮 key 为 `action.confirm` 而 dialog 的 `type` 为 `warning`/`error` 或文案含 "delete"/"discard"/"publish" 等动词,应视为违规。
   - **校验补充**:`warning`/`error` 类型 dialog/confirm 中,确认按钮不得使用 `action.ok`;危险/业务动作必须使用具体动词 key。

## 10. 测试 locale: `xx-pseudo`

`xx-pseudo` 是用于本地化测试的伪 locale。启用后,所有可见文案自动加重音、方括号和 30-50% 长度膨胀,用于发现漏提取文案和英文布局溢出。

### 规则

1. **触发**:`useLocale('xx-pseudo')` 或在 LangSwitcher 增加"Pseudo"选项(仅 dev 构建可见)。
2. **转换函数**(实现在 `src/web/src/locales/pseudo.js`):
  - 字符替换表(只对 ASCII 字母):`A→Å`、`B→Ɓ`、`C→Ç`、`D→Ð`、`E→É`、`F→Ƒ`、`G→Ğ`、`H→Ħ`、`I→Ï`、`J→Ĵ`、`K→Ķ`、`L→Ŀ`、`M→Ḿ`、`N→Ń`、`O→Ø`、`P→Ƥ`、`Q→Ǫ`、`R→Ŕ`、`S→Ş`、`T→Ŧ`、`U→Û`、`V→Ṽ`、`W→Ŵ`、`X→Ẋ`、`Y→Ý`、`Z→Ž`;小写同形映射为 `å`、`ƀ`、`ç`、`ð`、`é`、`ƒ`、`ğ`、`ħ`、`ï`、`ĵ`、`ķ`、`ŀ`、`ḿ`、`ñ`、`ø`、`ƥ`、`ǫ`、`ŕ`、`ş`、`ŧ`、`û`、`ṽ`、`ŵ`、`ẋ`、`ý`、`ž`
   - **包裹**:输出整体外加 `[...]` 方括号
   - **均匀膨胀 30-50%**:在原字符串末尾追加重音填充字符 `ḓƒ` 直到达到目标长度;**用同一字符串的哈希作为种子**,保证同一 key 每次膨胀比例一致(避免布局视觉抖动)
3. **占位符保护**:`{x}` / `{namedSlot}` / `{count, plural, ...}` 等 vue-i18n 占位符语法**不参与替换或膨胀**,原样透传。实现上用正则 `/\{[^}]*\}/g` 切段,只对段间纯文本应用变换。
4. **HTML 实体保护**:`&lt;` / `&nbsp;` 等不参与替换。
5. **数字、标点、空格**不变换。

### 示例

| 原文 (en-US) | 伪 locale 输出 |
|---|---|
| `Save` | `[Şåṽéḓƒ]` |
| `Please enter server address` | `[Ƥŀéåşé éñŧéŕ şéŕṽéŕ åððŕéşşḓƒḓƒḓƒḓƒḓƒ]` |
| `Restarting. {n} remaining.` | `[Ŕéşŧåŕŧïñğ. {n} ŕéḿåïñïñğ.ḓƒḓƒḓƒḓƒḓƒḓƒḓ]` |
| `Delete this item? This cannot be undone.` | `[Ðéŀéŧé ŧħïş ïŧéḿ? Ŧħïş çåññøŧ ƀé ûñðøñé.ḓƒḓƒḓƒḓƒḓƒḓƒḓƒḓƒḓƒḓƒ]` |

### 用途与 CI 钩子

- **本地开发**:看到非方括号文本 = 漏 i18n;看到布局错位 = 真实英文会撑爆
- **PR 截图建议**:`zh-CN` / `en-US` / **`xx-pseudo`** 至少覆盖一种受影响界面
- **构建排除**:production 构建不包含 `xx-pseudo` locale 文件
- **失效检查**:如果发现某文案在 pseudo locale 下不带方括号,说明该文案可能是硬编码字符串,必须修复

## 11. 菜单项

> 菜单文字统一使用 `sidebar.menu` scope。菜单项虽然来自 `menu.js` 而不是业务术语表,仍必须进入 GLOSSARY,否则 `tShort(nav.*, 'sidebar.menu')` 无法通过运行时 scope 校验。

| 中文 | EN | Short | Short scope | 频次 | 风险 | 备注 |
|---|---|---|---|---|---|---|
| 运行总览 | Operation Overview | Overview | `sidebar.menu` | - | 中 | |
| 实时展示 | Live Display | Live | `sidebar.menu` | - | 中 | |
| 图片分析 | Image Analysis | Images | `sidebar.menu` | - | 中 | |
| 视频接入 | Video Access | Video | `sidebar.menu` | - | 中 | |
| 场景任务 | Scene Tasks | Tasks | `sidebar.menu` | - | 中 | 菜单沿用现有 nav.sceneTasks key |
| 事件中心 | Event Center | Events | `sidebar.menu` | - | 中 | |
| 检测/分析 | Detection / Analysis | Detection | `sidebar.menu` | - | 中 | |
| 人脸人体 | Face and Body | Faces | `sidebar.menu` | - | 中 | |
| 计数统计 | Counting Statistics | Counts | `sidebar.menu` | - | 中 | 菜单显示使用 Counting Statistics |
| 模型仓库 | Model Repository | Models | `sidebar.menu` | - | 中 | |
| 底库管理 | Base Library | Libraries | `sidebar.menu` | - | 中 | |
| 人脸底库 | Face Library | Faces | `sidebar.menu` | - | 中 | |
| 工服底库 | Uniform Library | Uniforms | `sidebar.menu` | - | 中 | |
| 物品底库 | Item Library | Items | `sidebar.menu` | - | 中 | |
| 数据对接 | Data Docking | Docking | `sidebar.menu` | - | 中 | |
| 音频管理 | Audio Management | Audio | `sidebar.menu` | - | 中 | |
| 外设管理 | Peripheral Management | Peripherals | `sidebar.menu` | - | 中 | |
| 网络音柱 | Network Speaker | Speakers | `sidebar.menu` | - | 中 | |
| 联动管理 | Linkage Management | Linkage | `sidebar.menu` | - | 中 | |
| 系统管理 | System Management | System | `sidebar.menu` | - | 中 | |
| 系统设置 | System Settings | Settings | `sidebar.menu` | - | 中 | |
| 网络设置 | Network Settings | Network | `sidebar.menu` | - | 中 | |
| 系统维护 | System Maintenance | Maintenance | `sidebar.menu` | - | 中 | |
| 个性化设置 | Personalization | Personalize | `sidebar.menu` | - | 中 | |

## 布局高风险术语

以下术语在英文界面中更容易造成布局膨胀。实现时应优先调整布局;只有 glossary 中提供 Short 且 scope 匹配时才使用短形式。

| 中文 | EN / Short | 处理要求 |
|---|---|---|
| 计数统计 | Counting Analytics / Counting | 完整标题使用 Counting Analytics;紧凑菜单或卡片可使用 Counting |
| 单机版 | Standalone Edition / Standalone | 状态徽标可使用 Standalone |
| 联网版 | Connected Edition / Connected | 状态徽标可使用 Connected |
| 算法服务 | Algorithm Service / Algo Service | 避免短成 Algorithm |
| 参数设置 | Parameter Settings / Settings | 仅在侧栏菜单或流程节点中使用 Settings |
| 灵敏度计算 | Sensitivity Calculation / Sensitivity | 紧凑流程节点可使用 Sensitivity |
| 前端大屏展示告警音 | Dashboard Alarm Sound / Alarm Sound | 大屏卡片可使用 Alarm Sound |
| 底库照 | Reference Photo / Reference | 与底库概念家族保持一致 |
| 检测照 | Detection Photo / Detection | 仅在表头或卡片中使用短形式 |
| 人脸底库照 | Face Library Photo / Face Photo | 仅在表头或卡片中使用短形式 |
| 授权失败 | Authorization failed / Auth failed | badge 可使用 Auth failed |
| 丢包率 | Packet Loss Rate / Loss Rate | dashboard 或表头可使用 Loss Rate |
| 说明 | Description / Desc. | 表头可使用 Desc. + tooltip |
| 默认 | Default | 不设短形式,通过布局或 tooltip 处理 |

## 补充术语

以下术语出现频次较低,但语义需要统一。新增 UI 文案时应复用这些译法。

### 业务实体

| 中文 | 建议 EN | 备注 |
|---|---|---|
| 任务名称 | Task Name | |
| 算法版本 | Algorithm Version | |
| 导入任务 | Import Task | |
| 运行策略 | Run Policy | 如强调执行策略,可使用 Execution Policy |
| 时间模板 | Time Template | |
| 脸库 | Face Library | 与"底库家族"一致 |
| 照片地址 | Photo URL | 如果是本地路径可用 Photo Path |
| 失败原因 | Failure Reason | |

### 车辆领域

| 中文 | 建议 EN | 备注 |
|---|---|---|
| 车牌号 | License Plate | 表单/详情默认用 License Plate;窄表头可用 Plate No. |
| 车类型 | Vehicle Type | |
| 车牌颜色 | Plate Color | |
| 车身颜色 | Vehicle Color | |
| 车辆朝向 | Vehicle Direction | |
| 运煤车类型 | Coal Truck Type | 行业词 |

### AI/算法术语

| 中文 | 建议 EN | 备注 |
|---|---|---|
| 类别 | Category / Class | 业务分组用 Category;模型输出类别用 Class |
| 置信度 | Confidence | 通用术语 |
| 分类 | Classification | |
| 原子追踪 | Atomic Tracking | 算法名 |
| 人体人脸检测 | Face and Body Detection | **与 §5.2"人脸人体"统一为 Face and Body 顺序** |
| 检测视觉大模型 | Detection Model | **不暴露 VLM 缩写到用户界面** |
| 语言视觉大模型 | Vision-Language Model | 同上,如用于用户界面避免缩写 |

### 状态扩展

| 中文 | 建议 EN | 备注 |
|---|---|---|
| 待处理 / 排队中 | Pending | **与 §4 "未上传 / Not uploaded" 是不同状态**,独立 key |

### 其他实测低频

| 中文 | 频次 | 建议 EN | 备注 |
|---|---|---|---|
| 升级 | 1 | Upgrade | 系统升级动作 |
| 导出 | 2 | Export | 导出 CSV/Excel 等 |
| 实时 | 2 | Real-time | 字段标签 |
| 节点 | 2 | Node | 流程图节点,与"新节点"一致 |
| 区域 | 1 | Area | 与"检测区域/区域名称"一致 |
| 登录 | 2 | Sign in / Login | 动作按钮用 Sign in;名词或技术上下文用 Login |
