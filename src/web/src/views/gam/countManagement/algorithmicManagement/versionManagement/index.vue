<template>
  <div>
    <el-tabs v-model="activeName" type="card" @tab-click="handleClick">
      <el-tab-pane v-for="item in gpuCodes" :label="item.value" :key="item.id" :name="item.code">
        <eltable :gpuCode="item.code" v-if="activeName == item.code"></eltable>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script>
import eltable from './components/el-table.vue'
export default {
  components: {
    eltable
  },
  data() {
    return {
      gpuCodes: [],
      gpuCode: '',
      activeName: ''
    }
  },
  mounted() {
    this.getGPUCodes()
  },
  methods: {
    // 获取算力类型
    getGPUCodes() {
      this.$API.getGPUCodes().then((res) => {
        const { resData } = res
        this.gpuCodes = resData || []
        this.activeName = this.gpuCodes[0].code
      })
    },
    handleClick(tab) {
      this.activeName = tab.name
      this.gpuCode = tab.name
    }
  }
}
</script>

// <style scoped lang="scss">
// /deep/ .el-tabs__header {
//   background-color: white;
//   margin-left: 10px;
// }
</style>
