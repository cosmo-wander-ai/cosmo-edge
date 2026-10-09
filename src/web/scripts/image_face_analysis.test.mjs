import test from 'node:test'
import assert from 'node:assert/strict'
import { supportsFaceComparison, faceTaskConfig, imageTargets } from '../src/utils/imageFaceAnalysis.mjs'

test('face capability follows metadata or face recognizer, not translated names', () => {
  assert.equal(supportsFaceComparison({ algorithmMetadata: JSON.stringify({ params: [{ type: 'faceSet' }] }) }), true)
  assert.equal(supportsFaceComparison({ algorithmName: '人脸识别' }), false)
  const node = { actionId: 'PA_00005', configObject: { params: [{ key: 'featureInput', value: '0' }] } }
  assert.equal(supportsFaceComparison({ algorithmProcessdata: [node] }), true)
  node.configObject.params[0].value = '1'
  assert.equal(supportsFaceComparison({ algorithmProcessdata: [node] }), false)
  assert.equal(supportsFaceComparison({ algorithmMetadata: '{broken' }), false)
})

test('face request requires selected libraries and a valid threshold; extraction clears comparison', () => {
  assert.throws(() => faceTaskConfig(true, [], 80))
  for (const score of [-1, 0, 101, NaN]) assert.throws(() => faceTaskConfig(true, ['a'], score))
  const values = Object.fromEntries(faceTaskConfig(true, ['a', 'a', 'b'], 82.5).params.map(p => [p.key, p.value]))
  assert.deepEqual(values, { 'param.faceSet': 'a,b', 'param.limitScore': '82.5', 'param.faceCompare': '1' })
  assert.equal(faceTaskConfig(false, ['old'], 80).params.find(p => p.key === 'param.faceSet').value, '')
})

test('the same face in overlapping regions is shown once and top-level results take precedence', () => {
  const face = { box: { x: 5, y: 5, width: 100, height: 100 }, matchInfo: { matched: false } }
  const result = { areaList: [{ targetList: [face] }, { targetList: [face] }] }
  assert.equal(imageTargets(result).length, 1)
  result.targetList = [face]
  assert.equal(imageTargets(result).length, 1)
  assert.deepEqual(imageTargets(null), [])
})
