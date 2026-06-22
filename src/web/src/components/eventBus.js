// Vue 3 EventBus using mitt
import mitt from 'mitt'

const emitter = mitt()

// 为了兼容 Vue 2 的 $emit 和 $on 语法
export default {
  $emit: emitter.emit,
  $on: emitter.on,
  $off: emitter.off
}
