import assert from 'node:assert/strict'
import { test } from 'node:test'
import { readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { chooseProfile, deviceAddress, deviceName, requestConfig, probeDevice, saveDevice, runDeviceBatch } from '../src/views/gam/taskManager/components/onvifAccess.js'

const profiles = [
  { token: 'sub', name: 'main', codec: 'H264', width: 1280, height: 720, fps: 30 },
  { token: 'main', name: 'sub', codec: 'H264', width: 1920, height: 1080, fps: 25 }
]
const row = () => ({ endpoint: '192.0.2.10', channelName: 'Camera', profileToken: '', saved: false })
const credentials = { username: 'test', password: 'fixture-only', separateRtspCredentials: false }

test('chooses highest resolution rather than name; preserves existing Profile', () => {
  assert.equal(chooseProfile(profiles).token, 'main')
  assert.equal(chooseProfile(profiles, 'sub').token, 'sub')
  assert.equal(chooseProfile([...profiles, { ...profiles[1], token: 'hevc', codec: 'H265' }]).token, 'main')
  assert.throws(() => chooseProfile([]), /no_profiles/)
  assert.throws(() => chooseProfile([{ token: 'bad', codec: 'H264', width: 0, height: 0 }]), /unsupported_stream/)
  assert.equal(chooseProfile([...profiles, { ...profiles[1], token: 'too-large', width: 10000, height: 10000 }]).token, 'main')
  assert.throws(() => chooseProfile([{ ...profiles[0], width: 640, height: 360 }]), /unsupported_stream/)
})
test('automatic resolution bounds match the existing media pipeline', async () => {
  // CMake stages the web sources separately and supplies the original repository root.
  const repoRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
  const source = await readFile(resolve(repoRoot, 'src/util/VideoInfo.h'), 'utf8')
  for (const [name, value] of Object.entries({ kVideoMinWidth: 1280, kVideoMinHeight: 720, kVideo4KWidth: 7680, kVideo4KHeight: 4320 }))
    assert.match(source, new RegExp(`${name}\\s*=\\s*${value}\\s*;`))
  assert.match(source, /width \* height >= kVideoMinWidth \* kVideoMinHeight/)
  assert.match(source, /width \* height <= kVideoMaxWidth \* kVideoMaxHeight/)
})
test('generates names from discovery with safe address fallback', () => {
  assert.equal(deviceName({ endpoint: 'http://192.0.2.10/onvif/device_service', scopes: 'onvif://www.onvif.org/name/Front%20door' }), 'Front door')
  assert.equal(deviceName({ endpoint: '192.0.2.10', scopes: 'onvif://www.onvif.org/name/%xx' }), 'ONVIF 192.0.2.10')
  assert.equal(deviceAddress('http://192.0.2.10:8080/onvif/device_service'), '192.0.2.10:8080')
})
test('editing omits blank passwords while retaining separate credentials', () => {
  const config = requestConfig({ ...row(), source: 'onvif://test' }, { username: 'test', password: '', rtspPassword: '', rtspUsername: 'rtsp', separateRtspCredentials: true })
  assert.equal('password' in config, false)
  assert.equal('rtspPassword' in config, false)
  assert.equal(config.rtspUsername, 'rtsp')
  assert.equal(config.separateRtspCredentials, true)
})
test('one add action queries automatically before save and never repeats a saved row', async () => {
  const calls = []; const item = row()
  const api = async (operation, data) => { calls.push({ operation, data }); return { profiles } }
  await saveDevice(api, item, credentials)
  await saveDevice(api, item, credentials)
  assert.deepEqual(calls.map(c => c.operation), ['OnvifProbe', 'OnvifSave'])
  assert.equal(calls[1].data.profileToken, 'main')
  assert.equal(item.saved, true)
})
test('failed probe shows row error and does not save; corrected retry succeeds', async () => {
  const item = row(); let fail = true; let saves = 0
  const api = async operation => {
    if (operation === 'OnvifSave') { saves++; return {} }
    if (fail) throw new Error('unauthorized')
    return { profiles }
  }
  await runDeviceBatch([item], item => saveDevice(api, item, credentials))
  assert.equal(item.error, 'unauthorized'); assert.equal(saves, 0); assert.equal(item.pending, false)
  fail = false
  await runDeviceBatch([item], item => saveDevice(api, item, credentials))
  assert.equal(item.error, ''); assert.equal(saves, 1)
})
test('tries alternate advertised address only for connection errors, not authentication errors', async () => {
  const item = { ...row(), endpoints: ['192.0.2.11'] }; let calls = 0
  await probeDevice(async () => { if (++calls === 1) throw new Error('network_error'); return { profiles } }, item, credentials)
  assert.equal(calls, 2); assert.equal(item.endpoint, '192.0.2.11')
  calls = 0
  await assert.rejects(probeDevice(async () => { calls++; throw new Error('unauthorized') }, { ...row(), endpoints: ['192.0.2.11'] }, credentials), /unauthorized/)
  assert.equal(calls, 1)
})
test('cancel during query prevents subsequent save', async () => {
  let active = true; const calls = []; const item = row()
  await saveDevice(async operation => { calls.push(operation); active = false; return { profiles } }, item, credentials, () => active)
  assert.deepEqual(calls, ['OnvifProbe']); assert.equal(item.saved, false)
})
test('cancel during dispatched save retains success and prevents duplicate retry', async () => {
  let active = true; const item = row()
  await saveDevice(async operation => { if (operation === 'OnvifSave') active = false; return { profiles } }, item, credentials, () => active)
  assert.equal(item.saved, true)
})
test('batch isolates failures and bounds concurrent requests to two', async () => {
  const items = Array.from({ length: 6 }, (_, id) => ({ ...row(), id })); let count = 0; let peak = 0
  await runDeviceBatch(items, async item => {
    count++; peak = Math.max(peak, count)
    await new Promise(resolve => setTimeout(resolve, 2))
    count--
    if (item.id === 1) throw new Error('timeout')
    item.saved = true
  })
  assert.equal(peak, 2); assert.equal(items[1].error, 'timeout')
  assert.equal(items.filter(item => item.saved).length, 5)
})
test('batch cancellation stops queued work', async () => {
  let active = true; let started = 0
  await runDeviceBatch([row(), row(), row()], async () => { started++; active = false }, () => active)
  assert.equal(started, 1)
})
