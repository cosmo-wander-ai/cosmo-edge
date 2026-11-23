import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const empty = { default: { render: () => null } }
for (const fail of [false, true]) {
  const values = new Map([['mtk', 'synthetic-session'], ['token', 'synthetic-session'], ['account', 'test'], ['runMode', '1']])
  const storage = { getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, value), removeItem: key => values.delete(key) }
  const navigations = [], warnings = []
  let release, reject, requests = 0
  const pending = new Promise((resolve, refuse) => { release = resolve; reject = refuse })
  const layout = await mountComponent('views/main/index.vue', {
    mocks: {
      'vue-router': { useRouter: () => ({ replace: target => navigations.push(target) }), useRoute: () => ({ path: '/home' }) },
      'js-md5': { default: value => value }, '@/components/OnboardingGuide.vue': empty,
      '@/components/eventBus.js': { default: { $on() {}, $off() {}, $emit() {} } }
    },
    globals: {
      localStorage: storage,
      window: { localStorage: storage, location: { search: '', hash: '' } },
      document: { addEventListener() {}, removeEventListener() {} }
    },
    api: { boxGetLogo: async () => ({ resData: {} }), logout: () => { requests++; return pending } },
    message: { warning: value => warnings.push(value) }
  })
  try {
    layout.all(n => n.props.class === 'user-info')[0].props.onClick({ stopPropagation() {} })
    await layout.settle()
    const logout = layout.all(n => n.props.class === 'dropdown-item danger')[0].props.onClick
    const operation = logout()
    logout()
    assert.equal(requests, 1)
    assert.equal(values.get('mtk'), 'synthetic-session', 'credential remains available to the pending logout request')
    assert.equal(navigations.length, 0)
    if (fail) reject(new Error('synthetic network error'))
    else release({ resCode: 1 })
    await operation
    await layout.settle()
    assert.equal(values.has('mtk'), false)
    assert.equal(values.has('token'), false)
    assert.equal(values.has('account'), false)
    assert.equal(values.has('runMode'), false)
    assert.equal(navigations.at(-1), '/boxLogin')
    assert.equal(warnings.length, fail ? 1 : 0)
    if (fail) assert.equal(warnings[0], 'system.logoutUnconfirmed')
  } finally { layout.unmount() }
}
console.log('logout UI success/failure/double click: PASS')
