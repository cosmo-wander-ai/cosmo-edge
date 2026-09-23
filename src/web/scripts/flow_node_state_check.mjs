import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { h, onMounted } from 'vue'
import * as Vue from 'vue'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { createNodeState, updateAtomicList, updateNodeConfig } from '../src/views/gam/countManagement/arrangeDetail/flow/nodeState.js'

// CMake runs this script from an isolated build/web/web_unified copy.
const repositoryRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
const plain = value => JSON.parse(JSON.stringify(value))
const empty = { default: { render: () => null } }
const config = { params: [{ key: 'message', value: 'saved' }], webConfig: { labelList: [], labelFilterList: [], metaDataParams: [], atomic: {} } }
const imported = { actionId: 'action-1', flowActionId: 'node-1', configObject: config, extension: { retained: true } }
const state = createNodeState(imported, { actionId: 'action-1', configObject: { stale: true } })
assert.ok(!Object.hasOwn(state.flowData, 'configObject'))
assert.ok(!Object.hasOwn(state.actionDetail, 'configObject'))
state.configObject.params[0].value = 'local'
assert.equal(imported.configObject.params[0].value, 'saved', 'editing cannot mutate imported templates')
const updated = updateNodeConfig([{ id: 'node-1', data: state }], 'node-1', config)
assert.equal(updated[0].data.configObject, config)
assert.ok(!Object.hasOwn(updated[0].data.flowData, 'configObject'))
assert.ok(!Object.hasOwn(updated[0].data.actionDetail, 'configObject'))

const originalAtomics = Object.freeze([
  Object.freeze({ position: 'first', atomicCode: 'a', atomicName: 'A', labelList: [] }),
  Object.freeze({ position: '7', atomicCode: 'old', atomicName: 'Old', labelList: [], retainedOnlyIfUntouched: true }),
  Object.freeze({ position: 'last', atomicCode: 'z', atomicName: 'Z', labelList: [] })
])
const replacementLabels = [{ class_name: 'helmet', used: true }]
const replacedAtomics = updateAtomicList(originalAtomics, { position: 7, atomicCode: 'new', atomicName: 'New', labelList: replacementLabels })
assert.deepEqual(replacedAtomics.map(item => item.position), ['first', '7', 'last'], 'atomic replacement preserves ordering and stringifies position')
assert.deepEqual(replacedAtomics[1], { position: '7', atomicCode: 'new', atomicName: 'New', labelList: replacementLabels })
assert.equal(replacedAtomics[0], originalAtomics[0], 'untouched entries keep their identity')
assert.equal(originalAtomics[1].atomicCode, 'old', 'atomic replacement does not mutate its input')
const appendedAtomics = updateAtomicList(originalAtomics, { position: 'added', labelList: null })
assert.deepEqual(appendedAtomics.at(-1), { position: 'added', atomicCode: '', atomicName: '', labelList: [] })
assert.deepEqual(appendedAtomics.slice(0, 3), originalAtomics, 'new atomic entries append after existing ones')
assert.equal(updateAtomicList(originalAtomics, {}), originalAtomics, 'a missing position is a no-op')

