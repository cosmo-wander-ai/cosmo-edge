<template>
  <div class="model-repo-page">
    <!-- 页面头部：搜索 + 操作按钮 -->
    <div class="repo-header">
      <div class="search-box">
        <el-input
          v-model="searchKeyword"
          :placeholder="t('glossary.searchModelPlaceholder')"
          :prefix-icon="SearchIcon"
          clearable
          @input="debouncedSearch"
        />
      </div>
      <div class="header-actions">
        <el-button type="primary" class="btn-primary-gradient" @click="addClick">
          <el-icon><Plus /></el-icon> {{ t('action.addModel') }}
        </el-button>
        <el-button v-if="platformType === '15'" @click="importModelClick">
          <el-icon><Upload /></el-icon> {{ t('action.importModel') }}
        </el-button>
        <el-button v-if="platformType !== '15'" @click="batchUpdateClick">{{ t('action.batchUpdate') }}</el-button>
      </div>
    </div>

    <!-- 类型筛选 Tab -->
    <div class="type-tabs">
      <button
        v-for="tab in modelTypeTabs"
        :key="tab.value"
        class="type-tab"
        :class="{ active: activeTypeTab === tab.value }"
        @click="switchTypeTab(tab.value)"
      >
        {{ tab.label }}
        <span class="tab-count">{{ tab.count }}</span>
      </button>
    </div>

    <!-- 卡片网格 -->
    <div class="model-grid">
      <div
        v-for="model in filteredModels"
        :key="model.modelCode"
        class="model-card"
      >
        <div class="card-top">
          <div class="card-icon" :class="getModelIconClass(model)" v-html="getModelSvg(model)"></div>
        </div>
        <div class="card-info">
          <div class="card-title">{{ model.modelName }}</div>
          <div class="card-meta">ID: {{ model.modelCode }} · {{ model.version || '—' }}</div>
        </div>
        <div class="card-tags">
          <span class="model-type-tag" :class="getModelTypeColor(model)">{{ getModelTypeLabel(model) }}</span>
        </div>
        <div class="card-footer">
          <span class="alg-count">{{ t('glossary.linkedSceneTasks', { n: getModelAlgCount(model.modelCode) }) }}</span>
          <div class="card-actions">
            <el-button link size="small" @click="detailClick(model)">{{ t('action.details') }}</el-button>
            <el-button link size="small" @click="editConfigClick(model)">{{ t('action.configure') }}</el-button>
          </div>
        </div>
      </div>
    </div>

    <!-- 分页（仅"全部"Tab时显示） -->
    <div class="pagination-container" v-show="!activeTypeTab">
      <el-pagination
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        :current-page="pageData.pageNum"
        :page-sizes="[12, 24, 48]"
        :page-size="pageData.pageSize"
        :total="pageData.total"
        layout="total, sizes, prev, pager, next, jumper"
      />
    </div>

    <!-- 添加模型对话框 -->
    <el-dialog :title="addModelDialogTitle" v-model="uploadAlgorithmicVisible" width="500px" center @close="uploadAlgorithmicClosed">
      <el-form ref="addModelFormRef" :model="addModelForm" :rules="addModelRules" :label-width="currentLocale === 'en-US' ? '180px' : '120px'" size="small">
        <el-form-item :label="t('glossary.mainType')" prop="modelMainType">
          <el-select v-model="addModelForm.modelMainType" :placeholder="t('validate.selectMainType')" class="form-content" @change="handleModelMainTypeChange">
            <el-option v-for="group in modelTypeGroups" :key="group.value" :label="group.label" :value="group.value"></el-option>
          </el-select>
        </el-form-item>
        <el-form-item :label="t('glossary.subType')" prop="modelType">
          <el-select v-model="addModelForm.modelType" :placeholder="t('validate.selectSubType')" class="form-content" :disabled="!addModelForm.modelMainType" @change="handleModelTypeChange">
            <el-option v-for="item in currentSubModelTypes" :key="item.value" :label="item.label" :value="item.value"></el-option>
          </el-select>
        </el-form-item>
        <el-form-item :label="t('field.modelName')" prop="modelName">
          <el-input v-model="addModelForm.modelName" class="form-content" :placeholder="t('validate.enterModelName')"></el-input>
        </el-form-item>
        <el-form-item :label="t('glossary.modelDesc')" prop="description">
          <el-input v-model="addModelForm.description" class="form-content" type="textarea" :rows="3" :placeholder="t('validate.enterModelDesc')"></el-input>
        </el-form-item>
        <el-form-item v-if="showPreprocessOptions" :label="t('glossary.normalization')" prop="normalizationMode">
          <el-select v-model="addModelForm.normalizationMode" :placeholder="t('validate.selectNormalization')" class="form-content">
            <el-option label="0-1" value="0-1"></el-option>
            <el-option label="-1-1" value="-1-1"></el-option>
            <el-option :label="t('glossary.normNone')" value="none"></el-option>
          </el-select>
        </el-form-item>
        <el-form-item v-if="showPreprocessOptions" :label="t('glossary.colorChannel')" prop="colorChannel">
          <el-select v-model="addModelForm.colorChannel" :placeholder="t('validate.selectColorChannel')" class="form-content">
            <el-option label="RGB" value="rgb"></el-option>
            <el-option label="BGR" value="bgr"></el-option>
          </el-select>
        </el-form-item>
        <!-- 非SAM2类型：单个模型文件上传 -->
        <el-form-item v-if="addModelForm.modelType !== 'sam2'" :label="t('glossary.modelFile')" prop="modelFile">
          <el-upload ref="uploadModelFileRef" action="#" :file-list="addModelForm.modelFileList" :limit="1" :auto-upload="false" :accept="isX86 ? '.onnx' : '.bmodel'" :on-change="handleModelFileChange" :on-remove="handleModelFileRemove">
            <el-button size="small" type="primary">{{ t('action.browse') }}</el-button>
            <template #tip>
              <div class="upload-warn">{{ t('glossary.selectModelFileTip', { ext: isX86 ? '.onnx' : '.bmodel' }) }}</div>
            </template>
          </el-upload>
        </el-form-item>
        <!-- SAM2类型：两个模型文件上传 -->
        <el-form-item v-if="addModelForm.modelType === 'sam2'" label="Encoder" prop="encoderFile">
          <el-upload ref="uploadEncoderFileRef" action="#" :file-list="addModelForm.encoderFileList" :limit="1" :auto-upload="false" :accept="isX86 ? '.onnx' : '.bmodel'" :on-change="handleEncoderFileChange" :on-remove="handleEncoderFileRemove">
            <el-button size="small" type="primary">{{ t('action.browse') }}</el-button>
            <template #tip>
              <div class="upload-warn">{{ t('glossary.selectEncoderTip', { ext: isX86 ? '.onnx' : '.bmodel' }) }}</div>
            </template>
          </el-upload>
        </el-form-item>
        <el-form-item v-if="addModelForm.modelType === 'sam2'" label="Decoder" prop="decoderFile">
          <el-upload ref="uploadDecoderFileRef" action="#" :file-list="addModelForm.decoderFileList" :limit="1" :auto-upload="false" :accept="isX86 ? '.onnx' : '.bmodel'" :on-change="handleDecoderFileChange" :on-remove="handleDecoderFileRemove">
            <el-button size="small" type="primary">{{ t('action.browse') }}</el-button>
            <template #tip>
              <div class="upload-warn">{{ t('glossary.selectDecoderTip', { ext: isX86 ? '.onnx' : '.bmodel' }) }}</div>
            </template>
          </el-upload>
        </el-form-item>
        <!-- dino 模型：vocab.txt -->
        <el-form-item v-if="addModelForm.modelType === 'dino'" label="vocab.txt" prop="vocabFile">
          <el-upload ref="uploadVocabFileRef" action="#" :file-list="addModelForm.vocabFileList" :limit="1" :auto-upload="false" accept=".txt" :on-change="handleVocabFileChange" :on-remove="handleVocabFileRemove">
            <el-button size="small" type="primary">{{ t('action.browse') }}</el-button>
            <template #tip>
              <div class="upload-warn">{{ t('glossary.vocabTip') }}</div>
            </template>
          </el-upload>
        </el-form-item>
        <!-- qwen3vl/qwen3_5 模型：tokenizer.json -->
        <el-form-item v-if="addModelForm.modelType === 'qwen3vl' || addModelForm.modelType === 'qwen3_5'" label="tokenizer.json" prop="tokenizerFile">
          <el-upload ref="uploadTokenizerFileRef" action="#" :file-list="addModelForm.tokenizerFileList" :limit="1" :auto-upload="false" accept=".json" :on-change="handleTokenizerFileChange" :on-remove="handleTokenizerFileRemove">
            <el-button size="small" type="primary">{{ t('action.browse') }}</el-button>
            <template #tip>
              <div class="upload-warn">{{ t('glossary.tokenizerTip') }}</div>
            </template>
          </el-upload>
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" size="small" @click="sureAddModel" :loading="addModelLoading">{{ t('action.confirm') }}</el-button>
          <el-button size="small" @click="uploadAlgorithmicVisible = false">{{ t('action.cancel') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <el-dialog :title="t('glossary.algorithmList')" v-model="listDialogVisible" center width="400px" @close="listDialogVisible = false">
      <div v-if="algorithmicList.length !==0" class="div-center">
        <span v-for="(item, index) in algorithmicList" :key="index">
          <span>{{ item }}</span>
          <span v-if="index !== algorithmicList.length-1">，</span>
        </span>
      </div>
      <div v-else class="div-center">{{ t('common.noData') }}</div>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" size="small" @click="listDialogVisible = false">{{ t('action.close') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <el-dialog :title="t('glossary.modelDetail')" v-model="modelDetailDialogVisible" width="620px" @close="modelDetailDialogVisible = false">
      <div class="detail-header">
        <div class="detail-icon" :class="getModelIconClass(detailModel)" v-html="getModelSvg(detailModel)"></div>
        <div class="detail-title-area">
          <h3>{{ detailModel.modelName }}</h3>
          <div class="detail-badges">
            <span class="model-type-tag" :class="getModelTypeColor(detailModel)">{{ getModelTypeLabel(detailModel) }}</span>
            <span class="alg-count-badge">{{ t('glossary.linkedSceneTasks', { n: getModelAlgCount(detailModel.modelCode) }) }}</span>
          </div>
        </div>
      </div>

      <div class="detail-section">
        <div class="detail-row"><span class="detail-label">{{ t('field.modelId') }}</span><span>{{ detailModel.modelCode }}</span></div>
        <div class="detail-row"><span class="detail-label">{{ t('glossary.versionNo') }}</span><span>{{ detailModel.version }}</span></div>
        <div v-if="platformType !== '15'" class="detail-row"><span class="detail-label">{{ t('glossary.computeType') }}</span><span>{{ returnLabelWithCode(detailModel.gpuCode) }}</span></div>
        <div class="detail-row"><span class="detail-label">{{ t('glossary.modelDesc') }}</span><span>{{ detailModel.description || '—' }}</span></div>
        <div class="detail-row"><span class="detail-label">{{ t('field.updateTime') }}</span><span>{{ detailModel.updateTime || '—' }}</span></div>
      </div>

      <el-collapse class="detail-collapse">
        <el-collapse-item :title="t('glossary.techParams')">
          <div class="detail-row"><span class="detail-label">{{ t('glossary.inputCount') }}</span><span>{{ detailModel.inputCount || 1 }}</span></div>
          <div class="detail-row"><span class="detail-label">{{ t('glossary.inputDim') }}</span><span>{{ detailModel.inputDim || '—' }}</span></div>
          <div class="detail-row"><span class="detail-label">{{ t('glossary.outputCount') }}</span><span>{{ detailModel.outputCount || 1 }}</span></div>
          <div class="detail-row"><span class="detail-label">{{ t('glossary.outputDim') }}</span><span>{{ detailModel.outputDim || '—' }}</span></div>
        </el-collapse-item>
      </el-collapse>

      <div class="detail-section" v-if="modelLabelData.length > 0">
        <h4 class="section-title">{{ t('glossary.algorithmLabels') }}</h4>
        <el-table :data="modelLabelData" size="small" border>
          <el-table-column v-if="platformType !== '15'" prop="nameCN" :label="t('field.name')" />
          <el-table-column prop="class_name" :label="t('glossary.classId')" />
          <el-table-column prop="threshold" :label="t('field.threshold')">
            <template #default="scope">
              <span v-if="scope.row.threshold">{{ Array.isArray(scope.row.threshold) ? scope.row.threshold.join('，') : scope.row.threshold }}</span>
            </template>
          </el-table-column>
        </el-table>
      </div>

      <div class="detail-section" v-if="getModelUsageTasks(detailModel.modelCode).length > 0">
        <h4 class="section-title">{{ t('glossary.linkedTasks') }}</h4>
        <div v-for="task in getModelUsageTasks(detailModel.modelCode)" :key="task.algCode" class="usage-task-item">
          <span class="task-dot" :class="task.running ? 'running' : 'stopped'">●</span>
          <span class="task-name">{{ task.algName }}</span>
          <span class="task-status-text">{{ task.running ? t('status.running') : t('status.stopped') }}</span>
        </div>
      </div>

      <template #footer>
        <el-button v-if="!defaultModels.includes(detailModel.modelCode)" @click="editFromDetail">{{ t('action.editModel') }}</el-button>
        <el-button v-if="!defaultModels.includes(detailModel.modelCode)" type="danger" plain @click="deleteFromDetail">{{ t('action.delete') }}</el-button>
        <el-button type="primary" @click="modelDetailDialogVisible = false">{{ t('action.close') }}</el-button>
      </template>
    </el-dialog>

    <el-dialog :title="t('glossary.uploadResult')" v-model="uploadReultVisible" center width="600">
      <div>
        <el-table :data="uploadResult" style="width: 100%" stripe border>
          <el-table-column type="index"></el-table-column>
          <el-table-column prop="fileName" :label="t('field.fileName')">
          </el-table-column>
          <el-table-column prop="message" :label="t('field.result')">
            <template #default="scope">
              <span v-if="scope.row.success" style="color:green;">{{ scope.row.message }}</span>
              <span v-else style="color:red;">{{ scope.row.message }}</span>
            </template>
          </el-table-column>
        </el-table>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" size="small" @click="uploadReultVisible = false">{{ t('action.close') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <!-- 修改模型对话框 -->
    <el-dialog :title="editModelDialogTitle" v-model="editModelDialogVisible" width="500px" center @close="editModelDialogVisible = false">
      <el-form ref="editModelFormRef" :model="editModelForm" :rules="editModelRules" :label-width="currentLocale === 'en-US' ? '180px' : '120px'" size="small">
        <el-form-item label="model code" prop="modelCode">
          <el-input v-model="editModelForm.modelCode" class="form-content" disabled></el-input>
        </el-form-item>
        <el-form-item :label="t('field.modelName')" prop="modelName">
          <el-input v-model="editModelForm.modelName" class="form-content" :placeholder="t('validate.enterModelName')"></el-input>
        </el-form-item>
        <el-form-item :label="t('glossary.modelDesc')" prop="description">
          <el-input v-model="editModelForm.description" class="form-content" type="textarea" :rows="3" :placeholder="t('validate.enterModelDesc')"></el-input>
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" size="small" @click="sureEditModel" :loading="editModelLoading">{{ t('action.confirm') }}</el-button>
          <el-button size="small" @click="editModelDialogVisible = false">{{ t('action.cancel') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <!-- 删除模型确认对话框 -->
    <el-dialog :title="t('glossary.deleteConfirmTitle')" v-model="deleteModelDialogVisible" width="500px" center @close="deleteModelDialogVisible = false">
      <div style="text-align: center; padding: 20px 0;">
        <p style="font-size: 16px; margin-bottom: 10px;">{{ t('glossary.deleteModelConfirm', { name: deleteModelInfo.modelName, code: deleteModelInfo.modelCode }) }}</p>
        <p style="color: #f56c6c; font-size: 14px;">{{ t('glossary.deleteModelWarn') }}</p>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="danger" size="small" @click="sureDeleteModel" :loading="deleteModelLoading">{{ t('action.confirmDelete') }}</el-button>
          <el-button size="small" @click="deleteModelDialogVisible = false">{{ t('action.cancel') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <!-- 导入模型对话框 -->
    <el-dialog :title="t('action.importModel')" v-model="importModelDialogVisible" width="500px" center @close="importModelDialogClosed">
      <div>
        <div class="upload-div">{{ t('glossary.selectModelPackage') }}</div>
        <div style="display: flex; align-items: center; padding: 0 20px;">
          <el-input v-model="importModelFileName" disabled :placeholder="t('validate.selectFile')" size="small" style="flex: 1; margin-right: 10px;" />
          <el-upload ref="importModelUploadRef" action="#" :show-file-list="false" :auto-upload="false" accept=".tar.gz,.tgz,.zip" :on-change="handleImportFileChange">
            <el-button size="small">{{ t('action.browse') }}</el-button>
          </el-upload>
        </div>
        <div class="upload-warn" style="margin-left: 20px;">{{ t('glossary.importModelTip', { modelFile: isX86 ? 'model.onnx' : 'model.nn' }) }}</div>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" size="small" @click="sureImportModel" :loading="importModelLoading">{{ t('action.confirm') }}</el-button>
          <el-button size="small" @click="importModelDialogVisible = false">{{ t('action.cancel') }}</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import {
  ref,
  computed,
  reactive,
  onMounted,
  nextTick,
  getCurrentInstance
} from 'vue'
import { useRouter } from 'vue-router'
import { Search, Plus, Upload } from '@element-plus/icons-vue'
import { t, localeColon, currentLocale } from '@/i18n'

const SearchIcon = Search

const { proxy } = getCurrentInstance()
const router = useRouter()

// ── 搜索与筛选 ──
const searchKeyword = ref('')
let searchTimer = null
const debouncedSearch = () => {
  if (searchTimer) clearTimeout(searchTimer)
  searchTimer = setTimeout(() => {
    formData.modelName = searchKeyword.value.trim()
    formData.modelCode = ''
    pageData.pageNum = 1
    init()
  }, 300)
}

const activeTypeTab = ref('')
const switchTypeTab = (val) => {
  activeTypeTab.value = val
}

// ── 模型类型映射 ──
// 通过对每个子类型调用 atomicModelList 来建立可靠映射
const modelTypeMap = ref({})       // modelCode → subType (e.g. 'classify', 'yolov8_det')
const modelMainTypeMap = ref({})   // modelCode → mainType (e.g. 'detect', 'classify')
const allModelsByType = ref({})    // mainType → [model, model, ...]
const allModelsCount = ref(0)

// 子类型 → 主类型的映射表
const subTypeToMain = {
  yolov5_det: 'detect', yolov8_det: 'detect', yolov9_det: 'detect',
  yolov11_det: 'detect', yolov12_det: 'detect', yolo26_det: 'detect',
  classify: 'classify', keypoints: 'keypoints', feature: 'feature',
  dino: 'foundation', sam2: 'foundation', qwen3vl: 'foundation', qwen3_5: 'foundation'
}

const loadModelTypes = () => {
  const subTypes = Object.keys(subTypeToMain)
  const filePath = ''
  const byType = { detect: [], classify: [], keypoints: [], feature: [], foundation: [] }
  const seen = new Set()

  const promises = subTypes.map(st =>
    proxy.$API.atomicModelList({ modelName: '', modelType: st, filePath }).then(res => {
      const list = res?.resData?.list || []
      const mainType = subTypeToMain[st]
      list.forEach(item => {
        const code = item.atomicCode || item.modelCode || ''
        if (!code || seen.has(code)) return
        seen.add(code)
        modelTypeMap.value[code] = st
        modelMainTypeMap.value[code] = mainType
        byType[mainType].push({
          ...item,
          modelCode: code,
          modelName: item.atomicName || item.modelName || '',
          _mainType: mainType,
          _subType: st
        })
      })
    }).catch(() => {})
  )

  Promise.all(promises).then(() => {
    allModelsByType.value = byType
    allModelsCount.value = seen.size
  })
}

// 获取模型的主类型
const getModelMainType = (model) => {
  // 优先用已建立的映射
  const mapped = modelMainTypeMap.value[model.modelCode]
  if (mapped) return mapped

  // fallback：名称推断
  const name = (model.modelName || '').toLowerCase()
  if (/dino/i.test(name)) return 'foundation'
  if (/sam2/i.test(name)) return 'foundation'
  if (/qwen/i.test(name)) return 'foundation'
  if (/yolo|det/i.test(name)) return 'detect'
  if (/classif/i.test(name)) return 'classify'
  if (/keypoint|pose/i.test(name)) return 'keypoints'
  if (/feature|embed/i.test(name)) return 'feature'
  return 'other'
}

// 展示数据：选中类型Tab时展示该类型全部模型，"全部"时展示分页数据
const filteredModels = computed(() => {
  if (!activeTypeTab.value) return tableData.value
  // 选中特定类型时，展示该类型全部模型（不分页）
  return allModelsByType.value[activeTypeTab.value] || []
})

// 类型 Tab 计数：使用按类型查询的精确结果
const modelTypeTabs = computed(() => {
  const byType = allModelsByType.value
  return [
    { label: t('common.all'), value: '', count: allModelsCount.value || pageData.total },
    { label: t('glossary.modelTypeDetect'), value: 'detect', count: (byType.detect || []).length },
    { label: t('glossary.modelTypeClassify'), value: 'classify', count: (byType.classify || []).length },
    { label: t('glossary.modelTypeKeypoints'), value: 'keypoints', count: (byType.keypoints || []).length },
    { label: t('glossary.modelTypeFeature'), value: 'feature', count: (byType.feature || []).length },
    { label: t('glossary.modelTypeFoundation'), value: 'foundation', count: (byType.foundation || []).length }
  ]
})

// ── 模型类型视觉映射 ──
const getModelTypeLabel = (model) => {
  return modelTypeMap.value[model?.modelCode] || model?._subType || getModelMainType(model) || 'unknown'
}
const getModelTypeColor = (model) => {
  const t = getModelMainType(model)
  const map = { detect: 'tag-detect', classify: 'tag-classify', keypoints: 'tag-keypoints', feature: 'tag-feature', foundation: 'tag-foundation' }
  return map[t] || 'tag-detect'
}
const getModelIconClass = (model) => {
  const t = getModelMainType(model)
  const map = { detect: 'icon-blue', classify: 'icon-green', keypoints: 'icon-cyan', feature: 'icon-purple', foundation: 'icon-gradient' }
  return map[t] || 'icon-blue'
}

// 按模型类型返回语义化 SVG 图标
const modelSvgMap = {
  // 检测：取景框/目标定位
  detect: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M3 7V3h4"/><path d="M17 3h4v4"/><path d="M21 17v4h-4"/><path d="M7 21H3v-4"/><rect x="7.5" y="7.5" width="9" height="9" rx="1.5"/><circle cx="12" cy="12" r="2"/></svg>',
  // 分类：四宫格/分类矩阵
  classify: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/></svg>',
  // 关键点：人体姿态骨架
  keypoints: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="4" r="2.5"/><path d="M12 6.5v4"/><path d="M12 10.5l-5 3.5"/><path d="M12 10.5l5 3.5"/><path d="M7 14l-2 6"/><path d="M17 14l2 6"/><circle cx="7" cy="14" r="1.2"/><circle cx="17" cy="14" r="1.2"/><circle cx="5" cy="20" r="1.2"/><circle cx="19" cy="20" r="1.2"/></svg>',
  // 特征提取：指纹/DNA 双螺旋
  feature: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v20"/><path d="M6 6c3 0 3 4 6 4s3-4 6-4"/><path d="M6 14c3 0 3 4 6 4s3-4 6-4"/><circle cx="6" cy="6" r="1"/><circle cx="18" cy="6" r="1"/><circle cx="6" cy="14" r="1"/><circle cx="18" cy="14" r="1"/></svg>',
  // 大模型：星光/AI 大脑
  foundation: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2l2 5 5 1-4 3.5 1 5.5-4-3-4 3 1-5.5L5 8l5-1 2-5z"/><circle cx="5" cy="18" r="2"/><circle cx="19" cy="18" r="2"/><path d="M7 18h10"/></svg>',
  // 默认：立方体
  other: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 16.5v-9l-9-5.25L3 7.5v9l9 5.25 9-5.25z"/><path d="M3.27 6.96L12 12.01l8.73-5.05M12 22.08V12"/></svg>'
}

const getModelSvg = (model) => {
  const t = getModelMainType(model)
  return modelSvgMap[t] || modelSvgMap.other
}

// ── 模型使用状态（通过 algorithmInquire 获取算法列表中的 models 字段关联） ──
const algList = ref([])
const modelUsageMap = ref({})
const loadAlgUsage = () => {
  const params = { pageNum: 1, pageSize: 999 }
  proxy.$API.algorithmInquire && proxy.$API.algorithmInquire(params).then(res => {
    const rows = res?.resData?.rows || []
    algList.value = rows
    const usageMap = {}
    rows.forEach(alg => {
      const models = alg.models || []
      const isRunning = alg.runningStatus && String(alg.runningStatus) !== '0'
      models.forEach(m => {
        const code = m.modelCode || m.atomicCode
        if (!code) return
        if (!usageMap[code]) {
          usageMap[code] = { inUse: false, tasks: [] }
        }
        usageMap[code].tasks.push({
          algCode: alg.algorithmId,
          algName: alg.algorithmName,
          running: !!isRunning
        })
        if (isRunning) {
          usageMap[code].inUse = true
        }
      })
    })
    modelUsageMap.value = usageMap
  }).catch(() => {})
}
const getModelUsageTasks = (modelCode) => {
  if (!modelCode) return []
  return modelUsageMap.value[modelCode]?.tasks || []
}
const getModelAlgCount = (modelCode) => {
  if (!modelCode) return 0
  return modelUsageMap.value[modelCode]?.tasks?.length || 0
}

// ── 详情弹窗内操作 ──
const editFromDetail = () => {
  modelDetailDialogVisible.value = false
  editClick(detailModel.value)
}
const deleteFromDetail = () => {
  modelDetailDialogVisible.value = false
  deleteClick(detailModel.value)
}

// ── 原有数据 ──
const topBarData = reactive({
  formList: [
    { label: t('field.modelName'), model: 'modelName' },
    { label: t('field.modelId'), model: 'modelCode' },
    { label: t('glossary.computeType'), model: 'gpuCode', type: 'select', dataList: [] }
  ]
})

const tableData = ref([])
const formData = reactive({
  gpuCode: '',
  modelName: '',
  modelCode: ''
})

const pageData = reactive({
  pageNum: 1,
  pageSize: 12,
  total: 0
})

const gpuCodes = ref([])
const uploadAlgorithmicVisible = ref(false)
const uploadAlgorithmicName = ref('')
const uploadAlgorithmicData = ref([])
const listDialogVisible = ref(false)
const algorithmicList = ref([])
const modelDetailDialogVisible = ref(false)
const detailModel = ref({})
const modelLabelData = ref([])
const engineTypeList = ref([])
const platformType = ref(localStorage.getItem('platformType') || '')
const isX86 = ref(false)
const addModelDialogTitle = computed(() => addModelMode.value === 'edit' ? t('action.editModel') : t('action.addModel'))
const addModelMode = ref('add')
const isMultiple = ref(false)
const uploadResult = ref([])
const uploadReultVisible = ref(false)

const addModelFormRef = ref(null)
const editModelFormRef = ref(null)
const uploadModelFileRef = ref(null)
const uploadEncoderFileRef = ref(null)
const uploadDecoderFileRef = ref(null)
const uploadVocabFileRef = ref(null)
const uploadTokenizerFileRef = ref(null)

const addModelForm = reactive({
  modelMainType: '',
  modelName: '',
  modelType: '',
  description: '',
  modelFile: '',
  modelFileList: [],
  encoderFile: '',
  encoderFileList: [],
  decoderFile: '',
  decoderFileList: [],
  vocabFile: '',
  vocabFileList: [],
  tokenizerFile: '',
  tokenizerFileList: [],
  normalizationMode: '0-1',
  colorChannel: 'rgb'
})

const defaultModels = ref([
  '1000001',
  '1000005',
  '1000010',
  '1000012',
  '1000016',
  '1001003',
  '1001007',
  '1001008'
])

const modelTypeGroups = computed(() => [
  {
    label: t('glossary.detectAlg'),
    value: 'detect',
    children: [
      { label: 'yolov5_det', value: 'yolov5_det' },
      { label: 'yolov8_det', value: 'yolov8_det' },
      { label: 'yolov9_det', value: 'yolov9_det' },
      { label: 'yolov11_det', value: 'yolov11_det' },
      { label: 'yolov12_det', value: 'yolov12_det' },
      { label: 'yolo26_det', value: 'yolo26_det' }
    ]
  },
  {
    label: t('glossary.classifyAlg'),
    value: 'classify',
    children: [{ label: 'classify', value: 'classify' }]
  },
  {
    label: t('glossary.keypointsAlg'),
    value: 'keypoints',
    children: [{ label: 'keypoints', value: 'keypoints' }]
  },
  {
    label: t('glossary.featureAlg'),
    value: 'feature',
    children: [{ label: 'feature', value: 'feature' }]
  },
  {
    label: t('glossary.foundationAlg'),
    value: 'foundation',
    children: [
      { label: 'dino', value: 'dino' },
      { label: 'sam2', value: 'sam2' },
      { label: 'qwen3vl', value: 'qwen3vl' },
      { label: 'qwen3_5', value: 'qwen3_5' }
    ]
  }
])

const currentSubModelTypes = computed(() => {
  return (
    modelTypeGroups.value.find(
      (item) => item.value === addModelForm.modelMainType
    )?.children || []
  )
})

const showPreprocessOptions = computed(() => {
  return !!addModelForm.modelMainType && addModelForm.modelMainType !== 'foundation'
})

const addModelRules = {
  modelName: [
    { required: true, message: t('validate.enterModelName'), trigger: 'blur' },
    {
      validator: function (rule, value, callback) {
        if (!value) {
          callback()
          return
        }
        if (/_/.test(value)) {
          callback(new Error(t('validate.nameNoUnderscore')))
          return
        }
        callback()
      },
      trigger: 'change'
    }
  ],
  modelType: [{ required: true, message: t('validate.selectModelType'), trigger: 'blur' }],
  modelFile: [
    {
      validator: (rule, value, callback) => {
        if (addModelForm.modelType === 'sam2') {
          callback()
          return
        }
        if (
          !addModelForm.modelFileList ||
          addModelForm.modelFileList.length === 0
        ) {
          callback(new Error(t('validate.uploadModelFile')))
        } else {
          callback()
        }
      },
      trigger: 'change'
    }
  ],
  encoderFile: [
    {
      validator: (rule, value, callback) => {
        if (addModelForm.modelType !== 'sam2') {
          callback()
          return
        }
        if (
          !addModelForm.encoderFileList ||
          addModelForm.encoderFileList.length === 0
        ) {
          callback(new Error(t('validate.uploadEncoderFile')))
        } else {
          callback()
        }
      },
      trigger: 'change'
    }
  ],
  decoderFile: [
    {
      validator: (rule, value, callback) => {
        if (addModelForm.modelType !== 'sam2') {
          callback()
          return
        }
        if (
          !addModelForm.decoderFileList ||
          addModelForm.decoderFileList.length === 0
        ) {
          callback(new Error(t('validate.uploadDecoderFile')))
        } else {
          callback()
        }
      },
      trigger: 'change'
    }
  ],
  vocabFile: [
    {
      validator: (rule, value, callback) => {
        if (addModelForm.modelType !== 'dino') {
          callback()
          return
        }
        if (
          !addModelForm.vocabFileList ||
          addModelForm.vocabFileList.length === 0
        ) {
          callback(new Error(t('validate.uploadVocabFile')))
        } else {
          callback()
        }
      },
      trigger: 'change'
    }
  ],
  tokenizerFile: [
    {
      validator: (rule, value, callback) => {
        if (addModelForm.modelType !== 'qwen3vl' && addModelForm.modelType !== 'qwen3_5') {
          callback()
          return
        }
        if (
          !addModelForm.tokenizerFileList ||
          addModelForm.tokenizerFileList.length === 0
        ) {
          callback(new Error(t('validate.uploadTokenizerFile')))
        } else {
          callback()
        }
      },
      trigger: 'change'
    }
  ]
}

const addModelLoading = ref(false)

const editModelDialogVisible = ref(false)
const editModelDialogTitle = computed(() => t('action.editModel'))
const editModelForm = reactive({
  modelCode: '',
  modelName: '',
  description: ''
})
const editModelRules = {
  modelName: [{ required: true, message: t('validate.enterModelName'), trigger: 'blur' }]
}
const editModelLoading = ref(false)

const deleteModelDialogVisible = ref(false)
const deleteModelInfo = reactive({})
const deleteModelLoading = ref(false)

const importModelDialogVisible = ref(false)
const importModelFileName = ref('')
const importModelFile = ref(null)
const importModelLoading = ref(false)
const importModelUploadRef = ref(null)

const indexcount = (index) => {
  return (pageData.pageNum - 1) * pageData.pageSize + index + 1
}

const getGPUCodes = () => {
  gpuCodes.value = [{ label: t('common.all'), value: '' }]
  proxy.$API.getGPUCodes().then((res) => {
    const { resData } = res
    resData.forEach((item) => {
      gpuCodes.value.push({
        label: item.value,
        value: item.code
      })
    })
    topBarData.formList[2].dataList = gpuCodes.value
  })
}

const getEngineTypeList = () => {
  proxy.$API.engineTypeList({}).then((res) => {
    const { resData } = res
    engineTypeList.value = resData || []
  })
}

const init = () => {
  const params = {
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    ...formData
  }
  proxy.$API.getAtomicModelPage(params).then((res) => {
    const { resData } = res
    tableData.value = resData.rows || []
    pageData.total = resData.total
  })
}

const refreshModelRepository = () => {
  init()
  loadModelTypes()
  loadAlgUsage()
}

const searchList = () => {
  pageData.pageNum = 1
  refreshModelRepository()
}

const detailClick = (obj) => {
  detailModel.value = obj
  modelLabelData.value = obj.label ? JSON.parse(obj.label) : []
  modelDetailDialogVisible.value = true
}

const editConfigClick = (row) => {
  router.push({
    path: '/gam/modelConfig',
    query: {
      modelCode: row.modelCode,
      modelName: row.modelName
    }
  })
}

const handleModelMainTypeChange = () => {
  addModelForm.modelType = ''
  handleModelTypeChange()
  nextTick(() => {
    addModelFormRef.value && addModelFormRef.value.clearValidate()
  })
}

const editClick = (obj) => {
  editModelDialogVisible.value = true
  editModelForm.modelCode = obj.modelCode
  editModelForm.modelName = obj.modelName || ''
  editModelForm.description = obj.description || ''
  nextTick(() => {
    editModelFormRef.value && editModelFormRef.value.clearValidate()
  })
}

const sureEditModel = () => {
  editModelFormRef.value.validate((valid) => {
    if (!valid) return
    editModelLoading.value = true
    const updateParams = {
      modelCode: editModelForm.modelCode,
      modelName: editModelForm.modelName,
      description: editModelForm.description || ''
    }
    proxy.$API
      .updateAtomicModel(updateParams)
      .then((res) => {
        if (res.resCode === 1) {
          proxy.$message.success(t('validate.editSucceeded'))
          editModelDialogVisible.value = false
          init()
        } else {
          const msg =
            res.resMsg && res.resMsg[0] ? res.resMsg[0].msgText : t('validate.editFailed')
          proxy.$message.error(msg)
        }
        editModelLoading.value = false
      })
      .catch((err) => {
        editModelLoading.value = false
      })
  })
}

const deleteClick = (obj) => {
  deleteModelInfo.modelCode = obj.modelCode
  deleteModelInfo.modelName = obj.modelName
  deleteModelDialogVisible.value = true
}

const sureDeleteModel = () => {
  deleteModelLoading.value = true
  const deleteParams = {
    modelCode: deleteModelInfo.modelCode
  }
  proxy.$API
    .deleteAtomicModel(deleteParams)
    .then((res) => {
      if (res.resCode === 1) {
        proxy.$message.success(t('validate.deleteSucceeded'))
        deleteModelDialogVisible.value = false
        formData.gpuCode = ''
        formData.modelName = ''
        formData.modelCode = ''
        pageData.pageNum = 1
        init()
      } else {
        const msg =
          res.resMsg && res.resMsg[0] ? res.resMsg[0].msgText : t('validate.deleteFailed')
        proxy.$message.error(msg)
      }
      deleteModelLoading.value = false
    })
    .catch((err) => {
      deleteModelLoading.value = false
    })
}

const handleCurrentChange = (page) => {
  pageData.pageNum = page
  init()
}
const handleSizeChange = (pageSize) => {
  pageData.pageSize = pageSize
  init()
}

const returnLabelWithCode = (code) => {
  const index = gpuCodes.value.findIndex((item) => item.value == code)
  return index === -1 ? '' : gpuCodes.value[index].label
}

const algorithmicListClick = (obj) => {
  algorithmicList.value = obj.algorithmList
  listDialogVisible.value = true
}

const addClick = () => {
  addModelMode.value = 'add'
  uploadAlgorithmicVisible.value = true
  addModelForm.modelCode = ''
  addModelForm.modelName = ''
  addModelForm.modelType = ''
  addModelForm.description = ''
  addModelForm.modelFile = ''
  addModelForm.modelFileList = []
  nextTick(() => {
    addModelFormRef.value && addModelFormRef.value.clearValidate()
    uploadModelFileRef.value && uploadModelFileRef.value.clearFiles()
  })
}

const batchUpdateClick = () => {
  proxy.$API.updateAtomicModel({}).then(() => {
    searchList()
    proxy.$message.success(t('common.operationSucceeded'))
  })
}

const sureUploadAlgorithmic = () => {
  if (uploadAlgorithmicData.value.length === 0) {
    return proxy.$message.warning(t('validate.uploadFileFirst'))
  }
  const uploadResults = uploadAlgorithmicData.value.map(async (file) => {
    const fileSize = file.raw ? file.raw.size : file.size
    const fileName = file.raw ? file.raw.name : file.name
    if (fileSize > 3 * 1024 * 1024 * 1024) {
      return { fileName, success: false, message: t('validate.fileSizeExceed3GB') }
    }
    try {
      const resData = await uploadFile({ file: file.raw })
      if (resData?.resCode == 1) {
        return { fileName, success: true, message: t('validate.uploadSucceeded') }
      } else {
        return {
          fileName,
          success: false,
          message: t('validate.uploadFailed') + localeColon + (resData?.resMsg[0].msgText || t('validate.unknownError'))
        }
      }
    } catch (error) {
      return {
        fileName,
        success: false,
        message: t('validate.uploadFailed') + localeColon + (error.message || t('validate.unknownError'))
      }
    }
  })
  Promise.all(uploadResults).then((results) => {
    uploadResult.value = results
    uploadReultVisible.value = true
    uploadAlgorithmicVisible.value = false
    uploadAlgorithmicData.value = []
    searchList()
    // 延迟二次刷新，等待后端完成文件处理
    setTimeout(() => { searchList() }, 1500)
  })
}

const uploadFile = (params) => {
  const formData = new FormData()
  formData.append('file', params.file)
  return proxy.$API.uploadAtomicModel(formData).then((res) => res)
}
const handleChange = (file, fileList) => {
  if (!isMultiple.value) {
    uploadAlgorithmicData.value = [file]
  } else {
    uploadAlgorithmicData.value = fileList
  }
}
const handleRemove = (file, fileList) => {
  uploadAlgorithmicData.value = fileList
}
const importModelClick = () => {
  importModelFileName.value = ''
  importModelFile.value = null
  importModelDialogVisible.value = true
}

const handleImportFileChange = (file) => {
  const rawFile = file.raw || file
  const name = rawFile.name || ''
  if (!name.match(/\.(tar\.gz|tgz|zip)$/i)) {
    proxy.$message.warning(t('validate.selectTarGzOrZip'))
    return
  }
  if (rawFile.size > 3 * 1024 * 1024 * 1024) {
    proxy.$message.warning(t('validate.fileSizeExceed3GB'))
    return
  }
  importModelFileName.value = name
  importModelFile.value = rawFile
}

const importModelDialogClosed = () => {
  importModelFileName.value = ''
  importModelFile.value = null
}

const sureImportModel = async () => {
  if (!importModelFile.value) {
    proxy.$message.warning(t('validate.selectFile'))
    return
  }
  try {
    importModelLoading.value = true
    // Upload file using existing chunked upload
    const filePath = await uploadSingleFile(importModelFile.value)
    // Call import API
    const res = await proxy.$API.importModel({ filePath })
    if (res && res.resCode === 1) {
      proxy.$message.success(t('validate.importSucceeded'))
      importModelDialogVisible.value = false
      searchList()
      // 延迟二次刷新，等待后端完成文件处理
      setTimeout(() => { searchList() }, 1500)
    } else {
      const msg = (res?.resMsg && res.resMsg[0]?.msgText) || t('validate.importFailed')
      proxy.$message.error(msg)
    }
  } catch (err) {
    // axios interceptor already shows error toast for resCode!=1 responses,
    // only show additional message for network/upload errors
    if (err && !err.resCode && err.resCode !== 0) {
      proxy.$message.error(t('validate.importFailed') + localeColon + (err.message || t('validate.networkError')))
    }
  } finally {
    importModelLoading.value = false
  }
}

const uploadAlgorithmicClosed = () => {
  uploadAlgorithmicData.value = []
  addModelForm.modelFileList = []
  addModelForm.modelFile = ''
  addModelForm.encoderFileList = []
  addModelForm.encoderFile = ''
  addModelForm.decoderFileList = []
  addModelForm.decoderFile = ''
  addModelForm.vocabFileList = []
  addModelForm.vocabFile = ''
  addModelForm.tokenizerFileList = []
  addModelForm.tokenizerFile = ''
  addModelForm.normalizationMode = '0-1'
  addModelForm.colorChannel = 'rgb'
  addModelFormRef.value && addModelFormRef.value.resetFields()
}

const handleModelTypeChange = () => {
  addModelForm.modelFileList = []
  addModelForm.modelFile = ''
  addModelForm.encoderFileList = []
  addModelForm.encoderFile = ''
  addModelForm.decoderFileList = []
  addModelForm.decoderFile = ''
  addModelForm.vocabFileList = []
  addModelForm.vocabFile = ''
  addModelForm.tokenizerFileList = []
  addModelForm.tokenizerFile = ''
  addModelForm.normalizationMode = '0-1'
  addModelForm.colorChannel = 'rgb'
  nextTick(() => {
    uploadModelFileRef.value && uploadModelFileRef.value.clearFiles()
    uploadEncoderFileRef.value && uploadEncoderFileRef.value.clearFiles()
    uploadDecoderFileRef.value && uploadDecoderFileRef.value.clearFiles()
    uploadVocabFileRef.value && uploadVocabFileRef.value.clearFiles()
    uploadTokenizerFileRef.value && uploadTokenizerFileRef.value.clearFiles()
  })
}

const handleModelFileChange = (file, fileList) => {
  addModelForm.modelFileList =
    fileList.length > 0 ? [fileList[fileList.length - 1]] : []
  addModelForm.modelFile =
    addModelForm.modelFileList.length > 0 ? 'file_selected' : ''
}
const handleModelFileRemove = (file, fileList) => {
  addModelForm.modelFileList = fileList || []
  addModelForm.modelFile =
    fileList && fileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('modelFile')
}
const handleEncoderFileChange = (file, fileList) => {
  addModelForm.encoderFileList =
    fileList.length > 0 ? [fileList[fileList.length - 1]] : []
  addModelForm.encoderFile =
    addModelForm.encoderFileList.length > 0 ? 'file_selected' : ''
}
const handleEncoderFileRemove = (file, fileList) => {
  addModelForm.encoderFileList = fileList || []
  addModelForm.encoderFile =
    fileList && fileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('encoderFile')
}
const handleDecoderFileChange = (file, fileList) => {
  addModelForm.decoderFileList =
    fileList.length > 0 ? [fileList[fileList.length - 1]] : []
  addModelForm.decoderFile =
    addModelForm.decoderFileList.length > 0 ? 'file_selected' : ''
}
const handleDecoderFileRemove = (file, fileList) => {
  addModelForm.decoderFileList = fileList || []
  addModelForm.decoderFile =
    fileList && fileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('decoderFile')
}
const handleVocabFileChange = (file, fileList) => {
  addModelForm.vocabFileList =
    fileList.length > 0 ? [fileList[fileList.length - 1]] : []
  addModelForm.vocabFile =
    addModelForm.vocabFileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('vocabFile')
}
const handleVocabFileRemove = (file, fileList) => {
  addModelForm.vocabFileList = fileList || []
  addModelForm.vocabFile =
    fileList && fileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('vocabFile')
}
const handleTokenizerFileChange = (file, fileList) => {
  addModelForm.tokenizerFileList =
    fileList.length > 0 ? [fileList[fileList.length - 1]] : []
  addModelForm.tokenizerFile =
    addModelForm.tokenizerFileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('tokenizerFile')
}
const handleTokenizerFileRemove = (file, fileList) => {
  addModelForm.tokenizerFileList = fileList || []
  addModelForm.tokenizerFile =
    fileList && fileList.length > 0 ? 'file_selected' : ''
  addModelFormRef.value && addModelFormRef.value.validateField('tokenizerFile')
}

const uploadSingleFile = async (file) => {
  const CHUNK_SIZE = 32 * 1024 * 1024
  const totalSize = file.size || 0
  const totalChunks = Math.max(1, Math.ceil(totalSize / CHUNK_SIZE))
  const uploadId = `${Date.now()}_${Math.random().toString(16).slice(2)}`
  let lastResp = null
  for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
    const start = chunkIndex * CHUNK_SIZE
    const end = Math.min(totalSize, start + CHUNK_SIZE)
    const blob = file.slice(start, end)
    const uploadFormData = new FormData()
    uploadFormData.append('file', blob, file.name)
    uploadFormData.append('uploadId', uploadId)
    uploadFormData.append('chunkIndex', String(chunkIndex))
    uploadFormData.append('totalChunks', String(totalChunks))
    uploadFormData.append('totalSize', String(totalSize))
    uploadFormData.append('chunkSize', String(end - start))
    lastResp = await proxy.$API.uploadAtomicModelTemp(uploadFormData)
    const respData = lastResp?.resData || lastResp?.data
    if (!respData) {
      throw new Error(t('validate.uploadNoResponse'))
    }
    const resCode = respData?.resCode ?? respData?.resData?.resCode
    if (resCode !== undefined && resCode !== 1) {
      const msg =
        (respData.resMsg && respData.resMsg[0] && respData.resMsg[0].msgText) ||
        (respData.resData &&
          respData.resData.resMsg &&
          respData.resData.resMsg[0] &&
          respData.resData.resMsg[0].msgText) ||
        t('validate.tempUploadFailed')
      throw new Error(msg)
    }
  }
  const finalData = lastResp?.resData || lastResp?.data
  let tempFilePath = ''
  if (finalData?.resData?.filePath) {
    tempFilePath = finalData.resData.filePath
  } else if (finalData?.filePath) {
    tempFilePath = finalData.filePath
  }
  if (!tempFilePath) {
    throw new Error(t('validate.cannotGetFilePath'))
  }
  return tempFilePath
}

const sureAddModel = async () => {
  try {
    const valid = await new Promise((resolve) => {
      addModelFormRef.value.validate((isValid) => {
        resolve(isValid)
      })
    })
    if (!valid) return
    const isSam2 = addModelForm.modelType === 'sam2'
    if (isSam2) {
      if (
        !addModelForm.encoderFileList ||
        addModelForm.encoderFileList.length === 0
      ) {
        proxy.$message.warning(t('validate.uploadEncoderFile'))
        return
      }
      if (
        !addModelForm.decoderFileList ||
        addModelForm.decoderFileList.length === 0
      ) {
        proxy.$message.warning(t('validate.uploadDecoderFile'))
        return
      }
      const encoderFile =
        addModelForm.encoderFileList[0].raw || addModelForm.encoderFileList[0]
      const decoderFile =
        addModelForm.decoderFileList[0].raw || addModelForm.decoderFileList[0]
      const ext = isX86.value ? '.onnx' : '.bmodel'
      if (!encoderFile.name.endsWith(ext)) {
        proxy.$message.warning(t('validate.encoderFormatError', { ext }))
        return
      }
      if (!decoderFile.name.endsWith(ext)) {
        proxy.$message.warning(t('validate.decoderFormatError', { ext }))
        return
      }
    } else {
      if (
        !addModelForm.modelFileList ||
        addModelForm.modelFileList.length === 0
      ) {
        proxy.$message.warning(t('validate.uploadModelFile'))
        return
      }
      const file =
        addModelForm.modelFileList[0].raw || addModelForm.modelFileList[0]
      const ext = isX86.value ? '.onnx' : '.bmodel'
      if (!file.name.endsWith(ext)) {
        proxy.$message.warning(t('validate.uploadFormatError', { ext }))
        return
      }
    }
    addModelLoading.value = true
    const addModelParams = {
      modelCode: String(addModelForm.modelCode).trim(),
      modelName: (addModelForm.modelName || '').trim(),
      modelType: (addModelForm.modelType || '').trim(),
      description: (addModelForm.description || '').trim()
    }
    if (showPreprocessOptions.value) {
      addModelParams.normalizationMode = addModelForm.normalizationMode || '0-1'
      addModelParams.colorChannel = addModelForm.colorChannel || 'rgb'
    }
    if (isSam2) {
      const encoderFile =
        addModelForm.encoderFileList[0].raw || addModelForm.encoderFileList[0]
      const decoderFile =
        addModelForm.decoderFileList[0].raw || addModelForm.decoderFileList[0]
      const [encoderPath, decoderPath] = await Promise.all([
        uploadSingleFile(encoderFile),
        uploadSingleFile(decoderFile)
      ])
      addModelParams.bmodelFiles = [
        { role: 'encoder', filePath: encoderPath },
        { role: 'decoder', filePath: decoderPath }
      ]
    } else {
      const file =
        addModelForm.modelFileList[0].raw || addModelForm.modelFileList[0]
      const filePath = await uploadSingleFile(file)
      addModelParams.bmodelFiles = [{ role: 'main', filePath }]
    }
    if (addModelForm.modelType === 'dino') {
      if (
        !addModelForm.vocabFileList ||
        addModelForm.vocabFileList.length === 0
      ) {
        proxy.$message.warning(t('validate.dinoRequiresVocab'))
        addModelLoading.value = false
        return
      }
      const vocabFile =
        addModelForm.vocabFileList[0].raw || addModelForm.vocabFileList[0]
      if (!vocabFile.name || !vocabFile.name.toLowerCase().endsWith('.txt')) {
        proxy.$message.warning(t('validate.vocabFormatError'))
        addModelLoading.value = false
        return
      }
      addModelParams.vocabFilePath = await uploadSingleFile(vocabFile)
    }
    if (addModelForm.modelType === 'qwen3vl' || addModelForm.modelType === 'qwen3_5') {
      if (
        !addModelForm.tokenizerFileList ||
        addModelForm.tokenizerFileList.length === 0
      ) {
        proxy.$message.warning(t('validate.modelRequiresTokenizer'))
        addModelLoading.value = false
        return
      }
      const tokenizerFile =
        addModelForm.tokenizerFileList[0].raw ||
        addModelForm.tokenizerFileList[0]
      if (
        !tokenizerFile.name ||
        !tokenizerFile.name.toLowerCase().endsWith('.json')
      ) {
        proxy.$message.warning(t('validate.tokenizerFormatError'))
        addModelLoading.value = false
        return
      }
      addModelParams.tokenizerFilePath = await uploadSingleFile(tokenizerFile)
      if (!addModelParams.tokenizerFilePath) {
        proxy.$message.error(t('validate.tokenizerUploadFailed'))
        addModelLoading.value = false
        return
      }
    }
    const res = await proxy.$API.addAtomicModel(addModelParams)
    if (!res) {
      proxy.$message.error(t('validate.addNoResponse'))
      addModelLoading.value = false
      return
    }
    const d = res
    if (d.resCode === 1) {
      proxy.$message.success(t('common.addSucceeded'))
      uploadAlgorithmicVisible.value = false
      pageData.pageNum = 1
      refreshModelRepository()
      // 延迟二次刷新，等待后端完成文件处理
      setTimeout(() => { refreshModelRepository() }, 1500)
    } else {
      const msg = (d.resMsg && d.resMsg[0] && d.resMsg[0].msgText) || t('common.addFailed')
      proxy.$message.error(msg)
    }
    addModelLoading.value = false
  } catch (err) {
    addModelLoading.value = false
  }
}

onMounted(() => {
  if (platformType.value !== '15') {
    getGPUCodes()
    getEngineTypeList()
  }
  // Detect if platform is X86
  if (proxy.$API.queryDeviceInfo) {
    proxy.$API.queryDeviceInfo().then(res => {
      const devInfoList = res?.resData?.devInfoList || []
      const deviceTypeItem = devInfoList.find(item => item.key === 'deviceType')
      if (deviceTypeItem && deviceTypeItem.value.toLowerCase().includes('x86')) {
        isX86.value = true
      }
    }).catch(() => {})
  }
  loadModelTypes()
  loadAlgUsage()
  init()
})


</script>

<style lang="scss" scoped>
.model-repo-page {
  padding: 0 0 24px;
  min-height: 100%;
}

// ── 页面头部 ──
.repo-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px 0;
}
.search-box {
  width: 320px;
}
.header-actions {
  display: flex;
  gap: 10px;
}
.btn-primary-gradient {
  background: linear-gradient(135deg, #3182ce, #4299e1) !important;
  border: none !important;
  color: #fff !important;
  &:hover { background: linear-gradient(135deg, #2b6cb0, #2b6cb0) !important; }
}

// ── 类型筛选 Tab ──
.type-tabs {
  display: flex;
  gap: 8px;
  padding: 16px 24px 8px;
  flex-wrap: wrap;
}
.type-tab {
  padding: 5px 16px;
  border-radius: 20px;
  border: 1px solid #e5e7eb;
  background: #fff;
  cursor: pointer;
  font-size: 13px;
  color: #374151;
  transition: all 0.2s;
  &:hover { border-color: #3182ce; color: #3182ce; }
  &.active {
    background: #3182ce;
    color: #fff;
    border-color: #3182ce;
  }
}
.tab-count {
  margin-left: 4px;
  opacity: 0.7;
  font-size: 12px;
}

// ── 卡片网格 ──
.model-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
  padding: 12px 24px;
}
@media (max-width: 1200px) {
  .model-grid { grid-template-columns: repeat(2, 1fr); }
}
@media (max-width: 768px) {
  .model-grid { grid-template-columns: 1fr; }
}

.model-card {
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  border: 1px solid #f0f0f0;
  transition: all 0.25s ease;
  &:hover {
    transform: translateY(-2px);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.08);
    border-color: rgba(49, 130, 206, 0.2);
  }
}
.card-top {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 12px;
}

// ── 图标 ──
.card-icon {
  width: 44px;
  height: 44px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  svg { width: 22px; height: 22px; }
  &.icon-blue { background: rgba(59, 130, 246, 0.1); color: #3b82f6; }
  &.icon-green { background: rgba(34, 197, 94, 0.1); color: #22c55e; }
  &.icon-cyan { background: rgba(6, 182, 212, 0.1); color: #06b6d4; }
  &.icon-purple { background: rgba(66, 153, 225, 0.1); color: #4299e1; }
  &.icon-gradient { background: linear-gradient(135deg, rgba(124, 58, 237, 0.1), rgba(249, 115, 22, 0.1)); color: #2b6cb0; }
}


// ── 关联场景任务标签 ──
.alg-count-badge {
  font-size: 12px;
  color: #6b7280;
  background: #f3f4f6;
  padding: 2px 8px;
  border-radius: 10px;
}

// ── 卡片信息 ──
.card-info { margin-bottom: 10px; }
.card-title {
  font-size: 15px;
  font-weight: 600;
  color: #1f2937;
  margin-bottom: 4px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.card-meta {
  font-size: 12px;
  color: #9ca3af;
}

// ── 类型标签 ──
.card-tags { margin-bottom: 12px; }
.model-type-tag {
  display: inline-block;
  padding: 2px 10px;
  border-radius: 12px;
  font-size: 12px;
  &.tag-detect { background: #dbeafe; color: #2563eb; }
  &.tag-classify { background: #dcfce7; color: #16a34a; }
  &.tag-keypoints { background: #cffafe; color: #0891b2; }
  &.tag-feature { background: #ebf8ff; color: #2b6cb0; }
  &.tag-foundation { background: linear-gradient(135deg, #ebf8ff, #fff7ed); color: #2b6cb0; }
}

// ── 卡片底部 ──
.card-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 12px;
  border-top: 1px solid #f5f5f5;
}
.alg-count {
  font-size: 12px;
  color: #9ca3af;
}
.card-actions {
  display: flex;
  gap: 4px;
  .el-button {
    font-size: 13px;
    color: #3182ce !important;
    &:hover { color: #2b6cb0 !important; }
  }
}

// ── 分页 ──
.pagination-container {
  display: flex;
  justify-content: center;
  padding: 16px 24px;
}

// ── 增强详情弹窗 ──
.detail-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 20px;
  padding-bottom: 16px;
  border-bottom: 1px solid #f0f0f0;
}
.detail-icon {
  width: 52px;
  height: 52px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  svg { width: 26px; height: 26px; }
}
.detail-title-area {
  h3 { margin: 0 0 6px; font-size: 18px; color: #1f2937; }
}
.detail-badges {
  display: flex;
  align-items: center;
  gap: 10px;
}
.detail-section {
  margin-bottom: 16px;
}
.detail-row {
  display: flex;
  padding: 8px 0;
  font-size: 14px;
  border-bottom: 1px solid #fafafa;
}
.detail-label {
  width: 90px;
  flex-shrink: 0;
  color: #6b7280;
}
.section-title {
  font-size: 14px;
  font-weight: 600;
  color: #374151;
  margin: 12px 0 8px;
}
.detail-collapse {
  margin-bottom: 16px;
  :deep(.el-collapse-item__header) {
    font-size: 14px;
    font-weight: 600;
    color: #374151;
  }
}

// ── 使用状态 ──
.usage-task-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 0;
  font-size: 13px;
}
.task-dot {
  font-size: 10px;
  &.running { color: #22c55e; }
  &.stopped { color: #9ca3af; }
}
.task-name { flex: 1; }
.task-status-text {
  font-size: 12px;
  color: #9ca3af;
}

// ── 弹窗通用 ──
.upload-div {
  margin-left: 20px;
  margin-bottom: 10px;
}
.upload-warn {
  margin-top: 5px;
  color: #3182ce;
  font-size: 12px;
}
.form-content {
  width: calc(100% - 50px);
}
.div-center {
  text-align: center;
  padding: 10px;
}
</style>
