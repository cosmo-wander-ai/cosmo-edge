import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
import API from './api'
import { message } from './utils/message'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import './styles/global.scss'
import { showImagePreview } from '@/utils/imagePreview'
import { i18n, t, tShort, setLocale, currentLocale } from '@/i18n'
import { loadResourceLocale } from '@/utils/resourceLocaleLoader'


const app = createApp(App)

// 全局配置 API 和 message
app.config.globalProperties.$API = API
app.config.globalProperties.$message = message
app.config.globalProperties.$imgView = showImagePreview

app.use(i18n)
app.use(ElementPlus)
app.use(router)

app.config.globalProperties.$t = t
app.config.globalProperties.$tShort = tShort
app.config.globalProperties.$setLocale = setLocale

// Load resource locale before mounting so that i18n.global.te() can resolve
// resource.* keys from the very first render.  loadResourceLocale never throws,
// so this will not block mount even if the fetch fails.
;(async () => {
  await loadResourceLocale(currentLocale.value)
  app.mount('#app')
})()