for (const linkage of [false, true]) {
  const prefix = '@/views/gam/countManagement/arrangeDetail/flow/'
  const graph = { nodes: [], edges: [], pendingNodes: null, pendingEdges: null }
  const listeners = new Map()
  const bus = {
    $on(name, fn) { if (!listeners.has(name)) listeners.set(name, new Set()); listeners.get(name).add(fn) },
    $off(name, fn) { listeners.get(name)?.delete(fn) },
    $emit(name, value) { for (const fn of listeners.get(name) || []) fn(value) }
  }
  const action = { id: 'action-1', actionName: 'Action', inputParamConfig: JSON.stringify([{ key: 'message', name: 'Message', type: 'text', level: '1', defaultValue: 'default' }]) }
  const workflow = [
    { ...plain(imported), actionName: 'Action', remark: '', preFlowActionId: '-1' },
    { ...plain(imported), flowActionId: 'node-2', actionName: 'Action', remark: '', preFlowActionId: 'node-1' }
  ]
  let id = 0
  const mocks = {
    '@vue-flow/core': {
      VueFlow: {
        props: ['nodes', 'edges'], emits: ['update:nodes', 'update:edges'],
        setup(props, { attrs, emit }) {
          graph.publishNodes = nodes => emit('update:nodes', nodes)
          graph.publishEdges = edges => emit('update:edges', edges)
          onMounted(() => {
            if (graph.pendingNodes) graph.publishNodes(graph.pendingNodes)
            if (graph.pendingEdges) graph.publishEdges(graph.pendingEdges)
          })
          return () => {
            graph.nodes = props.nodes
            graph.edges = props.edges
            return h('flow-fixture', { ...attrs, nodes: props.nodes, edges: props.edges })
          }
        }
      },
      useVueFlow: () => ({
        setNodes(value) {
          graph.pendingNodes = typeof value === 'function' ? value(graph.nodes) : value
          graph.publishNodes?.(graph.pendingNodes)
        },
        setEdges(value) {
          graph.pendingEdges = typeof value === 'function' ? value(graph.edges) : value
          graph.publishEdges?.(graph.pendingEdges)
        },
        addNodes() {}, onPaneReady() {}, getEdges: { get value() { return graph.edges } }
      })
    },
    '@vue-flow/background': { Background: empty.default },
    '@vue-flow/controls': { Controls: empty.default },
    '@vue-flow/minimap': { MiniMap: empty.default },
    '@vue-flow/core/dist/style.css': {}, '@vue-flow/minimap/dist/style.css': {}, '@vue-flow/controls/dist/style.css': {},
    dagre: { default: {} }, lodash: { default: lodash },
    uuid: { v4: () => `id-${++id}` },
    '@/components/eventBus.js': { default: bus },
    '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default])),
    './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': { default: empty.default }
  }
  for (const name of ['ActionView.vue', 'CustomFormNode.vue', 'StartNode.vue', 'EndNode.vue', 'ActionEdge.vue', 'StageGroupNode.vue']) {
    mocks[`./${name}`] = empty
    mocks[`${prefix}${name}`] = empty
  }
  const entry = linkage ? 'views/box/strategyManagement/components/ArrangeFlow.vue' : 'views/gam/countManagement/arrangeDetail/flow/ArrangeFlow.vue'
  const props = linkage
    ? { width: 1000, height: 700, actionList: [action], strategyId: 'strategy-1', workFlow: JSON.stringify(workflow) }
    : { width: 1000, height: 700, actionList: [action], algorithmData: { algorithmCode: 'scene-1', algorithmProcessdata: JSON.stringify(workflow), atomicList: '[]' } }
  const editor = await mountComponent(entry, {
    props, mocks,
    globals: {
      requestAnimationFrame() {},
      document: { addEventListener() {}, removeEventListener() {} },
      localStorage: { setItem() { assert.fail('collecting a node must not persist a localStorage snapshot') } }
    }
  })
  const savedItems = () => {
    const payload = editor.instance.saveFlowData()
    return JSON.parse(linkage ? payload.workFlow : payload.algorithmProcessdata)
  }
  try {
    assert.deepEqual(savedItems().map(item => item.configObject), [config, config], 'untouched node JSON round-trips')
    for (const node of graph.nodes.filter(node => node.type === 'customForm')) {
      assert.ok(Object.hasOwn(node.data, 'configObject'))
      assert.ok(!Object.hasOwn(node.data.flowData, 'configObject'))
      assert.ok(!Object.hasOwn(node.data.actionDetail, 'configObject'))
    }
    bus.$emit('flow:openDetailPanel', 'node-1')
    await editor.settle()
    const input = () => editor.all(node => node.type === 'el-input')[0]
    assert.equal(input().props.modelValue, 'saved', 'panel reads the canonical node config')
    input().props.activate('edited-before-debounce')
    let saved = savedItems()
    assert.equal(saved[0].configObject.params[0].value, 'edited-before-debounce', 'immediate save flushes the current panel')
    assert.equal(saved[1].configObject.params[0].value, 'saved', 'sibling node keeps its own config')
    assert.deepEqual(saved[0].extension, { retained: true }, 'flow extension fields survive edits')
    bus.$emit('flow:openDetailPanel', 'node-2')
    await editor.settle()
    assert.equal(input().props.modelValue, 'saved')
    input().props.activate('second-edit')
    bus.$emit('flow:openDetailPanel', 'node-1')
    await editor.settle()
    assert.equal(input().props.modelValue, 'edited-before-debounce', 'reopened panel reflects its current node')
    saved = savedItems()
    assert.equal(saved[1].configObject.params[0].value, 'second-edit', 'switching nodes collects the outgoing panel')
    bus.$emit('flow:removeNodes', ['node-1'])
    await editor.settle()
    assert.equal(editor.all(node => node.type === 'el-input').length, 0, 'removing the selected node clears its derived panel')
    assert.equal(savedItems().some(item => item.flowActionId === 'node-1'), false)
    editor.instance.clearFlow()
    await editor.settle()
    assert.equal(savedItems().length, 0)
    assert.equal(editor.all(node => node.type === 'el-input').length, 0, 'clearing the flow also clears its panel')
  } finally { editor.unmount() }
  assert.ok([...listeners.values()].every(set => set.size === 0), 'graph listeners are removed on unmount')
}

