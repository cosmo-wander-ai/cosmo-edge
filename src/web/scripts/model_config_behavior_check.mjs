import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import * as Vue from 'vue'
import { i18n, mountComponent } from './helpers/mount_behavior_component.mjs'

const plain = (value) => JSON.parse(JSON.stringify(value))
const repositoryRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
const text = (node) => [node.text, ...node.children.map(text)].join(' ')
const inert = { default: { render: () => null } }
const route = { query: { modelCode: 'fixture-model' } }
const componentMocks = {
  'vue-router': { useRoute: () => route, useRouter: () => ({ back() {} }) },
  './ParamsConfig.vue': inert,
  './configFLow.vue': inert
}

for (const platform of ['bm1688', 'cv186x', 'x86']) {
  const config = JSON.parse(await readFile(path.join(
    repositoryRoot, `data/resource/aiboxresource_${platform}/model_template/yolov8_det.json`
  ), 'utf8'))
  const saved = []
  let reads = 0
  const view = await mountComponent('views/gam/countManagement/modelConfig/index.vue', {
    mocks: componentMocks,
    api: {
      getModelConfig: async (request) => {
        reads++
        assert.equal(request.modelCode, route.query.modelCode)
        return { resData: { configJson: JSON.stringify(config), isExportable: false } }
      },
      saveModelConfig: async (request) => { saved.push(request) }
    }
  })
  try {
    const save = view.all((n) => n.type === 'el-button' && text(n).includes('action.save'))[0]
    assert.equal(save.props.disabled, false)
    save.props.onClick()
    await view.settle()
    assert.equal(reads, 1, 'model display loads only its actual configuration')
    assert.equal(saved.length, 1)
    const result = JSON.parse(saved[0].configJson)
    assert.equal(result.chip_type, config.chip_type)
    assert.equal(result.model_type, config.model_type)
    assert.deepEqual(result.models[0].inputs, config.models[0].inputs)
    assert.deepEqual(result.models[0].outputs, config.models[0].outputs)
    assert.deepEqual(result.models[0].params, config.models[0].params)
    const exportButton = view.all((n) => n.type === 'el-button' && text(n).includes('action.export'))[0]
    assert.equal(exportButton.props.disabled, true, 'preset export policy remains enforced')
  } finally { view.unmount() }

  const graph = await mountComponent('views/gam/countManagement/modelConfig/configFLow.vue', {
    props: { flowData: { inputs: config.models[0].inputs, outputs: config.models[0].outputs } },
    mocks: {
      '@vue-flow/core': {
        VueFlow: { props: ['nodes', 'edges'], setup: (props) => () => Vue.h('graph-fixture', { nodes: props.nodes, edges: props.edges }) },
        useVueFlow: () => ({ onPaneReady() {} }),
        Handle: { render: () => null }
      },
      '@vue-flow/background': { Background: { render: () => null } },
      '@vue-flow/controls': { Controls: { render: () => null } },
      '@vue-flow/core/dist/style.css': {},
      '@vue-flow/controls/dist/style.css': {}
    }
  })
  try {
    const { nodes, edges } = graph.all((n) => n.type === 'graph-fixture')[0].props
    assert.equal(nodes.length, 2)
    assert.deepEqual(plain(nodes[0].data.list), config.models[0].inputs)
    assert.deepEqual(plain(nodes[1].data.list), config.models[0].outputs)
    assert.equal(edges[0].source, 'input-root')
    assert.equal(edges[0].target, 'output-root')
  } finally { graph.unmount() }
}

