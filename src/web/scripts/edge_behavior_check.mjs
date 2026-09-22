import assert from 'node:assert/strict'
import { h } from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const platforms = ['1', '-1', null, '15']
const empty = { default: { render: () => null } }
const plain = value => JSON.parse(JSON.stringify(value))
const text = node => [node.text, ...node.children.map(text)].join(' ')
const button = (component, label, root = component.root) => {
  const found = component.all(n => n.type === 'el-button' && text(n).trim() === label, root)[0]
  assert.ok(found, `missing action: ${label}`)
  return found
}
const settle = async component => { for (let i = 0; i < 6; i++) await component.settle() }
const makeStorage = platform => {
  const values = new Map([['runMode', '1']])
  if (platform !== null) values.set('platformType', platform)
  const reads = [], writes = []
  return {
    values, reads, writes,
    getItem(key) { reads.push(key); return values.get(key) ?? null },
    setItem(key, value) { writes.push(key); values.set(key, String(value)) },
    removeItem(key) { values.delete(key) }
  }
}
const globalsFor = storage => ({
  localStorage: storage,
  window: { localStorage: storage, location: { search: '', hash: '' } },
  document: { addEventListener() {}, removeEventListener() {} }
})
const assertNoPlatformAccess = storage => {
  assert.ok(!storage.reads.includes('platformType'), 'business paths must not read the retired platform selector')
  assert.ok(!storage.writes.includes('platformType'), 'business paths must not recreate the retired platform selector')
}

for (const platform of platforms) {
  const storage = makeStorage(platform)
  const navigations = []
  const router = { replace: target => navigations.push(target) }
  const login = await mountComponent('views/LoginPage.vue', {
    mocks: { 'vue-router': { useRouter: () => router }, 'js-md5': { default: value => `hashed:${value}` } },
    globals: globalsFor(storage),
    api: {
      dologin: async data => { assert.deepEqual(plain(data), { account: 'operator', pwd: 'hashed:password' }); return { resData: { mtk: 'new-token', account: 'operator' } } },
      queryOnboardingStatus: async () => ({ resData: { onboardingCompleted: true } })
    }
  })
  try {
    assert.equal(storage.values.has('platformType'), false, 'login entry clears the old key')
    for (const [id, value] of [['username', 'operator'], ['password', 'password']]) {
      login.all(n => n.type === 'input' && n.props.id === id)[0].props['onUpdate:modelValue'](value)
    }
    await login.all(n => n.type === 'form')[0].props.onSubmit({ preventDefault() {} })
    await settle(login)
    assert.equal(storage.values.get('mtk'), 'new-token')
    assert.equal(storage.values.get('runMode'), '0', 'login retains the existing runMode initialization')
    assert.deepEqual(plain(navigations), [{ path: '/home' }])
    assertNoPlatformAccess(storage)
  } finally { login.unmount() }

  if (platform !== null) storage.values.set('platformType', platform)
  const layout = await mountComponent('views/main/index.vue', {
    mocks: {
      'vue-router': { useRouter: () => router, useRoute: () => ({ path: '/home' }) },
      'js-md5': { default: value => value }, '@/components/OnboardingGuide.vue': empty,
      '@/components/eventBus.js': { default: { $on() {}, $off() {}, $emit() {} } }
    },
    globals: globalsFor(storage), api: { boxGetLogo: async () => ({ resData: {} }) }
  })
  try {
    layout.all(n => n.props.class === 'user-info')[0].props.onClick({ stopPropagation() {} })
    await settle(layout)
    layout.all(n => n.props.class === 'dropdown-item danger')[0].props.onClick()
    assert.equal(storage.values.has('platformType'), false, 'logout clears the old key')
    assert.equal(storage.values.has('mtk'), false)
    assert.equal(navigations.at(-1), '/boxLogin')
    assertNoPlatformAccess(storage)
  } finally { layout.unmount() }
}

for (const platform of platforms) for (const [type, method, listKey] of [
  ['commoditySet', 'boxQueryThingsLibInfo', 'thingsLibList'],
  ['workClothesSet', 'boxQueryPersonLibInfo', 'personLibList'],
  ['faceSet', 'boxQueryFaceLibInfo', 'faceLibList']
]) {
  const storage = makeStorage(platform)
  let requests = 0
  const form = await mountComponent('views/gam/taskManager/editTask/dynamicForm.vue', {
    props: { modelValue: [{ key: 'resource', name: 'Resource', type, value: 'resource-1', senior: 0, channelEditable: true, isColumn: true }] },
    mocks: { './distanceDialog.vue': empty, '@/assets/CatchPhoto.png': { default: '' }, echarts: { number: Number } },
    globals: { ...globalsFor(storage), setTimeout: () => 1, clearTimeout() {} },
    api: { [method]: async data => {
      requests++
      assert.deepEqual(plain(data), { pageNum: 1, pageSize: 1000 })
      return { resData: { [listKey]: [{ id: 'resource-1', name: 'Resource one' }] } }
    } }
  })
  try {
    await settle(form)
    assert.equal(requests, 1)
    if (type === 'faceSet') {
      const transfer = form.all(n => n.type === 'el-transfer')[0]
      assert.deepEqual(plain(transfer.props.data), [{ key: 'resource-1', label: 'Resource one' }])
    } else {
      const option = form.all(n => n.type === 'el-option' && n.props.value === 'resource-1')[0]
      assert.equal(option?.props.label, 'Resource one')
    }
    assert.equal(form.instance.collect()[0].value, 'resource-1')
    assertNoPlatformAccess(storage)
  } finally { form.unmount() }
}

