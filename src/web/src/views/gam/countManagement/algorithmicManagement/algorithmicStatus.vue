<template>
  <div class="mv-wrap">
    <el-alert type="info" :closable="false" show-icon>
      <div>{{ t('field.description') }}{{ localeColon }}</div>
      <div>{{ t('validate.modelUploadedGreen') }}</div>
      <div>{{ t('validate.modelNotUploadedBlack') }}</div>
    </el-alert>
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="80" @search="searchList" />

    <div class="mv-table-wrap">
      <!--  数据表格 -->
      <el-table ref="tableRef" :data="tableData" stripe style="width: 100%" :max-height="tableMaxHeight">
        <el-table-column type="index" :label="t('field.no')" prop="date" width="100" :index="indexcount"></el-table-column>
        <el-table-column prop="algorithmName" :label="t('field.algorithmName')">
          <template #default="scope">
            <span>{{ scope.row.algorithmName }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="algorithmId" :label="t('field.algorithmId')"></el-table-column>
        <template v-if="platformType !=='15'">
          <el-table-column prop="supplier" :label="t('field.serviceProvider')"></el-table-column>
          <el-table-column v-if="engineTypeList.length === 0" :label="t('glossary.modelStatus')">
            <template #default="scope">
              <span v-for="item in scope.row.envStatus.common" class="span-normal" :key="item.modelCode">{{ item.modelName }}</span>
            </template>
          </el-table-column>
          <!-- "resData": ["nvidiaT4","nvidiaA100","ascend310"] -->
          <el-table-column v-if="engineTypeList.includes('nvidiaT4')" :label="'nvidiaT4 ' + t('glossary.modelStatus')">
            <template #default="scope">
              <span v-for="item in scope.row.envStatus.nvidiaT4" :class="returnSpanStyle(item)" :key="item.modelCode">{{ item.modelName }}</span>
            </template>
          </el-table-column>
          <el-table-column v-if="engineTypeList.includes('ascend310')" :label="'ascend310 ' + t('glossary.modelStatus')">
            <template #default="scope">
              <span v-for="item in scope.row.envStatus.ascend310" :class="returnSpanStyle(item)" :key="item.modelCode">{{ item.modelName }}</span>
            </template>
          </el-table-column>
          <el-table-column v-if="engineTypeList.includes('nvidiaA100')" :label="'nvidiaA100 ' + t('glossary.modelStatus')">
            <template #default="scope">
              <span v-for="item in scope.row.envStatus.nvidiaA100" :class="returnSpanStyle(item)" :key="item.modelCode">{{ item.modelName }}</span>
            </template>
          </el-table-column>
           <el-table-column v-if="engineTypeList.includes('BM1688')" :label="'BM1688 ' + t('glossary.modelStatus')">
            <template #default="scope">
              <span v-for="item in scope.row.envStatus.BM1688" :class="returnSpanStyle(item)" :key="item.modelCode">{{ item.modelName }}</span>
            </template>
          </el-table-column>
        </template>
        <el-table-column v-if="platformType ==='15'" :label="t('glossary.modelStatus')">
          <template #default="scope">
            <span v-for="item in scope.row.models" :key="item.modelCode" :class="{'span-normal': true, 'span-green': item.modelStatus === 1}">
              {{ item.modelName }}
            </span>
          </template>
        </el-table-column>
      </el-table>
      <!-- 分页 -->
      <div class="pagination-container">
        <el-pagination
          ref="paginationRef"
          v-model:current-page="pageData.pageNum"
          v-model:page-size="pageData.pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="pageData.total"
          layout="total, sizes, prev, pager, next, jumper"
          @size-change="handleSizeChange"
          @current-change="handleCurrentChange"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onBeforeUnmount, nextTick, getCurrentInstance } from 'vue'
import { t, localeColon } from '@/i18n'
import TopBar from '@/components/TopBar.vue'

const { proxy } = getCurrentInstance()

const platformType = ref(localStorage.getItem('platformType') || '')
const topBarRef = ref(null)
const tableRef = ref(null)
const paginationRef = ref(null)
const tableMaxHeight = ref(600)
let resizeTimer = null
const calculateTableHeight = () => {
  nextTick(() => {
    const tableEl = tableRef.value && tableRef.value.$el ? tableRef.value.$el : tableRef.value
    const paginationEl = paginationRef.value && paginationRef.value.$el ? paginationRef.value.$el : paginationRef.value
    const top = tableEl ? tableEl.getBoundingClientRect().top : 0
    const paginationHeight = paginationEl ? paginationEl.getBoundingClientRect().height : 72
    const bottomPadding = 24
    const viewportH = window.innerHeight
    tableMaxHeight.value = Math.max(200, viewportH - top - paginationHeight - bottomPadding - 40)
  })
}
const handleResize = () => {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => calculateTableHeight(), 100)
}

const topBarData = reactive({
  formList: [
    {
      label: t('field.algorithmName'),
      model: 'algorithmName'
    },
    {
      label: t('field.algorithmId'),
      model: 'algorithmId'
    },
    {
      label: t('field.serviceProvider'),
      model: 'supplier',
      type: 'select',
      dataList: []
    }
  ]
})

const formData = reactive({
  algorithmId: '',
  algorithmName: '',
  supplier: ''
})

const tableData = ref([])
const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})
const engineTypeList = ref([])

// 序号连续
const indexcount = (index) => {
  return (pageData.pageNum - 1) * pageData.pageSize + index + 1
}

const init = () => {
  const params = {
    ...formData,
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    engineTypeList: engineTypeList.value
  }
  proxy.$API.algorithmInquire(params).then((res) => {
    const { resData } = res
    tableData.value = resData.rows
    pageData.total = resData.total
  })
}

const searchList = () => {
  pageData.pageNum = 1
  if (platformType.value === '15') {
    init()
    return
  }
  getEngineTypeList()
}

const getEngineTypeList = () => {
  proxy.$API.engineTypeList({}).then((res) => {
    const { resData } = res
    engineTypeList.value = resData || []
    init()
  })
}

const getSupplier = () => {
  proxy.$API.getSupplier().then((res) => {
    const { resData } = res
    resData.forEach((item) => {
      topBarData.formList[2].dataList.push({
        label: item.value,
        value: item.code
      })
    })
  })
}

// 分页
const handleCurrentChange = (page) => {
  pageData.pageNum = page
  init()
}

const handleSizeChange = (size) => {
  pageData.pageSize = size
  init()
}

const returnSpanStyle = (obj) => {
  if (obj.modelUploadSatus == 1) {
    return { 'span-normal': true, 'span-green': true }
  } else {
    return { 'span-normal': true }
  }
}

onMounted(() => {
  if (platformType.value === '15') {
    topBarData.formList.splice(2, 1)
    init()
  } else {
    getEngineTypeList()
    getSupplier()
  }
  calculateTableHeight()
  window.addEventListener('resize', handleResize)
})
onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
})
</script>
<style scoped lang="scss">
.el-alert {
  align-items: flex-start;
  div {
    font-size: 13px;
    padding-bottom: 5px;
  }
}

:deep(.el-alert__icon) {
  font-size: 16px;
  margin-top: 6px;
}

:deep(.el-alert__content) {
  padding: 0;
}

.span-normal {
  display: block;
  padding: 0 5px;
}

.span-green {
  color: #59c871;
}

.span-blue {
  color: #3c74f7;
}
</style>
