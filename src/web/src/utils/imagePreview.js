import { createApp } from 'vue'
import { ImgView } from '@/components/ImgView'

/**
 * 图片预览工具函数
 * @param {string|Array} images - 图片URL或图片URL数组
 * @param {string} titleName - 可选的标题名称
 */
export function showImagePreview(images, titleName = '') {
  // 创建一个容器元素
  const container = document.createElement('div')
  document.body.appendChild(container)
  
  // 创建 Vue 应用实例
  const imgViewApp = createApp(ImgView, {
    urlList: Array.isArray(images) ? images : [images],
    titleName,
    onClose: () => {
      // 关闭时清理
      imgViewApp.unmount()
      document.body.removeChild(container)
    }
  })
  
  // 挂载到容器
  imgViewApp.mount(container)
}

export default showImagePreview