for (const platform of platforms) {
  const storage = makeStorage(platform)
  const points = [[0, 0], [7, 3], [1, 1]]
  const canvas = await mountComponent('views/gam/taskManager/editTask/DetectionCanvas.vue', {
    props: { width: 7, height: 3, allPoints: [{ points, shieldPoints: points, associatedAreas: points, linePoints: points, directionType: '1' }] },
    mocks: { '@/assets/CatchPhoto.png': { default: '' } }, globals: globalsFor(storage)
  })
  try {
    const [result] = canvas.instance.submit()
    for (const key of ['points', 'shieldPoints', 'associatedAreas', 'linePoints']) {
      assert.deepEqual(plain(result[key]), [{ xRatio: 0, yRatio: 0 }, { xRatio: 1, yRatio: 1 }, { xRatio: 0.142857, yRatio: 0.333333 }])
    }
    assert.equal(result.directionType, '1')
    assertNoPlatformAccess(storage)
  } finally { canvas.unmount() }
}

for (const platform of platforms) for (const [supported, authorized] of [[false, false], [true, false], [true, true]]) {
  const storage = makeStorage(platform)
  let queries = 0
  const maintain = await mountComponent('views/box/systemManagement/systemMaintain/index.vue', {
    mocks: {
      './components/RunningDetail.vue': empty,
      'element-plus': { ElMessage: {}, ElMessageBox: {}, ElLoading: {} }
    },
    globals: globalsFor(storage),
    api: { queryModelAuthorization: async () => { queries++; return { resData: { supported, authorized } } } }
  })
  try {
    await settle(maintain)
    const tabs = maintain.all(n => n.type === 'el-tab-pane' && n.props.name === 'authorization')
    assert.equal(tabs.length, supported ? 1 : 0)
    if (supported) {
      assert.ok(text(tabs[0]).includes(authorized ? 'systemManage.authorized' : 'systemManage.notAuthorized'))
      assert.equal(maintain.all(n => n.type === 'el-tag', tabs[0])[0].props.type, authorized ? 'success' : 'warning')
    }
    assert.equal(queries, 1)
    assertNoPlatformAccess(storage)
  } finally { maintain.unmount() }
}

