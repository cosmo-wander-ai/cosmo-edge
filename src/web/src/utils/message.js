import { ElMessage } from 'element-plus'

let messageInstance = null

const resetMessage = (options) => {
  if (messageInstance) {
    messageInstance.close()
  }
  messageInstance = ElMessage(options)
}

// 将封装后的方法挂载到 Vue.prototype 上
['error', 'success', 'info', 'warning'].forEach(type => {
  resetMessage[type] = options => {
    if (typeof options === 'string') {
      options = {
        message: options
      }
    }
    options.type = type
    return resetMessage(options)
  }
})

export const message = resetMessage
