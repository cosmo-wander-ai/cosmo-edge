// This blocking head script resolves appearance before the first paint. It has
// no application, authentication or network dependencies and survives logout.
;(function (window, document) {
  'use strict'
  const key = 'cosmo.appearance'
  const valid = (value) => ['light', 'dark', 'system'].includes(value)
  const media = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null
  const listeners = new Set()
  let preference = 'light'
  let resolved = 'light'
  try {
    const saved = window.localStorage.getItem(key)
    if (valid(saved)) preference = saved
  } catch (_) { /* Appearance remains usable when browser storage is blocked. */ }

  function apply() {
    resolved = preference === 'system' ? (media && media.matches ? 'dark' : 'light') : preference
    const root = document.documentElement
    root.dataset.theme = resolved
    root.dataset.appearance = preference
    root.classList.toggle('dark', resolved === 'dark')
    root.style.colorScheme = resolved
    root.style.backgroundColor = resolved === 'dark' ? '#14171d' : '#f3f5f8'
    for (const listener of listeners) listener({ preference, resolved })
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
    if (event.key !== key && event.key !== null) return
    preference = valid(event.newValue) ? event.newValue : 'light'
    apply()
  })
  window.cosmoAppearance = Object.freeze({
    getState: () => ({ preference, resolved }),
    setPreference,
    subscribe(listener) {
      listeners.add(listener)
      return () => listeners.delete(listener)
    }
  })
  apply()
})(window, document)
