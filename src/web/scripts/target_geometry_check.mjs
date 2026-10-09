import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'
import { drawAlarmVideoTargetGeometry, drawTargetGeometry } from '../src/utils/targetGeometry.js'

const box = { x: 10, y: 20, width: 80, height: 40 }
const draw = target => {
  const calls = []
  const ctx = Object.fromEntries(
    ['beginPath', 'moveTo', 'lineTo', 'closePath', 'stroke', 'strokeRect']
      .map(name => [name, (...args) => calls.push([name, ...args])])
  )
  drawTargetGeometry(ctx, target)
  return calls
}

// OBB vertices are authoritative even when their AABB cannot reconstruct them.
const rotated = [{ x: 50.25, y: 10.5 }, { x: 90.25, y: 50.5 },
  { x: 80.25, y: 60.5 }, { x: 40.25, y: 20.5 }]
assert.deepEqual(draw({ box, orientedCorners: rotated }), [
  ['beginPath'], ['moveTo', 50.25, 10.5], ['lineTo', 90.25, 50.5],
  ['lineTo', 80.25, 60.5], ['lineTo', 40.25, 20.5], ['closePath'], ['stroke']
])

// A zero-degree OBB remains a polygon without an angle field.
const aligned = [{ x: 10, y: 20 }, { x: 90, y: 20 },
  { x: 90, y: 60 }, { x: 10, y: 60 }]
assert.deepEqual(draw({ box, orientedCorners: aligned }), [
  ['beginPath'], ['moveTo', 10, 20], ['lineTo', 90, 20],
  ['lineTo', 90, 60], ['lineTo', 10, 60], ['closePath'], ['stroke']
])

// Keep source vertices outside the image; canvas clips lines at its boundary.
const crossing = [{ x: -10.5, y: 5 }, { x: 10.5, y: -5 },
  { x: 30.5, y: 15 }, { x: 9.5, y: 25 }]
assert.deepEqual(draw({ box, orientedCorners: crossing }), [
  ['beginPath'], ['moveTo', -10.5, 5], ['lineTo', 10.5, -5],
  ['lineTo', 30.5, 15], ['lineTo', 9.5, 25], ['closePath'], ['stroke']
])

// Ordinary detections and malformed optional geometry retain the AABB path.
for (const orientedCorners of [undefined, null, [], aligned.slice(0, 3),
  [null, ...aligned.slice(1)], [{ x: NaN, y: 20 }, ...aligned.slice(1)]]) {
  assert.deepEqual(draw({ box, orientedCorners }), [['strokeRect', 10, 20, 80, 40]])
}

const drawPlayback = (rect, frame) => {
  const calls = []
  const ctx = Object.fromEntries(
    ['beginPath', 'moveTo', 'lineTo', 'closePath', 'stroke', 'strokeRect', 'save', 'rect', 'clip', 'restore']
      .map(name => [name, (...args) => calls.push([name, ...args])])
  )
  drawAlarmVideoTargetGeometry(ctx, rect, frame, { x: 0, y: 60, width: 640, height: 360 })
  return calls
}

const legacyRect = { xRatio: 0.1, yRatio: 0.2, wRatio: 0.3, hRatio: 0.4 }
const sourceFrame = { sourceWidth: 1920, sourceHeight: 1080 }
const playbackCorners = [{ x: -30, y: 0 }, { x: 1920, y: 0 },
  { x: 1920, y: 1080 }, { x: -30, y: 1080 }]
const playbackRect = { ...legacyRect, orientedCorners: playbackCorners }
const originalPlaybackRect = structuredClone(playbackRect)
assert.deepEqual(drawPlayback(playbackRect, sourceFrame), [
  ['save'], ['beginPath'], ['rect', 0, 60, 640, 360], ['clip'],
  ['beginPath'], ['moveTo', -10, 60], ['lineTo', 640, 60],
  ['lineTo', 640, 420], ['lineTo', -10, 420], ['closePath'], ['stroke'], ['restore']
])
assert.deepEqual(playbackRect, originalPlaybackRect)

