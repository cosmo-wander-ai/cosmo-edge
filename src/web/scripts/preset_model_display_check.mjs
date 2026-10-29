import assert from 'node:assert/strict'
import test from 'node:test'
import { PRESET_MODEL_DISPLAY_CATALOG, resolvePresetModelDisplay } from '../src/utils/presetModelDisplay.js'

const expectedCodes = [
  '1000001', '1000005', '1000010', '1000012', '1000016',
  '1001001', '1001003', '1001005', '1001007', '1001008', '1001010',
  '2000001', '2000011', '2000020', '2001002', '2001003', '2001008',
  '2001036', '2002008', '2002011', '2180145', '2248766', '3000003',
  '3612265', '6047042', '7486163', '7792861', '9183416', '9233591', '9909281'
]

test('catalog covers exactly the reviewed preset identifiers with concise titles', () => {
  assert.deepEqual(Object.keys(PRESET_MODEL_DISPLAY_CATALOG).sort(), expectedCodes.sort())
  assert.equal(new Set(expectedCodes).size, 30)
  assert.ok(Object.isFrozen(PRESET_MODEL_DISPLAY_CATALOG))
  for (const [code, entry] of Object.entries(PRESET_MODEL_DISPLAY_CATALOG)) {
    assert.ok(Object.isFrozen(entry))
    assert.ok(entry.originalName && entry.description && entry.descriptionEn)
    assert.ok(entry.title.length >= 4 && entry.title.length <= 6, `${code}: ${entry.title}`)
    const display = resolvePresetModelDisplay({ modelCode: code, modelName: entry.originalName, isExportable: false })
    assert.equal(display.title, entry.title)
    assert.equal(display.subtitle, entry.originalName)
    assert.ok(display.searchText.includes(code))
    assert.ok(display.searchText.includes(entry.originalName))
    assert.ok(display.searchText.includes(entry.title))
  }
})

test('an imported model with the same identifier and original name stays unchanged', () => {
  const preset = { modelCode: '7486163', modelName: 'helmet', isExportable: false }
  const imported = { ...preset, isExportable: true }
  assert.equal(resolvePresetModelDisplay(preset).title, '安全帽分类')
  assert.deepEqual(resolvePresetModelDisplay(imported), {
    title: 'helmet', subtitle: '', description: '', searchText: 'helmet 7486163'
  })
})

test('source must be explicitly preset, with no inference from name or identifier', () => {
  for (const isExportable of [undefined, null, 0, 'false', '', true]) {
    assert.equal(resolvePresetModelDisplay({ modelCode: '1000001', modelName: 'FaceRGBDetection', isExportable }).title, 'FaceRGBDetection')
  }
  assert.equal(resolvePresetModelDisplay({ modelCode: 'unknown', modelName: 'FaceRGBDetection', isExportable: false }).title, 'FaceRGBDetection')
  assert.equal(resolvePresetModelDisplay({ modelCode: '__proto__', modelName: 'FaceRGBDetection', isExportable: false }).title, 'FaceRGBDetection')
})

test('renamed models and names belonging to another preset never receive an alias', () => {
  for (const modelName of ['客户的安全帽模型', 'Helmet', 'helmet-v2', 'FaceRGBDetection', '']) {
    const model = { modelCode: '7486163', modelName, isExportable: false }
    const display = resolvePresetModelDisplay(model)
    assert.equal(display.title, modelName)
    assert.equal(display.subtitle, '')
    assert.equal(display.description, '')
  }
})

test('atomic DTOs, numeric identifiers, and surrounding whitespace preserve original text', () => {
  const display = resolvePresetModelDisplay({ atomicCode: 6047042, atomicName: '  YOLOV8n  ', isExportable: false })
  assert.equal(display.title, '人员检测')
  assert.equal(display.subtitle, '  YOLOV8n  ')
  assert.ok(display.searchText.includes('6047042'))
})

test('English and unknown locales keep technical names without Chinese display copy', () => {
  const model = { modelCode: '2002011', modelName: 'PhoneSmoking', isExportable: false }
  for (const locale of ['en-US', 'fr-FR', null]) {
    const display = resolvePresetModelDisplay(model, locale)
    assert.equal(display.title, 'PhoneSmoking')
    assert.equal(display.subtitle, '')
    assert.equal(display.description, PRESET_MODEL_DISPLAY_CATALOG['2002011'].descriptionEn)
    assert.ok(!/[\u3400-\u9fff]/u.test(display.description))
    assert.ok(display.searchText.includes('吸烟与手机'))
  }
})

test('resolution never mutates input metadata and safely handles incomplete models', () => {
  const model = Object.freeze({ atomicCode: '1000005', atomicName: 'FaceRGBRecognition', isExportable: false, labels: Object.freeze([]) })
  const before = JSON.stringify(model)
  const display = resolvePresetModelDisplay(model)
  display.title = 'Temporary view label'
  assert.equal(JSON.stringify(model), before)
  assert.equal(resolvePresetModelDisplay(model).title, '人脸特征')
  for (const empty of [null, undefined, {}]) {
    assert.deepEqual(resolvePresetModelDisplay(empty), { title: '', subtitle: '', description: '', searchText: '' })
  }
})

test('feature extraction and image classification do not promise complete event recognition', () => {
  assert.equal(PRESET_MODEL_DISPLAY_CATALOG['1000005'].title, '人脸特征')
  assert.equal(PRESET_MODEL_DISPLAY_CATALOG['1001007'].title, '人员特征')
  assert.equal(PRESET_MODEL_DISPLAY_CATALOG['1001010'].title, '趴卧分类')
  assert.equal(PRESET_MODEL_DISPLAY_CATALOG['2002008'].title, '倒地分类')
  assert.equal(PRESET_MODEL_DISPLAY_CATALOG['2000020'].title, '两轮车分类')
  for (const code of ['1001010', '2002008', '2000020']) {
    assert.doesNotMatch(PRESET_MODEL_DISPLAY_CATALOG[code].description, /睡岗|跌倒事件|进电梯/u)
  }
})
