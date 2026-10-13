// This blocking head script resolves appearance before the first paint. It has
// no application, authentication or network dependencies and survives logout.
;(function (window, document) {
  'use strict'
  const key = 'cosmo.appearance'
  const paletteKey = 'cosmo.palette'
  const valid = (value) => ['light', 'dark', 'system'].includes(value)
  const validPalette = (value) => ['modern', 'classic-blue'].includes(value)
  const media = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null
  const listeners = new Set()
  let preference = 'light'
  let resolved = 'light'
  let palette = 'modern'
  try {
    const saved = window.localStorage.getItem(key)
    if (valid(saved)) preference = saved
  } catch (_) { /* Appearance remains usable when browser storage is blocked. */ }
  try {
    const saved = window.localStorage.getItem(paletteKey)
    if (validPalette(saved)) palette = saved
  } catch (_) { /* The palette also works without persistent storage. */ }

  function apply() {
    resolved = preference === 'system' ? (media && media.matches ? 'dark' : 'light') : preference
    const root = document.documentElement
    root.dataset.theme = resolved
    root.dataset.appearance = preference
    root.dataset.palette = palette
    root.classList.toggle('dark', resolved === 'dark')
    root.style.colorScheme = resolved
    root.style.backgroundColor = resolved === 'dark' ? '#14171d' : '#f3f5f8'
    for (const listener of listeners) listener({ preference, resolved, palette })
  }

  function setPalette(value) {
    if (!validPalette(value)) return
    palette = value
    try { window.localStorage.setItem(paletteKey, value) } catch (_) { /* Use in-memory choice. */ }
    apply()
  }

  function setPreference(value) {
    if (!valid(value)) return
    preference = value
    try { window.localStorage.setItem(key, value) } catch (_) { /* Use in-memory choice. */ }
    apply()
  }

  function systemChanged() {
    if (preference === 'system') apply()
  }
  if (media && media.addEventListener) media.addEventListener('change', systemChanged)
  else if (media && media.addListener) media.addListener(systemChanged)
  window.addEventListener('storage', (event) => {
    if (event.key === null) {
      preference = 'light'
      palette = 'modern'
    } else if (event.key === key) {
      preference = valid(event.newValue) ? event.newValue : 'light'
    } else if (event.key === paletteKey) {
      palette = validPalette(event.newValue) ? event.newValue : 'modern'
    } else return
    apply()
  })
  window.cosmoAppearance = Object.freeze({
    getState: () => ({ preference, resolved, palette }),
    setPreference,
    setPalette,
    subscribe(listener) {
      listeners.add(listener)
      return () => listeners.delete(listener)
    }
  })
  apply()
})(window, document)
