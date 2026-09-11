import * as Vue from 'vue'
import { loadBehaviorModule } from './load_behavior_module.mjs'

export const i18n = {
  t: (key) => key,
  localeColon: ':',
  currentLocale: Vue.ref('en-US'),
  i18n: { global: { te: () => false, t: (key) => key, locale: Vue.ref('en-US') } }
}
const node = (type, text = '') => ({ type, text, props: {}, children: [], parent: null })
export async function mountComponent(entry, { props = {}, mocks = {}, globals = {}, api = {} } = {}) {
  const { default: component } = await loadBehaviorModule(entry, {
    mocks: { vue: Vue, '@/i18n': i18n, 'element-plus': { ElMessage: () => {} },
      '@element-plus/icons-vue': { Plus: 'plus', QuestionFilled: 'question', CircleCheckFilled: 'check' }, ...mocks },
    globals: { setTimeout, clearTimeout, setInterval, clearInterval, URLSearchParams, ...globals }
  })
  const renderer = Vue.createRenderer({
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
  app.config.globalProperties.$route = { query: {} }
  app.config.globalProperties.$message = { error() {}, success() {} }
  app.config.warnHandler = (message) => { if (!message.startsWith('Failed to resolve component') && !message.startsWith('Failed to resolve directive')) console.warn(message) }
  app.config.errorHandler = (error) => { throw error }
  app.directive('loading', {})
  app.component('el-tree', { methods: { setCurrentKey() {} }, render: () => null })
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
  const instance = app.mount(root)
  const all = (predicate, n = root) => [...(predicate(n) ? [n] : []), ...n.children.flatMap((child) => all(predicate, child))]
  const settle = async () => { await Promise.resolve(); await Vue.nextTick(); await Promise.resolve(); await Vue.nextTick() }
  await settle()
  return { root, instance, all, settle, unmount: () => app.unmount() }
}
