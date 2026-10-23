import assert from 'node:assert/strict'
import { h } from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// Exercise the compiled service component, including its ordinary source save.
for (const outcome of ['success', 'invalid', 'save-error']) {
  const sourceSaves = [], batchRequests = []
  const storage = { getItem: () => null }
  const service = await mountComponent('views/gam/taskManager/editTask/serviceConfig.vue', {
    props: { channelId: 'source-channel', joinType: 0 },
    mocks: {
      './areaSetting2.vue': { default: { render: () => null } },
      './paramSetting.vue': { default: {
        methods: { validateAndCollect: async () => ({
          valid: outcome !== 'invalid',
          params: [{ key: 'param.sensitivity', value: '7', type: 'text', senior: 0 }]
        }) },
        render: () => null
      } },
      './BatchApplication.vue': { default: { render() { return h('batch-application', this.$attrs) } } },
      '@/components/eventBus.js': { default: { $emit() {} } },
      uuid: { v4: () => 'fixture-id' }
    },
    globals: {
      localStorage: storage, window: { location: { search: '', hash: '' }, localStorage: storage },
      setTimeout: () => 1, clearTimeout() {}
    },
    api: {
      algorithmInquire: async () => ({ resData: { rows: [
        { algorithmId: 'algorithm', algorithmCode: '2001', algorithmCategory: '2', algorithmName: 'Test' }
      ] } }),
      selectAllAlgorithmInfo: async () => ({ resData: { algorithmIds: ['algorithm'] } }),
      queryTimeTemplatePage: async () => ({ resData: { rows: [] } }),
      selectConfigByAlgorithmId: async () => ({ resData: {
        category: 0, taskEnableStatus: 0, scheduleId: 'schedule',
        algorithmMetadata: JSON.stringify({
          regionType: 'polygon', scheduleSupport: 1,
          region: { heads: [{ key: 'name', type: 'text' }] },
          shieldedRegion: { heads: [{ key: 'name', type: 'text' }] },
          params: [{ key: 'param.sensitivity', name: 'Sensitivity', type: 'text', value: '7', senior: 0 }]
        }),
        taskConfig: {
          areas: [{ areaId: 'source-area', name: 'Source area', points: [{ xRatio: 0.1, yRatio: 0.2 }], params: [] }],
          shieldedAreas: [{ areaId: 'source-shield', name: 'Source shield', points: [{ xRatio: 0.3, yRatio: 0.4 }], params: [] }],
          params: []
        }
      } }),
      saveOrUpdate: async data => {
        sourceSaves.push(data)
        if (outcome === 'save-error') throw new Error('source save failed')
        return { resCode: 1 }
      },
      applyParamsBatch: async data => {
        assert.equal(sourceSaves.length, 1, 'source save completes before batch application')
        batchRequests.push(data)
        return { resCode: 1, resData: { failedList: [] } }
      }
    }
  })
  try {
    for (let i = 0; i < 6; i++) await service.settle()
    await service.all(node => node.type === 'batch-application')[0].props.onConfirm({
      targetChannelIds: ['target-a', 'target-b']
    })
    for (let i = 0; i < 6; i++) await service.settle()
    assert.equal(batchRequests.length, outcome === 'success' ? 1 : 0, outcome)
    assert.equal(sourceSaves.length, outcome === 'invalid' ? 0 : 1, outcome)
    if (outcome !== 'success') continue

    const batch = batchRequests[0]
    assert.equal(Object.hasOwn(batch.taskConfig, 'areas'), false, 'batch must not send source regions')
    assert.equal(Object.hasOwn(batch.taskConfig, 'shieldedAreas'), false, 'batch must not send source masks')
    assert.deepEqual(Array.from(batch.targetChannelIds), ['target-a', 'target-b'])
    assert.equal(batch.algorithmId, 'algorithm')
    assert.equal(batch.scheduleId, 'schedule')
    assert.equal(batch.taskConfig.params.find(p => p.key === 'param.sensitivity').value, '7')
    assert.equal(sourceSaves[0].taskConfig.areas[0].areaId, 'source-area', 'ordinary source save keeps its regions')
    assert.equal(sourceSaves[0].taskConfig.shieldedAreas[0].areaId, 'source-shield')
    assert.notEqual(batch.taskConfig, sourceSaves[0].taskConfig, 'batch preparation uses a separate config object')
    assert.equal(Object.hasOwn(sourceSaves[0], 'targetChannelIds'), false)
  } finally { service.unmount() }
}

console.log('Batch region preservation checks passed')
