<template>
  <div class="task-page">
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="80" @search="searchList" />
    <!-- 工具栏 -->
    <div class="task-toolbar">
      <div class="toolbar-left">
        <span class="task-count">{{ t('common.totalTasks', { n: pageData.total }) }}</span>
      </div>
      <div class="toolbar-right">
        <el-button v-if="platformType !=='15'" type="primary" size="small" @click="syncClick">{{ t('action.syncAll') }}</el-button>
        <el-button type="primary" size="small" class="btn-primary-gradient" @click="addAlgorithmic">{{ t('action.createTask') }}</el-button>
        <el-checkbox v-model="selectAll" @change="toggleSelectAll" :indeterminate="isIndeterminate" style="margin-right: 8px;">{{ t('action.selectAll') }}</el-checkbox>
        <el-button size="small" :disabled="batchDeleteIds.length === 0" @click="batchDelete">{{ t('action.bulkDelete') }}</el-button>
        <el-button size="small" @click="uploadAlgorithmic">{{ t('action.importTask') }}</el-button>
        <el-dropdown @command="handleExportCommand" trigger="click">
          <el-button size="small">{{ t('action.export') }} <el-icon style="margin-left:4px"><ArrowDown /></el-icon></el-button>
          <template #dropdown>
            <el-dropdown-menu>
              <el-dropdown-item command="all">{{ t('action.exportAllTasks') }}</el-dropdown-item>
              <el-dropdown-item command="selected" :disabled="batchDeleteIds.length === 0">{{ t('action.exportSelectedTasks') }}</el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>
      </div>
    </div>

    <!-- 卡片网格 -->
    <div class="task-grid">
      <div
        v-for="row in tableData"
        :key="row.algorithmId"
        class="task-card"
        :class="{ selected: isCardSelected(row) }"
      >
        <!-- 选择框 -->
        <div class="card-select">
          <el-checkbox :model-value="isCardSelected(row)" @change="toggleCardSelect(row)" />
        </div>
        <!-- 删除按钮 (hover显示) -->
        <el-button v-if="row.supplier != 'HJ'" class="card-delete-btn" link @click.stop="deleteClick(row)">
          <el-icon><Delete /></el-icon>
        </el-button>
        <!-- 图标 -->
        <div class="card-top">
          <div class="card-icon" :class="getCategoryIconClass(row)" v-html="getCategorySvg(row)"></div>
        </div>
        <!-- 信息 -->
        <div class="card-info">
          <div class="card-title" :title="resolveResourceAlgorithmName(row)">{{ resolveResourceAlgorithmName(row) }}</div>
          <div class="card-meta">ID: {{ row.algorithmId }} · {{ getUsageLabel(row) }}</div>
          <el-tooltip v-if="row.remark" :content="resolveResourceAlgorithmRemark(row)" placement="top" :show-after="300" popper-class="task-remark-tooltip">
            <div class="card-desc">{{ resolveResourceAlgorithmRemark(row) }}</div>
          </el-tooltip>
        </div>
        <!-- 类型标签 -->
        <div class="card-tags">
          <span class="category-tag" :class="getCategoryTagColor(row)">{{ returnCategoryName(row) }}</span>
        </div>
        <!-- 底部 -->
        <div class="card-footer">
          <div class="card-status">
            <span class="status-indicator" :class="getModelStatusOk(row) ? 'status-ok' : 'status-warn'" :title="getModelStatusOk(row) ? '' : returnLostModel(row.models || [])">
              ● {{ getModelStatusOk(row) ? t('status.normal') : t('glossary.modelMissing') }}
            </span>
            <span class="running-count" v-if="channelCountMap[row.algorithmId]">{{ t('common.runningChannels', { n: channelCountMap[row.algorithmId] }) }}</span>
          </div>
          <div class="card-actions">
            <el-button link size="small" @click.stop="arrangeDetailClick(row)">{{ t('action.arrangeAlgorithm') }}</el-button>
            <el-button link size="small" v-if="row.supplier != 'HJ'" @click.stop="editClick(row)">{{ t('action.edit') }}</el-button>
          </div>
        </div>
      </div>
    </div>

    <!-- 分页 -->
    <div class="pagination-container">
      <el-pagination ref="paginationRef" v-model:current-page="pageData.pageNum" v-model:page-size="pageData.pageSize" :page-sizes="[12, 24, 48]" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper" @size-change="handleSizeChange" @current-change="handleCurrentChange" />
    </div>

    <!-- 删除确认 -->
    <el-dialog :title="t('common.notice')" v-model="deleteDialogVisible" width="350px" center @close="batchDeleteFalg = false">
      <div class="tips">{{ t('validate.deleteTaskWarning') }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" size="small" @click="sureDeleteAlgorithmic">{{ t('action.ok') }}</el-button>
          <el-button size="small" @click="deleteDialogVisible = false">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <el-dialog :title="t('glossary.versionDetails')" v-model="detailDialogVisible" width="800px" center>
      <VersionDetailDialog v-if="detailDialogVisible" :algorithmId="algorithmId" />
    </el-dialog>
    <el-dialog :title="algorithmDialogTitle" v-model="addAlgorithmicVisible" width="500px" center @close="addAlgorithmiClosed">
      <div>
        <el-form ref="addAlgorithmicFormRef" :model="addAlgorithmicForm" :rules="addAlgorithmicRules" label-position="right" label-width="110px">
          <el-form-item :label="t('field.taskName') + localeColon" prop="algorithmName">
            <el-input v-model="addAlgorithmicForm.algorithmName" class="input-width" size="small" :placeholder="t('validate.enterTaskName')" maxlength="32"></el-input>
          </el-form-item>
          <el-form-item v-if="algorithmDialogMode ==='edit'" :label="t('field.taskIdLabel') + localeColon" prop="algorithmId">
            <el-input v-model="addAlgorithmicForm.algorithmId" class="input-width" size="small" :placeholder="t('validate.idRangeExcept')" @input="(e)=>handleInput(e, 'algorithmId')" :disabled="algorithmDialogMode === 'edit'"></el-input>
          </el-form-item>
          <el-form-item v-if="algorithmDialogMode ==='edit'" :label="t('field.integrationId') + localeColon" prop="checkType">
            <el-input v-model="addAlgorithmicForm.checkType" class="input-width" size="small" :placeholder="t('validate.idRangeExcept')" @input="(e)=>handleInput(e, 'checkType')"></el-input>
          </el-form-item>
          <el-form-item :label="t('field.dataSourceType') + localeColon" prop="algorithmUsage">
            <el-select v-model="addAlgorithmicForm.algorithmUsage" class="input-width" :placeholder="t('validate.selectDataSourceType')" size="small" :disabled="algorithmDialogMode === 'edit'">
              <el-option v-for="item in algorithmUsageOptions" :key="item.value" :label="item.label" :value="item.value">
              </el-option>
            </el-select>
          </el-form-item>
          <el-form-item :label="t('field.taskType') + localeColon" prop="algorithmCategory">
            <el-select v-model="addAlgorithmicForm.algorithmCategory" class="input-width" :placeholder="t('validate.selectTaskType')" size="small" @change="algorithmCategoryChange">
              <el-option v-for="item in uniqueCategoryOptions" :key="item.value" :label="item.label" :value="item.value">
              </el-option>
            </el-select>
          </el-form-item>
          <el-form-item :label="t('field.description') + localeColon" prop="remark">
            <el-input v-model="addAlgorithmicForm.remark" type="textarea" class="input-width centered-placeholder" size="small" :rows="4" :placeholder="t('validate.maxChars', { n: 300 })" maxlength="300"></el-input>
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" size="small" @click="sureAddAlgorithmic">{{ t('action.ok') }}</el-button>
          <el-button size="small" @click="addAlgorithmicVisible = false">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <el-dialog :title="t('action.importTask')" v-model="uploadAlgorithmicVisible" width="400px" center @close="uploadAlgorithmicClosed">
      <div>
        <div class="upload-div">{{ t('validate.selectFile') }}{{ localeColon }}</div>
        <el-input v-model="uploadAlgorithmicName" class="upload-input" disabled :placeholder="t('validate.selectFile')" size="small"></el-input>
        <el-upload ref="uploadAlgorithmicRef" class="upload-btn" action="#" :show-file-list="false" :file-list="uploadAlgorithmicData" :auto-upload="false" accept=".tar.gz,.tgz" :http-request="uploadFile" :on-change="handleChange">
          <el-button size="small">{{ t('action.browse') }}</el-button>
        </el-upload>
        <div class="upload-warn">*{{ t('validate.selectTarGzMax') }}</div>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" size="small" @click="sureUploadAlgorithmic">{{ t('action.ok') }}</el-button>
          <el-button size="small" @click="uploadAlgorithmicVisible = false">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script>
import TopBar from '@/components/TopBar.vue'
import VersionDetailDialog from './components/VersionDetailDialog.vue'
import { ArrowDown, Delete } from '@element-plus/icons-vue'
import moment from 'moment'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmRemark, resolveResourceAlgorithmName } from '@/utils/i18nResource'

