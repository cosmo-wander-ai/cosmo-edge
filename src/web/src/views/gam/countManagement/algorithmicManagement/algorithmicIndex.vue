<template>
  <div class="task-page ui-admin-page">
    <div class="task-page-heading">
      <h1>{{ t('nav.sceneTasks') }}</h1>
      <el-button type="primary" @click="addAlgorithmic">{{ t('action.createTask') }}</el-button>
    </div>
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="80" @search="searchList" />
    <!-- 工具栏 -->
    <div class="task-toolbar">
      <div class="toolbar-left">
        <span class="task-count">{{ t('common.totalTasks', { n: pageData.total }) }}</span>
      </div>
      <div class="toolbar-right">
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

    <div class="task-list" tabindex="0" :aria-label="t('nav.sceneTasks')">
      <table class="task-table">
        <colgroup>
          <col class="select-column" />
          <col class="identity-column" />
          <col class="usage-column" />
          <col class="category-column" />
          <col class="channels-column" />
          <col class="updated-column" />
          <col class="actions-column" />
        </colgroup>
        <thead>
          <tr>
            <th scope="col" class="task-select-cell">
              <el-checkbox v-model="selectAll" @change="toggleSelectAll" :indeterminate="isIndeterminate" :aria-label="t('action.selectAll')" />
            </th>
            <th scope="col">{{ t('field.taskName') }}</th>
            <th scope="col">{{ t('glossary.analysisUsage') }}</th>
            <th scope="col">{{ t('field.taskType') }}</th>
            <th scope="col">{{ t('glossary.taskChannelSummary') }}</th>
            <th scope="col">{{ t('field.updateTime') }}</th>
            <th scope="col" class="task-actions-cell">{{ t('field.actions') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in tableData" :key="row.algorithmId" :class="{ selected: isCardSelected(row) }">
            <td class="task-select-cell">
              <el-checkbox :model-value="isCardSelected(row)" :aria-label="resolveResourceAlgorithmName(row)" @change="toggleCardSelect(row)" />
            </td>
            <td class="task-identity">
              <div class="task-identity-content">
                <SceneTaskIcon class="task-category-icon" :task="row" />
                <el-tooltip :content="getTaskIdentityDetails(row)" placement="top-start" :show-after="400" popper-class="task-remark-tooltip">
                  <div class="task-identity-text" tabindex="0">
                    <div class="task-name">{{ resolveResourceAlgorithmName(row) }}</div>
                    <div class="task-meta">ID: {{ row.algorithmId }}</div>
                  </div>
                </el-tooltip>
              </div>
            </td>
            <td>{{ getUsageLabel(row) }}</td>
            <td>{{ returnCategoryName(row) || '—' }}</td>
            <td class="task-channel-cell">
              <el-tooltip :content="getChannelDetails(row)" placement="top" :show-after="400" popper-class="task-remark-tooltip">
                <div class="task-channel-summary" tabindex="0">
                  <span class="channel-total">{{ getChannelTotalLabel(row) }}</span>
                  <div v-if="channelSummary.available && (channelSummary.complete || taskDisplay[row.algorithmId].channels.total)" class="channel-states">
                    <span v-for="state in getVisibleChannelStates(row)" :key="state" class="channel-state" :class="state">
                      {{ t(`glossary.taskChannel${state[0].toUpperCase() + state.slice(1)}`) }} {{ getChannelStateCount(row, state) }}
                    </span>
                    <span v-if="taskDisplay[row.algorithmId].channels.unknown" class="channel-state unknown">{{ t('glossary.taskChannelUnknown') }} {{ getChannelStateCount(row, 'unknown') }}</span>
                  </div>
                </div>
              </el-tooltip>
            </td>
            <td class="task-updated" :title="row.releaseTime || ''">
              <span>{{ getUpdatedParts(row).date }}</span>
              <span class="task-meta">{{ getUpdatedParts(row).time }}</span>
            </td>
            <td class="task-actions-cell">
              <div class="task-actions">
                <el-button link size="small" @click.stop="arrangeDetailClick(row)" class="ui-action ui-action-flow">{{ t('action.arrangeAlgorithm') }}</el-button>
                <el-button link size="small" v-if="row.supplier != 'HJ'" @click.stop="editClick(row)" class="ui-action ui-action-edit">{{ t('action.edit') }}</el-button>
                <el-button link size="small" v-if="row.supplier != 'HJ'" @click.stop="deleteClick(row)" class="ui-action ui-action-delete">
                  {{ t('action.delete') }}
                </el-button>
              </div>
            </td>
          </tr>
          <tr v-if="!tableData.length" class="task-empty-row">
            <td colspan="7">{{ t('common.noData') }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 分页 -->
    <div class="pagination-container">
      <el-pagination ref="paginationRef" v-model:current-page="pageData.pageNum" v-model:page-size="pageData.pageSize" :page-sizes="[12, 24, 48]" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper" @size-change="handleSizeChange" @current-change="handleCurrentChange" />
    </div>

    <!-- 删除确认 -->
    <el-dialog :title="t('common.notice')" v-model="deleteDialogVisible" width="350px" center @close="batchDeleteFalg = false" class="ui-admin-dialog">
      <div class="tips">{{ t('validate.deleteTaskWarning') }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" size="small" @click="sureDeleteAlgorithmic">{{ t('action.ok') }}</el-button>
          <el-button size="small" @click="deleteDialogVisible = false">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <el-dialog :title="algorithmDialogTitle" v-model="addAlgorithmicVisible" width="500px" center @close="addAlgorithmiClosed" class="ui-admin-dialog">
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
    <el-dialog :title="t('action.importTask')" v-model="uploadAlgorithmicVisible" width="400px" center :close-on-click-modal="!uploadAlgorithmicLoading" :close-on-press-escape="!uploadAlgorithmicLoading" :show-close="!uploadAlgorithmicLoading" @close="uploadAlgorithmicClosed" class="ui-admin-dialog">
      <div>
        <div class="upload-div">{{ t('validate.selectFile') }}{{ localeColon }}</div>
        <el-input v-model="uploadAlgorithmicName" class="upload-input" disabled :placeholder="t('validate.selectFile')" size="small"></el-input>
        <el-upload ref="uploadAlgorithmicRef" class="upload-btn" action="#" :show-file-list="false" :file-list="uploadAlgorithmicData" :auto-upload="false" :disabled="uploadAlgorithmicLoading" accept=".tar.gz,.tgz" :http-request="uploadFile" :on-change="handleChange">
          <el-button size="small">{{ t('action.browse') }}</el-button>
        </el-upload>
        <div class="upload-warn">*{{ t('validate.selectTarGzMax') }}</div>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" size="small" :loading="uploadAlgorithmicLoading" :disabled="uploadAlgorithmicLoading" @click="sureUploadAlgorithmic">{{ t('action.ok') }}</el-button>
          <el-button size="small" :disabled="uploadAlgorithmicLoading" @click="uploadAlgorithmicVisible = false">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script>
import TopBar from '@/components/TopBar.vue'
import { ArrowDown } from '@element-plus/icons-vue'
import moment from 'moment'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmRemark, resolveResourceAlgorithmName } from '@/utils/i18nResource'
import SceneTaskIcon from '@/components/SceneTaskIcon.vue'
import { emptyTaskChannelSummary, summarizeTaskChannels } from '@/utils/sceneTaskDisplay'
import {
  uploadFileInChunks,
  UploadPurpose
} from '@/utils/chunkUpload'

export default {
  components: {
    TopBar,
    ArrowDown,
    SceneTaskIcon
  },
  setup() {
    return { t, localeColon, resolveResourceAlgorithmRemark, resolveResourceAlgorithmName }
  },
  data() {
    return {
      tableData: [],
      // 分页
      pageData: {
        pageNum: 1,
        pageSize: 12,
        total: 0
      },
      deleteDialogVisible: false,
      deleteAlgorithmId: '', // 要删除的算法ID
      dialogFormBtn: false,
      value: '',
      batchDeleteIds: [],
      selectedCards: [],
      selectAll: false,
      channelSummary: { available: false, complete: false, byAlgorithm: {} },
      channelSummaryLoading: true,
      channelSummaryRequest: 0,
      channelStateKeys: ['running', 'paused', 'error', 'stopped'],
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
        algorithmUsage: [
          { required: true, message: t('validate.selectDataSourceType'), trigger: 'change' }
        ],
        algorithmCategory: [
          { required: true, message: t('validate.selectTaskType'), trigger: 'change' }
        ]
      },
      uploadAlgorithmicName: '',
      uploadAlgorithmicData: [],
      uploadAlgorithmicLoading: false,
      batchDeleteFalg: false
    }
  },
  computed: {
    taskDisplay() {
      return Object.fromEntries(this.tableData.map(row => [row.algorithmId, {
        channels: this.channelSummary.byAlgorithm[row.algorithmId] || emptyTaskChannelSummary()
      }]))
    },
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
        }
      ]
      return { formList: list }
    },
    algorithmCategoryOptions() {
      return [
        { label: t('glossary.faceAndBody'), value: '1', eventType: 'face' },
        { label: t('glossary.detection'), value: '2', eventType: 'behavior' },
        { label: t('glossary.detection'), value: '3', eventType: 'motorCommodity' },
        { label: t('glossary.countingAnalytics'), value: '8', eventType: 'passengerNumber' },
        { label: t('glossary.countingAnalytics'), value: '9', eventType: 'peopleCountByArea' },
        { label: t('glossary.vehicleAnalysis'), value: '10', eventType: 'motorCommodity' },
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
    this.init()
  },
  methods: {
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
        engineTypeList: []
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
      })
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
    arrangeDetailClick(obj) {
      this.$router.push({
        path: '/gam/arrangeDetail',
        query: {
          resetUrl: this.$route.path,
          algorithmId: obj.algorithmId,
          supplier: obj.supplier,
          algorithmUsage: obj.algorithmUsage
        }
      })
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
    // // 关闭
    // close() {
    //   this.dialogFormBtn = false
    // },
    // // 添加
    // Btnredact() {}
    searchList() {
      this.pageData.pageNum = 1
      this.init()
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
      this.$API
        .boxDeleteAlgorithmLayout({ algorithmId: this.deleteAlgorithmId })
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
            remark: this.addAlgorithmicForm.remark
          }
          params.algorithmCategory = Number(params.algorithmCategory)
          params.checkType = Number(params.checkType)
          if (this.algorithmDialogMode === 'create') {
            this.$API.addAlgorithmLayout(params).then((res) => {
              this.$message.success(t('common.operationSucceeded'))
              this.addAlgorithmicVisible = false
              this.searchList()
            })
          } else {
            params.algorithmId = this.addAlgorithmicForm.algorithmId
            this.$API.boxUpdateAlgorithmLayout(params).then((res) => {
              this.$message.success(t('common.operationSucceeded'))
              this.addAlgorithmicVisible = false
              this.searchList()
            })
          }
        }
      })
    },
    uploadAlgorithmic() {
      this.uploadAlgorithmicVisible = true
    },
    sureUploadAlgorithmic() {
      if (this.uploadAlgorithmicLoading) return
      if (this.uploadAlgorithmicData.length === 0)
        return this.$message.warning(t('validate.uploadFileFirst'))
      this.$refs.uploadAlgorithmicRef.submit()
    },
    async uploadFile(params) {
      if (!Number.isSafeInteger(params.file.size) || params.file.size <= 0) {
        this.$message.error(t('api.error.UpLoadDataEmpty'))
        throw new RangeError(t('api.error.UpLoadDataEmpty'))
      }
      this.uploadAlgorithmicLoading = true
      let stagedUpload
      try {
        stagedUpload = await uploadFileInChunks(params.file, {
          purpose: UploadPurpose.ALGORITHM,
          uploadChunk: formData => this.$API.uploadAtomicModelTemp(formData),
          cancelUpload: data => this.$API.cancelAtomicModelUpload(data),
          getCapabilities: () => this.$API.getUploadCapabilities(),
          onProgress: progress => {
            if (typeof params.onProgress === 'function') {
              params.onProgress({ percent: progress.percent })
            }
          }
        })
        if (!stagedUpload.uploadId) {
          throw new Error(t('validate.missingUploadId'))
        }
        await this.$API.boxAlgorithmUpload({
          uploadId: stagedUpload.uploadId
        })
        this.$message.success(t('common.operationSucceeded'))
        this.uploadAlgorithmicVisible = false
        this.searchList()
      } catch (error) {
        if (stagedUpload?.uploadId) {
          try {
            await this.$API.cancelAtomicModelUpload({ uploadId: stagedUpload.uploadId })
          } catch (_) {
            // The staging service also expires abandoned sessions by TTL.
          }
        }
        throw error
      } finally {
        this.uploadAlgorithmicLoading = false
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
    getTaskIdentityDetails(row) {
      return [resolveResourceAlgorithmName(row), `ID: ${row.algorithmId}`,
        row.confVersionName && `${t('glossary.taskConfiguration')}${localeColon.value} ${row.confVersionName}`,
        resolveResourceAlgorithmRemark(row)].filter(Boolean).join('\n')
    },
    getChannelTotalLabel(row) {
      if (this.channelSummaryLoading) return '…'
      if (!this.channelSummary.available) return t('glossary.taskChannelUnknown')
      const summary = this.taskDisplay[row.algorithmId].channels
      if (!this.channelSummary.complete && !summary.total) return t('glossary.taskChannelUnknown')
      return `${this.channelSummary.complete ? '' : '≥'}${t('home.channelUnit', { n: summary.total })}`
    },
    getChannelStateCount(row, state) {
      const count = this.taskDisplay[row.algorithmId].channels[state]
      return this.channelSummary.complete ? count : count ? `≥${count}` : '—'
    },
    getVisibleChannelStates(row) {
      return this.channelStateKeys.filter(state => this.taskDisplay[row.algorithmId].channels[state] > 0)
    },
    getChannelDetails(row) {
      if (this.channelSummaryLoading) return t('glossary.taskChannelLoading')
      if (!this.channelSummary.available) return t('glossary.taskChannelUnavailable')
      const labels = {
        running: t('glossary.taskChannelRunning'), paused: t('glossary.taskChannelPaused'),
        error: t('glossary.taskChannelError'), stopped: t('glossary.taskChannelStopped'),
        unknown: t('glossary.taskChannelUnknown')
      }
      const summary = this.taskDisplay[row.algorithmId].channels
      return [
        `${t('glossary.taskChannelSummary')}${localeColon.value} ${this.getChannelTotalLabel(row)}`,
        ...Object.entries(labels).filter(([state]) => state !== 'unknown' || summary.unknown).map(([state, label]) => `${label} ${this.getChannelStateCount(row, state)}`),
        !this.channelSummary.complete && t('glossary.taskChannelPartial')
      ].filter(Boolean).join('\n')
    },
    getUpdatedParts(row) {
      const value = String(row.releaseTime || '').trim()
      const match = value.match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2}:\d{2})$/)
      return match ? { date: match[1], time: match[2] } : { date: value || '—', time: '' }
    },
    // Display the actual task state; loading or partial lists must not imply zero.
    loadChannelCounts() {
      const request = ++this.channelSummaryRequest
      this.channelSummaryLoading = true
      this.channelSummary = { available: false, complete: false, byAlgorithm: {} }
      this.$API.getChannelList({ pageNum: 1, pageSize: 500 }).then(res => {
        if (request !== this.channelSummaryRequest) return
        this.channelSummary = summarizeTaskChannels(res?.resData)
      }).catch(() => {
        if (request === this.channelSummaryRequest) this.channelSummary = { available: false, complete: false, byAlgorithm: {} }
      }).finally(() => {
        if (request === this.channelSummaryRequest) this.channelSummaryLoading = false
      })
    }
  }
}
</script>

