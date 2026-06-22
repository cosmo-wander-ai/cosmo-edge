// 全局状态管理
let globalState = {
  loading: false,
  loginState: false
}

const actions = {
  setGlobalState(state) {
    globalState = { ...globalState, ...state }
  },
  getGlobalState() {
    return globalState
  }
}

export default actions
