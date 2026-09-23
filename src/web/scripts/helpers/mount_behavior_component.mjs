import * as Vue from 'vue'
import { loadBehaviorModule } from './load_behavior_module.mjs'

// Vue's native input directive checks these browser types on updates. The
// renderer has no focused document or shadow root.
globalThis.Document ??= class Document {}
globalThis.ShadowRoot ??= class ShadowRoot {}

export const i18n = {
  t: (key) => key,
  tShort: (key) => key,
  translateApiMessage: (key, text) => key || text,
  localeColon: ':',
  localeOptions: [],
  setLocale() {},
  currentLocale: Vue.ref('en-US'),
  i18n: { global: { te: () => false, t: (key) => key, locale: Vue.ref('en-US') } }
}
const node = (type, text = '') => ({
  type, text, props: {}, children: [], parent: null, style: {},
  addEventListener() {}, removeEventListener() {},
  getRootNode: () => ({ activeElement: null }),
  setAttribute(key, value) { this.props[key] = value },
  removeAttribute(key) { delete this.props[key] },
  getContext: () => new Proxy({}, { get: (target, key) => target[key] ?? (() => {}), set: (target, key, value) => { target[key] = value; return true } })
})
export async function mountComponent(entry, { props = {}, mocks = {}, globals = {}, api = {}, router = {}, route = { query: {} }, message = {}, components = {} } = {}) {
  const { default: component } = await loadBehaviorModule(entry, {
    mocks: { vue: Vue, '@/i18n': i18n, 'element-plus': { ElMessage: () => {} },
      '@element-plus/icons-vue': Object.fromEntries(['Plus', 'QuestionFilled', 'CircleCheckFilled', 'Search', 'Upload', 'ArrowDown', 'Delete', 'SwitchButton', 'Menu', 'House', 'View', 'Document', 'VideoCamera', 'Connection', 'Cpu', 'Picture', 'Headset', 'Iphone', 'Link', 'Setting', 'DataBoard', 'Monitor', 'Box'].map(name => [name, name])), ...mocks },
    globals: { setTimeout, clearTimeout, setInterval, clearInterval, URL, URLSearchParams, ...globals }
  })
  const teleportTargets = new Map()
  const renderer = Vue.createRenderer({
    querySelector(selector) {
      if (!teleportTargets.has(selector)) teleportTargets.set(selector, node('teleport-target'))
      return teleportTargets.get(selector)
    },
    createElement: node, createText: (text) => node('#text', text), createComment: (text) => node('#comment', text),
    setText: (n, text) => { n.text = text }, setElementText: (n, text) => { n.text = text; n.children = [] },
    parentNode: (n) => n.parent, nextSibling: (n) => n.parent?.children[n.parent.children.indexOf(n) + 1],
    patchProp: (n, key, previous, value) => { n.props[key] = value },
    insert(n, parent, anchor) { if (n.parent) n.parent.children.splice(n.parent.children.indexOf(n), 1); n.parent = parent; const i = parent.children.indexOf(anchor); parent.children.splice(i < 0 ? parent.children.length : i, 0, n) },
    remove(n) { n.parent?.children.splice(n.parent.children.indexOf(n), 1); n.parent = null }
  })
  const root = node('root')
  const app = renderer.createApp(component, props)
  app.config.globalProperties.$API = api
  app.config.globalProperties.$route = route
  app.config.globalProperties.$router = router
  app.config.globalProperties.$message = { error() {}, success() {}, warning() {}, ...message }
  app.config.warnHandler = (message) => { if (!message.startsWith('Failed to resolve component') && !message.startsWith('Failed to resolve directive')) console.warn(message) }
  app.config.errorHandler = (error) => { throw error }
  app.directive('loading', {})
  app.component('router-view', { render: () => null })
  app.component('el-tree', { methods: { setCurrentKey() {}, filter() {} }, render() { return Vue.h('el-tree', this.$attrs) } })
  for (const name of ['card', 'dialog', 'form']) {
    app.component(`el-${name}`, {
      methods: { validate: (callback) => callback(true), clearValidate() {} },
      render() { return Vue.h(`el-${name}`, this.$attrs, ['header', 'default', 'footer'].flatMap(name => this.$slots[name]?.() || [])) }
    })
  }
  // Controls expose the same model/event contract as Element Plus; business
  // conversion and parent event handlers always run in the compiled SFC.
  for (const name of ['checkbox', 'input', 'switch', 'select', 'radio-group', 'slider']) {
    app.component(`el-${name}`, {
      inheritAttrs: false,
      props: ['modelValue', 'trueValue', 'falseValue', 'disabled'],
      emits: ['update:modelValue', 'change'],
      setup(p, { attrs, emit, slots }) {
        return () => Vue.h(`el-${name}`, { ...attrs, ...p, activate(value) {
          const next = name === 'checkbox' ? (value ? p.trueValue ?? true : p.falseValue ?? false) : value
          emit('update:modelValue', next); emit('change', next)
        } }, slots.default?.())
      }
    })
  }
  for (const [name, component] of Object.entries(components)) app.component(name, component)
  const instance = app.mount(root)
  const all = (predicate, n = root) => [...(predicate(n) ? [n] : []), ...n.children.flatMap((child) => all(predicate, child))]
  const settle = async () => { await Promise.resolve(); await Vue.nextTick(); await Promise.resolve(); await Vue.nextTick() }
  await settle()
  return { root, instance, all, settle, unmount: () => app.unmount() }
}
