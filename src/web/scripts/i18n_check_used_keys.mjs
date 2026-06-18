// i18n used-key check script
//
// Scans src/**/*.{vue,js,mjs,ts} for statically-referenced i18n keys and
// verifies that every key exists in both zh-CN.js and en-US.js locale files.
//
// Supported key patterns:
//   t('key')  t("key")  $t('key')  $t("key")  proxy.$t('key')
//   nameI18nKey: 'key'   descriptionI18nKey: "key"   (and other *I18nKey props)

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const srcDir = path.resolve(__dirname, '..', 'src')
const localeDir = path.join(srcDir, 'i18n', 'locales')

// ── Whitelist ────────────────────────────────────────────────────────────
// Keys that appear in source but are built dynamically or come from external
// config, so they won't be found via static scanning of locale files.
// Add entries here with a comment explaining the reason.
const WHITELIST = new Set([
])

// ── Flatten locale object to dot-path set ────────────────────────────────
const flattenKeys = (obj, prefix = '') => {
  if (!obj || typeof obj !== 'object' || Array.isArray(obj)) return [prefix]
  return Object.entries(obj).flatMap(([k, v]) =>
    flattenKeys(v, prefix ? `${prefix}.${k}` : k)
  )
}

const importLocale = async (name) => {
  const url = pathToFileURL(path.join(localeDir, `${name}.js`)).href
  return (await import(url)).default
}

// ── Collect source files ─────────────────────────────────────────────────
const EXTENSIONS = new Set(['.vue', '.js', '.mjs', '.ts'])

const collectFiles = (dir) => {
  const entries = fs.readdirSync(dir, { withFileTypes: true })
  return entries.flatMap((entry) => {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) {
      // Skip non-source dirs
      if (entry.name === 'node_modules' || entry.name === 'dist') return []
      return collectFiles(full)
    }
    return EXTENSIONS.has(path.extname(entry.name)) ? [full] : []
  })
}

// ── Key extraction regexes ───────────────────────────────────────────────
// 1. t('key')  $t('key')  proxy.$t('key')  — single/double quotes
const RE_T_CALL = /(?:^|[^\w$])(?:t|\$t|proxy\.\$t)\(\s*['"]([A-Za-z0-9_.-]+)['"]\s*[),]/g

// 2. *I18nKey: 'key'  *I18nKey: "key"
const RE_I18N_KEY_PROP = /\b(?:label|title|name|description|failedTip|placeholder)I18nKey\s*:\s*['"]([A-Za-z0-9_.-]+)['"]/g

// ── Main ─────────────────────────────────────────────────────────────────
const [zhCN, enUS] = await Promise.all([
  importLocale('zh-CN'),
  importLocale('en-US')
])

const zhKeys = new Set(flattenKeys(zhCN))
const enKeys = new Set(flattenKeys(enUS))

const files = collectFiles(srcDir)
const errors = []          // { key, locale, file, line }
const checkedKeys = new Set()

for (const file of files) {
  const content = fs.readFileSync(file, 'utf8')
  const lines = content.split('\n')
  const relPath = path.relative(process.cwd(), file)

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const lineNo = i + 1

    for (const re of [RE_T_CALL, RE_I18N_KEY_PROP]) {
      re.lastIndex = 0
      let m
      while ((m = re.exec(line)) !== null) {
        const key = m[1]
        if (WHITELIST.has(key)) continue
        checkedKeys.add(key)

        if (!zhKeys.has(key)) {
          errors.push({ key, locale: 'zh-CN', file: relPath, line: lineNo })
        }
        if (!enKeys.has(key)) {
          errors.push({ key, locale: 'en-US', file: relPath, line: lineNo })
        }
      }
    }
  }
}

if (errors.length) {
  // De-duplicate: same key+locale only show first occurrence
  const seen = new Set()
  const unique = errors.filter((e) => {
    const id = `${e.key}|${e.locale}`
    if (seen.has(id)) return false
    seen.add(id)
    return true
  })

  console.error(`Missing i18n keys (${unique.length} issue(s)):`)
  for (const e of unique) {
    console.error(`  [${e.locale}] ${e.key}  (${e.file}:${e.line})`)
  }
  process.exit(1)
}

console.log(`I18N used keys OK (${checkedKeys.size} keys checked).`)
