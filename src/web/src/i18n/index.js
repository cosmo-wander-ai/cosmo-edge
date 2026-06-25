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
