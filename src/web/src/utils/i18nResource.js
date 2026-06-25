// i18n resource resolver for dynamic backend config items.
//
// When the backend returns objects with *I18nKey fields (e.g. nameI18nKey,
// descriptionI18nKey), these helpers resolve the locale text first, falling
// back to the original field value when the key is missing or absent.

import { i18n } from '@/i18n'

const te = (key) => key && i18n.global.te(key)
const t = (key) => i18n.global.t(key)

const sanitizeSegment = (value) =>
  String(value)
    .toLowerCase()
    .replace(/[^a-z0-9]/g, '_')
    .replace(/_{2,}/g, '_')
    .replace(/^_|_$/g, '')

const zhFallbackTerms = [
  ['图片语言视觉大模型', 'Image-Language-Vision Foundation Model'],
  ['图片分割大模型', 'Image-Segmentation Foundation Model'],
  ['图片检测大模型', 'Image-Detection Foundation Model'],
  ['图片检测分类任务', 'Image Detection & Classification Task'],
  ['图片检测任务', 'Image Detection Task'],
  ['视觉语言大模型', 'Vision-Language Foundation Model'],
  ['视觉分割大模型', 'Vision-Segmentation Foundation Model'],
  ['视觉检测大模型', 'Vision-Detection Foundation Model'],
  ['区域入侵', 'Regional Intrusion'],
  ['吸烟检测', 'Smoking Detection'],
  ['区域人数统计', 'Regional People Counting'],
  ['人脸比对', 'Face Comparison'],
  ['未穿工服', 'No Uniform'],
  ['人流量统计', 'Passenger Flow Statistics'],
  ['安全帽检测算法', 'Safety Helmet Detection Algorithm'],
  ['安全帽手机检测', 'Safety Helmet Phone Detection'],
  ['玩手机检测', 'Phone Usage Detection'],
  ['手机检测', 'Phone Detection'],
  ['离岗检测', 'Off-post Detection'],
  ['未戴安全帽', 'No Safety Helmet'],
  ['最小headShoulder尺寸', 'Minimum Head-shoulder Size'],
  ['headShoulder检测方式', 'Head-shoulder Detection Method'],
  ['category0检测方式', 'Category 0 Detection Method'],
  ['区域名称', 'Region Name'],
  ['告警时间间隔（秒）', 'Alarm Interval (sec)'],
  ['告警时间间隔', 'Alarm Interval'],
  ['告警次数', 'Alarm Count'],
  ['灵敏度', 'Sensitivity'],
  ['静止目标去重时间(小时)', 'Stationary Target Deduplication Duration (hours)'],
  ['静止目标去重时间（小时）', 'Stationary Target Deduplication Duration (hours)'],
  ['静止目标去重时间', 'Stationary Target Deduplication Duration'],
  ['静止目标去重', 'Stationary Target Deduplication'],
  ['静止目标重叠率', 'Stationary Target Overlap Rate'],
  ['区域中的目标数（建议改名）', 'Target Count in Region (Suggest Rename)'],
  ['区域中的目标数', 'Target Count in Region'],
  ['追踪历史帧数', 'Tracking History Frames'],
  ['追踪半径', 'Tracking Radius'],
  ['追踪帧数', 'Tracking Frames'],
  ['静止阈值', 'Still Threshold'],
  ['检测时间单位', 'Detection Time Unit'],
  ['检测时间', 'Detection Time'],
  ['检测方式', 'Detection Method'],
  ['最小', 'Minimum '],
  ['尺寸', ' Size'],
  ['底部', 'Bottom'],
  ['中心', 'Center'],
  ['顶部', 'Top'],
  ['置信度偏移', ' Confidence Offset'],
  ['置信度', ' Confidence'],
  ['category0', 'Category 0'],
  ['headShoulder', 'Head-shoulder'],
  ['安全帽', 'Safety Helmet'],
  ['头盔', 'Helmet'],
  ['行人', 'Pedestrian'],
  ['检测', ' Detection'],
  ['秒', 's'],
  ['小时', 'hours']
]

