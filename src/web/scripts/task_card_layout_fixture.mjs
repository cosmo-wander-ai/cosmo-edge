// Build only in the repository-supported Docker environment. The generated
// fixture lives below ignored node_modules, outside the production dist.
import { mkdir, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { build } from 'vite'
import vue from '@vitejs/plugin-vue'
const web = fileURLToPath(new URL('../', import.meta.url))
const root = path.join(web, 'node_modules/.cache/f01-layout')
await mkdir(root, { recursive: true })
await writeFile(path.join(root, 'fixture.js'), `
import { createApp, h, ref, nextTick } from 'vue'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import '@/styles/global.scss'
import Cards from '@/views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue'
import Parameters from '@/views/gam/countManagement/arrangeDetail/flow/ParameterSetting.vue'
import { i18n, setLocale, elementLocale } from '@/i18n'
localStorage.setItem('platformType', '15')
const query = new URLSearchParams(location.search)
setLocale(query.get('locale') || 'zh-CN')
const count = Number(query.get('count') || 0)
const rows = Array.from({length: count}, (_, i) => ({algorithmId: 'fixture-' + i, algorithmName: (query.get('locale') === 'en-US' ? 'Long scenario title ' : '长场景名称') .repeat(12), remark: 'Long description 长说明 '.repeat(24), algorithmCategory: '2', algorithmUsage: '1', supplier: 'fixture', models: []}))
const parameter = ref()
const mode = query.get('mode')
const app = createApp({ render() { return mode === 'parameter' ? h('div', [h(Parameters, {ref: parameter, algorithmMetadata: {params: [{key:'threshold', name: 'Threshold', type:'text', value:'1',level:'2'}]}}), h('button', {id: 'save', onClick() { document.querySelector('#saved').textContent = JSON.stringify(parameter.value.saveParamConfig()) }}, 'Save'), h('output', {id:'saved'})]) : h(Cards) } })
app.use(ElementPlus, {locale: elementLocale.value}).use(i18n)
app.config.globalProperties.$API = {algorithmInquire: async () => ({resData: {rows, total: count}}), boxCameraPage: async () => ({resData: {rows: []}})}
app.config.globalProperties.$route = {path: '/gam/algorithmicManagement'}
app.config.globalProperties.$router = {push() { document.body.dataset.clicked = 'true' }}
app.mount('#app')
await nextTick(); await document.fonts.ready; await new Promise(requestAnimationFrame); await new Promise(requestAnimationFrame)
document.body.dataset.ready = 'true'
`)
await writeFile(path.join(root, 'index.html'), '<html><head><meta charset="UTF-8"><style>html,body,#app{height:100%;margin:0}#app{box-sizing:border-box;padding:16px;overflow:hidden}</style></head><body><div id="app"></div><script type="module" src="/fixture.js"></script></body></html>')
await build({ configFile: false, root, base: '/', plugins: [vue()], resolve: { alias: { '@': path.join(web, 'src') } }, build: { target: 'esnext', outDir: path.join(root, 'dist'), emptyOutDir: true }, logLevel: 'warn' })
console.log('Layout fixture built in node_modules/.cache/f01-layout/dist')
