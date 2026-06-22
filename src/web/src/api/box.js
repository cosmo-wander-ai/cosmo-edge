import { request } from '@/utils/request'

/**
 * @returns {Promise}
 */

const timeTemplate = {
  queryTimeTemplatePage(data) {
    return request({
      url: '/gtw/cwai/schedule/Page',
      method: 'post',
      data
    })
  },
  addTimeTemplate(data) {
    return request({
      url: '/gtw/cwai/schedule/Add',
      method: 'post',
      data
    })
  },
  updateTimeTemplate(data) {
    return request({
      url: '/gtw/cwai/schedule/Update',
      method: 'post',
      data
    })
  },
  deleteTimeTemplate(data) {
    return request({
      url: '/gtw/cwai/schedule/Delete',
      method: 'post',
      data
    })
  },
}

const system = {
  // 时间设置
  queryTimeConfig(data) {
    return request({
      url: '/gtw/cwai/System/QueryTime',
      method: 'post',
      data
    })
  },
  testNtpdate(data) {
    return request({
      url: '/gtw/cwai/System/NTPDate',
      method: 'post',
      data
    })
  },
  modifyTime(data) {
    return request({
      url: '/gtw/cwai/System/ModifyTime',
      method: 'post',
      data
    })
  },
  // 告警叠加参数查询
  queryPictureQuality(data) {
    return request({
      url: '/gtw/cwai/System/QueryPictureQuality',
      method: 'post',
      data
    })
  },
  // 告警叠加参数设置
  setPictureQuality(data) {
    return request({
      url: '/gtw/cwai/System/SetPictureQuality',
      method: 'post',
      data
    })
  },
  // 重置告警叠加参数
  resetPictureQuality(data) {
    return request({
      url: '/gtw/cwai/System/ResetPictureQuality',
      method: 'post',
      data
    })
  },
  // 告警视频参数查询
  queryAlarmVideoDuration(data) {
    return request({
      url: '/gtw/cwai/System/QueryAlarmVideoDuration',
      method: 'post',
      data
    })
  },
  // 告警视频参数设置
  setAlarmVideoDuration(data) {
    return request({
      url: '/gtw/cwai/System/SetAlarmVideoDuration',
      method: 'post',
      data
    })
  },
  // 重置告警视频参数
  resetAlarmVideoDuration(data) {
    return request({
      url: '/gtw/cwai/System/ResetAlarmVideoDuration',
      method: 'post',
      data
    })
  },
  // 任务运行详情
  queryRunningDetail(data) {
    return request({
      url: '/gtw/cwai/Task/runningDetail',
      method: 'post',
      data,
    })
  },
  // 日志下载
  boxExportFile(data) {
    return request({
      url: '/gtw/cwai/System/ExportFile',
      method: 'post',
      data,
    })
  },
  // 升级
  boxSystemUpgrade(data) {
    return request({
      url: '/gtw/cwai/System/Upgrade',
      method: 'post',
      data,
    })
  },
  // 检测主机状态
  boxCheckDeviceStatus(data) {
    return request({
      url: '/gtw/cwai/System/QueryDeviceStatus',
      method: 'post',
      data,
    })
  },
  // 重启，恢复出厂设置
  boxResetSystem(data) {
    return request({
      url: '/gtw/cwai/System/ResetSystem',
      method: 'post',
      data,
    })
  },
  // 获取logo
  boxGetLogo(data) {
    return request({
      url: '/gtw/cwai/System/QuerySystemLogo',
      method: 'post',
      data,
    })
  },
  // 设置logo
  boxSetLogo(data) {
    return request({
      url: '/gtw/cwai/System/SetSystemLogo',
      method: 'post',
      data,
    })
  },
  // 备定时重启参数查询
  boxQueryDevRestartParam(data) {
    return request({
      url: '/gtw/cwai/System/QueryDevRestartParam',
      method: 'post',
      data,
    })
  },
  // 设备定时重启参数设置
  boxModifyDevRestartParam(data) {
    return request({
      url: '/gtw/cwai/System/ModifyDevRestartParam',
      method: 'post',
      data,
    })
  },
  // 修改密码
  boxModifyPassword(data) {
    return request({
      url: '/gtw/cwai/login/ModifyPassword',
      method: 'post',
      data,
    })
  },
  // 下载设备信息 
  downloadDeviceInfo(data) {
    return request({
      url: '/gtw/cwai/System/DownloadDeviceInfo',
      method: 'post',
      data,
    })
  },
  // 授权文件上传
  lisenceUpload(data) {
    return request({
      url: '/gtw/cwai/System/LisenceUpload',
      method: 'post',
      data,
    })
  },
  // 授权状态查询
  queryAuthServiceStatus() {
    return request({
      url: '/gtw/cwai/System/QueryAuthServiceStatus',
      method: 'post',
    })
  },

  // 网络设置
  // 网卡查询
  queryNetCard() {
    return request({
      url: '/gtw/cwai/network/QueryNetCard',
      method: 'get'
    })
  },
  // 网卡设置
  modifyNetCard(data) {
    return request({
      url: '/gtw/cwai/network/ModifyNetCard',
      method: 'post',
      data,
    })
  },
  // DNS查询
  queryNetDns() {
    return request({
      url: '/gtw/cwai/network/QueryNetDns',
      method: 'get',
    })
  },
  // DNS设置
  modifyNetDns(data) {
    return request({
      url: '/gtw/cwai/network/ModifyNetDns',
      method: 'post',
      data,
    })
  },
  // 网络质量检测
  networkQualityCheck(data) {
    return request({
      url: '/gtw/cwai/network/NetworkQualityCheck',
      method: 'post',
      data,
    })
  },
  // IP可达检测
  ipAccessibleCheck(data) {
    return request({
      url: '/gtw/cwai/network/IpAccessibleCheck',
      method: 'post',
      data,
    })
  },
  // 查询http接口参数
  queryHttpInterfaceParam(data) {
    return request({
      url: '/gtw/cwai/System/QueryHttpInterfaceParam',
      method: 'post',
      data
    })
  },
  // 设置http接口参数
  setHttpInterfaceParam(data) {
    return request({
      url: '/gtw/cwai/System/SetHttpInterfaceParam',
      method: 'post',
      data,
    })
  },
  // 查询文档url
  queryDocumentUrl(data) {
    return request({
      url: '/gtw/cwai/System/QueryDocumentUrl',
      method: 'post',
      data,
    })
  },
  // 设置MQTT
  setMqttAdapterParam(data) {
    return request({
      url: '/gtw/cwai/System/SetMqttAdapterParam',
      method: 'post',
      data,
    })
  },
  // 查询MQTT
  queryMqttAdapterParam(data) {
    return request({
      url: '/gtw/cwai/System/QueryMqttAdapterParam',
      method: 'post',
      data
    })
  },
  // 获取接口文档地址
  queryDocumentUrl(data) {
    return request({
      url: '/gtw/cwai/System/QueryDocumentUrl',
      method: 'post',
      data,
    })
  },
  // 运行模式参数修改
  modifyRunModeParam(data) {
    return request({
      url: '/gtw/cwai/System/ModifyRunModeParam',
      method: 'post',
      data,
    })
  },
  // 联网模式参数获取
  queryIotNetworkParam(data) {
    return request({
      url: '/gtw/cwai/System/QueryIotNetworkParam',
      method: 'post',
      data
    })
  },
  // 联网模式参数设置
  modifyIotNetworkParam(data) {
    return request({
      url: '/gtw/cwai/System/ModifyIotNetworkParam',
      method: 'post',
      data,
    })
  }

}

