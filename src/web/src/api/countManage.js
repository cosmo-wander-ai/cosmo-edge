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
  // 查询算法版本列表
  algorithmVersion: data => {
    return request({
      url: '/gtw/cwai/algorithm/version/list',
      method: 'post',
      data
    })
  },
  // 查询算法版本详情列表
  algorithmInfo: data => {
    return request({
      url: '/gtw/cwai/algorithm/version/info',
      method: 'post',
      data
    })
  },
  // 添加算法版本
  algorithmAdd: data => {
    return request({
      url: '/gtw/cwai/algorithm/version/add',
      method: 'post',
      data,
    })
  },
  // 删除算法信息
  sureDeleteAlgorithm: data => {
    return request({
      url: '/gtw/cwai/algorithm/sureDeleteAlgorithm',
      method: 'post',
      data
    })
  },
  // 删除算法版本
  algorithmDelete: data => {
    return request({
      url: '/gtw/cwai/algorithm/version/delete',
      method: 'post',
      data
    })
  },
  // 切换（启用/禁用）算法版本
  algorithmSwitch: data => {
    return request({
      url: '/gtw/cwai/algorithm/version/switch',
      method: 'post',
      data
    })
  },

  // /algorithm
  // 算法授权列表
  activateList: data => {
    return request({
      url: '/gtw/cwai/algorithm/license/activateList',
      method: 'post',
      data
    })
  },
  // 客户端算法授权列表
  custLicense: data => {
    return request({
      url: '/gtw/cwai/cust/license/page',
      method: 'post',
      data
    })
  },
  // 离线授权
  offlineUpload: data => {
    return request({
      url: '/gtw/cwai/algorithm/license/offlineUpload',
      method: 'post',
      data
    })
  },
  // 在线授权
  onlineActivate: data => {
    return request({
      url: '/gtw/cwai/algorithm/license/onlineActivate',
      method: 'post',
      data
    })
  },
  // 服务管理-qps服务趋势查询
  qpsTrend: (data) => {
    return request({
      url: '/gtw/cwai/qps/overview',
      method: 'post',
      data
    })
  },
  // 查看算力资源
  modeCusterAlgInfo: data => {
    return request({
      url: '/gtw/cwai/algorithm/license/get/modeCusterAlgInfo',
      method: 'post',
      data
    })
  },
  // 保存自定义参数
  algorithmUpdate: data => {
    return request({
      url: '/gtw/cwai/algorithm/update',
      method: 'post',
      data
    })
  },
  // 硬件信息下载
  downloadDeviceInfo: () => {
    return '/gtw/cwai/algorithm/license/downloadDeviceInfo'
  },
  // 服务分配获取服务方
  getSupplier: () => {
    return request({
      url: '/gtw/cwai/dic/getDictByType/ALG_SUPPLIER',
      method: 'get'
    })
  },
  // 查询算法文件列表
  versionListFile: () => {
    return request({
      url: '/gtw/cwai/algorithm/version/listFile',
      method: 'post'
    })
  },
  // 批量启用
  enableBatch: () => {
    return request({
      url: '/gtw/cwai/algorithm/version/enableBatch',
      method: 'post'
    })
  },
  // 批量更新
  updateBatch: () => {
    return request({
      url: '/gtw/cwai/algorithm/version/updateBatch',
      method: 'post'
    })
  },
  // 批量删除
  versionBatchDelete: () => {
    return request({
      url: '/gtw/cwai/algorithm/version/batch/delete',
      method: 'post'
    })
  },
  // 编排动作
  atomicActionList: (data) => {
    return request({
      url: '/gtw/cwai/atomic/action/list',
      method: 'post',
      data
    })
  },
  // 查询原子模型列表接口
  atomicModelList: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/list',
      method: 'post',
      data
    })
  },
  // 编排算法详情接口
  algorithmLayoutDetail: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/detail',
      method: 'post',
      data
    })
  },
  // 保存编排算法
  saveAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/save',
      method: 'post',
      data
    })
  },
  // 列表查询场景算法接口
  algorithmLayoutList: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/list',
      method: 'post',
      data
    })
  },
  // 添加场景算法接口
  addAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/add',
      method: 'post',
      data
    })
  },
  boxUpdateAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/update',
      method: 'post',
      data
    })
  },
  updateAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/update',
      method: 'post',
      data
    })
  },
  // 删除场景算法接口
  deleteAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/delete',
      method: 'post',
      data
    })
  },
  boxDeleteAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/delete',
      method: 'post',
      data
    })
  },
  // 导入编排算法
  importAlgorithmLayout: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/import',
      method: 'post',
      data
    })
  },
  // 导出编排算法
  exportAlgorithmLayout: () => {
    return '/gtw/cwai/algorithm/layout/export'
  },
  //分页查询原子模型接口 
  getAtomicModelPage: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/page',
      method: 'post',
      data
    })
  },
  //原子模型模型批量更新  
  updateAtomicModelBatch: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/batchUpdate',
      method: 'post',
      data
    })
  },
  // 原子模型上传接口
  uploadAtomicModel: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/upload',
      method: 'post',
      data
    })
  },
  //系统算法授权查看  
  algorithmLicenseView: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/license/view',
      method: 'post',
      data
    })
  },
  // 分页查询客户算法授权列表
  getCustLicense: (data) => {
    return request({
      url: '/gtw/cwai/cust/license/pageList',
      method: 'post',
      data
    })
  },
  // 获取算法任务列表
  getListByAlg: (data) => {
    return request({
      url: '/gtw/cwai/task/listGroupByAlg',
      method: 'post',
      data
    })
  },
  syncAlgParamToTaskConfig: (data) => {
    return request({
      url: '/gtw/cwai/task/syncAlgParamToTaskConfig',
      method: 'post',
      data
    })
  },
  // 启用某个版本编排算法
  enableVersion: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/enableVersion',
      method: 'post',
      data
    })
  },
  // 删除某个版本编排算法
  deleteVersion: (data) => {
    return request({
      url: '/gtw/cwai/algorithm/layout/deleteVersion',
      method: 'post',
      data
    })
  },
  exportSingleAlg: () => {
    return '/gtw/cwai/algorithm/layout/exportSingleAlg'
  },
  // 创建图片分析任务
  pTaskCreate: data => {
    return request({
      url: '/gtw/cwai/aihost/PTaskCreate',
      method: 'post',
      data,
      timeout: 120000
    })
  },
  // 取消图片分析任务
  pTaskCancle: data => {
    return request({
      url: '/gtw/cwai/aihost/PTaskCancle',
      method: 'post',
      data,
      timeout: 30000
    })
  },
  // 图片分析接口
  imageAnalysis: data => {
    return request({
      url: '/gtw/cwai/aihost/PTaskDetectPic',
      method: 'post',
      data,
      timeout: 60000
    })
  },
  // 查询已运行分析服务类型列表
  engineTypeList: data => {
    return request({
      url: '/gtw/cwai/analysis/node/engineTypeList',
      method: 'post',
      data
    })
  },

  // 查询原子模型组件列表接口
  getModelComponents: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/getModelComponents',
      method: 'post',
      data
    })
  },
  // 查询原子模型配置接口
  getModelConfig: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/getConfig',
      method: 'post',
      data
    })
  },
  // 保存原子模型配置接口
  saveModelConfig: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/saveConfig',
      method: 'post',
      data
    })
  },
  // 导出原子模型配置接口
  exportModelConfig: () => {
    return '/gtw/cwai/atomic/model/exportConfig'
  },
  // 上传原子模型配置接口
  uploadAtomicModelTemp: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/uploadTemp',
      method: 'post',
      data,
    })
  },
  // 添加原子模型接口
  addAtomicModel: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/add',
      method: 'post',
      data
    })
  },
  // 导入模型接口
  importModel: (data) => {
    return request({
      url: '/gtw/cwai/atomic/model/importModel',
      method: 'post',
      data
    })
  },
}
