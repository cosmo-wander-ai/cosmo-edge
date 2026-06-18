// Resource locale loader — fetches resource i18n JSON files from the static
// public directory and merges them into vue-i18n messages at runtime.
//
// Resource keys live under the "resource.*" namespace, which is disjoint from
// the app's own locale keys, so mergeLocaleMessage is safe and will not
// overwrite existing translations.
//
// IMPORTANT: The resource JSON files use flat dot-separated keys (e.g.
// "resource.action.aa_00001.actionname"). vue-i18n's te() only traverses
// nested objects, so we convert flat keys to a nested structure before merging.
// This ensures both te() and t() work correctly for the resolver.
//
// Usage:
//   - At app startup:  await loadResourceLocale('zh-CN')
//   - On locale switch: loadResourceLocale('en-US')  (fire-and-forget is OK)

import { i18n } from '@/i18n'

/**
 * Cache: locale → true (loaded) | Promise (in-flight) | undefined (not started)
 * Using a Promise cache avoids duplicate concurrent fetches for the same locale.
 */
const cache = {}

/**
 * Convert a flat { "a.b.c": value } object to nested { a: { b: { c: value } } }.
 * Required because vue-i18n's te() only walks nested structures.
 */
function flatToNested(obj) {
  const result = {}
  for (const [key, value] of Object.entries(obj)) {
    const parts = key.split('.')
    let current = result
    for (let i = 0; i < parts.length - 1; i++) {
      if (!current[parts[i]] || typeof current[parts[i]] !== 'object') {
        current[parts[i]] = {}
      }
      current = current[parts[i]]
    }
    current[parts[parts.length - 1]] = value
  }
  return result
}

/**
 * Load resource locale messages for the given locale and merge into vue-i18n.
 *
 * - Uses `import.meta.env.BASE_URL` to build the fetch URL.
 * - Validates that the fetched JSON is a plain object before merging.
 * - Converts flat dot-keys to nested structure for te() compatibility.
 * - On failure, logs a warning and silently falls back (resolvers will use
 *   the original Chinese text via their fallback logic).
 * - Concurrent calls for the same locale share the same Promise.
 * - Already-loaded locales return immediately.
 *
 * @param {string} [locale] - e.g. 'zh-CN' or 'en-US'. Defaults to current.
 * @returns {Promise<void>}
 */
export async function loadResourceLocale(locale) {
  const lang = locale || i18n.global.locale.value
  // Already loaded — skip
  if (cache[lang] === true) return
  // In-flight — reuse the existing promise
  if (cache[lang] instanceof Promise) return cache[lang]

  const url = `${import.meta.env.BASE_URL}resource-i18n/resource.${lang}.json`

  const promise = (async () => {
    try {
      const res = await fetch(url)
      if (!res.ok) {
        throw new Error(`HTTP ${res.status} ${res.statusText}`)
      }
      const messages = await res.json()
      // Validate: must be a plain object
      if (!messages || typeof messages !== 'object' || Array.isArray(messages)) {
        throw new Error('Invalid JSON: expected a plain object')
      }
      // Convert flat dot-keys to nested so te() can find them
      const nested = flatToNested(messages)
      i18n.global.mergeLocaleMessage(lang, nested)
      cache[lang] = true
    } catch (e) {
      console.warn(`[resource-i18n] Failed to load ${lang}, fallback to original text`, e)
      // Mark as failed so we don't retry endlessly, but don't mark as loaded
      // so a page reload can retry. Remove the promise from cache.
      delete cache[lang]
    }
  })()

  cache[lang] = promise
  return promise
}

