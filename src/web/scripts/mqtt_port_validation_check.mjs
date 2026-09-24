import assert from 'node:assert/strict'
import test from 'node:test'

const validationModule = await import('../src/utils/networkPortValidation.js').catch(() => ({}))
const { isValidMqttPort } = validationModule

test('MQTT 端口校验拒绝超出 TCP 端口范围的值', () => {
  assert.equal(typeof isValidMqttPort, 'function')
  assert.equal(isValidMqttPort('0'), false)
  assert.equal(isValidMqttPort('65536'), false)
  assert.equal(isValidMqttPort('-1'), false)
})

test('MQTT 端口校验接受范围内的十进制整数', () => {
  assert.equal(typeof isValidMqttPort, 'function')
  assert.equal(isValidMqttPort('1'), true)
  assert.equal(isValidMqttPort('1883'), true)
  assert.equal(isValidMqttPort('65535'), true)
})
