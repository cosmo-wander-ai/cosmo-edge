import assert from 'node:assert/strict'
import * as Vue from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// Mount the production SFC and record canvas commands. No source extraction or
// replacement: the real appearance watcher and drawing/submit paths run here.
const plain = value => JSON.parse(JSON.stringify(value))
const palette = { '--bg-secondary': '#282F3B', '--text-secondary': '#ABB4C3' }
const fixture = {
  points: [[10, 10], [100, 10], [60, 60]],
  shieldPoints: [[20, 20], [30, 20], [25, 30]],
  associatedAreas: [[120, 20], [140, 20], [130, 40]],
  linePoints: [[20, 90], [100, 90]], directionType: '1'
}

for (const initialMode of ['loading', 'failed', 'photo']) {
  const appearance = Vue.shallowRef({ preference: 'light', resolved: 'light' })
  const allPoints = Vue.reactive([])
  const operations = []
  let mode = initialMode
  let useTokens = true
  let sourceWrites = 0
  let contextReads = 0
  let scheduled = 0
  let requests = 0
  let expectedDrawErrors = 0
  const request = () => { requests++; assert.fail('theme changes must not request data or media') }
  const view = await mountComponent('views/gam/taskManager/editTask/DetectionCanvas.vue', {
    props: {
      width: 200, height: 100, imageSrc: 'fixture-photo', allPoints,
      activeIndex: null, shieldActiveIndex: null, isDrawingLine: true
    },
    mocks: {
      '@/assets/CatchPhoto.png': { default: 'fixture-fallback' },
      '@/composables/useAppearance': {
        useAppearance: () => ({ appearance: Vue.readonly(appearance), setAppearance() {} })
      }
    },
    globals: {
      getComputedStyle(element) {
        assert.equal(element.type, 'canvas', 'read inherited theme tokens from the canvas')
        return { getPropertyValue: token => useTokens ? palette[token] || '' : '' }
      },
      console: {
        log() {},
        error(message) {
          assert.equal(message, '绘制图片失败:')
          expectedDrawErrors++
        }
      },
      fetch: request,
      XMLHttpRequest: class { constructor() { request() } },
      setTimeout() { scheduled++; return scheduled }, clearTimeout() {}
    },
    api: new Proxy({}, { get: () => request }),
    message: { warning(message) { assert.fail(`unexpected validation warning: ${message}`) } }
  })
  let unmounted = false
  try {
    const canvas = view.all(node => node.type === 'canvas')[0]
    const img = view.all(node => node.type === 'img')[0]
    canvas.width = 200
    canvas.height = 100
    Object.defineProperty(img, 'src', {
      get: () => 'fixture-photo', set() { sourceWrites++ }, configurable: true
    })
    const context = new Proxy({}, {
      get(target, key) {
        if (key in target) return target[key]
        if (typeof key !== 'string' || key.startsWith('__v_')) return undefined
        return (...args) => {
          const operation = { method: key, args: key === 'drawImage' ? args.slice(1) : args }
          if (['fill', 'fillRect', 'fillText'].includes(key)) operation.fillStyle = target.fillStyle
          if (['stroke', 'strokeRect'].includes(key)) operation.strokeStyle = target.strokeStyle
          operations.push(operation)
          if (key === 'drawImage' && mode === 'failed') throw new Error('controlled draw failure')
        }
      },
      set(target, key, value) { target[key] = value; return true }
    })
    canvas.getContext = type => {
      assert.equal(type, '2d')
      contextReads++
      return context
    }
    const load = async () => {
      img.complete = mode !== 'loading'
      img.naturalWidth = mode === 'loading' ? 0 : 200
      img.props.onLoad()
      await view.settle()
    }
    await load()
    allPoints.push(plain(fixture))
    await view.settle()
    view.instance.drawingLinePoints.push([150, 70], [180, 80])
    view.instance.directionType = '1'
    view.instance.selectedPolygon = 0
    view.instance.isReversed = true
    await view.settle()
    const state = () => plain({
      submitted: view.instance.submit(), allPoints,
      draft: view.instance.drawingLinePoints, direction: view.instance.directionType,
      selected: view.instance.selectedPolygon, reversed: view.instance.isReversed
    })
    const preserved = state()
    const annotations = commands => plain(commands.filter(command =>
      ['fill', 'stroke', 'strokeRect', 'arc', 'moveTo', 'lineTo'].includes(command.method)))
    const assertPlaceholder = resolved => {
      const backgrounds = operations.filter(command => command.method === 'fillRect')
      const labels = operations.filter(command => command.method === 'fillText')
      assert.equal(backgrounds.length, 1)
      assert.equal(labels.length, 1)
      assert.deepEqual(backgrounds[0].args, [0, 0, 200, 100])
      assert.equal(backgrounds[0].fillStyle, resolved === 'dark'
        ? palette['--bg-secondary'] : mode === 'loading' ? '#f8f9fa' : '#f0f0f0')
      assert.equal(labels[0].fillStyle, resolved === 'dark'
        ? palette['--text-secondary'] : mode === 'loading' ? '#6c757d' : '#999')
      assert.equal(labels[0].args[0], mode === 'loading' ? 'common.imageLoading' : 'common.imageLoadFailed')
    }
    operations.length = 0
    view.instance.drawLineOperation('type')
    const originalAnnotations = annotations(operations)
    assert.ok(originalAnnotations.some(command => command.method === 'arc' && command.args[0] === 180),
      'fixture includes an unfinished line vertex')
    assert.ok(originalAnnotations.some(command => command.fillStyle === 'rgba(178, 178, 178, 0.5)'),
      'fixture includes a shield region with its real annotation color')
    const originalReads = contextReads
    let previous = 'light'
    for (const [preference, resolved] of [
      ['dark', 'dark'], ['system', 'dark'], ['system', 'light'],
      ['light', 'light'], ['dark', 'dark'], ['light', 'light']
    ]) {
      operations.length = 0
      appearance.value = { preference, resolved }
      await view.settle()
      if (mode === 'photo' || resolved === previous) {
        assert.equal(operations.length, 0, 'loaded photos and unchanged resolved themes must not repaint')
      } else {
        assertPlaceholder(resolved)
        assert.deepEqual(annotations(operations), originalAnnotations,
          'all saved regions and unfinished line geometry/colors must survive the placeholder repaint')
      }
      assert.deepEqual(state(), preserved, 'theme switch must preserve submit data and editing state')
      assert.equal(contextReads, originalReads, 'theme changes must not reinitialize the canvas')
      assert.equal(sourceWrites, 0, 'theme changes must not reload the image')
      assert.equal(scheduled, 0, 'theme changes must not schedule fallback image loads')
      assert.equal(requests, 0)
      previous = resolved
    }
    if (mode !== 'photo') {
      // A missing CSS token still gets a dark placeholder; a subsequently
      // loaded photo stops placeholder-only redraws without resetting the data.
      useTokens = false
      operations.length = 0
      appearance.value = { preference: 'dark', resolved: 'dark' }
      await view.settle()
      assertPlaceholder('dark')
      mode = 'photo'
      await load()
      assert.deepEqual(state(), preserved)
      operations.length = 0
      appearance.value = { preference: 'light', resolved: 'light' }
      await view.settle()
      assert.equal(operations.length, 0, 'a successful photo must end placeholder theme repainting')
      mode = initialMode
      await load()
    }
    assert.equal(expectedDrawErrors > 0, initialMode === 'failed')
    assert.equal(sourceWrites, 0)
    assert.equal(requests, 0)
    view.unmount()
    unmounted = true
    operations.length = 0
    appearance.value = { preference: 'dark', resolved: 'dark' }
    await view.settle()
    assert.equal(operations.length, 0, 'theme watcher must stop when the component unmounts')
  } finally {
    if (!unmounted) view.unmount()
  }
}

console.log('Detection placeholder theme passed: real SFC, loading/draw failure/dark fallback, light restoration, unchanged photo/annotation colors and saved/draft state, no canvas reinitialization/media reload/API requests, watcher cleanup')
