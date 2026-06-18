import { request } from '@/utils/request'

export default {
  // 算法管理分页列表
  algorithmInquire: data => {
    return request({
      url: '/gtw/cwai/algorithm/page',
      method: 'post',
      data
    })
  },
  getTaskList: data => {
    return request({
      url: '/gtw/cwai/task/page',
      method: 'post',
      data
    })
  },
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
  // 开始任务
  enableTask: data => {
    return request({
      url: '/gtw/cwai/task/enableTask',
      method: 'post',
      data
    })
  },
  // 停止任务
  disableTask: data => {
    return request({
      url: '/gtw/cwai/task/disableTask',
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
  //批量启动
  batchStart: data => {
    return request({
      url: '/gtw/cwai/task/batchStart',
      method: 'post',
      data
    })
  },
  //批量停止
  batchStop: data => {
    return request({
      url: '/gtw/cwai/task/batchStop',
      method: 'post',
      data
    })
  },
  //批量删除
  batchDelete: data => {
    return request({
      url: '/gtw/cwai/task/batchDelete',
      method: 'post',
      data
    })
  },
  // 异常数据列表
  abnormalDataAPI(data) {
    return request({
      url: '/gtw/cwai/exception/list',
      method: 'post',
      data,
      timeout: 60000
    })
  },
  // 错误枚举信息
  exceptionTypeInfoAPI() {
    return request({
      url: '/gtw/cwai/exception/exceptionTypeInfo',
      method: 'post'
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
  // 人数清零 
  personCountReset(data) {
    return request({
      url: '/gtw/cwai/event/personCountReset',
      method: 'post',
      data
    })
  },
  // 设备诊断-诊断历史分页列表查询 
  historyPage(data) {
    return request({
      url: '/gtw/cwai/device/diagnose/history/page',
      method: 'post',
      data
    })
  },
  // 设备诊断-诊断历史记录下载
  recordDownload() {
    return '/gtw/cwai/device/diagnose/history/record/download'
  },
  // 查询参数配置列表 
  queryListByPage(data) {
    return request({
      url: '/gtw/cwai/task/polling/config',
      method: 'post',
      data
    })
  },
  // 任务管理-切换模式 
  taskSwitchMode(data) {
    return request({
      url: '/gtw/cwai/task/switchMode',
      method: 'post',
      data
    })
  },
  // 任务管理-一键启用轮询 
  startAll(data) {
    return request({
      url: '/gtw/cwai/task/polling/startAll',
      method: 'post',
      data
    })
  },
  // 任务管理-一键停止轮询 
  stopAll(data) {
    return request({
      url: '/gtw/cwai/task/polling/stopAll',
      method: 'post',
      data
    })
  },
  // 任务管理-查询轮询服务详情 
  pageList(data) {
    return request({
      url: '/gtw/cwai/task/polling/pageList',
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
  // 任务管理-查看生命周期配置
  getInstanceInfo(data) {
    return request({
      url: '/gtw/cwai/api/alg/sysConfig/getInstanceInfo',
      method: 'post',
      data
    })
  },
  // 任务管理-保存生命周期配置
  sysConfigModify(data) {
    return request({
      url: '/gtw/cwai/api/alg/sysConfig/modify',
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
  // 获取通道监控列表 
  getMonitorChannelList: data => {
    return request({
      url: '/gtw/cwai/monitor/channel/list',
      method: 'post',
      data
    })
  },
  //获取通道监控详情
  getMonitorChannelDetail: data => {
    return request({
      url: '/gtw/cwai/monitor/channel/detail',
      method: 'post',
      data
    })
  },
  // 获取任务监控列表 
  getMonitorTaskList: data => {
    return request({
      url: '/gtw/cwai/monitor/task/list',
      method: 'post',
      data
    })
  },
  //获取任务监控详情
  getMonitorTaskDetail: data => {
    return request({
      url: '/gtw/cwai/monitor/task/detail',
      method: 'post',
      data
    })
  },
  // 离线视频编辑原始通道编码
  updateChannelCode: data => {
    return request({
      url: '/gtw/cwai/device/video/update',
      method: 'post',
      data
    })
  },
  // 获取设备位置树
  DeviceLocationTree: data => {
    return request({
      url: '/gtw/cwai/region/get',
      method: 'post',
      data
    })
  },
  // 指定分析机列表页
  allHostInfo: data => {
    return request({
      url: '/gtw/cwai/aiHostMonitor/allHostInfo',
      method: 'post',
      data
    })
  },
  // 指定分析机保存
  startTaskBySelect: data => {
    return request({
      url: '/gtw/cwai/task/startTaskBySelect',
      method: 'post',
      data
    })
  },
  // 导出任务列表
  ExportTaskExcel: () => {
    return '/gtw/cwai/task/exportExcel'
  },
  // 轮询策略分页列表
  schedulePollingPageList: data => {
    return request({
      url: '/gtw/cwai/cust/schedule/polling/pageList',
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
  // 添加轮询策略
  addSchedulePollin: data => {
    return request({
      url: '/gtw/cwai/cust/schedule/polling/add',
      method: 'post',
      data
    })
  },
  // 修改轮询策略
  updateSchedulePollin: data => {
    return request({
      url: '/gtw/cwai/cust/schedule/polling/update',
      method: 'post',
      data
    })
  },
  // 删除轮询策略
  deleteSchedulePollin: data => {
    return request({
      url: '/gtw/cwai/cust/schedule/polling/delete',
      method: 'post',
      data
    })
  },
  // 查询算法列表结果按照算法类型分组
  getAlgorithmListByCategory: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/list/groupByCategory',
      method: 'post',
      data
    })
  },
  algGroupByCategory: data => {
    return request({
      url: '/gtw/cwai/cust/license/algGroupByCategory',
      method: 'post',
      data
    })
  },
  // 任务运行详情
  taskRunningDetail: data => {
    return request({
      url: '/gtw/cwai/task/runningDetail',
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
  // 删除通道
  boxDeleteCamera: data => {
    return request({
      url: '/gtw/cwai/Camera/Delete',
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
  // 离线视频单个添加
  addSingleVideo: data => {
    return request({
      url: '/gtw/cwai/device/addSingleVideo',
      method: 'post',
      data
    })
  },
  //离线设备单个修改
  updateSingleVideo: data => {
    return request({
      url: '/gtw/cwai/device/updateSingleVideo',
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
