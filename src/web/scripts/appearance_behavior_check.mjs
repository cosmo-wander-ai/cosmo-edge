import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../public/appearance.js', import.meta.url), 'utf8')
function browser({ saved, dark = false, blocked = false, legacy = false } = {}) {
  const storage = new Map(saved === undefined ? [] : [['cosmo.appearance', saved]])
  storage.set('existing-form', 'unchanged')
  const events = new Map()
  let mediaListener
  const media = { matches: dark }
  media[legacy ? 'addListener' : 'addEventListener'] = (...args) => { mediaListener = args.at(-1) }
  const classes = new Set()
  const root = { dataset: {}, style: {}, classList: { toggle(name, value) { value ? classes.add(name) : classes.delete(name) } } }
  const window = {
    matchMedia: () => media,
    localStorage: {
      getItem(key) { if (blocked) throw Error('storage blocked'); return storage.get(key) ?? null },
      setItem(key, value) { if (blocked) throw Error('storage blocked'); storage.set(key, value) }
    },
    addEventListener(name, callback) { events.set(name, callback) }
  }
  // No router, fetch, media playback or reload API is available to appearance.
  vm.runInNewContext(source, { window, document: { documentElement: root } })
  return {
    controller: window.cosmoAppearance, root, classes, storage,
    system(value) { media.matches = value; mediaListener() },
    external(value) { events.get('storage')({ key: 'cosmo.appearance', newValue: value }) }
  }
}

for (const dark of [false, true]) {
  const b = browser({ dark })
  assert.equal(b.root.dataset.theme, 'light', 'first visit must stay light regardless of OS')
  for (const preference of ['light', 'dark', 'system']) {
    b.controller.setPreference(preference)
    const expected = preference === 'system' ? (dark ? 'dark' : 'light') : preference
    assert.equal(b.root.dataset.theme, expected)
    assert.equal(b.root.style.colorScheme, expected)
    assert.equal(b.classes.has('dark'), expected === 'dark')
    assert.equal(b.storage.get('cosmo.appearance'), preference)
    assert.equal(browser({ saved: preference, dark }).root.dataset.theme, expected, 'reload applies before app mount')
    assert.equal(b.storage.get('existing-form'), 'unchanged')
  }
}
for (const legacy of [false, true]) {
  const b = browser({ saved: 'system', legacy })
  const notifications = []
  const unsubscribe = b.controller.subscribe(value => notifications.push(value.resolved))
  b.system(true)
  b.system(false)
  assert.deepEqual(notifications, ['dark', 'light'])
  b.controller.setPreference('dark')
  b.system(false)
  assert.equal(b.root.dataset.theme, 'dark', 'manual choice must ignore OS changes')
  b.controller.setPreference('light')
  b.system(true)
  assert.equal(b.root.dataset.theme, 'light')
  unsubscribe()
  const count = notifications.length
  b.controller.setPreference('system')
  assert.equal(notifications.length, count)
  b.external('dark')
  assert.equal(b.root.dataset.theme, 'dark', 'another tab selection updates appearance only')
  b.external(null)
  assert.equal(b.root.dataset.theme, 'light', 'cleared preference uses first-visit default')
}
for (const saved of ['invalid', '', '{}']) {
  const b = browser({ saved, dark: true })
  assert.equal(b.root.dataset.theme, 'light')
  b.controller.setPreference('invalid')
  assert.equal(b.root.dataset.theme, 'light')
}
const blocked = browser({ blocked: true, dark: true })
blocked.controller.setPreference('dark')
assert.equal(blocked.root.dataset.theme, 'dark', 'blocked storage must not break in-memory selection')
blocked.controller.setPreference('system')
blocked.system(false)
assert.equal(blocked.root.dataset.theme, 'light')
console.log('Appearance behavior passed: first-paint default, persistence, system changes, manual override, cross-tab updates, invalid/blocked storage and unrelated data preservation')
