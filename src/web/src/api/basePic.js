import { request } from '@/utils/request'

export default {
  // 人脸底库
  // 添加脸库
  modifyFaceLib(data) {
    return request({
      url: '/gtw/cwai/Library/ModifyFaceLib',
      method: 'post',
      data
    })
  },
  // 查询脸库信息
  queryFaceLibInfo(data) {
    return request({
      url: '/gtw/cwai/Library/QueryFaceLibInfo',
      method: 'post',
      data
    })
  },
  // 删除脸库
  deleteFaceLib(data) {
    return request({
      url: '/gtw/cwai/Library/DeleteFaceLib',
      method: 'post',
      data
    })
  },
  // 查询脸库列表
  queryFaces(data) {
    return request({
      url: '/gtw/cwai/Library/QueryFaces',
      method: 'post',
      data
    })
  },
  // 添加人脸
  modifyFacePicLib(data) {
    return request({
      url: '/gtw/cwai/Library/ModifyFacePicLib',
      method: 'post',
      data
    })
  },
  // 删除人脸
  boxDeletePerson(data) {
    return request({
      url: '/gtw/cwai/Library/DeletePerson',
      method: 'post',
      data
    })
  },
  queryImportStatus(data) {
    return request({
      url: '/gtw/cwai/File/QueryImportStatus',
      method: 'post',
      data
    })
  },
  boxImportFile(data) {
    return request({
      url: '/gtw/cwai/File/ImportFile',
      method: 'post',
      data
    })
  },

  // 工服底库
  // 添加工服底库
  modifyPersonLib(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/ModifyPersonLib',
      method: 'post',
      data
    })
  },
  // 查询工服底库信息
  queryPersonLibInfo(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/QueryPersonLibInfo',
      method: 'post',
      data
    })
  },
  // 删除工服库
  deletePersonLib(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/DeletePersonLib',
      method: 'post',
      data
    })
  },
  // 查询工服底库列表
  queryPersonPictures(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/QueryPersonPictures',
      method: 'post',
      data
    })
  },
  // 检测工服照片
  detectPerson(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/DetectPerson',
      method: 'post',
      data
    })
  },
  // 添加工服照片
  addLibPerson(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/AddLibPerson',
      method: 'post',
      data
    })
  },
  // 删除工服
  deleteLibPerson(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/DeleteLibPerson',
      method: 'post',
      data
    })
  },
  // 抓图
  getPersonPicture(data) {
    return request({
      url: '/gtw/cwai/BodyLibrary/GetPersonPicture',
      method: 'post',
      data
    })
  },

  // 物品库
  // 查询物品库
  queryThingsLibInfo(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/QueryThingsLibInfo',
      method: 'post',
      data
    })
  },
  // 修改物品库
  modifyThingsLib(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/ModifyThingsLib',
      method: 'post',
      data
    })
  },
  // 删除物品库
  deleteThingsLib(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/DeleteThingsLib',
      method: 'post',
      data
    })
  },
  // 查询物品库列表
  queryThingsPictures(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/QueryThingsPictures',
      method: 'post',
      data
    })
  },
  // 添加物品库
  addLibThings(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/AddLibThings',
      method: 'post',
      data
    })
  },
  // 获取物品照片
  getThingsPicture(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/GetThingsPicture',
      method: 'post',
      data
    })
  },
  // 删除物品照片
  deleteLibThings(data) {
    return request({
      url: '/gtw/cwai/ThingsLibrary/DeleteLibThings',
      method: 'post',
      data
    })
  }
}