export default {
  components: {
    TopBar,
    VersionDetailDialog,
    ArrowDown,
    Delete
  },
  setup() {
    return { t, localeColon, resolveResourceAlgorithmRemark, resolveResourceAlgorithmName }
  },
  data() {
    return {
      platformType: localStorage.getItem('platformType') || '',
      authData: null,
      tableData: [],
      tableMaxHeight: 600,
      // 分页
      pageData: {
        pageNum: 1,
        pageSize: 12,
        total: 0
      },
      deleteDialogVisible: false,
      detailDialogVisible: false,
      algorithmId: '',
      deleteAlgorithmId: '', // 要删除的算法ID
      dialogFormBtn: false,
      value: '',
      batchDeleteIds: [],
      selectedCards: [],
      selectAll: false,
      channelCountMap: {},
      channelCountMap: {},
      supplierOptions: [],
      formData: {
        algorithmId: '',
        algorithmCategory: '',
        algorithmUsage: '',
        algorithmName: '',
        supplier: ''
      },
      algorithmDialogMode: 'create',
      addAlgorithmicVisible: false,
      uploadAlgorithmicVisible: false,
      addAlgorithmicForm: {
        algorithmName: '',
        algorithmId: '',
        checkType: '',
        remark: '',
        algorithmUsage: '',
        algorithmCategory: '',
        eventType: ''
      },
      addAlgorithmicRules: {
        algorithmName: [
          { required: true, message: t('validate.enterTaskName'), trigger: 'change' },
          { validator: function(rule, value, callback) {
              if (!value) { callback(); return; }
              if (/_/.test(value)) { callback(new Error(t('validate.taskNameNoUnderscore'))); return; }
              callback();
            }, trigger: 'change' }
        ],
        // algorithmId: [
        //   { required: true, message: '请输入算法ID', trigger: 'change' },
        //   { validator: this.validateNumber, trigger: 'change' }
        // ],
        // checkType: [
        //   { required: true, message: '请输入算法对接ID', trigger: 'change' },
        //   { validator: this.validateNumber2, trigger: 'change' }
        // ],
        algorithmUsage: [
          { required: true, message: t('validate.selectDataSourceType'), trigger: 'change' }
        ],
        algorithmCategory: [
          { required: true, message: t('validate.selectTaskType'), trigger: 'change' }
        ]
      },
      uploadAlgorithmicName: '',
      uploadAlgorithmicData: [],
      engineTypeList: [],
      batchDeleteFalg: false
    }
  },
  mounted() {
    this.calculateTableHeight()
    window.addEventListener('resize', this.handleResize)
  },
  unmounted() {
    window.removeEventListener('resize', this.handleResize)
  },
  computed: {
    topBarData() {
      const list = [
        {
          label: t('field.taskName'),
          model: 'algorithmName'
        },
        {
          label: t('field.taskType'),
          model: 'algorithmCategory',
          type: 'select',
          filterable: true,
          dataList: [
            { label: t('common.all'), value: '' },
            { label: t('glossary.faceAndBody'), value: 'face' },
            { label: t('glossary.detection'), value: 'detect' },
            { label: t('glossary.countingAnalytics'), value: 'count' }
          ]
        },
        {
          label: t('field.dataSourceType'),
          model: 'algorithmUsage',
          type: 'select',
          dataList: [
            { label: t('common.all'), value: '' },
            { label: t('glossary.videoAnalysis'), value: 1 },
            { label: t('glossary.imageAnalysis'), value: 2 }
          ]
        },
        {
          label: t('field.serviceProvider'),
          model: 'supplier',
          type: 'select',
          dataList: this.supplierOptions
        }
      ]
      if (this.platformType === '15') {
        list.splice(2, 2)
      }
      return { formList: list }
    },
    algorithmCategoryOptions() {
      return [
        { label: t('glossary.faceAndBody'), value: '1', eventType: 'face' },
        { label: t('glossary.detection'), value: '2', eventType: 'behavior' },
        { label: t('glossary.detection'), value: '3', eventType: 'motorCommodity' },
        { label: t('glossary.countingAnalytics'), value: '8', eventType: 'passengerNumber' },
        { label: t('glossary.countingAnalytics'), value: '9', eventType: 'peopleCountByArea' },
        { label: t('glossary.countingAnalytics'), value: '11', eventType: 'vehicleNumber' }
      ]
    },
    algorithmUsageOptions() {
      return [
        { label: t('glossary.videoAnalysis'), value: '1' },
        { label: t('glossary.imageAnalysis'), value: '2' }
      ]
    },
    uniqueCategoryOptions() {
      const seen = new Set()
      return this.algorithmCategoryOptions.filter(item => {
        if (seen.has(item.label)) return false
        seen.add(item.label)
        return true
      })
    },
    isIndeterminate() {
      return this.selectedCards.length > 0 && this.selectedCards.length < this.tableData.length
    },
    algorithmDialogTitle() {
      return this.algorithmDialogMode === 'create' ? t('action.createTask') : t('action.editTask')
    }
  },
  created() {
    if (this.platformType !== '15') {
      this.getEngineTypeList()
      this.getSupplier()
      this.initAuthSummary()
    } else {
      this.init()
    }
  },
  methods: {
    validateAlgorithmName(rule, value, callback) {
      if (!value) { callback(); return; }
      if (/_/.test(value)) { callback(new Error(t('validate.taskNameNoUnderscore'))); return; }
      callback();
    },
    // 表格高度计算，启用固定表头
    calculateTableHeight() {
      this.$nextTick(() => {
        const tableEl =
          this.$refs.tableRef && this.$refs.tableRef.$el
            ? this.$refs.tableRef.$el
            : this.$refs.tableRef
        const paginationEl =
          this.$refs.paginationRef && this.$refs.paginationRef.$el
            ? this.$refs.paginationRef.$el
            : this.$refs.paginationRef
        const top = tableEl ? tableEl.getBoundingClientRect().top : 0
        const paginationHeight = paginationEl
          ? paginationEl.getBoundingClientRect().height
          : 72
        const bottomPadding = 24
        const viewportH = window.innerHeight
        this.tableMaxHeight = Math.max(
          200,
          viewportH - top - paginationHeight - bottomPadding - 40
        )
      })
    },
    handleResize() {
      this.calculateTableHeight()
    },
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
    // 序号连续
    indexMethod(index) {
      let page = this.pageData.pageNum
      let size = this.pageData.pageSize
      return index + 1 + (page - 1) * size
    },
    getEngineTypeList() {
      this.$API.engineTypeList({}).then((res) => {
        const { resData } = res
        this.engineTypeList = resData || []
        this.init()
      })
    },
    // 算法管理list
    init() {
      this.selectedCards = []
      this.batchDeleteIds = []
      this.loadChannelCounts()
      let page = localStorage.getItem('pageName')
      if (page) {
        this.pageData.pageNum = parseInt(page)
      }
      // console.log(this.pageData.pageNum)
      // 搜索分类映射：合并 key → 实际 category 值
      const categoryGroupMap = {
        'face': ['1'],
        'detect': ['2', '3'],
        'count': ['8', '9', '11']
      }
      const selectedGroup = this.formData.algorithmCategory
      const expandedCategories = categoryGroupMap[selectedGroup] || (selectedGroup ? [selectedGroup] : [])
      const params = {
        ...this.formData,
        algorithmCategory: expandedCategories.length === 1 ? expandedCategories[0] : '',
        pageNum: this.pageData.pageNum,
        pageSize: this.pageData.pageSize,
        engineTypeList: this.engineTypeList
      }
      // 如果选了合并分类且包含多个值，需要前端过滤
      // algorithmInquire API 只支持单值 algorithmCategory，空串=全部
      // 传空串获取全部，前端过滤 + 前端分页
      if (expandedCategories.length > 1) {
        params.algorithmCategory = ''
        params._filterCategories = expandedCategories
        params.pageNum = 1
        params.pageSize = 9999
      }
      this.$API.algorithmInquire(params).then((res) => {
        const { resData } = res
        let rows = resData.rows || []
        // 前端过滤：当合并分类选了多个 category 值时
        if (params._filterCategories) {
          rows = rows.filter(r => params._filterCategories.includes(String(r.algorithmCategory)))
          this.pageData.total = rows.length
          // 前端分页
          const start = (this.pageData.pageNum - 1) * this.pageData.pageSize
          rows = rows.slice(start, start + this.pageData.pageSize)
        } else {
          this.pageData.total = resData.total
        }
        this.tableData = rows
        localStorage.removeItem('pageName')
        if (!this.tableData.length && this.pageData.pageNum > 1) {
          this.pageData.pageNum--
          this.init()
        }
        this.calculateTableHeight()
      })
    },
    getSupplier() {
      this.$API.getSupplier().then((res) => {
        const { resData } = res
        const options = []
        resData.forEach((item) => {
          options.push({
            label: item.value,
            value: item.code
          })
        })
        this.supplierOptions = options
      })
    },
    initAuthSummary() {
      this.$API.algorithmLicenseView({ authId: 10000 }).then((res) => {
        const { resData } = res
        this.authData = resData
        if (this.authData) {
          this.authTimes = [
            this.authData.authStartTime.split(' ')[0],
            this.authData.authEndTime.split(' ')[0]
          ]
        } else {
          this.authTimes = []
        }
      })
    },
    formaterDate(obj) {
      if (!obj || !obj.authStartTime) return ''
      const beginTime = obj.authStartTime.split(' ')[0]
      const endTime = obj.authEndTime.split(' ')[0]
      return `${beginTime} ~ ${endTime}`
    },
    returnAuthDay(authData) {
      if (!authData) return ''
      return authData.authDay > 36499 ? t('common.permanent') : t('common.days', { n: authData.authDay })
    },
    // 表格居中
    headClass() {
      return 'text-align:center;font-weight: 700;'
    },
    cellClass() {
      return 'text-align:center'
    },
    handleInput(val, key) {
      // 使用正则表达式替换非数字字符
      this.addAlgorithmicForm[key] = val.replace(/[^\d]/g, '')
    },
    // 分页
    handleCurrentChange(page) {
      this.pageData.pageNum = page
      this.init()
    },
    handleSizeChange(pageSize) {
      this.pageData.pageSize = pageSize
      this.init()
    },
    // 版本管理
    versionManagement(val) {
      localStorage.setItem('pageName', this.pageData.pageNum)
      const query = {
        algorithmId: val
      }
      // this.$router.push({ name: this.platformType === "1" || this.platformType === "6" ? "gamversionManagement" : 'versionManagement', query: query });
      var platformType = localStorage.getItem('platformType')
      if (platformType !== '-1') {
        // this.$router.push({path:'/gam/versionManagement',query:query})
        this.$router.push({
          path: '/gam/versionManagement',
          query: { resetUrl: this.$route.path, algorithmId: val }
        })
      } else {
        this.$router.push({ path: '/versionManagement', query: query })
      }
    },
    //  自定义参数
    userDefinedManagement(val) {
      let platformType = localStorage.getItem('platformType')
      if (platformType !== '-1') {
        this.$router.push({
          path: '/gam/userDefined',
          query: { resetUrl: this.$route.path, query: val }
        })
      }
    },
    arrangeDetailClick(obj) {
      let platformType = localStorage.getItem('platformType')
      if (platformType !== '-1') {
        this.$router.push({
          path: '/gam/arrangeDetail',
          query: {
            resetUrl: this.$route.path,
            algorithmId: obj.algorithmId,
            supplier: obj.supplier,
            algorithmUsage: obj.algorithmUsage
          }
        })
      }
    },
    detailClick(obj) {
      this.algorithmId = obj.algorithmId
      this.detailDialogVisible = true
    },
    editClick(row) {
      this.algorithmDialogMode = 'edit'
      this.addAlgorithmicForm = {
        algorithmName: row.algorithmName,
        algorithmId: row.algorithmId,
        remark: row.remark,
        algorithmUsage: row.algorithmUsage,
        algorithmCategory: row.algorithmCategory + '',
        eventType: row.eventType,
        checkType: row.checkType
      }
      this.addAlgorithmicVisible = true
    },
    // 删除
    deleteClick(row) {
      this.deleteDialogVisible = true
      this.deleteAlgorithmId = row.algorithmId
    },
    batchDelete() {
      this.deleteDialogVisible = true
      this.batchDeleteFalg = true
    },
    handleSelectionChange(val) {
      this.batchDeleteIds = val.map((item) => item.algorithmId)
    },
    // // 关闭
    // close() {
    //   this.dialogFormBtn = false
    // },
    // // 添加
    // Btnredact() {}
    searchList() {
      this.pageData.pageNum = 1
      if (this.platformType === '15') {
        this.init()
      } else {
        this.getEngineTypeList()
      }
    },
    sureDeleteAlgorithmic() {
      if (this.batchDeleteFalg) {
        const deletePromises = this.batchDeleteIds.map((id) => {
          return this.$API.boxDeleteAlgorithmLayout({ algorithmId: id })
        })
        Promise.all(deletePromises).then(() => {
          this.$message.success(t('common.operationSucceeded'))
          this.deleteDialogVisible = false
          this.batchDeleteFalg = false
          this.batchDeleteIds = []
          this.formData.algorithmId = ''
          this.formData.algorithmCategory = ''
          this.formData.algorithmUsage = ''
          this.formData.algorithmName = ''
          this.formData.supplier = ''
          this.pageData.pageNum = 1
          this.searchList()
        })
        return
      }
      const params = {
        id: this.deleteAlgorithmId
      }
      if (this.platformType === '15') {
        this.$API
          .boxDeleteAlgorithmLayout({ algorithmId: params.id })
          .then(() => {
            this.$message.success(t('common.operationSucceeded'))
            this.deleteDialogVisible = false
            this.formData.algorithmId = ''
            this.formData.algorithmCategory = ''
            this.formData.algorithmUsage = ''
            this.formData.algorithmName = ''
            this.formData.supplier = ''
            this.pageData.pageNum = 1
            this.searchList()
          })
          .catch(() => {
            this.deleteDialogVisible = false
          })
      } else {
        this.$API.deleteAlgorithmLayout(params).then(() => {
          this.$message.success(t('common.operationSucceeded'))
          this.deleteDialogVisible = false
          this.formData.algorithmId = ''
          this.formData.algorithmCategory = ''
          this.formData.algorithmUsage = ''
          this.formData.algorithmName = ''
          this.formData.supplier = ''
          this.pageData.pageNum = 1
          this.searchList()
        })
      }
    },
    addAlgorithmic() {
      this.algorithmDialogMode = 'create'
      this.addAlgorithmicVisible = true
    },
    addAlgorithmiClosed() {
      this.addAlgorithmicForm = {
        algorithmName: '',
        algorithmId: '',
        remark: '',
        algorithmUsage: '',
        algorithmCategory: '',
        eventType: '',
        checkType: ''
      }
      this.$refs.addAlgorithmicFormRef.resetFields()
    },
    sureAddAlgorithmic() {
      this.$refs.addAlgorithmicFormRef.validate((valid) => {
        if (valid) {
          const params = {
            algorithmCategory: this.addAlgorithmicForm.algorithmCategory,
            // algorithmCode: this.addAlgorithmicForm.algorithmId,
            algorithmName: this.addAlgorithmicForm.algorithmName,
            algorithmUsage: Number(this.addAlgorithmicForm.algorithmUsage),
            eventType: this.addAlgorithmicForm.eventType,
            checkType: this.addAlgorithmicForm.checkType,
            remark: this.addAlgorithmicForm.remark,
            filePath: '/appfs/cosmo_wander/cwai_data/resource/algorithm'
          }
          params.algorithmCategory = Number(params.algorithmCategory)
          params.checkType = Number(params.checkType)
          if (this.algorithmDialogMode === 'create') {
            this.$API.addAlgorithmLayout(params).then((res) => {
              this.$message.success(t('common.operationSucceeded'))
              this.addAlgorithmicVisible = false
              this.searchList()
              this.synchronousCustAlgorithnm()
            })
          } else {
            if (this.platformType === '15') {
              params.algorithmId = this.addAlgorithmicForm.algorithmId
              params.algorithmCategory = Number(params.algorithmCategory)
              delete params.supplier
              delete params.algorithmCode
              this.$API.boxUpdateAlgorithmLayout(params).then((res) => {
                this.$message.success(t('common.operationSucceeded'))
                this.addAlgorithmicVisible = false
                this.searchList()
                // this.synchronousCustAlgorithnm()
              })
            } else {
              this.$API.updateAlgorithmLayout(params).then((res) => {
                this.$message.success(t('common.operationSucceeded'))
                this.addAlgorithmicVisible = false
                this.searchList()
                this.synchronousCustAlgorithnm()
              })
            }
          }
        }
      })
    },
    uploadAlgorithmic() {
      this.uploadAlgorithmicVisible = true
    },
    sureUploadAlgorithmic() {
      if (this.uploadAlgorithmicData.length === 0)
        return this.$message.warning(t('validate.uploadFileFirst'))
      this.$refs.uploadAlgorithmicRef.submit()
    },
    uploadFile(params) {
      // 检查文件大小限制（500MB = 500 * 1024 * 1024 字节）
      const maxSize = 500 * 1024 * 1024
      if (params.file.size > maxSize) {
        this.$message.error(t('validate.fileMaxSize', { n: 500 }))
        return
      }

      let formData = new FormData()
      formData.append('file', params.file)
      if (this.platformType === '15') {
        this.$API.boxAlgorithmUpload(formData).then((res) => {
          this.$message.success(t('common.operationSucceeded'))
          this.uploadAlgorithmicVisible = false
          this.searchList()
        })
      } else {
        this.$API.importAlgorithmLayout(formData).then((res) => {
          this.$message.success(t('common.operationSucceeded'))
          this.uploadAlgorithmicVisible = false
          this.searchList()
          this.synchronousCustAlgorithnm()
        })
      }
    },
    handleChange(file) {
      this.uploadAlgorithmicName = file.name
      this.uploadAlgorithmicData = [file]
    },
    algorithmCategoryChange(value) {
      const selected = this.algorithmCategoryOptions.find(
        (option) => option.value == value
      )
      this.addAlgorithmicForm.eventType = selected.eventType
    },
    uploadAlgorithmicClosed() {
      this.uploadAlgorithmicName = ''
      this.uploadAlgorithmicData = []
    },
    exportAlgorithmic() {
      let this_ = this
      const categoryGroupMap = { 'face': '1', 'detect': '', 'count': '' }
      const resolvedCategory = categoryGroupMap[this.formData.algorithmCategory] ?? this.formData.algorithmCategory
      let i = {
        algorithmCategory: resolvedCategory,
        algorithmCode: this.formData.algorithmId,
        algorithmName: this.formData.algorithmName,
        algorithmUsage: this.formData.algorithmUsage,
        supplier: this.formData.supplier
      }
      const year = moment().format('YYYY-MM-DD')
      const time = moment().format('HH:mm:ss')
      const randomNumber = Math.floor(Math.random() * 9000) + 1000
      const zipName = `${year}_${time}_${randomNumber}.tar.gz`
      let x = new XMLHttpRequest()
      x.open('post', this.$API.exportAlgorithmLayout(), true)
      x.responseType = 'blob'
      x.setRequestHeader('Content-Type', 'application/json;charset=UTF-8')
      x.setRequestHeader('token', localStorage.getItem('token'))
      x.setRequestHeader('lang', localStorage.getItem('language'))
      x.setRequestHeader('mtk', localStorage.getItem('token'))
      x.onload = function (e) {
        // console.log(this.status)
        if (this.status == 200) {
          let blob = this.response
          let a = document.createElement('a')
          let url = window.URL.createObjectURL(blob)
          a.href = url
          a.download = zipName
          a.click()
        }
      }
      x.send(JSON.stringify(i))
    },
    exportSelectedAlgorithmic() {
      if (this.batchDeleteIds.length === 0) {
        return this.$message.warning(t('validate.selectTasksToExport'))
      }
      let i = {
        algorithmIds: this.batchDeleteIds
      }
      const year = moment().format('YYYY-MM-DD')
      const time = moment().format('HH:mm:ss')
      const randomNumber = Math.floor(Math.random() * 9000) + 1000
      const zipName = `${year}_${time}_${randomNumber}.tar.gz`
      let x = new XMLHttpRequest()
      x.open('post', this.$API.exportAlgorithmLayout(), true)
      x.responseType = 'blob'
      x.setRequestHeader('Content-Type', 'application/json;charset=UTF-8')
      x.setRequestHeader('token', localStorage.getItem('token'))
      x.setRequestHeader('lang', localStorage.getItem('language'))
      x.setRequestHeader('mtk', localStorage.getItem('token'))
      x.onload = function (e) {
        if (this.status == 200) {
          let blob = this.response
          let a = document.createElement('a')
          let url = window.URL.createObjectURL(blob)
          a.href = url
          a.download = zipName
          a.click()
        }
      }
      x.send(JSON.stringify(i))
    },
    validateNumber(rule, value, callback) {
      if (this.algorithmDialogMode === 'edit') callback()
      if (value === '' || isNaN(value)) {
        callback(new Error(t('validate.enterAlgorithmId')))
      } else {
        const num = Number(value)
        if (num === 10000) {
          callback(new Error(t('validate.idRangeExcept')))
        } else if (num < 0 || num > 99999) {
          callback(new Error(t('validate.idRangeExcept')))
        } else {
          callback()
        }
      }
    },
    validateNumber2(rule, value, callback) {
      const num = Number(value)
      if (num === 10000) {
        callback(new Error(t('validate.idRangeExcept')))
      } else if (num < 0 || num > 99999) {
        callback(new Error(t('validate.idRangeExcept')))
      } else {
        callback()
      }
    },
    // 同步原始算法信息
    synchronousCustAlgorithnm() {
      this.$API.synchronousCustAlgorithmInfo({}).then(() => {
        this.$API.getAlgInfo({ pageNum: 1, pageSize: 1000 }).then((res) => {
          const { resData } = res
          const newData = resData.rows || []
          localStorage.setItem('algorithms', JSON.stringify(newData))
        })
      })
    },
    syncClick() {
      this.$API.syncAlgParamToTaskConfig({ ids: [] }).then(() => {
        this.$message.success(t('common.syncSucceeded'))
      })
    },
    returnNodeModelStatus(row, key) {
      let missArr = []
      // 先检查 row.envStatus 是否存在，然后再检查 row.envStatus[key] 是否存在
      missArr =
        row.envStatus?.[key]?.filter((item) => item.modelUploadSatus == 0) || []
      let missStr = missArr?.map((item) => item.modelName).join('，')
      return missStr || undefined
    },
    returnLostModel(list) {
      const lostModels = list.filter((item) => item.modelStatus != 1)
      return lostModels
        .map((item) => item.modelName || item.modelCode)
        .join('，')
    },
    returnCategoryName(row) {
      let categoryName = ''
      if (row.algorithmCategory) {
        categoryName = this.algorithmCategoryOptions.find(
          (item) => item.value == row.algorithmCategory
        )?.label
      }
      return categoryName
    },
    // ── 卡片模式新增方法 ──
    toggleCardSelect(row) {
      const idx = this.selectedCards.findIndex(item => item.algorithmId === row.algorithmId)
      if (idx >= 0) {
        this.selectedCards.splice(idx, 1)
      } else {
        this.selectedCards.push(row)
      }
      this.batchDeleteIds = this.selectedCards.map(item => item.algorithmId)
      this.syncSelectAllState()
    },
    isCardSelected(row) {
      return this.selectedCards.some(item => item.algorithmId === row.algorithmId)
    },
    toggleSelectAll(val) {
      if (val) {
        this.selectedCards = [...this.tableData]
      } else {
        this.selectedCards = []
      }
      this.batchDeleteIds = this.selectedCards.map(item => item.algorithmId)
    },
    syncSelectAllState() {
      if (this.tableData.length === 0) {
        this.selectAll = false
        return
      }
      this.selectAll = this.selectedCards.length === this.tableData.length
    },
    handleExportCommand(command) {
      if (command === 'all') this.exportAlgorithmic()
      else if (command === 'selected') this.exportSelectedAlgorithmic()
    },
    getUsageLabel(row) {
      if (row.algorithmId === 'ecf72169b0676cdccca5a816192931e4' || row.algorithmId === 'fc2c7ac0add43a888b1f1b6e2294b58c') return t('glossary.videoImageAnalysis')
      if (row.algorithmUsage == 1) return t('glossary.videoAnalysis')
      if (row.algorithmUsage == 2) return t('glossary.imageAnalysis')
      return ''
    },
    getModelStatusOk(row) {
      return !this.returnLostModel(row.models || [])
    },
    getCategoryIconClass(row) {
      const map = { '1': 'icon-blue', '2': 'icon-purple', '3': 'icon-purple', '8': 'icon-green', '9': 'icon-green', '10': 'icon-blue', '11': 'icon-green' }
      return map[String(row.algorithmCategory)] || 'icon-blue'
    },
    getCategoryTagColor(row) {
      const map = { '1': 'tag-blue', '2': 'tag-purple', '3': 'tag-purple', '8': 'tag-green', '9': 'tag-green', '10': 'tag-blue', '11': 'tag-green' }
      return map[String(row.algorithmCategory)] || 'tag-blue'
    },
    getCategorySvg(row) {
      const svgs = {
        '1': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M6 21v-1a6 6 0 0 1 12 0v1"/></svg>',
        '2': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>',
        '3': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>',
        '8': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>',
        '9': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="4"/><line x1="12" y1="2" x2="12" y2="6"/><line x1="12" y1="18" x2="12" y2="22"/><line x1="2" y1="12" x2="6" y2="12"/><line x1="18" y1="12" x2="22" y2="12"/></svg>',
        '10': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="8" width="18" height="10" rx="2"/><circle cx="7.5" cy="18" r="2"/><circle cx="16.5" cy="18" r="2"/><path d="M6 8V6a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v2"/></svg>',
        '11': '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v18h18"/><path d="M7 16l4-4 4 4 5-8"/></svg>'
      }
      return svgs[String(row.algorithmCategory)] || svgs['2']
    },
    // ── 加载真实通道运行数（复用运行总览的 boxCameraPage 逻辑）──
    loadChannelCounts() {
      this.$API.boxCameraPage({ pageNum: 1, pageSize: 500 }).then(res => {
        const rows = res?.resData?.rows || []
        const countMap = {}
        rows.forEach(channel => {
          if (channel.taskList && Array.isArray(channel.taskList)) {
            channel.taskList.forEach(task => {
              if (task.status === 0) return
              if (!countMap[task.algorithmId]) countMap[task.algorithmId] = new Set()
              countMap[task.algorithmId].add(channel.videoChannelId)
            })
          }
        })
        const result = {}
        Object.keys(countMap).forEach(k => { result[k] = countMap[k].size })
        this.channelCountMap = result
      }).catch(() => {})
    }
  }
}
</script>