for (const platform of platforms) {
  const storage = makeStorage(platform)
  let configRef
  let enabled = 0
  const pollingId = 'legacy-polling'
  const timers = []
  const calls = { save: [], switch: [], delete: [] }
  const warnings = []
  let validation = async () => ({ valid: true, params: configRef.taskParam })
  const paramStub = { methods: { validateAndCollect: () => validation() }, render: () => null }
  const areaStub = {
    props: ['config'],
    render() {
      configRef = this.config
      return h({ methods: { submit: () => [] }, render: () => null }, { ref: 'canvasRef' })
    }
  }
  const service = await mountComponent('views/gam/taskManager/editTask/serviceConfig.vue', {
    props: { channelId: 'channel-1', joinType: 0 },
    mocks: {
      './areaSetting2.vue': { default: areaStub }, './paramSetting.vue': { default: paramStub },
      './BatchApplication.vue': empty, '@/components/eventBus.js': { default: { $emit() {} } }, uuid: { v4: () => 'fixture-id' }
    },
    globals: { ...globalsFor(storage), setTimeout: callback => { timers.push(callback); return timers.length }, clearTimeout() {} },
    message: { warning: value => warnings.push(value) },
    api: {
      algorithmInquire: async () => ({ resData: { rows: [{ algorithmId: 'algorithm-1', algorithmCode: '2001', algorithmCategory: '2', algorithmName: 'User algorithm' }] } }),
      selectAllAlgorithmInfo: async () => ({ resData: { algorithmIds: ['algorithm-1'] } }),
      boxGetTimeTemplate: async () => ({ resData: { rows: [{ scheduleId: 'schedule-1', scheduleName: 'Custom schedule' }] } }),
      selectConfigByAlgorithmId: async () => ({ resData: {
        category: 1, pollingId, scheduleId: 'schedule-1', taskEnableStatus: enabled,
        algorithmMetadata: JSON.stringify({ regionType: 'polygon', scheduleSupport: 1, params: [
          { key: 'editable', type: 'text', name: 'Editable', value: 'scene', channelEditable: true, senior: 2 },
          { key: 'sceneOnly', type: 'text', name: 'Scene', value: 'scene-value', channelEditable: false, senior: 2 }
        ] }),
        taskConfig: { areas: [], shieldedAreas: [], params: [{ key: 'editable', value: 'channel-value' }, { key: 'sceneOnly', value: 'stale-override' }] }
      } }),
      saveOrUpdate: async data => { calls.save.push(plain(data)); return { resCode: 1 } },
      boxSwitchTask: async data => { calls.switch.push(plain(data)); enabled = data.switch; return { resCode: 1 } },
      boxDeleteTask: async data => { calls.delete.push(plain(data)); return { resCode: 1 } }
    }
  })
  try {
    await settle(service)
    assert.equal(configRef.pollingId, 'legacy-polling')
    assert.equal(configRef.scheduleId, 'schedule-1')
    assert.deepEqual(plain(configRef.taskParam.map(param => [param.key, param.value, param.senior])), [['editable', 'channel-value', 0]])
    let finishValidation
    validation = () => new Promise(resolve => { finishValidation = resolve })
    button(service, 'action.save').props.onClick()
    await settle(service)
    assert.equal(calls.save.length, 0, 'save waits for child validation')
    finishValidation({ valid: true, params: configRef.taskParam })
    await settle(service)
    validation = async () => ({ valid: true, params: configRef.taskParam })
    assert.equal(calls.save.length, 1)
    assert.equal(calls.save[0].category, 1)
    assert.equal(calls.save[0].pollingId, 'legacy-polling')
    assert.equal(calls.save[0].scheduleId, 'schedule-1')
    assert.deepEqual(calls.save[0].taskConfig.params, [
      { key: 'editable', value: 'channel-value' },
      { key: 'param.privacyEnabled', value: '0' },
      { key: 'param.privacyLabels', value: '*' },
      { key: 'param.privacyStrength', value: '2' }
    ], 'legacy services save explicit disabled privacy defaults without altering platform-owned fields')
    button(service, 'action.enableService').props.onClick()
    await settle(service)
    button(service, 'action.disableService').props.onClick()
    await settle(service)
    assert.deepEqual(calls.switch, [1, 0].map(value => ({ channelId: 'channel-1', algorithmId: 'algorithm-1', switch: value })))
    button(service, 'action.deleteService').props.onClick()
    await settle(service)
    const confirm = service.all(n => n.type === 'el-dialog' && n.props.modelValue === true)[0]
    button(service, 'action.ok', confirm).props.onClick()
    await settle(service)
    assert.deepEqual(calls.delete, [{ channelId: 'channel-1', algorithmId: 'algorithm-1' }])
    timers.forEach(callback => callback())
    validation = async () => ({ valid: false, params: [] })
    button(service, 'action.save').props.onClick()
    await settle(service)
    assert.equal(calls.save.length, 1, 'failed asynchronous validation prevents saving')
    validation = async () => { throw new Error('validation unavailable') }
    timers.forEach(callback => callback())
    button(service, 'action.save').props.onClick()
    await settle(service)
    assert.equal(calls.save.length, 1, 'rejected validation prevents saving')
    validation = () => new Promise(resolve => { finishValidation = resolve })
    timers.forEach(callback => callback())
    button(service, 'action.save').props.onClick()
    await settle(service)
    configRef.channelId = 'replacement-channel'
    finishValidation({ valid: true, params: configRef.taskParam })
    await settle(service)
    assert.equal(calls.save.length, 1, 'late validation cannot save to a replacement channel')
    configRef.channelId = 'channel-1'
    validation = async () => ({ valid: true, params: configRef.taskParam })
    // The existing legacy polling validation remains part of save semantics.
    timers.forEach(callback => callback())
    configRef.pollingId = ''
    button(service, 'action.save').props.onClick()
    await settle(service)
    assert.equal(calls.save.length, 1)
    assert.ok(warnings.includes('validate.selectPollingStrategy'))
    assertNoPlatformAccess(storage)
  } finally { service.unmount() }
}

