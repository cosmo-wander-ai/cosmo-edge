import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

for (const [type, key, apiName, listKey] of [
  ['workClothesSet', 'param.workClothesSet', 'boxQueryPersonLibInfo', 'personLibList'],
  ['faceSet', 'param.faceSet', 'boxQueryFaceLibInfo', 'faceLibList']
]) {
  for (const mode of ['simple', 'detail']) {
    let libraries = [{ id: 12, name: 'Available library' }, { id: '34', name: 'Second library' }]
    const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/ParameterSetting.vue', {
      props: { algorithmMetadata: { params: [{ key, type, name: type, defaultValue: '99', level: '2' }] } },
      api: { [apiName]: async () => ({ resData: { [listKey]: libraries } }) }
    })
    try {
      if (mode === 'detail') {
        form.all(n => n.type === 'el-radio-group')[0].props.activate('detail')
        await form.settle()
        form.all(n => n.type === 'span' && String(n.props.class).includes('down-btn'))[0].props.onClick()
        await form.settle()
      }
      const selector = () => form.all(n => n.type === 'el-select' && n.props.multiple !== undefined)[0]
      const option = form.all(n => n.type === 'el-option' && n.props.label === 'Available library')[0]
      assert.ok(option, `${type}/${mode}: parameter configuration must offer libraries returned by the platform`)
      const select = selector()
      assert.ok(select && !select.props.disabled, `${type}: library selector must be usable`)
      assert.equal(form.instance.saveParamConfig()[0].defaultValue, '99', 'loading must preserve an existing binding absent from the list')
      select.props.activate(['12', '34'])
      await form.settle()
      const saved = form.instance.saveParamConfig()
      assert.equal(saved[0].defaultValue, '12,34', 'selected IDs must use the workflow string format')
      assert.equal(saved[0].type, type)
      assert.equal(saved[0].key, key)
      form.instance.$.props.algorithmMetadata = { params: saved }
      await form.settle()
      if (mode === 'detail') {
        form.all(n => n.type === 'span' && String(n.props.class).includes('down-btn'))[0].props.onClick()
        await form.settle()
      }
      assert.deepEqual(Array.from(selector().props.modelValue), ['12', '34'], 'saved selections must reload')
      libraries = [...libraries, { id: '56', name: 'New library' }]
      selector().props.onVisibleChange(true)
      await form.settle()
      assert.ok(form.all(n => n.type === 'el-option' && n.props.label === 'New library').length, 'opening must refresh available libraries')
      selector().props.activate([])
      await form.settle()
      assert.equal(form.instance.saveParamConfig()[0].defaultValue, '', 'clearing must persist without selecting a default library')
    } finally { form.unmount() }
  }
}

let resolveWorkwear
const switching = await mountComponent('views/gam/countManagement/arrangeDetail/flow/LibrarySelect.vue', {
  props: { type: 'workClothesSet', modelValue: '77' },
  api: {
    boxQueryPersonLibInfo: () => new Promise(resolve => { resolveWorkwear = resolve }),
    boxQueryFaceLibInfo: async () => ({ resData: { faceLibList: [{ id: '77', name: 'Face library' }] } })
  }
})
try {
  switching.instance.$.props.type = 'faceSet'
  await switching.settle()
  resolveWorkwear({ resData: { personLibList: [{ id: '12', name: 'Stale workwear library' }] } })
  await switching.settle()
  assert.deepEqual(switching.all(n => n.type === 'el-option').map(n => n.props.label), ['Face library'], 'late responses cannot replace another library type')
} finally { switching.unmount() }

const failing = await mountComponent('views/gam/countManagement/arrangeDetail/flow/LibrarySelect.vue', {
  props: { type: 'faceSet', modelValue: '77' },
  api: { boxQueryFaceLibInfo: async () => { throw new Error('Library service unavailable') } }
})
try {
  const select = failing.all(n => n.type === 'el-select')[0]
  assert.deepEqual(Array.from(select.props.modelValue), ['77'], 'request errors must not clear bindings')
  assert.equal(select.props.loading, false)
} finally { failing.unmount() }
console.log('Picture scene library selection checks passed')
