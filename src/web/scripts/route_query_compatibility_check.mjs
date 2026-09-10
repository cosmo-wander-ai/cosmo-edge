import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

import {
  getLocationQueryParam,
  parseLocationQuery
} from '../src/utils/locationQuery.js'

const upgradeLocation = {
  search: '?upgrade=1787713813946',
  hash: '#/gam/taskManager/realEditingTask?resetUrl=%2FvideoAccess&channelId=RT0000000001&channelName=2&joinType=0'
}

assert.deepEqual(parseLocationQuery(upgradeLocation), {
  upgrade: '1787713813946',
  resetUrl: '/videoAccess',
  channelId: 'RT0000000001',
  channelName: '2',
  joinType: '0'
})
assert.equal(getLocationQueryParam('channelId', upgradeLocation), 'RT0000000001')

assert.equal(getLocationQueryParam('channelId', {
  search: '?channelId=outer-channel',
  hash: '#/gam/taskManager/realEditingTask?channelId=hash-channel'
}), 'hash-channel')

assert.equal(getLocationQueryParam('channelCode', {
  search: '?channelCode=legacy-code',
  hash: '#/gam/taskManager/realEditingTask'
}), 'legacy-code')

assert.equal(getLocationQueryParam('channelId', {
  search: '',
  hash: '#/gam/taskManager/realEditingTask?channelId=hash-only'
}), 'hash-only')

assert.equal(getLocationQueryParam('channelId', {
  search: '?upgrade=1',
  hash: '#/gam/taskManager/realEditingTask'
}), undefined)

const serviceConfigPath = fileURLToPath(new URL(
  '../src/views/gam/taskManager/editTask/serviceConfig.vue',
  import.meta.url
))
const serviceConfigSource = readFileSync(serviceConfigPath, 'utf8')
assert.match(serviceConfigSource, /getLocationQueryParam\(name\)/)
assert.doesNotMatch(serviceConfigSource, /window\.location\.search/)

console.log('Route query compatibility checks passed')
