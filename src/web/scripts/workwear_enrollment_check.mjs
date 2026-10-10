import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { test } from 'node:test'
import vm from 'node:vm'
import * as vue from 'vue'
import { parse, compileScript } from '@vue/compiler-sfc'

const filename = new URL('../src/views/box/eventQuery/components/rukuDialog.vue', import.meta.url)
const { descriptor, errors } = parse(await readFile(filename, 'utf8'))
assert.deepEqual(errors, [])
const script = compileScript(descriptor, { id: 'workwear-enrollment-test' })

async function dialog(response, picture = '/event/2026/09/28/event_detect.jpg') {
  const calls = []; const messages = []; const events = []
  const proxy = {
    $API: { addLibPerson: async params => { calls.push(params); return response } },
    $message: Object.fromEntries(['success', 'error', 'warning'].map(type => [type, text => messages.push({ type, text })]))
  }
  const mocks = {
    vue: { ...vue, getCurrentInstance: () => ({ proxy }) },
    '@/i18n': { t: key => key, localeColon: ':' }
  }
  const context = vm.createContext({ console })
  const module = new vm.SourceTextModule(script.content, { context })
  await module.link(specifier => {
    const values = mocks[specifier]
    assert.ok(values, specifier)
    return new vm.SyntheticModule(Object.keys(values), function () {
      for (const [key, value] of Object.entries(values)) this.setExport(key, value)
    }, { context })
  })
  await module.evaluate()
  const scope = vue.effectScope()
  const state = scope.run(() => module.namespace.default.setup(
    { visible: false, rukuData: { detectedPicture: picture } },
    { expose() {}, emit: (...args) => events.push(args) }
  ))
  state.formData.value.libId = 'workwear-lib'
  return { state, calls, messages, events, stop: () => scope.stop() }
}

test('workwear enrollment submits the event crop and closes only after an ID is returned', async () => {
  const d = await dialog({ resData: { personId: ['new-person'] } })
  try {
    await d.state.addWorkClothes()
    assert.equal(d.calls[0].personList[0].pictureUrl, '/event/2026/09/28/event_detect.jpg')
    assert.equal(d.calls[0].personLibId, 'workwear-lib')
    assert.deepEqual(d.messages.map(m => m.type), ['success'])
    assert.deepEqual(d.events, [['update:visible', false]])
  } finally { d.stop() }
})

for (const response of [{ resData: { personId: [] } }, { resData: {} }]) {
  test(`workwear enrollment keeps the dialog open for an empty result: ${JSON.stringify(response)}`, async () => {
    const d = await dialog(response)
    try {
      await d.state.addWorkClothes()
      assert.deepEqual(d.messages.map(m => m.type), ['error'])
      assert.deepEqual(d.events, [])
    } finally { d.stop() }
  })
}

test('workwear enrollment does not submit a missing detection picture', async () => {
  const d = await dialog({ resData: { personId: [] } }, '')
  try {
    await d.state.addWorkClothes()
    assert.equal(d.calls.length, 0)
    assert.deepEqual(d.messages.map(m => m.type), ['error'])
    assert.deepEqual(d.events, [])
  } finally { d.stop() }
})
