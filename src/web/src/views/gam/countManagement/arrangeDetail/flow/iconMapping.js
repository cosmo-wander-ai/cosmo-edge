/**
 * 节点图标映射表 —— CustomFormNode 与 NodeDetailPanel 共用
 *
 * key : FlowNodeIcon 的语义图标标识；节点与参数面板统一使用中性色。
 */
export const ICON_KEY_MAP = {
  // === 视频解码 ===
  'BA_00001': { key: 'video' },
  // === 追踪/关联 ===
  'AA_00003': { key: 'tracking' },
  'BA_00009': { key: 'tracking' },     // 混合目标关联
  // === 分类类 ===
  'AA_00002': { key: 'grid' },       // 目标分类算法
  'AA_00009': { key: 'grid' },       // 序列化分类算法
  'AA_00011': { key: 'target' },     // 车牌文字识别
  'AA_10002': { key: 'grid' },       // 关联目标分类
  'AA_20002': { key: 'grid' },       // 指定区域分类
  'AA_30002': { key: 'grid' },       // 目标属性算法
  'AA_40002': { key: 'grid' },       // 相机移动分类
  'PA_00002': { key: 'grid' },       // 目标分类(图片)
  // === 类别筛选 ===
  'BA_00002': { key: 'filter' },
  // === 灵敏度/计时 ===
  'BA_00003': { key: 'timer' },      // 灵敏度计算-计时
  'BA_10003': { key: 'timer' },      // 灵敏度计算-计数
  'BA_20003': { key: 'timer' },      // 目标行为过滤
  // === 事件上报/输出 ===
  'BA_00004': { key: 'send' },        // 事件上报
  'BA_00006': { key: 'send' },        // 目标抓拍策略
  'BA_10004': { key: 'send' },        // 行为类人脸特征上报
  // === 判断类 ===
  'BA_00005': { key: 'judge' },      // 区域告警判断
  'BA_00007': { key: 'judge' },      // 同类目标距离判断
  'BA_00008': { key: 'judge' },      // 滤波状态判断
  'BA_90001': { key: 'judge' },      // 目标判断
  'BA_90002': { key: 'judge' },      // 分支判断
  'PB_90001': { key: 'judge' },      // 目标判断(图片)
}

/**
 * 根据 actionId 获取图标信息
 * @param {string} actionId
 * @returns {{ key: string }}
 */
export const getIconInfo = (actionId) => {
  if (!actionId) return { key: 'default' }
  if (ICON_KEY_MAP[actionId]) return ICON_KEY_MAP[actionId]
  // regex fallback
  if (/^(AA|PA)_/.test(actionId)) return { key: 'target' }
  if (/^(DA|PDA)_/.test(actionId)) return { key: 'ai' }
  if (/^PB_/.test(actionId)) return { key: 'judge' }
  return { key: 'default' }
}