// Preserve saved selections and ensure grouping nodes do not disable their
// descendants in the accessibility tree. Actual transfer clicks are covered by
// the device browser acceptance; this fixture only observes the real SFC data.
for (const key of ['algs', 'strageAlgorithms']) {
  const savedSelection = [{ channelId: 'channel-1', algorithmId: 'selected-algorithm' }]
  const alarmForm = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: {
      actionDetail: {
        actionId: 'LA_AlarmData_Code', flowActionId: 'alarm-node',
        inputParamConfig: JSON.stringify([{ key, type: 'taskList', name: 'Algorithms', level: '1', defaultValue: '[]' }])
      },
      configObject: { params: [{ key, value: JSON.stringify(savedSelection) }], webConfig: {} }
    },
    mocks: {
      './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty,
      uuid: { v4: () => 'fixture-id' }, lodash: { default: lodash },
      '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
      '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default])),
      'tree-transfer-vue3': {
        default: { props: ['fromData', 'toData'], render() { return h('transfer-data-fixture', { fromData: this.fromData, toData: this.toData }) } }
      }
    },
    api: {
      getChannelList: async () => ({ resData: { rows: [{
        videoChannelId: 'channel-1', channelName: 'Channel', taskList: [
          { algorithmId: 'available-algorithm', algorithmName: 'Available' },
          { algorithmId: 'selected-algorithm', algorithmName: 'Selected' }
        ]
      }] } })
    }
  })
  try {
    await alarmForm.settle()
    const transfer = alarmForm.all(node => node.type === 'transfer-data-fixture')[0]
    for (const side of ['fromData', 'toData']) {
      const [group] = transfer.props[side]
      assert.equal(group.id, 'channel-1')
      assert.notEqual(group.disabled, true, 'channel groups cannot impose aria-disabled on algorithm descendants')
      assert.equal(group.children.length, 1)
      assert.notEqual(group.children[0].disabled, true, 'algorithm leaves remain selectable')
    }
    const saved = alarmForm.instance.submitForm().params.find(param => param.key === key)
    assert.deepEqual(JSON.parse(saved.value), savedSelection, 'saving preserves only channelId/algorithmId pairs')
  } finally { alarmForm.unmount() }
}

// Hiding implementation settings must not drop the mode when a scene is saved.
for (const savedMode of [null, 'recognition', 'behavior']) {
  const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: {
      actionDetail: {
        id: 'BA_20003', flowActionId: 'result-node',
        inputParamConfig: JSON.stringify([
          { key: 'inputMode', type: 'select', name: 'Mode', level: '1', defaultValue: 'recognition', options: [{ name: 'Recognition', value: 'recognition' }] },
          { key: 'param.posSenDurationTimeType', type: 'text', name: 'Multiplier', level: '1', defaultValue: '1' },
          { key: 'param.posSenDurationTime', type: 'text', name: 'Duration', level: '2', defaultValue: '3000' }
        ])
      },
      configObject: { params: savedMode ? [{ key: 'inputMode', value: savedMode }] : [], webConfig: {} }
    },
    mocks: {
      './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': empty,
      uuid: { v4: () => 'fixture-id' }, lodash: { default: lodash },
      '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
      '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
    }
  })
  try {
    assert.equal(form.all(node => node.type === 'el-select').length, 0, 'technical input modes remain hidden')
    assert.equal(form.all(node => node.type === 'el-radio-group').length, 1, 'business accumulation purpose is selectable')
    const saved = form.instance.submitForm()
    assert.equal(saved.params.find(p => p.key === 'inputMode')?.value, savedMode || 'recognition')
    assert.equal(saved.params.find(p => p.key === 'param.posSenDurationTimeType')?.value, '1')
    assert.equal(saved.webConfig.metaDataParams.find(p => p.key === 'param.posSenDurationTime')?.defaultValue, '3000', 'duration remains a task parameter')
  } finally { form.unmount() }
}

