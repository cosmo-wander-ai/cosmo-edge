# I18N Short Scope 受控枚举 v1.3

> GLOSSARY.md 中 "Short scope" 列**只能填写本表中的 scope ID**。新增 scope 必须改本表并通过 review。CI 校验:`scripts/i18n_check_scopes.py`(待 Phase 1 落实)。
>
> 设计原则:scope 描述的是"UI 容器的宽度预算受何种约束",不是"业务模块"。同一业务文案出现在不同容器时,可以同时绑定多个 scope。

## Scope 一览

| Scope ID | 名称 | 宽度约束 | 典型组件 |
|---|---|---|---|
| `btn.compact` | 紧凑按钮 | 固定宽 ≤100px 或居于密集工具栏 | `<el-button size="small">`、批量操作栏按钮 |
| `inline.action` | 表格行内操作 | 极窄,无按钮外框,纯文本链接 | `<el-table>` 行末"详情/编辑/删除" |
| `table.header` | 表格列头 | 列宽随数据/容器分配,不可预设 | `<el-table-column label>` |
| `tag.badge` | 状态徽标 | 单/双字宽度,严格 1 行不换行 | `<el-tag>`、`<el-badge>`、状态药丸 |
| `sidebar.menu` | 侧栏菜单 | 一级/二级菜单文字,固定宽 ~180px | `<el-menu>` 项 |
| `flow.node` | 流程图节点 | 节点框宽度受图布局限制 | Vue Flow 节点 label/算子名 |
| `dashboard.card` | 大屏/Dashboard 卡片标题 | 卡片头部一行 | 大屏指标卡、KPI 卡片 |
| `tab.compact` | 紧凑 tab 标题 | tab 已有外层语境,允许省略前缀 | `<el-tabs>` 紧凑 tab 项 |
| `placeholder` | 表单 placeholder | 输入框内提示,设计上接受省略 | `<el-input placeholder>` |

## 使用规则

1. **默认不需要 Short**。绝大多数文案用当前 locale 全形式即可。只有在 GLOSSARY 中明确给出 Short 且 scope 匹配,组件才允许调用 Short。
   - **唯一调用 API**:`tShort(key, scope)`。`t(key)` 永远返回当前 locale 全形式;不得通过 `t('key.short')` 之类的 key 路径绕开 scope 校验。
   - 运行时:`tShort` 校验 `scope ∈ GLOSSARY[key].scopes`,不匹配 → dev 模式抛错、prod 模式回落当前 locale 全形式 + 上报埋点。
2. **同一 term 可绑多个 scope**:scope 列写 `table.header, tag.badge` 表示这两个场景都可用。
3. **未列出的容器一律用当前 locale 全形式**:dialog 标题、toast、详情页字段标签、空状态、错误 banner 等。
4. **Short 必须有全称兜底**(按 scope 分组):

   **A 组 — 必须有全称兜底**:Short 使用时必须提供 tooltip、`title`、`aria-label` 或紧邻上下文中的全称。
   - `table.header` — 列头孤立,纯短形式易歧义
   - `tag.badge` — 状态徽标无外部上下文,需 tooltip 兼顾可访问性(屏幕阅读器/色盲)
   - `inline.action` — 纯文本链接,如 "Info" 单独看不知道是页面还是 icon
   - `sidebar.menu` — 菜单文字孤立,远离业务上下文
   - `dashboard.card` — 卡片标题离功能区远,需 tooltip 解释指标含义

   **B 组 — 不强制 tooltip,但必须在截图 review 中确认语义清楚**:
   - `placeholder` — 输入框设计上短暂出现,挂 tooltip 是 anti-pattern
   - `btn.compact` — 按钮通常是纯动词,语义自足
   - `tab.compact` — 相邻 tab 自然提供并列语境
   - `flow.node` — 节点图形 + 连线 + 颜色已经在视觉上提供语境

5. **CI 校验**:Phase 1 会加 lint,GLOSSARY 中 Short scope 列填了不在本表的 ID 直接 fail。

## 不在受控集中的特殊位置

| 位置 | 处理 |
|---|---|
| 视频 OSD | 实测无中文,本轮 I18N 不覆盖;后续若新增 OSD 文案,单独定义 scope |
| 邮件/导出 PDF | 本工程暂未涉及,后续 v2 补 |
| 浏览器 `<title>` | 一律用当前 locale 全形式 |

## 变更历史

- v1.3 (2026-05-29):规则 1/3 与特殊位置中的 fallback 语义从 EN 全形式调整为当前 locale 全形式,对齐 GLOSSARY v0.5。
- v1.2 (2026-05-29):规则 1 增补 `tShort(key, scope)` 唯一调用 API + 运行时校验策略,对齐 GLOSSARY v0.4 §9 第 5 条。
- v1.1 (2026-05-29):规则 4 拆 A/B 组,覆盖全部 9 个 scope 的 tooltip 策略。
- v1 (2026-05-29):初版,9 个 scope。
