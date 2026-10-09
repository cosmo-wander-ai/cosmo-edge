import assert from 'node:assert/strict'
import { h, ref, reactive, provide, inject, getCurrentInstance } from 'vue'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { collectFlowData, collectMetaDataParams, createNodeState, updateAtomicList, updateNodeConfig } from '../src/views/gam/countManagement/arrangeDetail/flow/nodeState.js'

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

const extendedConfig = {
  ...plain(config), extension: { retained: true },
  webConfig: {
    ...plain(config.webConfig),
    atomic: { atomicCode: 'model', atomicNameI18nKey: 'resource.model.name', labelList: [], extension: true },
    metaDataParams: [{ key: 'threshold', nameI18nKey: 'resource.threshold', extension: true }]
  }
}
const extendedNode = { id: 'extended', type: 'customForm', data: createNodeState({ ...imported, configObject: extendedConfig }) }
const collected = collectFlowData([{ id: 'stage', type: 'stageGroup' }, extendedNode], [{ source: 'parent', target: 'extended' }])
assert.equal(collected.items.length, 1, 'stage decorations are not serialized')
assert.equal(collected.items[0].configObject, extendedNode.data.configObject, 'collection uses the canonical config without another editable copy')
assert.deepEqual(collected.items[0].configObject, extendedConfig)
assert.equal(collected.items[0].preFlowActionId, 'parent')
assert.deepEqual(collected.atomicCollected, [{ ...extendedConfig.webConfig.atomic, position: 'extended', atomicName: '' }])
assert.deepEqual(collectMetaDataParams([extendedNode]), extendedConfig.webConfig.metaDataParams)