const hasChinese = (value) =>
  typeof value === 'string' && /[\u3400-\u9fff]/.test(value)

export const resolveResourceFallbackText = (value) => {
  if (!hasChinese(value) || i18n.global.locale.value === 'zh-CN') return value
  let result = zhFallbackTerms.reduce(
    (text, [source, target]) => text.replaceAll(source, target),
    value
  )
  // Normalize Chinese parentheses to English
  result = result.replace(/（/g, ' (').replace(/）/g, ')')
  // Insert space between concatenated English segments, e.g. "PedestrianDetection" -> "Pedestrian Detection".
  result = result.replace(/([a-z)])([A-Z])/g, '$1 $2')
  return result.replace(/\s+/g, ' ').trim()
}

/**
 * Resolve a single i18n-enabled field on an item.
 *
 * Priority:
 *   1. item[`${field}I18nKey`] exists AND locale contains the key → t(key)
 *   2. Otherwise → item[field] || fallback
 *
 * @param {Object} item   - The backend config object
 * @param {string} field  - Base field name, e.g. 'name', 'description', 'failedTip'
 * @param {string} [fallback=''] - Fallback when both key and field are empty
 * @returns {string}
 */
export const resolveI18nText = (item, field = 'name', fallback = '') => {
  if (!item) return fallback
  const key = item[`${field}I18nKey`]
  if (key && te(key)) {
    return t(key)
  }
  return resolveResourceFallbackText(item[field] ?? fallback)
}

/**
 * Resolve an option's display label.
 *
 * Priority:
 *   1. option.labelI18nKey exists AND locale contains the key → t(key)
 *   2. option.label || option.name || ''
 *
 * @param {Object} option
 * @returns {string}
 */
export const resolveI18nOptionLabel = (option) => {
  if (!option) return ''
  const key = option.labelI18nKey
  if (key && te(key)) {
    return t(key)
  }
  return resolveResourceFallbackText(option.label || option.name || '')
}

/**
 * Resolve task/service parameter text when backend payloads do not carry
 * sidecar i18n keys. The resource scanner uses stable keys based on
 * algorithmCode + param key for algorithmMetadata params, so derive that as
 * a fallback after trying the explicit *I18nKey field.
 *
 * @param {Object} item
 * @param {string|number} algorithmCode
 * @param {string} field
 * @param {string} fallback
 * @returns {string}
 */
export const resolveResourceParamText = (
  item,
  algorithmCode,
  field = 'name',
  fallback = ''
) => {
  const resolved = resolveI18nText(item, field, fallback)
  if (!item || item[`${field}I18nKey`] || !algorithmCode || !item.key) {
    return resolved
  }

  const paramKey = sanitizeSegment(item.key)
  const fieldSuffix = sanitizeSegment(field)
  const key = `resource.param.${algorithmCode}.${algorithmCode}.${paramKey}.${fieldSuffix}`
  return te(key) ? t(key) : resolveResourceFallbackText(resolved)
}

/**
 * Resolve option labels for task/service parameter payloads without
 * labelI18nKey. Falls back to resource.option.<algorithm>.<param>.<value>.
 *
 * @param {Object} option
 * @param {Object} parentItem
 * @param {string|number} algorithmCode
 * @returns {string}
 */
export const resolveResourceParamOptionLabel = (
  option,
  parentItem,
  algorithmCode
) => {
  const resolved = resolveI18nOptionLabel(option)
  if (!option || option.labelI18nKey || !parentItem?.key || !algorithmCode) {
    return resolved
  }

  const paramKey = sanitizeSegment(parentItem.key)
  const optionValue = option.value
  if (optionValue === undefined || optionValue === '') return resolved

  const key =
    `resource.option.${algorithmCode}.${algorithmCode}.${paramKey}.` +
    `${sanitizeSegment(optionValue)}.options_name`
  return te(key) ? t(key) : resolveResourceFallbackText(resolved)
}

