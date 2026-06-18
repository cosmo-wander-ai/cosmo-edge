#!/usr/bin/env node
// resource_i18n_sync.mjs — Check or sync resource locale files between
// aiboxResource/i18n (source of truth) and public/resource-i18n (frontend copy).
//
// Usage:
//   node scripts/resource_i18n_sync.mjs --check   # compare, exit 1 on diff
//   node scripts/resource_i18n_sync.mjs --sync    # copy source → public, then check
//
// Environment:
//   AIBOX_RESOURCE_DIR  Override the aiboxResource repo root path.
//                       Default: ../../../aiboxResource  (relative to src/web)

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const webRoot = path.resolve(__dirname, '..')

// --- Resolve paths -----------------------------------------------------------

const aiboxDefault = path.resolve(webRoot, '..', '..', '..', 'aiboxResource')
const aiboxRoot = process.env.AIBOX_RESOURCE_DIR || aiboxDefault

const LOCALES = ['zh-CN', 'en-US']

const sourcePaths = Object.fromEntries(
  LOCALES.map(loc => [loc, path.join(aiboxRoot, 'i18n', `resource.${loc}.json`)])
)
const publicPaths = Object.fromEntries(
  LOCALES.map(loc => [loc, path.join(webRoot, 'public', 'resource-i18n', `resource.${loc}.json`)])
)

// --- Helpers -----------------------------------------------------------------

function readJson(filePath) {
  if (!fs.existsSync(filePath)) {
    return { ok: false, error: `File not found: ${filePath}` }
  }
  try {
    const raw = fs.readFileSync(filePath, 'utf8')
    const obj = JSON.parse(raw)
    if (!obj || typeof obj !== 'object' || Array.isArray(obj)) {
      return { ok: false, error: `Not a plain object: ${filePath}` }
    }
    return { ok: true, data: obj, raw }
  } catch (e) {
    return { ok: false, error: `JSON parse failed: ${filePath}: ${e.message}` }
  }
}

// --- Check -------------------------------------------------------------------

function check() {
  let pass = true
  const issues = []

  for (const loc of LOCALES) {
    const src = readJson(sourcePaths[loc])
    const pub = readJson(publicPaths[loc])

    if (!src.ok) {
      issues.push(`❌ Source ${loc}: ${src.error}`)
      pass = false
      continue
    }
    if (!pub.ok) {
      issues.push(`❌ Public ${loc}: ${pub.error}`)
      pass = false
      continue
    }

    const srcKeys = Object.keys(src.data).sort()
    const pubKeys = Object.keys(pub.data).sort()

    // Key set comparison
    const srcSet = new Set(srcKeys)
    const pubSet = new Set(pubKeys)
    const missInPub = srcKeys.filter(k => !pubSet.has(k))
    const extraInPub = pubKeys.filter(k => !srcSet.has(k))

    if (missInPub.length > 0) {
      issues.push(`❌ ${loc}: ${missInPub.length} key(s) in source but missing in public`)
      if (missInPub.length <= 5) missInPub.forEach(k => issues.push(`     + ${k}`))
      else issues.push(`     (first 5: ${missInPub.slice(0, 5).join(', ')} ...)`)
      pass = false
    }
    if (extraInPub.length > 0) {
      issues.push(`❌ ${loc}: ${extraInPub.length} extra key(s) in public not in source`)
      if (extraInPub.length <= 5) extraInPub.forEach(k => issues.push(`     - ${k}`))
      else issues.push(`     (first 5: ${extraInPub.slice(0, 5).join(', ')} ...)`)
      pass = false
    }

    // Value comparison (only for shared keys)
    const sharedKeys = srcKeys.filter(k => pubSet.has(k))
    const valueMismatches = []
    for (const k of sharedKeys) {
      if (src.data[k] !== pub.data[k]) {
        valueMismatches.push(k)
      }
    }
    if (valueMismatches.length > 0) {
      issues.push(`❌ ${loc}: ${valueMismatches.length} value mismatch(es)`)
      const show = valueMismatches.slice(0, 3)
      show.forEach(k => {
        issues.push(`     key: ${k}`)
        issues.push(`       source: ${JSON.stringify(src.data[k]).slice(0, 80)}`)
        issues.push(`       public: ${JSON.stringify(pub.data[k]).slice(0, 80)}`)
      })
      if (valueMismatches.length > 3) issues.push(`     ... and ${valueMismatches.length - 3} more`)
      pass = false
    }

    if (missInPub.length === 0 && extraInPub.length === 0 && valueMismatches.length === 0) {
      issues.push(`  ✅ ${loc}: ${srcKeys.length} keys, all match`)
    }
  }

  console.log('')
  issues.forEach(l => console.log(l))
  console.log('')

  if (pass) {
    console.log('═══════════════════════════════════════════════════════════')
    console.log(' ✅ Resource locale sync check passed.')
    console.log('═══════════════════════════════════════════════════════════')
  } else {
    console.log('═══════════════════════════════════════════════════════════')
    console.log(' ❌ Resource locale sync check FAILED.')
    console.log('    Run: npm run resource-i18n:sync')
    console.log('═══════════════════════════════════════════════════════════')
  }
  console.log('')
  return pass
}

// --- Sync --------------------------------------------------------------------

function sync() {
  console.log('\n[resource-i18n:sync] Syncing from aiboxResource → public/resource-i18n ...\n')

  // Validate sources first
  for (const loc of LOCALES) {
    const src = readJson(sourcePaths[loc])
    if (!src.ok) {
      console.error(`❌ Cannot sync: ${src.error}`)
      process.exit(1)
    }
    console.log(`  source ${loc}: ${Object.keys(src.data).length} keys`)
  }

  // Ensure target directory
  const pubDir = path.join(webRoot, 'public', 'resource-i18n')
  fs.mkdirSync(pubDir, { recursive: true })

  // Copy files (byte-for-byte to guarantee identical content)
  for (const loc of LOCALES) {
    fs.copyFileSync(sourcePaths[loc], publicPaths[loc])
    console.log(`  ✅ copied → public/resource-i18n/resource.${loc}.json`)
  }

  console.log('')

  // Auto-check after sync
  const ok = check()
  if (!ok) {
    process.exit(1)
  }
}

// --- Main --------------------------------------------------------------------

const args = process.argv.slice(2)
const mode = args[0]

if (mode === '--check') {
  const ok = check()
  process.exit(ok ? 0 : 1)
} else if (mode === '--sync') {
  sync()
} else {
  console.error('Usage: node scripts/resource_i18n_sync.mjs --check | --sync')
  process.exit(1)
}
