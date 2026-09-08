import { request } from '@/utils/request'

export default {
  // 查询所有算法列表及已配置的算法列表
  selectAllAlgorithmInfo: data => {
    return request({
      url: '/gtw/cwai/task/selectAllAlgorithmInfo',
      method: 'post',
      data
    })
  },
  // 根据通道id和算法Id查询算法配置信息
  selectConfigByAlgorithmId: data => {
    return request({
      url: '/gtw/cwai/task/selectConfigByAlgorithmId',
      method: 'post',
      data
    })
  },
  // 重新获取图片
  recaptureImage: data => {
    return request({
      url: '/gtw/cwai/task/recaptureImage',
      method: 'post',
      data,
      timeout: 30000
    })
  },
  // 保存算法配置信息
  saveOrUpdate: data => {
    return request({
      url: '/gtw/cwai/task/saveOrUpdate',
      method: 'post',
      data,
      timeout: 30000
    })
  },
  // 开始任务
  startTask: data => {
    return request({
      url: '/gtw/cwai/task/startTask',
      method: 'post',
      data
    })
  },
  // 停止任务
  stopTask: data => {
    return request({
      url: '/gtw/cwai/task/stopTask',
      method: 'post',
      data
    })
  },
  // 删除任务
  deleteTask: data => {
    return request({
      url: '/gtw/cwai/task/deleteTask',
      method: 'post',
      data
    })
  },
  //查询基础算法
  selectAlgorithmInfo(data) {
    return request({
      url: '/gtw/cwai/task/selectAlgorithmInfo',
      method: 'post',
      data
    })
  },
  // 任务管理-查询启用指定算法的通道列表
  listChannel(data) {
    return request({
      url: '/gtw/cwai/task/listChannel',
      method: 'post',
      data
    })
  },
  // 任务管理-批量应用任务参数
  applyParamsBatch(data) {
    return request({
      url: '/gtw/cwai/task/applyParamsBatch',
      method: 'post',
      data
    })
  },
  // 任务管理-channelCode查找channelId
  channelCodeDetail(data) {
    return request({
      url: '/gtw/cwai/videoDevice/channel/detail',
      method: 'post',
      data
    })
  },
  // 轮询策略列表
  schedulePollingList: data => {
    return request({
      url: '/gtw/cwai/cust/schedule/polling/list',
      method: 'post',
      data
    })
  },
  //box
  //通道列表
  boxCameraPage: data => {
    return request({
      url: '/gtw/cwai/camera/page',
      method: 'post',
      data
    })
  },
  // 添加通道
  boxAddCamera: data => {
    return request({
      url: '/gtw/cwai/Camera/Add',
      method: 'post',
      data
    })
  },
  // 更新通道
  boxUpdateCamera: data => {
    return request({
      url: '/gtw/cwai/Camera/Update',
      method: 'post',
      data
    })
  },
  // 盒子获取照片
  boxRecaptureImage: data => {
    return request({
      url: '/gtw/cwai/Camera/GetPicture',
      method: 'post',
      data
    })
  },
  // 时间模板
  boxGetTimeTemplate: data => {
    return request({
      url: '/gtw/cwai/schedule/Page',
      method: 'post',
      data
    })
  },
  // 启停任务
  boxSwitchTask: data => {
    return request({
      url: '/gtw/cwai/Task/SwitchTask',
      method: 'post',
      data
    })
  },
  // 视频文件通道创建
  boxAddVideoChannel: data => {
    return request({
      url: '/gtw/cwai/Camera/AddVideo',
      method: 'post',
      data
    })
  },
  // 盒子算法包上传
  boxAlgorithmUpload: data => {
    return request({
      url: '/gtw/cwai/algorithm/Upload',
      method: 'post',
      data
    })
  },
  // 盒子删除服务
  boxDeleteTask: data => {
    return request({
      url: '/gtw/cwai/task/delete',
      method: 'post',
      data
    })
  },
  // box 机务库
  boxQueryPersonLibInfo: data => {
    return request({
      url: '/gtw/cwai/BodyLibrary/QueryPersonLibInfo',
      method: 'post',
      data
    })
  },
  // box 人脸库
  boxQueryFaceLibInfo: data => {
    return request({
      url: '/gtw/cwai/Library/QueryFaceLibInfo',
      method: 'post',
      data
    })
  },
  // box 物品库
  boxQueryThingsLibInfo: data => {
    return request({
      url: '/gtw/cwai/ThingsLibrary/QueryThingsLibInfo',
      method: 'post',
      data
    })
  },
  // 通道删除(批量)
  boxBatchDeleteCamera: data => {
    return request({
      url: '/gtw/cwai/Camera/BatchDelete',
      method: 'post',
      data
    })
  },
  // 任务删除(批量)
  boxBatchdeleteTask: data => {
    return request({
      url: '/gtw/cwai/Task/batchdelete',
      method: 'post',
      data
    })
  },
  // 务管理-启停任务(批量)
  boxBatchSwitchTask: data => {
    return request({
      url: '/gtw/cwai/Task/BatchSwitchTask',
      method: 'post',
      data
    })
  },
  // 删除原子模型
  deleteAtomicModel: data => {
    return request({
      url: '/gtw/cwai/atomic/model/delete',
      method: 'post',
      data
    })
  },
  // 更新原子模型
  updateAtomicModel: data => {
    return request({
      url: '/gtw/cwai/atomic/model/update',
      method: 'post',
      data
    })
  },
  // 查询USB摄像头列表
  queryUsbCameraList: data => {
    return request({
      url: '/gtw/cwai/Camera/QueryUsbCameraList',
      method: 'post',
      data
    })
  },
}
