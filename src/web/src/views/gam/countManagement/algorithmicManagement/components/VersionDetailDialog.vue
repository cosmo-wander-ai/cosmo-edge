<template>
  <div>
    <el-tabs v-model="activeName" type="card" @tab-click="handleClick">
      <el-tab-pane v-for="item in gpuCodes" :label="item.value" :key="item.id" :name="item.code">
        <ContentTable :gpuCode="item.code" v-if="activeName == item.code" v-bind="$attrs" />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script>
import ContentTable from './ContentTable.vue'
export default {
  components: {
    ContentTable
  },

  data() {
    return {
      activeName: '',
      gpuCodes: [],
      gpuCode: ''
    }
  },
  mounted() {
    this.getGPUCodes()
  },
  methods: {
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

<style>
</style>