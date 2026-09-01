import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

import { filterDeviceInfoForDisplay } from '../src/utils/deviceInfo.js'

const deviceInfo = [
  { key: 'deviceType', name: '设备型号', value: 'TM-AB160-16T06' },
  { key: 'acceleratorBackend', name: '推理后端', value: 'SOPHON' },
  { key: 'rkllmAvailable', name: 'RKLLM能力', value: 'false' },
  { key: 'softwareVersion', name: '软件版本', value: 'V1.1.0.0' }
]

assert.deepEqual(
  filterDeviceInfoForDisplay(deviceInfo),
  [deviceInfo[0], deviceInfo[1], deviceInfo[3]]
)
assert.equal(
  filterDeviceInfoForDisplay([
    { key: 'rkllmAvailable', name: 'RKLLM能力', value: 'true' }
  ]).length,
  0
)
assert.deepEqual(filterDeviceInfoForDisplay(undefined), [])
assert.equal(deviceInfo.length, 4)

const deviceInfoComponentPath = fileURLToPath(new URL(
  '../src/views/box/systemManagement/systemConfig/components/DeviceInfo.vue',
  import.meta.url
))
const deviceInfoComponentSource = readFileSync(deviceInfoComponentPath, 'utf8')
assert.match(
  deviceInfoComponentSource,
  /deviceInfo\.value\s*=\s*filterDeviceInfoForDisplay\(resData\?\.devInfoList\)/
)

console.log('Device info visibility checks passed')