export const resolveResourceAlgorithmName = (item) => {
  if (!item) return ''
  const code = item.algorithmCode || item.algorithmId
  if (code) {
    const key = `resource.algorithm.${String(code).toLowerCase()}.algorithmname`
    if (te(key)) return t(key)
  }
  return resolveResourceFallbackText(item.algorithmName || item.name || '')
}

/**
 * Resolve an algorithm's remark (description) by deriving the i18n key
 * from the algorithm's stable code/ID.
 *
 * Key pattern: resource.algorithm.<algorithmCode>.remark
 *
 * Falls back to item.remark with zhFallbackTerms translation.
 *
 * @param {Object} item - Algorithm object with algorithmCode/algorithmId and remark
 * @returns {string}
 */
export const resolveResourceAlgorithmRemark = (item) => {
  if (!item) return ''
  const code = item.algorithmCode || item.algorithmId
  if (code) {
    const key = `resource.algorithm.${String(code).toLowerCase()}.remark`
    if (te(key)) return t(key)
  }
  return resolveResourceFallbackText(item.remark || '')
}

/**
 * Resolve a full config item, returning a shallow copy with display-ready fields.
 *
 * Resolved fields: name, description, failedTip, placeholder
 * Options: each option's label/name resolved via resolveI18nOptionLabel.
 *
 * The original object is NOT mutated.
 *
 * @param {Object} item
 * @returns {Object} - Shallow copy with resolved display strings
 */
export const resolveI18nConfigItem = (item) => {
  if (!item) return item
  const resolved = { ...item }
  // Resolve text fields
  for (const field of ['name', 'description', 'failedTip', 'placeholder']) {
    resolved[field] = resolveI18nText(item, field)
  }
  // Resolve options
  if (Array.isArray(item.options)) {
    resolved.options = item.options.map((opt) => ({
      ...opt,
      name: resolveI18nOptionLabel(opt),
      label: resolveI18nOptionLabel(opt)
    }))
  }
  return resolved
}

/**
 * Resolve a resource action/component display name by deriving the i18n key
 * from the item's stable ID.
 *
 * The backend atomic-action-list API only returns id / actionName (the DTO
 * does not carry actionNameI18nKey), so we construct the key ourselves:
 *   AA_00001 → resource.action.aa_00001.actionname
 *   IP0001   → resource.action.ip0001.componentname
 *   PR0001   → resource.action.pr0001.componentname
 *   LA_*     → resource.action.la_alarmdata_code.actionname  (linkage)
 *
 * Falls back to item.actionName / item.componentName / item.name.
 *
 * @param {Object} item - Action or component object with at least `id`
 * @param {'actionName'|'componentName'} [field='actionName'] - Display field name
 * @returns {string}
 */
export const resolveResourceActionName = (item, field = 'actionName') => {
  if (!item) return ''
  const id = item.id || item.actionId
  if (id) {
    const suffix = field === 'componentName' ? 'componentname' : 'actionname'
    const key = `resource.action.${String(id).toLowerCase()}.${suffix}`
    if (te(key)) return t(key)
  }
  return item[field] || item.actionName || item.name || ''
}

/**
 * Resolve a resource action remark (description hint) by deriving the i18n
 * key from the item's stable actionId.
 *
 * Key pattern: resource.action.<actionId>.remark
 *
 * Falls back to item.remark with zhFallbackTerms translation.
 *
 * @param {Object} item - Action or flow item with actionId and remark
 * @returns {string}
 */
export const resolveResourceActionRemark = (item) => {
  if (!item) return ''
  const id = item.id || item.actionId
  if (id) {
    const key = `resource.action.${String(id).toLowerCase()}.remark`
    if (te(key)) return t(key)
  }
  return resolveResourceFallbackText(item.remark || item.description || '')
}