const audio = {
  // 查询音频文件列表
  queryAudioFile(data) {
    return request({
      url: '/gtw/cwai/Audio/QueryAudioFile  ',
      method: 'post',
      data,
    })
  },
  deleteAudioFile(data) {
    return request({
      url: '/gtw/cwai/Audio/DeleteAudioFile',
      method: 'post',
      data,
    })
  },
}

const sound = {
  //  音柱导入
  modifyAudioDevice(data) {
    return request({
      url: '/gtw/cwai/Audio/ModifyAudioDevice',
      method: 'post',
      data,
    })
  },
  // 音柱查询
  queryAudioDevice(data) {
    return request({
      url: '/gtw/cwai/Audio/QueryAudioDevice',
      method: 'post',
      data,
    })
  },
  // 音柱删除
  deleteAudioDevice(data) {
    return request({
      url: '/gtw/cwai/Audio/DeleteAudioDevice',
      method: 'post',
      data,
    })
  },
  // 音柱测试
  testAudioDevice(data) {
    return request({
      url: '/gtw/cwai/Audio/TestAudioDevice',
      method: 'post',
      data,
    })
  }
}

const strategyManagement = {
  // 编排动作
  boxActionList(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Storages',
      method: 'post',
      data,
    })
  },
  //  添加策略
  boxStrategyAdd(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Add',
      method: 'post',
      data,
    })
  },
  // 更新策略
  boxStrategyUpdate(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Update',
      method: 'post',
      data,
    })
  },
  // 删除策略
  boxStrategyDelete(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Delete',
      method: 'post',
      data,
    })
  },
  //查询策略列表
  boxQueryStrategyList(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Page',
      method: 'post',
      data,
    })
  },
  // 策略开关
  boxStrategySwitch(data) {
    return request({
      url: '/gtw/cwai/AlarmStrage/Switch',
      method: 'post',
      data,
    })
  },
}

const event = {
  // 查询车辆属性字典
  getCarDict(data) {
    return request({
      url: '/gtw/cwai/System/Dict',
      method: 'post',
      data
    })
  }
}

export default {
  ...event,
  ...timeTemplate,
  ...system,
  ...audio,
  ...sound,
  ...strategyManagement,
  // 登录
  dologin(data) {
    return request({
      url: '/gtw/cwai/login/dologin',
      // url: '/gtw/adm/login/doLogin',
      method: 'post',
      data
    })
  },


  // 告警记录
  boxQueryEvent(data) {
    return request({
      url: '/gtw/cwai/event/page',
      method: 'post',
      data
    })
  },

  // 事件导出
  boxExportAlarm(data) {
    return request({
      url: '/gtw/cwai/event/ExportAlarm',
      method: 'post',
      data
    })
  },

  boxDeleteEvent(data) {
    return request({
      url: '/gtw/cwai/event/delete',
      method: 'post',
      data
    })
  },

  // 查询所有算法信息
  boxAllAlgorithmInfo(data) {
    return request({
      url: '/gtw/cwai/algorithm/page',
      method: 'post',
      data
    })
  },

  getChannelList(data) {
    return request({
      url: '/gtw/cwai/camera/page',
      method: 'post',
      data
    })
  },

  // 系统管理
  // 设备信息
  queryDeviceInfo() {
    return request({
      url: '/gtw/cwai/System/QueryDeviceInfo',
      method: 'get'
    })
  },
  // 资源消耗
  queryHardwareResource() {
    return request({
      url: '/gtw/cwai/System/QueryHardwareResource',
      method: 'get'
    })
  },

  // 流量算法列表查询
  queryPassFlowList(data) {
    return request({
      url: '/gtw/cwai/Algorithm/PassFlowList',
      method: 'post',
      data
    })
  },

  // 流量统计
  queryPassengerFlowNumber(data) {
    return request({
      url: '/gtw/cwai/event/QueryPassengerFlowNumber',
      method: 'post',
      data
    })
  },
}