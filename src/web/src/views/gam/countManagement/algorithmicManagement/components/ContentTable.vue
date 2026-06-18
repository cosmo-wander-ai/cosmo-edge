<template>
  <div class="mv-wrap">
    <div class="mv-table-wrap">
      <!-- 表格数据 -->
      <el-table :data="tableData" tooltip-effect="dark" style="width: 100%" stripe>
        <el-table-column type="index" :label="t('field.no')" prop="date" width="100"></el-table-column>
        <el-table-column prop="algorithmName" :label="t('field.algorithmName')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="versionNumber" :label="t('field.algorithmVersion')" min-width="150" show-overflow-tooltip>
          <template v-slot="scope">
            <div style="display:flex;line-height:40px;">
              <div style="margin-right:10px;">{{scope.row.versionNumber}}</div>
              <div v-if="scope.row.versionStatus == 4" style="color:red">{{stateMethod(scope.row.versionStatus)}}</div>
              <div v-else>{{stateMethod(scope.row.versionStatus)}}</div>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="sdkVersionNumber" :label="t('field.sdkVersion')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="versionDesc" :label="t('field.description')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="updateTime" :label="t('field.updateTime')" min-width="150" show-overflow-tooltip></el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script>
import { t } from '@/i18n'
export default {
  props: ['gpuCode'],
  setup() {
    return { t }
  },
  data() {
    return {
      algorithmModel: '',
      offlineList: [],
      explain: '',
      // 加载中
      loading: false,
      tableData: []
    }
  },
  watch: {
    gpuCode: {
      handler(newVal, oldVal) {
        this.init()
      },
      immediate: true
    }
  },
  methods: {
    // 状态
    stateMethod(data) {
      switch (data) {
        // 0：已禁用，1：已启用2：待更新，3：更新中，4：更新失败，5：更新成功
        case 0:
          return t('status.disabled')
        case 1:
          return t('status.published')
        case 2:
          return t('status.pendingUpdate')
        case 3:
          return t('status.updating')
        case 4:
          return t('status.updateFailed')
      }
    },
    init() {
      this.algorithmId = this.$attrs.algorithmId
      const params = {
        algorithmId: this.algorithmId,
        gpuCode: this.gpuCode
      }
      this.$API
        .algorithmInfo(params)
        .then((res) => {
          const { resData } = res
          this.tableData = resData
        })
        .catch((err) => { })
    },
    // 赋值input
    handleChange(file, fileList) {
      // console.log(fileList);
      this.algorithmModel = file.name
      if (fileList.length > 0) {
        this.offlineList = [file]
      }
    }
  }
}
</script>
