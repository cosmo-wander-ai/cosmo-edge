import assert from 'node:assert/strict'

import { getLocationParam } from '../src/utils/routeQuery.js'

const hashOnlyLocation = {
  search: '',
  hash: '#/gam/taskManager/realEditingTask?channelId=GB0000000597&channelName=Camera'
}
assert.equal(getLocationParam(hashOnlyLocation, 'channelId'), 'GB0000000597')

const upgradeRedirectLocation = {
  search: '?upgrade=1788153357211',
  hash: '#/gam/taskManager/realEditingTask?channelId=GB0000000597&channelName=Camera'
}
assert.equal(
  getLocationParam(upgradeRedirectLocation, 'channelId'),
  'GB0000000597',
  'hash-route parameters must remain available when the top-level URL has an upgrade marker'
)

assert.equal(getLocationParam(upgradeRedirectLocation, 'upgrade'), '1788153357211')

console.log('Route query compatibility checks passed')