for (const platform of platforms) {
  const storage = makeStorage(platform)
  const created = []
  const algorithms = await mountComponent('views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue', {
    mocks: { '@/components/TopBar.vue': empty, moment: { default: () => ({ format: () => '' }) } },
    globals: globalsFor(storage),
    api: {
      algorithmInquire: async () => ({ resData: { rows: [], total: 0 } }),
      boxCameraPage: async () => ({ resData: { rows: [] } }),
      addAlgorithmLayout: async data => { created.push(plain(data)); return { resCode: 1 } }
    }
  })
  try {
    button(algorithms, 'action.createTask').props.onClick()
    await settle(algorithms)
    const dialog = algorithms.all(n => n.type === 'el-dialog' && n.props.title === 'action.createTask')[0]
    const form = algorithms.all(n => n.type === 'el-form', dialog)[0].props.model
    Object.assign(form, { algorithmName: 'New task', remark: 'Custom description', algorithmUsage: '1', algorithmCategory: '2', checkType: '1' })
    button(algorithms, 'action.ok', dialog).props.onClick()
    await settle(algorithms)
    assert.equal(created.length, 1)
    assert.equal(created[0].algorithmName, 'New task')
    assert.ok(!text(algorithms.root).includes('action.syncAll'))
    assertNoPlatformAccess(storage)
  } finally { algorithms.unmount() }

  const updated = []
  const models = await mountComponent('views/gam/countManagement/atomicModel/index.vue', {
    mocks: { 'vue-router': { useRouter: () => ({ push() {} }) } },
    globals: globalsFor(storage),
    api: {
      atomicModelList: async () => ({ resData: { list: [] } }),
      algorithmInquire: async () => ({ resData: { rows: [] } }),
      queryDeviceInfo: async () => ({ resData: { devInfoList: [{ key: 'acceleratorBackend', value: 'rknn' }, { key: 'rkllmAvailable', value: 'true' }] } }),
      getAtomicModelPage: async () => ({ resData: { rows: [{ modelCode: 'model-1', modelName: 'Original model', description: 'Original description' }], total: 1 } }),
      updateAtomicModel: async data => { updated.push(plain(data)); return { resCode: 1 } }
    }
  })
  try {
    await settle(models)
    button(models, 'action.importModel')
    assert.ok(!text(models.root).includes('action.batchUpdate'))
    button(models, 'action.details').props.onClick()
    await settle(models)
    button(models, 'action.editModel').props.onClick()
    await settle(models)
    const dialog = models.all(n => n.type === 'el-dialog' && n.props.title === 'action.editModel')[0]
    const form = models.all(n => n.type === 'el-form', dialog)[0].props.model
    assert.equal(form.modelCode, 'model-1')
    assert.equal(form.modelName, 'Original model')
    form.modelName = 'Edited model'
    form.description = 'Edited description'
    button(models, 'action.confirm', dialog).props.onClick()
    await settle(models)
    assert.deepEqual(updated, [{ modelCode: 'model-1', modelName: 'Edited model', description: 'Edited description' }])
    assertNoPlatformAccess(storage)
  } finally { models.unmount() }
}

// Exercise the existing component upload boundary without staging real files.
for (const failConsumer of [false, true]) {
  const storage = makeStorage('-1')
  const uploaded = [], cancelled = [], progress = []
  const failure = new Error('algorithm package rejected')
  const file = { name: 'algorithm.zip', size: 42 }
  let stageCalls = 0
  const algorithms = await mountComponent('views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue', {
    mocks: {
      '@/components/TopBar.vue': empty, moment: { default: () => ({ format: () => '' }) },
      '@/utils/chunkUpload': {
        UploadPurpose: { ALGORITHM: 'algorithm' },
        uploadFileInChunks: async (received, options) => {
          stageCalls++
          assert.equal(received, file)
          assert.equal(options.purpose, 'algorithm')
          options.onProgress({ percent: 100 })
          return { uploadId: 'staged-algorithm' }
        }
      }
    },
    globals: globalsFor(storage),
    api: {
      algorithmInquire: async () => ({ resData: { rows: [], total: 0 } }),
      boxCameraPage: async () => ({ resData: { rows: [] } }),
      boxAlgorithmUpload: async data => { uploaded.push(plain(data)); if (failConsumer) throw failure; return { resCode: 1 } },
      cancelAtomicModelUpload: async data => { cancelled.push(plain(data)); return { resCode: 1 } }
    }
  })
  try {
    algorithms.instance.uploadAlgorithmicVisible = true
    const result = algorithms.instance.uploadFile({ file, onProgress: value => progress.push(plain(value)) })
    if (failConsumer) await assert.rejects(result, error => error === failure)
    else await result
    await settle(algorithms)
    assert.equal(stageCalls, 1)
    assert.deepEqual(progress, [{ percent: 100 }])
    assert.deepEqual(uploaded, [{ uploadId: 'staged-algorithm' }])
    assert.deepEqual(cancelled, failConsumer ? [{ uploadId: 'staged-algorithm' }] : [])
    assert.equal(algorithms.instance.uploadAlgorithmicLoading, false)
    assert.equal(algorithms.instance.uploadAlgorithmicVisible, failConsumer)
    assertNoPlatformAccess(storage)
  } finally { algorithms.unmount() }
}

console.log('Edge behavior checks passed (legacy storage, session, resources, coordinates, Guard, task actions, lists and algorithm upload)')
