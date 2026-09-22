import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const changes = []
const privacy = await mountComponent('views/gam/taskManager/editTask/alarmPrivacySetting.vue', {
  props: {
    modelValue: { enabled: '0', labels: '*', strength: '2' },
    availableLabels: ['Pedestrian', 'vehicle'],
    'onUpdate:modelValue': (value) => changes.push(value)
  }
})
try {
  assert.equal(changes.length, 0, 'loading settings must not change them')
  assert.equal(privacy.all((node) => node.type === 'el-radio-group').length, 0)
  privacy.all((node) => node.type === 'el-switch')[0].props.activate('1')
  assert.deepEqual(JSON.parse(JSON.stringify(changes.at(-1))), { enabled: '1', labels: '*', strength: '2' })
  privacy.instance.$.props.modelValue = changes.at(-1)
  await privacy.settle()
  privacy.all((node) => node.type === 'el-radio-group')[0].props.activate('selected')
  assert.equal(changes.at(-1).labels, 'Pedestrian')
  privacy.instance.$.props.modelValue = changes.at(-1)
  await privacy.settle()
  privacy.all((node) => node.type === 'el-select')[0].props.activate(['Pedestrian', 'vehicle'])
  assert.equal(changes.at(-1).labels, 'Pedestrian,vehicle')
  privacy.instance.$.props.modelValue = changes.at(-1)
  await privacy.settle()
  privacy.all((node) => node.type === 'el-radio-group')[1].props.activate('3')
  assert.equal(changes.at(-1).strength, '3')

  // Switching service/resetting is a parent replacement, not a user edit.
  const changeCount = changes.length
  privacy.instance.$.props.modelValue = { enabled: '0', labels: '*', strength: '2' }
  privacy.instance.$.props.availableLabels = []
  await privacy.settle()
  assert.equal(changes.length, changeCount)
  assert.equal(privacy.all((node) => node.type === 'el-switch')[0].props.modelValue, '0')
  assert.equal(privacy.all((node) => node.type === 'el-select').length, 0)

  privacy.instance.$.props.modelValue = { enabled: '1', labels: 'retired-class', strength: '2' }
  await privacy.settle()
  assert.equal(privacy.all((node) => node.type === 'el-radio' && node.props.value === 'selected')[0].props.disabled, true)
  assert.equal(privacy.all((node) => node.type === 'p' && node.props.class === 'privacy-error').length, 1)
  assert.equal(changes.length, changeCount, 'unavailable classes are reported without silently changing saved settings')
  privacy.all((node) => node.type === 'el-radio-group')[0].props.activate('all')
  assert.equal(changes.at(-1).labels, '*', 'users can recover by explicitly choosing all targets')
} finally {
  privacy.unmount()
}

console.log('Alarm image privacy component behavior checks passed')
