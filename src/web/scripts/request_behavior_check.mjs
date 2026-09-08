import assert from 'node:assert/strict'
import axios from 'axios'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'

const messages = []
const storage = new Map()
const location = { hash: '#/home' }
const currentLocale = { value: 'zh-CN' }
let adapter
const { request } = await loadBehaviorModule('utils/request.js', {
  mocks: {
    axios: { default: { create: config => axios.create({ ...config, adapter: config => adapter(config) }) } },
    '@/utils/message': { message: { error: text => messages.push(text) } },
    '@/i18n': {
      currentLocale,
      t: key => `${currentLocale.value}:${key}`,
      translateApiMessage: (key, text) => key ? `${currentLocale.value}:${key}` : text
    }
  },
  globals: {
    localStorage: { getItem: key => storage.get(key) ?? null, removeItem: key => storage.delete(key) },
    window: { location }, FormData
  },
  env: { VITE_APP_BASE_URL: '/test-base/' }
})

const reset = () => {
  messages.length = 0
  storage.clear()
  storage.set('mtk', 'test-mtk')
  storage.set('token', 'test-token')
  storage.set('language', 'keep-me')
  location.hash = '#/home'
}
const respond = data => config => Promise.resolve({ data, status: 200, headers: {}, config })
const businessError = code => ({ resCode: 0, resMsg: [{ msgCode: code, msgText: 'failure' }] })
const rejected = async (promise, expected) => {
  await assert.rejects(promise, error => error === expected)
}
let scenarios = 0
for (const locale of ['zh-CN', 'en-US']) {
  currentLocale.value = locale
  for (const resData of [{ value: 0 }, []]) {
    reset()
    const data = { resCode: 1, resData }
    adapter = respond(data)
    assert.equal(await request({ url: '/normal', method: 'post' }), data)
    assert.equal(messages.length, 0)
    scenarios++
  }
  for (const kind of ['business', '401', 'transport-code']) {
    for (const options of [{}, { silentError: true }, { suppressAuthRedirect: true },
      { suppressAuthRedirect: true, silentError: true }, { suppressAuthRedirect: 'true' }]) {
      reset()
      const data = businessError('10005')
      const error = kind === 'business' ? data : { response: { status: kind === '401' ? 401 : 500, data } }
      adapter = kind === 'business' ? respond(data) : () => Promise.reject(error)
      await rejected(request({ url: '/normal', ...options }), error)
      const redirected = options.suppressAuthRedirect !== true
      assert.deepEqual(messages, redirected ? [`${locale}:api.loginExpired`] : [])
      assert.equal(location.hash, redirected ? '#/boxLogin' : '#/home')
      assert.equal(storage.has('mtk'), !redirected)
      assert.equal(storage.has('token'), !redirected)
      assert.equal(storage.get('language'), 'keep-me')
      scenarios++
    }
  }
  for (const status of [null, 400, 500]) {
    for (const silentError of [false, true]) {
      reset()
      const data = businessError('OTHER')
      const error = status === null ? data : { response: { status, data } }
      adapter = status === null ? respond(data) : () => Promise.reject(error)
      await rejected(request({ url: '/gtw/cwai/System/QueryHardwareResource', silentError,
        suppressAuthRedirect: true }), error)
      assert.deepEqual(messages, silentError ? [] : ['failure'])
      assert.equal(location.hash, '#/home')
      assert.equal(storage.get('mtk'), 'test-mtk')
      scenarios++
    }
  }
  for (const status of [401, 500]) {
    reset()
    const error = { response: { status, data: businessError('OTHER') } }
    adapter = () => Promise.reject(error)
    await rejected(request({ url: '/gtw/cwai/System/QueryDeviceStatus' }), error)
    assert.deepEqual(messages, status === 401 ? [`${locale}:api.loginExpired`] : [])
    assert.equal(location.hash, status === 401 ? '#/boxLogin' : '#/home')
    scenarios++
  }
  for (const error of [new Error('offline'), new axios.AxiosError('timeout', 'ECONNABORTED'),
    new axios.CanceledError('cancelled')]) {
    reset()
    adapter = () => Promise.reject(error)
    await rejected(request({ url: '/normal' }), error)
    assert.deepEqual(messages, [])
    assert.equal(location.hash, '#/home')
    scenarios++
  }
  for (const token of ['test-mtk', null]) {
    for (const [url, explicitTimeout, expectedTimeout] of [
      ['/normal', undefined, 20000], ['/normal', 120000, 120000],
      ...[
        '/gtw/cwai/System/Upgrade', '/gtw/cwai/System/CheckUpgradeSpace',
        '/gtw/cwai/File/ImportFile', '/gtw/cwai/Camera/AddVideo',
        '/gtw/cwai/atomic/model/uploadTemp', '/gtw/cwai/atomic/model/upload',
        '/gtw/cwai/atomic/model/add', '/gtw/cwai/atomic/model/importModel',
        '/gtw/cwai/algorithm/Upload', '/gtw/cwai/algorithm/version/add'
      ].map(url => [url, 123, 600000])
    ]) {
      reset()
      if (token === null) storage.delete('mtk')
      adapter = config => {
        assert.equal(config.baseURL, '/test-base/')
        assert.equal(config.timeout, expectedTimeout)
        assert.equal(config.headers.mtk, token)
        assert.equal(config.headers.token, token)
        assert.equal(config.headers.fileMode, '1')
        assert.equal(config.headers.lang, locale.replace('-', '_'))
        assert.equal(config.headers['Accept-Language'], locale)
        return respond({ resCode: 1 })(config)
      }
      await request({ url, ...(explicitTimeout === undefined ? {} : { timeout: explicitTimeout }) })
      scenarios++
    }
  }
}

reset()
const pending = new Map()
adapter = config => new Promise((resolve, reject) => pending.set(config.url, { resolve, reject, config }))
const failure = new Error('one request failed')
const first = request({ url: '/first' })
const second = rejected(request({ url: '/second' }), failure)
const third = request({ url: '/third' })
await new Promise(resolve => setImmediate(resolve))
pending.get('/third').resolve({ data: { resCode: 1, resData: 3 }, status: 200, headers: {} })
pending.get('/second').reject(failure)
pending.get('/first').resolve({ data: { resCode: 1, resData: 1 }, status: 200, headers: {} })
assert.equal((await third).resData, 3)
assert.equal((await first).resData, 1)
await second
assert.deepEqual(messages, [])
console.log(`Request behavior checks passed (${scenarios + 1} scenarios; real Axios with isolated transport)`)
