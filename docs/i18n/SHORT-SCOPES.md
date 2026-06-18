# I18N Short Scope Rules

This file defines the controlled scope IDs that may appear in the `Short scope`
column of `GLOSSARY.md`. A short label is allowed only when its scope is
listed here and the corresponding glossary entry explicitly opts in to that
scope.

Scopes describe UI container constraints, not business modules. The same text
may use different scopes when it appears in different UI containers.

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

1. 默认使用当前 locale 的完整文案。只有 glossary 明确给出 Short 且 scope 匹配时,组件才可以调用短形式。
   - 唯一调用 API:`tShort(key, scope)`。
   - `t(key)` 始终返回当前 locale 的完整文案。
   - 不得通过 `t('key.short')` 等 key 路径绕开 scope 校验。
2. 同一 term 可绑定多个 scope。scope 列写 `table.header, tag.badge` 表示这两个场景都允许使用短形式。
3. 未列出的容器一律使用完整文案,例如 dialog 标题、toast、详情字段标签、空状态、错误 banner。
4. Short 必须有完整语义兜底。按 scope 分组:

   **A 组 — 必须提供全称兜底**:使用 Short 时必须提供 tooltip、`title`、`aria-label` 或紧邻上下文中的全称。
   - `table.header` — 列头孤立,纯短形式易歧义
   - `tag.badge` — 状态徽标无外部上下文,需 tooltip 兼顾可访问性(屏幕阅读器/色盲)
   - `inline.action` — 纯文本链接,如 "Info" 单独看不知道是页面还是 icon
   - `sidebar.menu` — 菜单文字孤立,远离业务上下文
   - `dashboard.card` — 卡片标题离功能区远,需 tooltip 解释指标含义

   **B 组 — 不强制 tooltip,但 UI 语义必须清楚**:
   - `placeholder` — 输入框设计上短暂出现,挂 tooltip 是 anti-pattern
   - `btn.compact` — 按钮通常是纯动词,语义自足
   - `tab.compact` — 相邻 tab 自然提供并列语境
   - `flow.node` — 节点图形 + 连线 + 颜色已经在视觉上提供语境

5. 校验脚本会拒绝未在本表登记的 Short scope。

## 不在受控集中的特殊位置

| 位置 | 处理 |
|---|---|
| 视频 OSD | 如需本地化 OSD 文案,单独定义 scope |
| 邮件/导出 PDF | 如需本地化导出内容,单独定义 scope |
| 浏览器 `<title>` | 一律用当前 locale 全形式 |
