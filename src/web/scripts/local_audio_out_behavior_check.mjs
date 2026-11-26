import assert from 'node:assert/strict'
import { test } from 'node:test'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const entry = 'views/box/audioManagement/audio/LocalAudioOutSettings.vue'
const defaultId = '1234567890'
const library = [{ fileId: defaultId, fileName: 'Default' }, { fileId: 'voice', fileName: 'Voice.mp3' }]
const form = view => view.all(n => n.type === 'el-form')[0]
const control = (view, type) => view.all(n => n.type === type)[0]
const submit = view => form(view).props.onSubmit({ preventDefault() {} })

test('device playback selects a library file, reads back saved settings, and restores them on failure', async () => {
  let state = { available: true, reason: '', enabled: false, fileId: defaultId }
  const writes = [], emitted = [], errors = [], successes = []
  let reads = 0
  const view = await mountComponent(entry, {
    props: { fileLabel: f => f.fileName, onSavedFile: id => emitted.push(id) },
    api: {
      queryLocalAudioOut: async () => { reads++; return { resData: { ...state } } },
      queryAudioFile: async data => {
        assert.equal(data.pageNum, 1)
        assert.equal(data.pageSize, 1000, 'the selector reads the complete library')
        return { resData: { audioFileList: library } }
      },
      setLocalAudioOut: async ({ enabled, fileId }) => {
        writes.push({ enabled, fileId })
        if (fileId === defaultId) throw { resCode: 0, resMsg: [{ msgText: 'conversionFailed' }] }
        state = { ...state, enabled, fileId }
      }
    },
    message: { error: value => errors.push(value), success: value => successes.push(value) }
  })
  try {
    assert.deepEqual(view.all(n => n.type === 'el-option').map(n => n.props.value), [defaultId, 'voice'])
    control(view, 'el-select').props.activate('voice')
    control(view, 'el-switch').props.activate(true)
    await view.settle()
    await submit(view)
    await view.settle()
    assert.deepEqual(writes, [{ enabled: true, fileId: 'voice' }])
    assert.deepEqual(emitted, [defaultId, 'voice'], 'only persisted selections protect a library file')
    assert.equal(successes.length, 1)
    control(view, 'el-select').props.activate(defaultId)
    await view.settle()
    await submit(view)
    await view.settle()
    assert.deepEqual(errors, ['localAudioOut.reasons.conversionFailed'])
    assert.equal(control(view, 'el-select').props.modelValue, 'voice')
    assert.equal(emitted.at(-1), 'voice')
    assert.equal(reads, 3, 'one initial read and one after each attempted write')
  } finally { view.unmount() }
})

test('an enabled output can be switched off with unavailable hardware and a missing recording', async () => {
  let enabled = true
  const writes = []
  const view = await mountComponent(entry, {
    props: { fileLabel: f => f.fileName },
    api: {
      queryLocalAudioOut: async () => ({
        resData: { enabled, available: false, fileId: 'lost', reason: 'audioDeviceUnavailable' }
      }),
      queryAudioFile: async () => ({ resData: { audioFileList: library } }),
      setLocalAudioOut: async data => { writes.push({ ...data }); enabled = data.enabled }
    }
  })
  try {
    assert.equal(control(view, 'el-switch').props.disabled, false)
    assert.equal(control(view, 'el-select').props.disabled, true)
    assert.equal(view.all(n => n.props.role === 'status')[0].text, 'localAudioOut.reasons.audioDeviceUnavailable')
    control(view, 'el-switch').props.activate(false)
    await view.settle()
    await submit(view)
    await view.settle()
    assert.deepEqual(writes, [{ enabled: false, fileId: 'lost' }])
    assert.equal(control(view, 'el-switch').props.disabled, true)
  } finally { view.unmount() }
})
