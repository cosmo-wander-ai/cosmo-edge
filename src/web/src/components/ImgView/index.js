import { createApp } from 'vue'
import ImgView from './imgView.vue'

export default {
  install(app) {
    // 创建全局方法 $imgView
    app.config.globalProperties.$imgView = (urlList, titleName = '') => {
      // 创建一个容器元素
      const container = document.createElement('div')
      document.body.appendChild(container)
      
      // 创建 Vue 应用实例
      const imgViewApp = createApp(ImgView, {
        urlList,
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
  }
}

// 也可以直接导出组件供单独使用
export { ImgView }