// The real association form must turn model label selections into runtime
// parameters, and migrating a legacy face node must retain its region/thresholds.
for (const platform of ['bm1688', 'cv186x', 'x86']) {
  const actions = JSON.parse(await readFile(path.join(
    repositoryRoot, `data/resource/aiboxresource_${platform}/layout/actions.json`
  ), 'utf8'))
  const accumulationAction = actions.find(item => item.id === 'BA_20003')
  for (const mode of [null, 'auto', 'recognition', 'behavior']) {
    const params = mode ? [
      { key: 'inputMode', value: mode },
      { key: 'param.minValidFaceCount', value: '7' },
      { key: 'param.faceSampleIntervalMs', value: '350' },
      { key: 'param.posSenDurationTimeType', value: '2' }
    ] : []
    if (mode === 'behavior') params.push({ key: 'param.posSenHitCount', value: '8' })
    if (mode === 'auto') params.push({ key: 'param.minValidObservationCount', value: '9' })
    const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
      props: { actionDetail: { ...accumulationAction, flowActionId: 'accumulation' }, configObject: { params, webConfig: {} } },
      mocks: {
        './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': empty,
        uuid: { v4: () => 'fixture-id' }, lodash: { default: lodash },
        '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
        '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
      }
    })
    try {
      const saved = form.instance.submitForm()
      const metadata = saved.webConfig.metaDataParams
      assert.equal(saved.params.find(p => p.key === 'inputMode')?.value, mode || 'auto')
      assert.equal(saved.params.find(p => p.key === 'param.posSenDurationTimeType')?.value, mode ? '2' : '1')
      assert.equal(metadata.find(p => p.key === 'param.minValidObservationCount')?.value, mode === 'auto' ? '9' : mode ? '7' : '3')
      assert.equal(metadata.find(p => p.key === 'param.observationIntervalMs')?.value, mode ? '350' : '200')
      assert.equal(metadata.length, 4, 'only four supported task parameters are exposed')
      assert.ok(!metadata.some(p => ['param.minValidFaceCount', 'param.faceSampleIntervalMs'].includes(p.key)))
      assert.equal(form.all(n => n.type === 'el-select').length, 0, 'automatic input and multiplier stay hidden')
      if (mode === 'behavior') assert.equal(saved.params.find(p => p.key === 'param.posSenHitCount')?.value, '8')
    } finally { form.unmount() }
  }
  const action = actions.find(item => item.id === 'AA_00006')
  for (const legacy of [false, true]) {
    const initial = legacy ? {
      params: [
        { key: 'atomicCode', value: 'detector' },
        { key: 'param.minFaceSize', value: '80' },
        { key: 'param.faceDetectionConfidence', value: '0.75' }
      ],
      webConfig: { labelList: [{ class_name: 'face', used: true }] }
    } : { params: [], webConfig: {} }
    const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
      props: { actionDetail: { ...action, flowActionId: 'association' }, configObject: initial },
      mocks: {
        vue: { ...Vue, resolveComponent: name => name === 'el-table-column' ? empty.default : Vue.resolveComponent(name) },
        './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': empty,
        uuid: { v4: () => 'fixture-id' }, lodash: { default: lodash },
        '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
        '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
      },
      api: { atomicModelList: async () => ({ resData: { list: [{
        atomicCode: 'detector', atomicName: 'Detector', label: JSON.stringify([
          { class_name: 'face', nameCN: 'Face' }, { class_name: 'plate', nameCN: 'Plate' }
        ])
      }] } }) }
    })
    try {
      await form.settle()
      if (!legacy) {
        form.all(n => n.type === 'el-select')[0].props.activate('detector')
        await form.settle()
      }
      const saved = form.instance.submitForm()
      assert.equal(saved.params.find(p => p.key === 'param.associationRegion')?.value, legacy ? 'upper' : 'whole')
      assert.equal(saved.webConfig.metaDataParams.find(p => p.key === 'param.minTargetSize')?.value, legacy ? '80' : '60')
      assert.equal(saved.webConfig.metaDataParams.find(p => p.key === 'param.detectionConfidence')?.value, legacy ? '0.75' : '0.66')
      assert.deepEqual(JSON.parse(saved.params.find(p => p.key === 'param.associationLabels').value), legacy ? ['face'] : [])
      assert.equal(saved.webConfig.metaDataParams.some(p => p.key.startsWith('aiParam.')), false, 'association does not expose unused per-label detector parameters')
      const rows = form.all(n => n.type === 'el-table')[0].props.data
      rows.forEach(row => { row.used = row.class_name === 'plate' })
      await form.settle()
      const changed = form.instance.submitForm()
      assert.deepEqual(JSON.parse(changed.params.find(p => p.key === 'param.associationLabels').value), ['plate'])
      rows.forEach(row => { row.used = false })
      await form.settle()
      assert.deepEqual(JSON.parse(form.instance.submitForm().params.find(p => p.key === 'param.associationLabels').value), [], 'deselecting every label must not fall back to face')
    } finally { form.unmount() }
  }
}

console.log('Flow node state checks passed')
