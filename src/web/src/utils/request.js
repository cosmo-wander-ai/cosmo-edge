import axios from 'axios' // 引入 axios
import { message } from '@/utils/message'
import { currentLocale, t, translateApiMessage } from '@/i18n'
import { formatActionableApiError, normalizeApiError } from '@/utils/apiError'

const longTimeoutApi = [
  '/gtw/cwai/System/Upgrade',
  '/gtw/cwai/System/CheckUpgradeSpace',
  '/gtw/cwai/File/ImportFile',
  '/gtw/cwai/Camera/AddVideo',
  '/gtw/cwai/atomic/model/uploadTemp',
  '/gtw/cwai/atomic/model/upload',
  '/gtw/cwai/atomic/model/add',
  '/gtw/cwai/atomic/model/importModel',
  '/gtw/cwai/algorithm/Upload',
  '/gtw/cwai/algorithm/version/add'
]

const service = axios.create({
  baseURL: import.meta.env.VITE_APP_BASE_URL || '/',
  timeout: 1000 * 20 // request timeout
})

/**
 * request拦截器，附加请求参数
 */
service.interceptors.request.use(config => {
  config.headers.mtk = localStorage.getItem('mtk')   // gam token
  config.headers.token = localStorage.getItem('mtk')
  config.headers.fileMode = 1 //告知后端返回相对路径
  config.headers.lang = currentLocale.value.replace('-', '_')
  config.headers['Accept-Language'] = currentLocale.value
  if (longTimeoutApi.includes(config.url)) {
    config.timeout = 1000 * 60 * 10  // 
  }
  return config
}, error => {
  return Promise.reject(error)
})

/**
 * response拦截器，处理响应
 */

const clearLoginInfo = () => {
  localStorage.removeItem('mtk')
  localStorage.removeItem('token')
  // 使用 hash 路由跳转
  window.location.hash = '#/boxLogin'
}

export const request = params => {
  return new Promise((resolve, reject) => {
    const suppressAuthRedirect = params.suppressAuthRedirect === true
    service(params)
      .then(res => {
        const data = res?.data || {}
        const resCode = data?.resCode
        const apiError = formatActionableApiError(data, translateApiMessage, t)
        const msgCode = apiError.code
        if (resCode === 1) {
          resolve(data)
        } else {
          if (msgCode === '10005' && !suppressAuthRedirect) {
            message.error(t('api.loginExpired'))
            clearLoginInfo()
          } else if (msgCode !== '10005' && !params.silentError) {
            message.error(apiError.displayMessage)
          }
          reject(data)
        }
      })
      .catch(err => {
        const normalized = normalizeApiError(err)
        const status = normalized.status
        const msgCode = normalized.code
        if (status === 401 || msgCode === '10005') {
          if (!suppressAuthRedirect) {
            message.error(t('api.loginExpired'))
            clearLoginInfo()
          }
          return reject(err)
        }
        if (err?.response && status >= 400) {
          if (params.url === '/gtw/cwai/System/QueryDeviceStatus') return reject(err)
          if (!params.silentError) {
            const actionable = formatActionableApiError(err, translateApiMessage, t)
            message.error(actionable.displayMessage || t('api.networkError'))
          }
          return reject(err)
        }
        return reject(err)
      })
  })
}