<style scoped lang="scss">
.task-page {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  overflow: hidden;
}
.task-page.ui-admin-page {
  .task-page-heading {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    flex-shrink: 0;
    gap: 12px;
    margin-bottom: 14px;

    h1 { margin: 0; font-size: 24px; line-height: 32px; font-weight: 650; letter-spacing: -0.4px; }
  }
  .task-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 8px 16px;
    padding: 0 0 14px;
    flex-shrink: 0;
  }
  .toolbar-left, .toolbar-right { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; }
  .toolbar-right .el-button + .el-button { margin-left: 0; }
  .task-count { color: var(--text-secondary); font-size: 13px; background: transparent; }
  .task-list {
    flex: 1;
    min-height: 0;
    overflow: auto;
    container-type: inline-size;
    border: 1px solid var(--border-color);
    border-radius: 7px;
    background: var(--bg-panel);
    scrollbar-gutter: stable;

    &:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; }
  }
  .task-table {
    width: 100%;
    min-width: 1000px;
    border-collapse: separate;
    border-spacing: 0;
    table-layout: fixed;
    color: var(--text-secondary);
    font-size: 13px;
    line-height: 20px;

    // Share the remaining width across content columns instead of stretching only the name.
    --task-content-width: calc(max(1000px, 100cqi) - 292px);
    .select-column { width: 40px; }
    .identity-column { width: calc(var(--task-content-width) * 0.35); }
    .usage-column { width: calc(var(--task-content-width) * 0.14); }
    .category-column { width: calc(var(--task-content-width) * 0.12); }
    .channels-column { width: calc(var(--task-content-width) * 0.18); }
    .updated-column { width: calc(var(--task-content-width) * 0.21); }
    .actions-column { width: 252px; }

    th, td { padding: 7px 14px; text-align: left; overflow-wrap: normal; }
    th {
      position: sticky;
      top: 0;
      z-index: 2;
      padding-block: 11px;
      background: var(--table-header-bg);
      border-bottom: 1px solid var(--border-color);
      color: var(--text-secondary);
      font-weight: 500;
    }
    td { height: 54px; box-sizing: border-box; border-bottom: 1px solid var(--border-light); background: var(--bg-panel); }
    tr:last-child td { border-bottom: 0; }
    tbody tr:hover td { background: var(--table-row-hover-bg); }
    tbody tr.selected td { background: var(--el-color-primary-light-9); }
    .task-select-cell { padding-inline: 14px 8px; }
    .task-actions-cell {
      position: sticky;
      right: 0;
      z-index: 1;
      border-left: 1px solid var(--border-light);
    }
    th.task-actions-cell { z-index: 3; }
  }
  .task-select-cell :deep(.el-checkbox) { height: 24px; }
  .task-identity-content { display: flex; align-items: center; gap: 9px; min-width: 0; }
  .task-category-icon { width: 20px; height: 20px; flex: 0 0 20px; color: var(--text-secondary); }
  .task-identity-text { min-width: 0; }
  .task-identity-text:focus-visible, .task-channel-summary:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; border-radius: 2px; }
  .task-name {
    color: var(--text-primary);
    font-size: 14px;
    font-weight: 600;
    line-height: 20px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .task-meta { display: block; min-width: 0; color: var(--text-secondary); font-size: 12px; line-height: 18px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-variant-numeric: tabular-nums; }
  .task-updated {
    font-size: 12px;
    font-variant-numeric: tabular-nums;
    > span { white-space: nowrap; }
    .task-meta { display: inline; margin-left: 6px; }
  }
  .task-channel-summary { display: flex; flex-direction: column; align-items: flex-start; gap: 0; font-variant-numeric: tabular-nums; }
  .channel-total { flex: 0 0 auto; font-weight: 500; color: var(--text-primary); white-space: nowrap; }
  .channel-states { display: flex; flex-wrap: wrap; align-items: center; gap: 0 6px; min-width: 0; font-size: 12px; line-height: 18px; }
  .channel-state {
    white-space: nowrap;
    + .channel-state::before { content: '·'; color: var(--text-tertiary, var(--text-secondary)); margin-right: 6px; }
    &.running { color: var(--success-color); }
    &.paused { color: var(--warning-color); }
    &.error { color: var(--danger-color); }
    &.stopped, &.unknown, &.empty { color: var(--text-secondary); }
  }
  .task-actions { display: flex; flex-wrap: wrap; align-items: center; gap: 2px 12px; }
  .task-actions .el-button.ui-action { font-size: 12px; }
  .task-table .task-empty-row td { height: 180px; text-align: center; color: var(--text-secondary); }
  .pagination-container { display: flex; justify-content: flex-end; padding: 12px 0 0; flex-shrink: 0; overflow-x: auto; }

  @container (max-width: 1120px) {
    .task-updated .task-meta { display: block; margin-left: 0; }
  }

  @media (max-width: 768px) {
    .task-page-heading { margin-bottom: 14px; }
    .task-table .task-actions-cell { position: static; }
  }
}
.tips { text-align: center; padding: 20px 0; }
.dialog-footer { display: flex; justify-content: center; }
.input-width { width: calc(100% - 80px); }
.upload-div { margin-left: 20px; margin-bottom: 10px; }
.upload-input { width: calc(100% - 100px); margin-left: 20px; margin-right: 10px; }
.upload-warn { margin-top: 5px; margin-left: 20px; color: var(--primary-color); font-size: 12px; }
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
