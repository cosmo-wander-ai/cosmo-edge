import assert from 'node:assert/strict'
import lodash from 'lodash'
import { h, getCurrentInstance, onErrorCaptured } from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { inputSizeOptions, isInputSizeValid } from '../src/utils/inferenceInputSize.js'

const empty = { default: { render: () => null } }
const multi = {
  atomicCode: 'multi', atomicName: 'Multi', label: '[]',
  inputSizes: [{ width: 320, height: 320 }, { width: 640, height: 640 }, { width: 640, height: 384 }],
  defaultInputSize: { width: 640, height: 640 }
}
const legacy = { atomicCode: 'legacy', atomicName: 'Legacy', label: '[]' }
assert.equal(inputSizeOptions(multi)[2].value, '640x384')
assert.equal(inputSizeOptions(multi).filter(option => option.isDefault).length, 1)
assert.equal(isInputSizeValid(multi, '384x640'), false)

const mocks = {
  './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty,
  'tree-transfer-vue3': empty, lodash: { default: lodash }, uuid: { v4: () => 'sizes-fixture' },
  '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
  '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
}
for (const [saved, expected] of [['', '640x640'], ['640x384', '640x384'], ['512x512', '512x512']]) {
  const errors = []
  let form
  const panel = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: {
      actionDetail: { actionId: 'AA_00001', inputParamConfig: JSON.stringify([{ key: 'atomicCode', type: 'modelSelect_detector', name: 'Model', level: '1' }]) },
      configObject: { params: [{ key: 'atomicCode', value: 'multi' }, ...(saved ? [{ key: 'inputSize', value: saved }] : [])], webConfig: {} }
    }, mocks,
    api: { atomicModelList: async () => ({ resData: { list: [multi, legacy] } }) },
    wrap: Component => ({ setup(_, { attrs }) {
      onErrorCaptured(error => { errors.push(error); return false })
      getCurrentInstance().appContext.app.component('el-table', { render: () => null })
      return () => h(Component, { ...attrs, ref: value => { form = value } })
    } })
  })
  try {
    for (let i = 0; i < 6; i++) await panel.settle()
    if (errors.length) throw errors[0]
    const sizeSelect = () => panel.all(node => node.type === 'el-select' && node.props.id === 'inference-size-sizes-fixture')[0]
    assert.equal(sizeSelect().props.modelValue, expected)
    assert.equal(form.submitForm().params.find(param => param.key === 'inputSize').value, expected)
    assert.equal(panel.all(node => node.props?.role === 'alert').length > 0, saved === '512x512')
    sizeSelect().props.activate('320x320')
    await panel.settle()
    assert.equal(form.submitForm().params.find(param => param.key === 'inputSize').value, '320x320')
    const modelSelect = panel.all(node => node.type === 'el-select' && node.props.modelValue === 'multi')[0]
    modelSelect.props.activate('legacy')
    await panel.settle()
    assert.equal(sizeSelect(), undefined)
    assert.equal(form.submitForm().params.some(param => param.key === 'inputSize'), false)
  } finally { panel.unmount() }
}
