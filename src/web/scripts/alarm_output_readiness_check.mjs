import assert from 'node:assert/strict'
import { withAlarmOutputChoices } from '../src/utils/alarmOutputReadiness.js'

const action = { id: 'LA_AlarmOutput_Code', inputParamConfig: JSON.stringify([
  { key: 'outputChannel', type: 'text', defaultValue: '1' },
  { key: 'duration', type: 'text', defaultValue: '5' }
]) }
assert.equal(withAlarmOutputChoices([action], null)[0].inputParamConfig, action.inputParamConfig)
const form = JSON.parse(withAlarmOutputChoices([action], [
  { channel: 3, status: 'ready' }, { channel: 4, status: 'gpioUnavailable' }
])[0].inputParamConfig)
assert.equal(form[0].type, 'select')
assert.deepEqual(form[0].options, [{ label: '3', value: '3' }])
assert.equal(form[0].defaultValue, '3')
assert.equal(form[1].defaultValue, '5')
assert.deepEqual(JSON.parse(withAlarmOutputChoices([action], [])[0].inputParamConfig)[0].options, [])
assert.equal(JSON.parse(action.inputParamConfig)[0].type, 'text')
console.log('Alarm output channel choice checks passed')