// Render real node, edge, picker and detail-panel SFCs. Only the Vue Flow
// renderer/geometry and Element Plus controls are substituted by the harness.
for (const linkage of [false, true]) {
  const graphs = []
  const flowKey = Symbol('testFlow')
  const documentListeners = new Set()
  let confirmation = 'confirm'
  let finishConfirmation
  const action = { id: 'action-1', actionName: 'Canonical action', actionType: 1, inputParamConfig: JSON.stringify([{ key: 'message', name: 'Message', type: 'text', level: '1', defaultValue: 'default' }]) }
  const workflow = [
    { ...plain(imported), actionName: 'Saved action', actionNameI18nKey: 'actions.first', remark: '', preFlowActionId: '-1' },
    { ...plain(imported), flowActionId: 'node-2', actionName: 'Saved action', remark: '', preFlowActionId: 'node-1' }
  ]
  const props = linkage
    ? { width: 1000, height: 700, actionList: [action], strategyId: 'strategy-1', workFlow: JSON.stringify(workflow) }
    : { width: 1000, height: 700, actionList: [action], algorithmData: { algorithmCode: 'scene-1', algorithmProcessdata: JSON.stringify(workflow), atomicList: '[]' } }
  const canvasProps = reactive([plain(props), plain(props)])
  const visible = ref([true, true])
  const canvases = []
  let id = 0
  const mocks = {
    '@vue-flow/core': {
      VueFlow: {
        props: ['nodes', 'edges', 'nodeTypes', 'edgeTypes'], emits: ['update:nodes', 'update:edges'],
        setup(props, { attrs }) {
          const graph = inject(flowKey)
          graph.owner = getCurrentInstance().parent
          const editorKey = Object.getOwnPropertySymbols(graph.owner.provides).find(key => key.description === 'flowEditor')
          graph.editor = inject(editorKey)
          return () => {
            graph.nodes = props.nodes
            graph.edges = props.edges
            return h('flow-fixture', { ...attrs, graph }, [
              ...props.nodes.map(node => h('node-fixture', { id: node.id, key: node.id }, [
                h(props.nodeTypes[node.type], { id: node.id, data: node.data })
              ])),
              ...props.edges.map(edge => h('edge-fixture', { id: edge.id, key: edge.id }, [
                h(props.edgeTypes[edge.type], { id: edge.id, sourceX: 0, sourceY: 0, targetX: 100, targetY: 0, sourcePosition: 'right', targetPosition: 'left' })
              ]))
            ])
          }
        }
      },
      useVueFlow: () => {
        const graph = { nodes: [], edges: [] }
        graphs.push(graph)
        provide(flowKey, graph)
        return { onPaneReady() {} }
      },
      Handle: empty.default, Position: { Left: 'left', Right: 'right' },
      BaseEdge: empty.default,
      EdgeLabelRenderer: { setup: (_, { slots }) => () => slots.default?.() },
      getBezierPath: () => ['', 50, 0], getSmoothStepPath: () => ['', 50, 0]
    },
    'element-plus': {
      ElMessage: () => {},
      ElMessageBox: { confirm: () => confirmation === 'pending'
        ? new Promise(resolve => { finishConfirmation = resolve })
        : confirmation === 'cancel' ? Promise.reject('cancel') : Promise.resolve() }
    },
    '@vue-flow/background': { Background: empty.default },
    '@vue-flow/controls': { Controls: empty.default },
    '@vue-flow/core/dist/style.css': {}, '@vue-flow/controls/dist/style.css': {},
    dagre: { default: {} }, lodash: { default: lodash },
    uuid: { v4: () => `id-${++id}` },
    '@/components/eventBus.js': { default: {
      $on(name) { assert.ok(!/^(flow|edgeMenu):/.test(name), 'graph listeners must be canvas-scoped') },
      $off() {},
      $emit(name) { assert.ok(!/^(flow|edgeMenu):/.test(name), 'graph operations must be canvas-scoped') }
    } },
    '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default])),
    './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': { default: empty.default }
  }
  const entry = linkage ? 'views/box/strategyManagement/components/ArrangeFlow.vue' : 'views/gam/countManagement/arrangeDetail/flow/ArrangeFlow.vue'
  const editor = await mountComponent(entry, {
    mocks,
    wrap: component => ({
      setup: () => () => h('editors', canvasProps.map((props, index) => visible.value[index]
        ? h(component, { ...props, key: index, ref: value => { canvases[index] = value } }) : null))
    }),
    globals: {
      requestAnimationFrame() {},
      document: {
        addEventListener(name, callback) { documentListeners.add(callback) },
        removeEventListener(name, callback) { documentListeners.delete(callback) }
      },
      localStorage: { setItem() { assert.fail('collecting a node must not persist a localStorage snapshot') } }
    }
  })
  const savedItems = (index = 0) => {
    const payload = canvases[index].saveFlowData()
    return JSON.parse(linkage ? payload.workFlow : payload.algorithmProcessdata)
  }
  const graphRoot = (index = 0) => editor.all(node => node.type === 'flow-fixture' && node.props.graph === graphs[index])[0]
  const cardRoot = (id, index = 0) => editor.all(node => node.type === 'node-fixture' && node.props.id === id, graphRoot(index))[0]
  const click = node => { assert.ok(node, 'expected rendered control'); node.props.onClick({ stopPropagation() {} }) }
  const hasClass = (node, name) => typeof node.props.class === 'string' && node.props.class.split(' ').includes(name)
  const nodeControl = (id, name, index = 0) => editor.all(node => hasClass(node, name), cardRoot(id, index))[0]
  const edgeControl = (edgeId, name, index = 0) => {
    const root = editor.all(node => node.type === 'edge-fixture' && node.props.id === edgeId, graphRoot(index))[0]
    return editor.all(node => hasClass(node, name), root)[0]
  }
  const openNode = (id, index = 0) => click(nodeControl(id, 'node-card', index))
  const input = () => editor.all(node => node.type === 'el-input')[0]
  const dialogRoots = () => [...editor.teleportTargets.values()].flatMap(root => editor.all(node => node.type === 'el-dialog' && node.props.modelValue, root))
  const chooseAction = () => click(editor.all(node => hasClass(node, 'item-action'), dialogRoots()[0])[0])
  const reload = (items, index = 0) => {
    if (linkage) {
      canvasProps[index].strategyId += '-next'
      canvasProps[index].workFlow = JSON.stringify(items)
    } else {
      canvasProps[index].algorithmData = { ...canvasProps[index].algorithmData, algorithmProcessdata: JSON.stringify(items) }
    }
  }
  const findComponent = (vnode, name) => {
    if (vnode?.component?.type.__name === name) return vnode.component
    if (vnode?.component) return findComponent(vnode.component.subTree, name)
    return Array.isArray(vnode?.children) ? vnode.children.map(child => findComponent(child, name)).find(Boolean) : undefined
  }
  try {
    assert.equal(graphs.length, 2, 'each canvas owns a flow instance')
    assert.deepEqual(savedItems().map(item => item.configObject), [config, config], 'untouched node JSON round-trips')
    assert.equal(savedItems()[0].actionName, linkage ? 'Saved action' : 'Canonical action', 'page-specific action names are preserved')
    for (const node of graphs[0].nodes.filter(node => node.type === 'customForm')) {
      assert.ok(Object.hasOwn(node.data, 'configObject'))
      assert.ok(!Object.hasOwn(node.data.flowData, 'configObject'))
      assert.ok(!Object.hasOwn(node.data.actionDetail, 'configObject'))
    }
    const secondBefore = savedItems(1)
    openNode('node-1')
    await editor.settle()
    assert.equal(input().props.modelValue, 'saved', 'real node opens its own panel')
    input().props.activate('edited-before-debounce')
    let saved = savedItems()
    assert.equal(saved[0].configObject.params[0].value, 'edited-before-debounce', 'immediate save flushes the panel')
    assert.equal(saved[1].configObject.params[0].value, 'saved', 'sibling config is detached')
    assert.deepEqual(saved[0].extension, { retained: true })
    assert.equal(saved[0].actionNameI18nKey, 'actions.first')
    openNode('node-2')
    await editor.settle()
    input().props.activate('second-edit')
    openNode('node-1')
    await editor.settle()
    assert.equal(input().props.modelValue, 'edited-before-debounce')
    assert.equal(savedItems()[1].configObject.params[0].value, 'second-edit', 'switching nodes collects the outgoing panel')

    graphs[0].editor.updateAtomic({ position: 'node-1', atomicCode: 'model-1' })
    graphs[0].editor.updateAtomic({ position: 'node-2', atomicCode: 'model-2' })
    await editor.settle()
    assert.deepEqual(plain(findComponent(graphs[0].owner.subTree, 'NodeDetailPanel').props.atomicList).map(item => item.position), ['node-1', 'node-2'])
    confirmation = 'cancel'
    click(nodeControl('node-1', 'node-delete'))
    await editor.settle()
    assert.equal(savedItems().length, 2, 'cancel does not change the graph or panel')
    assert.equal(input().props.modelValue, 'edited-before-debounce')
    confirmation = 'confirm'
    click(nodeControl('node-1', 'node-delete'))
    await editor.settle()
    assert.equal(editor.all(node => node.type === 'el-input').length, 0, 'deleting the current node closes its panel')
    assert.deepEqual(savedItems().map(item => [item.flowActionId, item.preFlowActionId]), [['node-2', '-1']])
    openNode('node-2')
    await editor.settle()
    assert.deepEqual(plain(findComponent(graphs[0].owner.subTree, 'NodeDetailPanel').props.atomicList).map(item => item.position), ['node-2'], 'deleted node atomics are removed')
    click(editor.all(node => hasClass(node, 'panel-close'))[0])
    await editor.settle()

    // Menus, dialogs and graph mutations stay within the initiating canvas.
    let insertEdge = graphs[0].edges.find(edge => edge.source === '-1').id
    const otherEdge = graphs[1].edges[0].id
    click(edgeControl(insertEdge, 'edge-action-button'))
    click(edgeControl(otherEdge, 'edge-action-button', 1))
    await editor.settle()
    assert.equal(editor.all(node => hasClass(node, 'edge-menu')).length, 2, 'different canvases can keep independent menus')
    click(edgeControl(insertEdge, 'menu-item'))
    await editor.settle()
    assert.equal(dialogRoots().length, 1)
    chooseAction()
    await editor.settle()
    const inserted = savedItems().find(item => item.flowActionId !== 'node-2')
    assert.equal(inserted.preFlowActionId, '-1')
    assert.equal(savedItems().find(item => item.flowActionId === 'node-2').preFlowActionId, inserted.flowActionId)
    assert.deepEqual(savedItems(1), secondBefore, 'insertion leaves the other canvas unchanged')

    // Existing saved branches remain editable through the visible edge menu.
    const branchNode = 'saved-branch'
    reload([...savedItems(), {
      ...plain(imported), flowActionId: branchNode, actionName: 'Saved branch',
      remark: '', preFlowActionId: '-1'
    }])
    await editor.settle()
    assert.equal(graphs[0].edges.filter(edge => edge.source === '-1').length, 2)
    assert.equal(graphs[0].nodes.filter(node => node.type === 'end').length, 2)
    assert.deepEqual(savedItems().find(item => item.flowActionId === branchNode).configObject, config)
    const branchEndEdge = graphs[0].edges.find(edge => edge.source === branchNode).id
    click(edgeControl(branchEndEdge, 'edge-action-button'))
    await editor.settle()
    click(edgeControl(branchEndEdge, 'menu-item'))
    await editor.settle()
    assert.equal(dialogRoots().length, 1)
    chooseAction()
    await editor.settle()
    const branchInserted = savedItems().find(item => item.preFlowActionId === branchNode)
    assert.ok(branchInserted, 'a node can be inserted into a saved branch')
    assert.equal(graphs[0].edges.filter(edge => edge.source === '-1').length, 2)
    assert.equal(graphs[0].nodes.filter(node => node.type === 'end').length, 2)
    assert.deepEqual(savedItems(1), secondBefore, 'editing a loaded branch leaves the other canvas unchanged')
    openNode(branchNode)
    await editor.settle()
    input().props.activate('edited-saved-branch')
    const savedBranches = savedItems()
    assert.equal(savedBranches.find(item => item.flowActionId === branchNode).configObject.params[0].value, 'edited-saved-branch')
    assert.deepEqual(savedBranches.find(item => item.flowActionId === branchNode).extension, { retained: true })
    reload(savedBranches)
    await editor.settle()
    assert.deepEqual(savedItems(), savedBranches, 'saved branches, inserted nodes and extension fields round-trip')
    assert.equal(graphs[0].nodes.filter(node => node.type === 'end').length, 2)
    openNode(branchInserted.flowActionId)
    await editor.settle()
    const branchEdge = graphs[0].edges.find(edge => edge.target === branchNode).id
    click(edgeControl(branchEdge, 'edge-action-button'))
    await editor.settle()
    const branchRoot = editor.all(node => node.type === 'edge-fixture' && node.props.id === branchEdge, graphRoot())[0]
    click(editor.all(node => hasClass(node, 'menu-item'), branchRoot)[1])
    await editor.settle()
    assert.equal(savedItems().some(item => item.flowActionId === branchNode), false)
    assert.equal(savedItems().some(item => item.flowActionId === branchInserted.flowActionId), false)
    assert.equal(graphs[0].nodes.filter(node => node.type === 'end').length, 1)
    assert.equal(editor.all(node => node.type === 'el-input').length, 0, 'following-flow deletion closes the affected panel')

    const reopened = savedItems()
    canvases[0].clearFlow()
    await editor.settle()
    assert.equal(savedItems().length, 0)
    assert.equal(graphs[0].edges.length, 0)
    reload(reopened)
    await editor.settle()
    assert.deepEqual(savedItems(), reopened, 'save/clear/reload round-trips')
    openNode('node-2')
    await editor.settle()
    input().props.activate('must-not-leak-to-next-strategy')
    reload(workflow)
    await editor.settle()
    assert.equal(editor.all(node => node.type === 'el-input').length, 0, 'strategy/template replacement closes the old panel')
    assert.equal(savedItems()[1].configObject.params[0].value, 'saved')
    assert.deepEqual(savedItems(1), secondBefore, 'configuration, atomic updates and removal are isolated')

    canvasProps[1].actionList = null
    await editor.settle()
    assert.deepEqual(savedItems(1), secondBefore, 'an unavailable action catalog keeps saved node names')

    confirmation = 'pending'
    click(nodeControl('node-1', 'node-delete'))
    const saveAfterUnmount = canvases[0].saveFlowData
    const beforeUnmount = saveAfterUnmount()
    visible.value[0] = false
    await editor.settle()
    finishConfirmation()
    await editor.settle()
    assert.deepEqual(saveAfterUnmount(), beforeUnmount, 'a late confirmation cannot mutate an unmounted canvas')
    assert.deepEqual(savedItems(1), secondBefore)
  } finally { editor.unmount() }
  assert.equal(documentListeners.size, 0, 'edge/document listeners are removed on unmount')
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

console.log('Flow node state checks passed')
