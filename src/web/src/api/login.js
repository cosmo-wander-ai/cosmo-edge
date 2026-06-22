import { request } from '@/utils/request'
export default {
  // 查询协议状态
  policyAuthQuery: data => {
    return request({
      url: '/gtw/base/api/user/login/clause/v1/query',
      method: 'post',
      data
    })
  },
  // 保存协议状态
  saveAuthStatus: data => {
    return request({
      url: '/gtw/base/api/user/login/clause/v1/edit',
      method: 'post',
      data
    })
  },
  // 退出登录
  loginLogout: () => {
    return request({
      url: '/gtw/adm/login/logout',
      method: 'get'
    })
  },
  // 发送邮件验证码
  sendVerifyCode: async data => {
    return await request({
      url: '/gtw/adm/api/pwd/v1/sendVerifyCode',
      method: 'post',
      data
    })
  },
  // // 根据验证码找回密码
  resetPwdByVerifyCode: data => {
    return request({
      url: '/gtw/adm/api/pwd/v1/resetPwdByVerifyCode',
      method: 'post',
      data
    })
  },
  // 查询滑块图片
  init: async data => {
    return request({
      url: '/gtw/adm/slideImage/init',
      method: 'post',
      data
    })
  },
  // 验证滑块
  check: async data => {
    return request({
      url: '/gtw/adm/slideImage/check',
      method: 'post',
      data
    })
  },
  // 选择的监控平台
  getSmType: data => {
    return request({
      url: '/gtw/base/uvs/platform/choose',
      method: 'post',
      data
    })
  },
  //根据mtk获取信息
  queryUserInfoByMtk: data => {
    return request({
      url: '/gtw/adm/api/user/v1/queryUserInfoByMtk',
      method: 'post',
      data
    })
  },
  // 运行模式参数获取
  queryRunModeParam: data => {
    return request({
      url: '/gtw/cwai/System/QueryRunModeParam',
      method: 'post',
      data
    })
  }
}
