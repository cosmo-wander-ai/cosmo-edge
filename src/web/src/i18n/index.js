import { computed, ref } from 'vue'
import { createI18n } from 'vue-i18n'
import zhCnElement from 'element-plus/es/locale/lang/zh-cn'
import enElement from 'element-plus/es/locale/lang/en'
import zhCN from './locales/zh-CN'
import enUS from './locales/en-US'
import { createPseudoMessages } from './pseudo'
import { shortGlossary } from './glossary'
import { SHORT_SCOPE_SET } from './shortScopes'
import { loadResourceLocale } from '@/utils/resourceLocaleLoader'

export const DEFAULT_LOCALE = 'zh-CN'
export const LOCALE_STORAGE_KEY = 'cosmo.locale'
export const LEGACY_LOCALE_STORAGE_KEY = 'language'

export const localeOptions = [
  { value: 'zh-CN', labelKey: 'locale.zh-CN' },
  { value: 'en-US', labelKey: 'locale.en-US' },
  ...(import.meta.env.DEV ? [{ value: 'xx-pseudo', label: 'Pseudo' }] : [])
]

const baseMessages = {
  'zh-CN': zhCN,
  'en-US': enUS
}

export const messages = import.meta.env.DEV
  ? { ...baseMessages, 'xx-pseudo': createPseudoMessages(enUS) }
  : baseMessages

const normalizeLocale = (locale) => {
  const normalized = String(locale || '').replace('_', '-')
  if (normalized === 'zh') return 'zh-CN'
  if (normalized === 'en') return 'en-US'
  return Object.prototype.hasOwnProperty.call(messages, normalized) ? normalized : DEFAULT_LOCALE
}

const getStoredLocale = () => {
  if (typeof localStorage === 'undefined') return DEFAULT_LOCALE
  return normalizeLocale(
    localStorage.getItem(LOCALE_STORAGE_KEY) ||
    localStorage.getItem(LEGACY_LOCALE_STORAGE_KEY) ||
    navigator.language
  )
}

export const currentLocale = ref(getStoredLocale())

export const i18n = createI18n({
  legacy: false,
  globalInjection: true,
  locale: currentLocale.value,
  fallbackLocale: 'en-US',
  messages,
  missingWarn: import.meta.env.DEV,
  fallbackWarn: import.meta.env.DEV
})

export const elementLocale = computed(() => {
  if (currentLocale.value === 'en-US' || (import.meta.env.DEV && currentLocale.value === 'xx-pseudo')) {
    return enElement
  }
  return zhCnElement
})

export const setLocale = (locale) => {
  const nextLocale = normalizeLocale(locale)
  currentLocale.value = nextLocale
  i18n.global.locale.value = nextLocale
  if (typeof localStorage !== 'undefined') {
    localStorage.setItem(LOCALE_STORAGE_KEY, nextLocale)
    localStorage.setItem(LEGACY_LOCALE_STORAGE_KEY, nextLocale.replace('-', '_'))
  }
  // Load resource locale for the new language (fire-and-forget).
  // Static import is used here to match main.js; loadResourceLocale has an
  // internal cache so repeated calls for the same locale are no-ops.
  loadResourceLocale(nextLocale)
}

setLocale(currentLocale.value)

export const t = (key, params = {}) => {
  // eslint-disable-next-line no-unused-expressions
  currentLocale.value // Track dependency
  return i18n.global.t(key, params)
}

// Locale-aware colon: English and pseudo locales use ': ', zh-CN uses '：'.
export const localeColon = computed(() =>
  currentLocale.value === 'zh-CN' ? '：' : ': '
)

const reportInvalidShortScope = (key, scope) => {
  const message = `[i18n] Invalid short scope "${scope}" for key "${key}"`
  if (import.meta.env.DEV) {
    throw new Error(message)
  }
  console.warn(message)
}

export const tShort = (key, scope, params = {}) => {
  const entry = shortGlossary[key]
  const fullKey = entry?.fullKey || key
  if (!SHORT_SCOPE_SET.has(scope) || !entry?.scopes?.includes(scope)) {
    reportInvalidShortScope(key, scope)
    return i18n.global.t(fullKey, params)
  }
  const shortKey = `short.${key}`
  if (!i18n.global.te(shortKey)) {
    return i18n.global.t(fullKey, params)
  }
  return i18n.global.t(shortKey, params)
}

export const translateApiMessage = (messageKey, fallback, params = {}) => {
  if (messageKey && i18n.global.te(messageKey)) {
    return t(messageKey, params)
  }
  return fallback || t('api.requestFailed')
}

// Pre-built list of known action names from i18n keys for efficient lookup.
// Each entry has a `cn` (Chinese text) and a `key` (i18n key path).
const _actionNameEntries = (() => {
  const keys = [
    '目标检测算法', '目标分类算法', '追踪算法', '关键点算法',
    '特征提取算法', '视频解码', '类别筛选', '尺寸过滤', '灵敏度计算-计时',
    '事件上报', '区域告警判断', '灵敏度计算-计数', '目标判断',
    '检测视觉大模型', '分割视觉大模型', '语言视觉大模型',
    '算法告警数据', '网络音柱联动'
  ]
  return keys.map(cn => ({ cn, key: `flow.actionNames.${cn}` }))
})()

// Translate a raw process/flow name string that may contain embedded Chinese
// action names (e.g. "taskId-目标检测算法-flowActionId").
// Replaces each known Chinese action name with its locale-aware translation.
export const translateActionName = (rawName) => {
  if (!rawName) return rawName
  let result = rawName
  for (const { cn, key } of _actionNameEntries) {
    if (result.includes(cn)) {
      const translated = i18n.global.te(key) ? t(key) : cn
      result = result.replace(cn, translated)
    }
  }
  return result
}

// Pre-built list of known algorithm names from i18n keys.
const _algorithmNameKeys = [
  '吸烟检测', '火焰检测', '人流量统计', '打电话检测',
  '视觉分割大模型分析', '离岗检测', '未戴安全帽', '玩手机检测',
  '人脸比对', '未穿反光衣', '分割大模型', '检测大模型',
  '视觉语言大模型分析', '检测类算法', '区域入侵', '区域人数统计',
  '绊线检测', '未穿工服', '车辆违停', '人脸识别算法',
  '人员跌倒', '人员聚集', '睡岗检测', '烟雾检测',
  '检测后分割算法', '检测后分类算法', '视觉检测大模型分析',
  '视觉语言大模型', 'No Safety Helmet'
]

// Translate a raw algorithm service name.
// Performs a direct lookup in the flow.algorithmNames i18n namespace
// and falls back to the original text when no translation exists.
export const translateAlgorithmName = (rawName) => {
  if (!rawName) return rawName
  const key = `flow.algorithmNames.${rawName}`
  if (i18n.global.te(key)) {
    return t(key)
  }
  return rawName
}