<style scoped lang="scss">
.task-page {
  display: flex;
  flex-direction: column;
  height: 100%;
}
.task-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 24px;
}
.toolbar-left { display: flex; align-items: center; }
.task-count {
  font-size: 13px;
  color: #6b7280;
  background: #f3f4f6;
  padding: 4px 14px;
  border-radius: 12px;
}
.toolbar-right { display: flex; align-items: center; gap: 8px; }
.btn-primary-gradient {
  background: linear-gradient(135deg, #3182ce, #4299e1) !important;
  border: none !important;
  color: #fff !important;
  &:hover { background: linear-gradient(135deg, #2b6cb0, #2b6cb0) !important; }
}
.task-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
  padding: 4px 24px 12px;
  flex: 1;
  overflow-y: auto;
  align-content: start;
}
@media (max-width: 1200px) { .task-grid { grid-template-columns: repeat(2, 1fr); } }
@media (max-width: 768px) { .task-grid { grid-template-columns: 1fr; } }
.task-card {
  position: relative;
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  border: 1px solid #f0f0f0;
  transition: all 0.25s ease;
  height: 220px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  &:hover {
    transform: translateY(-2px);
    box-shadow: 0 8px 24px rgba(0,0,0,0.08);
    border-color: rgba(49,130,206,0.2);
    .card-delete-btn { opacity: 1; }
  }
  &.selected {
    border-color: #3182ce;
    box-shadow: 0 0 0 2px rgba(49,130,206,0.15);
  }
}
.card-select { position: absolute; top: 12px; left: 12px; z-index: 1; }
.card-delete-btn {
  position: absolute; top: 12px; right: 12px;
  opacity: 0; transition: opacity 0.2s;
  color: #ef4444 !important;
  &:hover { color: #dc2626 !important; }
}
.card-top { margin-bottom: 12px; }
.card-icon {
  width: 44px; height: 44px; border-radius: 10px;
  display: flex; align-items: center; justify-content: center;
  :deep(svg) { width: 22px; height: 22px; }
  &.icon-blue { background: rgba(59,130,246,0.1); color: #3b82f6; }
  &.icon-purple { background: rgba(66,153,225,0.1); color: #4299e1; }
  &.icon-cyan { background: rgba(6,182,212,0.1); color: #06b6d4; }
  &.icon-green { background: rgba(34,197,94,0.1); color: #22c55e; }
  &.icon-orange { background: rgba(245,158,11,0.1); color: #f59e0b; }
}
.card-info { margin-bottom: 10px; }
.card-title {
  font-size: 15px; font-weight: 600; color: #1f2937;
  margin-bottom: 4px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.card-meta { font-size: 12px; color: #9ca3af; }
.card-desc {
  font-size: 12px; color: #9ca3af; margin-top: 4px;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  max-width: 100%;
}
.card-tags { margin-bottom: 12px; }
.category-tag {
  display: inline-block; padding: 2px 10px; border-radius: 12px; font-size: 12px;
  &.tag-blue { background: #dbeafe; color: #2563eb; }
  &.tag-purple { background: #ebf8ff; color: #2b6cb0; }
  &.tag-cyan { background: #cffafe; color: #0891b2; }
  &.tag-green { background: #dcfce7; color: #16a34a; }
  &.tag-orange { background: #fef3c7; color: #d97706; }
}
.card-footer {
  display: flex; justify-content: space-between; align-items: center;
  padding-top: 12px; border-top: 1px solid #f5f5f5;
  margin-top: auto;
}
.card-status { display: flex; align-items: center; gap: 10px; font-size: 12px; }
.status-indicator {
  &.status-ok { color: #22c55e; }
  &.status-warn { color: #ef4444; cursor: help; }
}
.running-count { color: #3182ce; font-weight: 500; }
.card-actions {
  display: flex; gap: 4px;
  .el-button { font-size: 13px; color: #3182ce !important; &:hover { color: #2b6cb0 !important; } }
}
.pagination-container { display: flex; justify-content: center; padding: 12px 24px 16px; flex-shrink: 0; }
.tips { text-align: center; padding: 20px 0; }
.dialog-footer { display: flex; justify-content: center; }
.input-width { width: calc(100% - 80px); }
.upload-div { margin-left: 20px; margin-bottom: 10px; }
.upload-input { width: calc(100% - 100px); margin-left: 20px; margin-right: 10px; }
.upload-warn { margin-top: 5px; margin-left: 20px; color: #3182ce; font-size: 12px; }
.upload-btn { display: inline-block; }
:deep(.el-textarea__inner) { height: 100px; }
</style>

<style lang="scss">
.task-remark-tooltip {
  max-width: 360px !important;
  line-height: 1.6 !important;
  word-break: break-word !important;
  white-space: pre-wrap !important;
}
</style>
