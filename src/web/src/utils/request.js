import axios from 'axios' // 引入 axios
import { message } from '@/utils/message'
import actions from '@/micro/state.js'
import { currentLocale, t, translateApiMessage } from '@/i18n'

const longTimeoutApi = [
  '/gtw/cwai/System/Upgrade',
  '/gtw/cwai/System/QueryDeviceStatus',
  '/gtw/cwai/File/ImportFile',
  '/gtw/cwai/Camera/AddVideo',
  '/gtw/cwai/atomic/model/uploadTemp',
  '/gtw/cwai/atomic/model/add',
  '/gtw/cwai/atomic/model/importModel',
  '/gtw/cwai/algorithm/version/add'
]

const silentApi = [
  '/gtw/cwai/System/Upgrade',
  '/gtw/cwai/System/QueryHardwareResource',
  '/gtw/cwai/network/IpAccessibleCheck',
  '/gtw/cwai/System/QueryDeviceStatus',
  '/gtw/cwai/LiveStream/RequestLiveStream',
  '/gtw/cwai/LiveStream/StreamKeepAlive',
  '/gtw/cwai/LiveStream/StreamStop',
  '/gtw/cwai/File/QueryImportStatus',
  '/gtw/cwai/File/ImportFile'
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
  actions.setGlobalState({ loading: false, loginState: true })
  localStorage.removeItem('mtk')
  localStorage.removeItem('token')
  // 使用 hash 路由跳转
  window.location.hash = '#/boxLogin'
}

export const request = params => {
  return new Promise((resolve, reject) => {
    if (!silentApi.includes(params.url)) actions.setGlobalState({ loading: true }) // 开启过渡效果    
    service(params)
      .then(res => {
        const data = res?.data || {}
        const resCode = data?.resCode
        const firstMsg = Array.isArray(data?.resMsg) ? data.resMsg[0] : {}
        const msgCode = firstMsg?.msgCode
        const messageKey = firstMsg?.messageKey || firstMsg?.msgKey || data?.messageKey || data?.msgKey
        const msgText = translateApiMessage(messageKey, firstMsg?.msgText || data?.message || data?.msg)
        if (resCode === 1) {
          resolve(data)
        } else {
          if (msgCode === '10005') {
            message.error(t('api.loginExpired'))
            clearLoginInfo()
          } else {
            message.error(msgText)
          }
          reject(data)
        }
      })
      .catch(err => {
        if (err?.status === 502 || err?.status === 500 || err?.status === 404 || err?.status === 400) {
          if (params.url === '/gtw/cwai/System/QueryDeviceStatus') return reject(err)
          message.error(t('api.networkError'))
          return reject(err)
        } else if (err?.response?.data?.resMsg?.[0]?.msgCode == '10005') {
          message.error(t('api.loginExpired'))
          clearLoginInfo()
        }
        return reject(err)
      })
      .finally(() => {
        actions.setGlobalState({ loading: false }) // 始终关闭过渡效果
      })
  })
}
