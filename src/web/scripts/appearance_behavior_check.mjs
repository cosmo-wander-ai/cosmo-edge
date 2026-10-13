import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../public/appearance.js', import.meta.url), 'utf8')
function browser({ saved, savedPalette, dark = false, blocked = false, legacy = false, noMedia = false } = {}) {
  const storage = new Map(saved === undefined ? [] : [['cosmo.appearance', saved]])
  if (savedPalette !== undefined) storage.set('cosmo.palette', savedPalette)
  storage.set('existing-form', 'unchanged')
  storage.set('playedCameraList', '["existing-camera"]')
  const events = new Map()
  let mediaListener
  const media = { matches: dark }
  media[legacy ? 'addListener' : 'addEventListener'] = (...args) => { mediaListener = args.at(-1) }
  const classes = new Set()
  const root = { dataset: {}, style: {}, classList: { toggle(name, value) { value ? classes.add(name) : classes.delete(name) } } }
  const window = {
    matchMedia: noMedia ? undefined : () => media,
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
    external(value, key = 'cosmo.appearance') {
      if (key === null) storage.clear()
      else if (value === null) storage.delete(key)
      else storage.set(key, value)
      events.get('storage')({ key, newValue: value })
    }
  }
}

for (const dark of [false, true]) {
  const b = browser({ dark })
  assert.equal(b.root.dataset.theme, 'light', 'first visit must stay light regardless of OS')
  assert.equal(b.root.dataset.palette, 'modern', 'the compatible palette is opt-in')
  assert.equal(b.storage.has('cosmo.palette'), false, 'startup does not rewrite stored preferences')
  for (const preference of ['light', 'dark', 'system']) {
    b.controller.setPreference(preference)
    const expected = preference === 'system' ? (dark ? 'dark' : 'light') : preference
    assert.equal(b.root.dataset.theme, expected)
    assert.equal(b.root.style.colorScheme, expected)
    assert.equal(b.classes.has('dark'), expected === 'dark')
    assert.equal(b.storage.get('cosmo.appearance'), preference)
    const previousVersion = browser({ saved: preference, dark })
    assert.equal(previousVersion.root.dataset.theme, expected, 'an existing mode is preserved before app mount')
    assert.equal(previousVersion.root.dataset.palette, 'modern', 'existing preferences migrate without changing colors')
    assert.equal(b.storage.get('existing-form'), 'unchanged')
    for (const palette of ['classic-blue', 'modern']) {
      b.controller.setPalette(palette)
      assert.equal(b.root.dataset.palette, palette)
      assert.equal(b.controller.getState().palette, palette)
      assert.equal(b.root.dataset.theme, expected, 'palette selection must preserve the mode')
      assert.equal(b.root.style.colorScheme, expected, 'a dark sidebar must not imply a dark color scheme')
      assert.equal(b.root.style.backgroundColor, expected === 'dark' ? '#14171d' : '#f3f5f8')
      assert.equal(b.classes.has('dark'), expected === 'dark')
      assert.equal(b.storage.get('cosmo.appearance'), preference)
      assert.equal(b.storage.get('cosmo.palette'), palette)
      assert.equal(b.storage.get('existing-form'), 'unchanged')
      assert.equal(b.storage.get('playedCameraList'), '["existing-camera"]')
      const restored = browser({ saved: preference, savedPalette: palette, dark })
      assert.equal(restored.root.dataset.theme, expected, 'mode is restored before app mount')
      assert.equal(restored.root.dataset.palette, palette, 'palette is restored before app mount')
    }
  }
}
for (const legacy of [false, true]) {
  const b = browser({ saved: 'system', savedPalette: 'classic-blue', legacy })
  const notifications = []
  const unsubscribe = b.controller.subscribe(value => notifications.push(`${value.resolved}:${value.palette}`))
  b.system(true)
  b.system(false)
  assert.deepEqual(notifications, ['dark:classic-blue', 'light:classic-blue'])
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
  assert.equal(b.root.dataset.palette, 'classic-blue')
  b.external('modern', 'cosmo.palette')
  assert.equal(b.root.dataset.theme, 'dark', 'a cross-tab palette change preserves mode')
  assert.equal(b.root.dataset.palette, 'modern')
  b.external('classic-blue', 'cosmo.palette')
  b.external(null)
  assert.equal(b.root.dataset.theme, 'light', 'cleared preference uses first-visit default')
  assert.equal(b.root.dataset.palette, 'classic-blue', 'removing one key preserves the other choice')
  b.controller.setPreference('dark')
  b.external(null, 'cosmo.palette')
  assert.equal(b.root.dataset.theme, 'dark')
  assert.equal(b.root.dataset.palette, 'modern', 'removing the palette uses its default')
  b.controller.setPalette('classic-blue')
  b.external(null, null)
  assert.equal(b.root.dataset.theme, 'light', 'storage.clear resets both appearance choices')
  assert.equal(b.root.dataset.palette, 'modern')
}
for (const invalid of ['invalid', '', '{}', null]) {
  const b = browser({ saved: invalid, savedPalette: invalid, dark: true })
  assert.equal(b.root.dataset.theme, 'light')
  assert.equal(b.root.dataset.palette, 'modern')
  b.controller.setPreference('dark')
  b.controller.setPalette('classic-blue')
  let updates = 0
  b.controller.subscribe(() => { updates++ })
  b.controller.setPreference(invalid)
  b.controller.setPalette(invalid)
  b.external('unrelated', 'other-setting')
  assert.equal(updates, 0, 'invalid selections and unrelated storage events do not repaint')
  assert.equal(b.root.dataset.theme, 'dark')
  assert.equal(b.root.dataset.palette, 'classic-blue')
  b.external(invalid, 'cosmo.palette')
  assert.equal(b.root.dataset.theme, 'dark')
  assert.equal(b.root.dataset.palette, 'modern', 'invalid external palette falls back independently')
  b.controller.setPalette('classic-blue')
  b.external(invalid)
  assert.equal(b.root.dataset.theme, 'light')
  assert.equal(b.root.dataset.palette, 'classic-blue', 'invalid external mode does not erase palette')
}
const blocked = browser({ blocked: true, dark: true })
blocked.controller.setPreference('dark')
blocked.controller.setPalette('classic-blue')
assert.equal(blocked.root.dataset.theme, 'dark', 'blocked storage must not break in-memory selection')
assert.equal(blocked.root.dataset.palette, 'classic-blue')
blocked.controller.setPreference('system')
blocked.system(false)
assert.equal(blocked.root.dataset.theme, 'light')
assert.equal(blocked.root.dataset.palette, 'classic-blue')
assert.equal(blocked.storage.get('existing-form'), 'unchanged')
const noMedia = browser({ saved: 'system', savedPalette: 'classic-blue', noMedia: true })
assert.equal(noMedia.root.dataset.theme, 'light', 'system mode falls back to light without matchMedia')
assert.equal(noMedia.root.dataset.palette, 'classic-blue')
console.log('Appearance behavior passed: independent mode/palette, upgrade defaults, first paint, persistence, system changes, cross-tab changes and clears, invalid/blocked storage, and unrelated state preservation')
