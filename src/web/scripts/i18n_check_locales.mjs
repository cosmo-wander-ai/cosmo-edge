import { pathToFileURL } from 'node:url'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const localeDir = path.resolve(__dirname, '..', 'src', 'i18n', 'locales')

const flattenKeys = (value, prefix = '') => {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return [prefix]
  return Object.entries(value).flatMap(([key, child]) => flattenKeys(child, prefix ? `${prefix}.${key}` : key))
}

const importLocale = async (name) => {
  const moduleUrl = pathToFileURL(path.join(localeDir, `${name}.js`)).href
  const module = await import(moduleUrl)
  return module.default
}

const [zhCN, enUS] = await Promise.all([
  importLocale('zh-CN'),
  importLocale('en-US')
])

const zhKeys = new Set(flattenKeys(zhCN))
const enKeys = new Set(flattenKeys(enUS))
const missingInZh = [...enKeys].filter(key => !zhKeys.has(key))
const missingInEn = [...zhKeys].filter(key => !enKeys.has(key))
const shortKeys = [...enKeys].filter(key => key.startsWith('short.'))
const orphanShortKeys = shortKeys
  .map(key => key.slice('short.'.length))
  .filter(key => !enKeys.has(key))

if (missingInZh.length || missingInEn.length || orphanShortKeys.length) {
  if (missingInZh.length) {
    console.error('Missing zh-CN locale keys:')
    missingInZh.forEach(key => console.error(`- ${key}`))
  }
  if (missingInEn.length) {
    console.error('Missing en-US locale keys:')
    missingInEn.forEach(key => console.error(`- ${key}`))
  }
  if (orphanShortKeys.length) {
    console.error('Short locale keys without full keys:')
    orphanShortKeys.forEach(key => console.error(`- short.${key} -> ${key}`))
  }
  process.exit(1)
}

console.log(`I18N locale keys OK (${enKeys.size} keys).`)