const legacyPlayback = [
  ['save'], ['beginPath'], ['rect', 0, 60, 640, 360], ['clip'],
  ['strokeRect', 64, 132, 192, 144], ['restore']
]
assert.deepEqual(drawPlayback(legacyRect, {}), legacyPlayback)
assert.deepEqual(drawPlayback(legacyRect, sourceFrame), legacyPlayback)
for (const frame of [{}, { sourceWidth: 0, sourceHeight: 1080 },
  { sourceWidth: 1920, sourceHeight: NaN }]) {
  assert.deepEqual(drawPlayback(playbackRect, frame), legacyPlayback)
}
for (const orientedCorners of [null, [], playbackCorners.slice(0, 3),
  [{ x: Infinity, y: 0 }, ...playbackCorners.slice(1)]]) {
  assert.deepEqual(drawPlayback({ ...legacyRect, orientedCorners }, sourceFrame), legacyPlayback)
}

// Exercise the player's actual frame callback: clearing a target must preserve
// every static region and tripwire, including empty pre-roll frames.
const playerSource = readFileSync(new URL('../src/components/videoPlaying265.vue', import.meta.url), 'utf8')
const playerScript = playerSource.match(/<script setup>([\s\S]*?)<\/script>/)[1]
  .replace(/^import .*$/gm, '')
const region = [{ xRatio: 0.1, yRatio: 0.1 }, { xRatio: 0.7, yRatio: 0.1 },
  { xRatio: 0.7, yRatio: 0.7 }, { xRatio: 0.1, yRatio: 0.7 }]
const recording = {
  area: {
    points: region,
    associatedAreas: [{ points: region.map(point => ({
      xRatio: point.xRatio + 0.1, yRatio: point.yRatio + 0.1
    })) }],
    linePoints: [{ xRatio: 0.2, yRatio: 0.2 }, { xRatio: 0.6, yRatio: 0.6 }]
  },
  targets: [{ index: 1, rects: [] },
    { index: 2, ...sourceFrame, rects: [playbackRect] }, { index: 3, rects: [] }]
}
let tick
let path = []
let visiblePaths = []
const playerContext = {
  clearRect() { visiblePaths = [] },
  beginPath() { path = [] },
  moveTo(...point) { path.push(['moveTo', ...point]) },
  lineTo(...point) { path.push(['lineTo', ...point]) },
  closePath() { path.push(['closePath']) },
  stroke() { visiblePaths.push([...path]) },
  strokeRect(...rect) { visiblePaths.push([['strokeRect', ...rect]]) },
  save() {}, restore() {}, rect() {}, clip() {}
}
const player = {
  console: { log() {} }, ref: value => ({ value }), watch() {}, onBeforeUnmount() {},
  defineEmits: () => () => {},
  defineProps: () => ({ width: 800, height: 450, structureDataUrl: 'recording.json' }),
  fetch: async () => ({ json: async () => recording }),
  setInterval(callback) { tick = callback; return 1 },
  clearInterval() {}, t: value => value, drawAlarmVideoTargetGeometry,
  ctx: playerContext
}
vm.createContext(player)
vm.runInContext(`${playerScript}
elCanvas.value = { getContext: () => ctx }
elVideo.value = { style: {}, duration: 5, videoWidth: 1920, videoHeight: 1080, play() {} }
globalThis.ready = onLoaded()
`, player)
await player.ready
const staticPaths = structuredClone(visiblePaths)
assert.equal(staticPaths.length, 3, 'initial main region, associated region and tripwire')
tick()
assert.deepEqual(visiblePaths, staticPaths, 'empty pre-roll must preserve all static geometry')
tick()
assert.deepEqual(visiblePaths.slice(0, 3), staticPaths, 'target frame must preserve all static geometry')
assert.equal(visiblePaths.length, 4, 'target frame must also draw the oriented target')
tick()
assert.deepEqual(visiblePaths, staticPaths, 'empty frame must clear the previous target only')

console.log('Target geometry checks passed (image and recorded-video geometry, source scaling, letterbox clipping, legacy and malformed fallback, empty-frame region preservation).')
