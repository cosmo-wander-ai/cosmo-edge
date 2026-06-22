import { request } from '@/utils/request'

function getIp() {
  return window.location.origin
}

export default {
  //  获取相机列表
  boxQueryCameraList(data) {
    return request({
      url: '/gtw/cwai/camera/page',
      method: 'post',
      data
    })
  },
  //  请求预览任务
  boxRequestLiveStream(data) {
    return request({
      url: '/gtw/cwai/LiveStream/RequestLiveStream',
      method: 'post',
      data
    })
  },
  // 视频心跳
  boxStreamKeepAlive(data) {
    return request({
      url: '/gtw/cwai/LiveStream/StreamKeepAlive',
      method: 'post',
      data
    })
  },
  // 预览任务停止
  boxStreamStop(data) {
    return request({
      url: '/gtw/cwai/LiveStream/StreamStop',
      method: 'post',
      data
    })
  },
  // 大屏展示弹窗参数获取
  queryPopUpParam(data) {
    return request({
      url: '/gtw/cwai/System/QueryPopUpParam',
      method: 'post',
      data
    })
  },
  // 大屏展示弹窗参数设置
  setPopUpParam(data) {
    return request({
      url: '/gtw/cwai/System/SetPopUpParam',
      method: 'post',
      data
    })
  },

  bigScreenWs() {
    let baseUrl = import.meta.env.MODE === 'development'
      ? import.meta.env.VITE_APP_API_URL
      : getIp()

    if (!baseUrl) {
      console.warn('bigScreenWs: baseUrl is empty')
      return ''
    }

    // 统一处理协议替换: http -> ws, https -> wss
    const wsUrl = baseUrl.replace(/^http/, 'ws')
    return `${wsUrl}/wsInterface/requestEventResult`
  }
}


