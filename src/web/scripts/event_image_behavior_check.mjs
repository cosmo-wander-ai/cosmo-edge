import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import vm from 'node:vm'
import { effect, reactive } from 'vue'

let token = 'first-user'
const calls = []
const context = vm.createContext({
  localStorage: { getItem: () => token }
})
const vue = new vm.SyntheticModule(['reactive'], function () {
  this.setExport('reactive', reactive)
}, { context })
const api = new vm.SyntheticModule(['request'], function () {
  this.setExport('request', options => new Promise((resolve, reject) => {
    calls.push({ options, resolve, reject })
  }))
}, { context })
const source = await fs.readFile(new URL('../src/utils/eventImage.js', import.meta.url), 'utf8')
const module = new vm.SourceTextModule(source, { context })
await module.link(specifier => specifier === 'vue' ? vue : api)
await module.evaluate()
const { eventImageUrl, loadEventImageUrl } = module.namespace
const settle = () => new Promise(resolve => setImmediate(resolve))

assert.equal(eventImageUrl('https://example.invalid/image.jpg'), 'https://example.invalid/image.jpg')
const path = '/event/2026/11/19/frame.jpg'
let renderedImage
effect(() => { renderedImage = eventImageUrl(path) })
assert.match(renderedImage, /^data:image\/gif/)
eventImageUrl(path)
assert.equal(calls.length, 1, 'concurrent renders share one request')
assert.equal(calls[0].options.url, '/gtw/cwai/Event/ReadImage')
assert.equal(calls[0].options.data.path, path)
calls[0].resolve({ resData: { dataUrl: 'data:image/jpeg;base64,Zm9v' } })
await settle()
assert.equal(eventImageUrl(path), 'data:image/jpeg;base64,Zm9v')
assert.equal(renderedImage, 'data:image/jpeg;base64,Zm9v', 'async completion updates the rendered image')

token = 'second-user'
assert.match(eventImageUrl(path), /^data:image\/gif/, 'a new session cannot reuse the old cache')
assert.equal(calls.length, 2)
calls[1].reject(new Error('not available'))
await settle()
assert.equal(eventImageUrl(path), '')

const previewPath = '/event/2026/11/19/preview.jpg'
let previewReady = false
const preview = loadEventImageUrl(previewPath).then(url => { previewReady = true; return url })
assert.match(eventImageUrl(previewPath), /^data:image\/gif/)
assert.equal(calls.length, 3, 'preview and thumbnail share the image request')
await settle()
assert.equal(previewReady, false, 'preview waits for the actual image')
calls[2].resolve({ resData: { dataUrl: 'data:image/jpeg;base64,YmFy' } })
assert.equal(await preview, 'data:image/jpeg;base64,YmFy')

const stalePath = '/event/2026/11/19/stale.jpg'
const stalePreview = loadEventImageUrl(stalePath)
const staleCall = calls.at(-1)
token = 'third-user'
const currentPreview = loadEventImageUrl(stalePath)
const currentCall = calls.findLast(call => call.options.data.path === stalePath)
assert.notEqual(currentCall, staleCall)
staleCall.resolve({ resData: { dataUrl: 'data:image/jpeg;base64,b2xk' } })
assert.equal(await stalePreview, '', 'a previous session cannot populate an open preview')
currentCall.resolve({ resData: { dataUrl: 'data:image/jpeg;base64,bmV3' } })
assert.equal(await currentPreview, 'data:image/jpeg;base64,bmV3')
assert.equal(await loadEventImageUrl('https://example.invalid/image.jpg'), 'https://example.invalid/image.jpg')
const galleryPath = '/weblibPic/facePicture/01234567-89ab-cdef-0123-456789abcdef.jpg'
const gallery = loadEventImageUrl(galleryPath)
const galleryCall = calls.at(-1)
assert.equal(galleryCall.options.url, '/gtw/cwai/Library/ReadFaceImage')
assert.equal(galleryCall.options.data.path, galleryPath)
galleryCall.resolve({ resData: { dataUrl: 'data:image/jpeg;base64,Z2FsbGVyeQ==' } })
assert.equal(await gallery, 'data:image/jpeg;base64,Z2FsbGVyeQ==')
assert.equal(eventImageUrl('/weblibPic/personPicture/example.jpg'), '/weblibPic/personPicture/example.jpg')
console.log('Event and authenticated face-library image loading checks passed')
