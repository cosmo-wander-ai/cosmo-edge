import assert from 'node:assert/strict'
import {
  createAlarmPrivacyDefaults,
  getAlarmPrivacyLabels,
  isAlarmPrivacyConfigValid,
  mergeAlarmPrivacyParams,
  readAlarmPrivacyParams
} from '../src/utils/alarmImagePrivacy.js'

assert.deepEqual(readAlarmPrivacyParams([]), { enabled: '0', labels: '*', strength: '2' },
  'existing services remain disabled until explicitly configured')

const original = [
  { key: 'param.alarmInterval', value: '30' },
  { key: 'platform.extension', value: 'retain-me' },
  { key: 'param.privacyEnabled', value: '0' },
  { key: 'param.privacyLabels', value: '*' }
]
const selected = { enabled: '1', labels: 'Pedestrian,vehicle', strength: '3' }
const merged = mergeAlarmPrivacyParams(original, selected)
assert.deepEqual(merged.slice(0, 2), original.slice(0, 2), 'unrelated platform parameters survive merging')
assert.equal(merged.filter((item) => item.key === 'param.privacyEnabled').length, 1)
assert.equal(original[2].value, '0', 'serialization must not mutate the source configuration')
assert.deepEqual(readAlarmPrivacyParams(merged), selected, 'saved selection and strength round-trip')
assert.deepEqual(readAlarmPrivacyParams(mergeAlarmPrivacyParams(merged, createAlarmPrivacyDefaults())),
  createAlarmPrivacyDefaults(), 'reset serializes explicit disabled defaults')

const availableLabels = getAlarmPrivacyLabels({ params: [
  { key: 'aiParam.Pedestrian.detPostion' },
  { key: 'aiParam.vehicle.detPostion' },
  { key: 'aiParam.Pedestrian.detPostion' },
  { key: 'aiParam.helmet.confidence' },
  { key: 'aiParam.unknown.confidenceConfig' }
] })
assert.deepEqual(availableLabels, ['Pedestrian', 'vehicle'],
  'classification confidences must not advertise detection classes')
assert.equal(isAlarmPrivacyConfigValid(selected, availableLabels), true)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, labels: 'helmet' }, availableLabels), false)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, labels: '' }, availableLabels), false)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, labels: 'Pedestrian,' }, availableLabels), false)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, labels: '*' }, []), true,
  'all detections remains available without class metadata')
assert.equal(isAlarmPrivacyConfigValid({ ...selected, strength: '4' }, availableLabels), false)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, enabled: '2' }, availableLabels), false)
assert.equal(isAlarmPrivacyConfigValid({ ...selected, enabled: '0', labels: 'retired-class' }, []), true,
  'users can disable a service whose class metadata is no longer available')
for (const enabled of ['0', '1']) {
  for (const labels of ['', ',', ',Pedestrian', 'Pedestrian,', 'Pedestrian,,vehicle',
    'Pedestrian vehicle', ' Pedestrian', 'Pedestrian ', 'Pedestrian\t', 'Pedestrian\n',
    'Pedestrian\u0000', 'Pedestrian\u007f', '*,Pedestrian', 'Pedestrian,*', 'Ped*', '**']) {
    assert.equal(isAlarmPrivacyConfigValid({ ...selected, enabled, labels }, availableLabels), false,
      `invalid label syntax must be rejected even when disabled: ${JSON.stringify({ enabled, labels })}`)
  }
}

console.log('Alarm image privacy parameter checks passed')