// A localized preset title must not leak into saved configuration or exports.
// The imported case intentionally reuses both the preset ID and its raw name.
const namedConfig = {
  algorithm_code: '1000001', chip_type: 'BM1688', model_type: 'yolov5_det', version: 'V1.0.0',
  labels: [{ id: '0', name: 'Face', threshold: [0.5, 0.5] }],
  models: [{ name: 'FaceRGBDetection', description: 'Original model description', params: {},
    inputs: [{ name: 'images', shape: [1, 3, 640, 640] }],
    outputs: [{ name: 'detections', shape: [1, 5, 8400] }] }]
}
const previousLocale = i18n.currentLocale.value
i18n.currentLocale.value = 'zh-CN'
try {
  for (const isExportable of [false, true]) {
    const saved = []
    const exported = []
    const downloads = []
    const view = await mountComponent('views/gam/countManagement/modelConfig/index.vue', {
      mocks: componentMocks,
      globals: {
        localStorage: { getItem: () => '' },
        XMLHttpRequest: class {
          open(method, url) { this.method = method; this.url = url }
          setRequestHeader() {}
          send(body) {
            exported.push({ method: this.method, url: this.url, body: JSON.parse(body) })
            this.status = 200
            this.response = new Uint8Array()
            this.onload()
          }
        },
        document: { createElement: () => ({ click() { downloads.push(this.download) } }) },
        window: { URL: { createObjectURL: () => 'blob:model-fixture', revokeObjectURL() {} } }
      },
      api: {
        getModelConfig: async () => ({ resData: { configJson: JSON.stringify(namedConfig), isExportable } }),
        saveModelConfig: async request => { saved.push(request) },
        exportModelConfig: () => '/fixture/model/export'
      }
    })
    try {
      const title = () => text(view.all(n => n.type === 'h1')[0]).trim()
      const subtitles = () => view.all(n => n.props.class === 'model-subtitle')
      assert.equal(title(), isExportable ? 'FaceRGBDetection' : '人脸检测')
      if (isExportable) {
        assert.equal(subtitles().length, 0, 'matching imported ID/name must not acquire a preset alias')
      } else {
        assert.equal(text(subtitles()[0]).trim(), 'FaceRGBDetection')
        i18n.currentLocale.value = 'en-US'
        await view.settle()
        assert.equal(title(), 'FaceRGBDetection')
        i18n.currentLocale.value = 'zh-CN'
        await view.settle()
        assert.equal(title(), '人脸检测')
      }
      const button = action => view.all(n => n.type === 'el-button' && text(n).includes(action))[0]
      button('action.save').props.onClick()
      await view.settle()
      assert.equal(saved.length, 1)
      assert.deepEqual(JSON.parse(saved[0].configJson), namedConfig, 'saving preserves raw name, description and model contract')
      assert.equal(button('action.export').props.disabled, !isExportable)
      button('action.export').props.onClick()
      await view.settle()
      if (isExportable) {
        assert.equal(saved.length, 2, 'export still saves the current original configuration first')
        assert.deepEqual(JSON.parse(saved[1].configJson), namedConfig)
        assert.deepEqual(exported, [{ method: 'post', url: '/fixture/model/export',
          body: { modelCode: '1000001', modelName: 'FaceRGBDetection' } }])
        assert.deepEqual(downloads, ['FaceRGBDetection_1000001.tar.gz'])
      } else {
        assert.equal(saved.length, 1)
        assert.equal(exported.length, 0, 'preset export remains blocked after localized rendering')
        assert.equal(downloads.length, 0)
      }
    } finally { view.unmount() }
  }
} finally { i18n.currentLocale.value = previousLocale }

for (const configJson of ['', '{broken', 'null', '[]', '{}', '{"models":[]}', '{"models":[null]}']) {
  let saves = 0
  const view = await mountComponent('views/gam/countManagement/modelConfig/index.vue', {
    mocks: componentMocks,
    api: {
      getModelConfig: async () => ({ resData: { configJson } }),
      saveModelConfig: async () => { saves++ }
    }
  })
  try {
    const alert = view.all((n) => n.type === 'el-alert')[0]
    assert.equal(alert?.props.title, 'common.modelConfigLoadFailed', 'invalid configuration is visible')
    for (const action of ['action.save', 'action.export']) {
      const button = view.all((n) => n.type === 'el-button' && text(n).includes(action))[0]
      assert.equal(button.props.disabled, true)
      button.props.onClick()
    }
    await view.settle()
    assert.equal(saves, 0, 'invalid configuration must not overwrite the model with an empty object')
  } finally { view.unmount() }
}

console.log('Model configuration behavior passed: localized titles, raw save/export, platform round-trip, graph inputs/outputs, preset export policy and invalid-data protection')
