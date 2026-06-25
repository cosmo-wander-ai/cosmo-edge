<template>
  <div class="image-analysis-page">
    <!-- 顶部工具栏 -->
    <div class="toolbar-section">
      <div class="toolbar-left">
        <el-select
          v-model="selectedAlgorithm"
          :placeholder="$t('imageAnalysis.selectAlgorithmPlaceholder')"
          filterable
          size="default"
          class="algorithm-select"
          @change="handleAlgorithmChange"
        >
          <el-option
            v-for="item in algorithmList"
            :key="item.algorithmId"
            :label="resolveResourceAlgorithmName(item)"
            :value="item.algorithmId"
          />
        </el-select>
        <el-upload
          ref="uploadRef"
          action="#"
          :auto-upload="false"
          :show-file-list="false"
          :on-change="handleFileChange"
          accept="image/*"
          multiple
        >
          <el-button type="primary" :icon="Upload">{{ $t('imageAnalysis.uploadImage') }}</el-button>
        </el-upload>
        <el-button
          type="success"
          :icon="VideoPlay"
          :loading="analyzing"
          :disabled="!selectedAlgorithm || uploadedFiles.length === 0"
          @click="startAnalysis"
        >
          {{ analyzing ? $t('imageAnalysis.analyzing') : $t('imageAnalysis.startAnalysis') }}
        </el-button>
        <el-button
          v-if="uploadedFiles.length > 0"
          type="danger"
          plain
          :icon="Delete"
          @click="clearAll"
        >
          {{ $t('action.clear') }}
        </el-button>
      </div>
      <div class="toolbar-right">
        <span class="file-count" v-if="uploadedFiles.length > 0">
          {{ $t('imageAnalysis.selectedImages', { n: uploadedFiles.length }) }}
        </span>
      </div>
    </div>

    <!-- 内容区域 -->
    <div class="content-section" v-if="uploadedFiles.length > 0 || results.length > 0">
      <!-- 图片预览 / 结果网格 -->
      <div class="image-grid">
        <div
          v-for="(item, index) in displayItems"
          :key="index"
          class="image-card"
          :class="{ 'has-result': item.result }"
          @click="openPreview(index)"
        >
          <div class="image-wrapper">
            <!-- 原始图 -->
            <img
              :src="item.preview"
              class="analysis-image"
              :alt="item.name"
            />
            <!-- Canvas 叠加层：渲染检测框 -->
            <canvas
              v-if="item.result && getTargetCount(item.result) > 0"
              ref="overlayCanvasRefs"
              class="overlay-canvas"
              :data-index="index"
            />
          </div>
          <div class="image-footer">
            <div class="image-name" :title="item.name">{{ item.name }}</div>
            <div class="image-status">
              <el-tag v-if="item.analyzing" type="warning" size="small" effect="dark">{{ $t('imageAnalysis.analyzingStatus') }}</el-tag>
              <el-tag v-else-if="item.result" type="success" size="small" effect="dark">{{ $t('imageAnalysis.completed') }}</el-tag>
              <el-tag v-else type="info" size="small" effect="plain">{{ $t('imageAnalysis.pending') }}</el-tag>
            </div>
          </div>
          <!-- 结果信息面板 -->
          <div v-if="item.result" class="result-panel">
            <!-- 检测类结果 -->
            <div v-if="getTargetCount(item.result) > 0" class="result-section">
              <div class="result-label">{{ $t('imageAnalysis.detectedTargets') }}</div>
              <div class="result-tags">
                <el-tag
                  v-for="(target, tIdx) in getTargets(item.result)"
                  :key="tIdx"
                  size="small"
                  :type="target.bLogicResult ? 'danger' : 'primary'"
                  effect="light"
                  class="target-tag"
                >
                  {{ getTargetLabel(target) }}
                </el-tag>
              </div>
            </div>
            <!-- 无结果 -->
            <div v-else class="result-section">
              <div class="result-label empty-result">{{ $t('imageAnalysis.noTargetsDetected') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 空状态 -->
    <div v-else class="empty-state">
      <el-empty :description="$t('imageAnalysis.emptyDescription')">
        <template #image>
          <div class="empty-icon">
            <svg viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
              <rect x="10" y="25" width="100" height="70" rx="8" fill="none" stroke="var(--primary-color)" stroke-width="2" opacity="0.4"/>
              <circle cx="40" cy="52" r="10" fill="none" stroke="var(--primary-color)" stroke-width="2" opacity="0.5"/>
              <polygon points="25,85 55,55 75,75 95,50 105,65 105,85" fill="var(--primary-color)" opacity="0.15"/>
              <rect x="42" y="4" width="36" height="18" rx="4" fill="var(--primary-color)" opacity="0.2"/>
              <text x="60" y="16" text-anchor="middle" fill="var(--primary-color)" font-size="9" opacity="0.6">AI</text>
            </svg>
          </div>
        </template>
      </el-empty>
    </div>

    <!-- 大图预览弹窗 -->
    <el-dialog
      v-model="previewVisible"
      :title="previewItem?.name || $t('imageAnalysis.imagePreview')"
      width="80%"
      top="5vh"
      class="preview-dialog"
      destroy-on-close
    >
      <div class="preview-body" v-if="previewItem">
        <div class="preview-image-wrap">
          <div class="preview-image-container">
            <img
              ref="previewImageRef"
              :src="previewItem.preview"
              class="preview-image"
              @load="onPreviewImageLoad"
            />
            <canvas
              v-if="previewItem.result && getTargetCount(previewItem.result) > 0"
              ref="previewCanvasRef"
              class="preview-overlay-canvas"
            />
          </div>
        </div>
        <div v-if="previewItem.result" class="preview-details">
          <el-descriptions :column="2" border size="small">
            <el-descriptions-item :label="$t('field.algorithmName')">{{ previewItem.result.algorithmCode || resolveResourceAlgorithmName(selectedAlgorithmInfo) }}</el-descriptions-item>
            <el-descriptions-item :label="$t('imageAnalysis.detectionCount')">{{ getTargetCount(previewItem.result) }}</el-descriptions-item>
          </el-descriptions>
          <div v-if="getTargetCount(previewItem.result) > 0" class="preview-targets">
            <div class="preview-targets-title">{{ $t('imageAnalysis.detectionDetails') }}</div>
            <el-table :data="getTargets(previewItem.result)" border size="small" max-height="300">
              <el-table-column type="index" label="#" width="50" />
              <el-table-column :label="$t('imageAnalysis.category')" min-width="120">
                <template #default="{ row }">{{ getTargetLabel(row) }}</template>
              </el-table-column>
              <el-table-column :label="$t('imageAnalysis.confidence')" min-width="100">
                <template #default="{ row }">{{ getTargetConfidence(row) }}</template>
              </el-table-column>
              <el-table-column :label="$t('imageAnalysis.position')" min-width="200">
                <template #default="{ row }">
                  <span v-if="row.box">
                    [{{ (row.box.x || 0).toFixed(3) }}, {{ (row.box.y || 0).toFixed(3) }},
                     {{ (row.box.w || 0).toFixed(3) }}, {{ (row.box.h || 0).toFixed(3) }}]
                  </span>
                </template>
              </el-table-column>
              <el-table-column :label="$t('imageAnalysis.logicResult')" width="80">
                <template #default="{ row }">
                  <el-tag :type="row.bLogicResult ? 'danger' : 'success'" size="small">
                    {{ row.bLogicResult ? $t('imageAnalysis.hit') : $t('status.normal') }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column :label="$t('imageAnalysis.featurePreview')" min-width="220">
                <template #default="{ row }">
                  <span v-if="row.featurePreview" class="feature-preview">
                    {{ row.featurePreview }}
                  </span>
                  <span v-else class="feature-preview-empty">-</span>
                </template>
              </el-table-column>
            </el-table>
          </div>
        </div>
      </div>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'ImageAnalysis'
}
</script>

<script setup>
import { ref, computed, getCurrentInstance, nextTick, onUnmounted, onDeactivated } from 'vue'
import { t } from '@/i18n'
import { ElMessage } from 'element-plus'
import { Upload, VideoPlay, Delete } from '@element-plus/icons-vue'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

// State
const selectedAlgorithm = ref('')
const selectedAlgorithmInfo = ref(null)
const algorithmList = ref([])
const uploadedFiles = ref([])
const results = ref([])
const analyzing = ref(false)
const taskCreated = ref(false)
const previewVisible = ref(false)
const previewItem = ref(null)
const previewIndex = ref(-1)
const previewImageRef = ref(null)
const previewCanvasRef = ref(null)
const uploadRef = ref(null)
const overlayCanvasRefs = ref([])

// Computed
const displayItems = computed(() => {
  return uploadedFiles.value.map((file, idx) => {
    const res = results.value[idx];
    return {
      name: file.name,
      // 如果后端分析返回了完整的处理后图片（包含 Mask 叠加和框），则优先展示后端图片
      preview: (res && res.fullPicture) ? res.fullPicture : file.preview,
      file: file.raw,
      analyzing: file.analyzing || false,
      result: res || null
    };
  })
})

// Init: load algorithm list
const loadAlgorithms = () => {
  const params = {
    algorithmId: '',
    algorithmCategory: '',
    algorithmUsage: '2',
    algorithmName: '',
    supplier: '',
    engineTypeList: [],
    pageNum: 1,
    pageSize: 1000
  }
  $API.algorithmInquire(params).then(res => {
    const { resData } = res
    algorithmList.value = resData?.rows || []
  }).catch(() => {
    algorithmList.value = []
  })
}
loadAlgorithms()

// Handlers
const handleAlgorithmChange = async (val) => {
  if (taskCreated.value && selectedAlgorithmInfo.value) {
    // 切换前销毁旧的任务，释放显存
    const cancelParams = {
      mvDebug: 'MiniV-Debug',
      algorithmCode: selectedAlgorithmInfo.value.algorithmId
    }
    await $API.pTaskCancle(cancelParams).catch(e => console.error(e))
  }
  const alg = algorithmList.value.find(a => a.algorithmId === val)
  selectedAlgorithmInfo.value = alg || null
  taskCreated.value = false
}

const cleanupBackend = async () => {
  if (taskCreated.value && selectedAlgorithmInfo.value) {
    const cancelParams = {
      mvDebug: 'MiniV-Debug',
      algorithmCode: selectedAlgorithmInfo.value.algorithmId
    }
    await $API.pTaskCancle(cancelParams).catch(e => console.error(e))
    taskCreated.value = false
  }
}

// 页面离开时，销毁任务释放显存
onUnmounted(cleanupBackend)
onDeactivated(cleanupBackend)

const handleFileChange = (file) => {
  // Read file for preview
  const reader = new FileReader()
  reader.onload = (e) => {
    uploadedFiles.value.push({
      name: file.name,
      raw: file.raw,
      preview: e.target.result,
      base64: e.target.result.split(',')[1],
      analyzing: false
    })
  }
  reader.readAsDataURL(file.raw)
}

const clearAll = () => {
  uploadedFiles.value = []
  results.value = []
}

// 确保图片任务已创建并同步最新参数
const ensureTaskCreated = async () => {
  // 命中缓存则直接复用，避免重复走重模型加载逻辑
  if (taskCreated.value) return true
  const alg = selectedAlgorithmInfo.value
  if (!alg) return false

  try {
    const createParams = {
      mvDebug: 'MiniV-Debug',
      algorithmCode: alg.algorithmId,
      algorithmUpdateTime: alg.algorithmUpdateTime || String(Date.now())
    }
    await $API.pTaskCreate(createParams)
    taskCreated.value = true
    return true
  } catch (err) {
    console.error('PTaskCreate failed:', err)
    ElMessage.error(t('imageAnalysis.taskCreateFailed'))

    // 🔥 如果因为模型太大导致加载超过 Axios 前端请求的超时时间，实际上后端可能还在默默拉起它。
    // 为了防止这个“脱缰”的模型永远锁死在显存里且前端丧失控制权，我们在报错捕获时发送取消令进行兜底清理。
    try {
      const cancelParams = {
        mvDebug: 'MiniV-Debug',
        algorithmCode: alg.algorithmId
      }
      await $API.pTaskCancle(cancelParams)
    } catch(e) {}

    return false
  }
}

const startAnalysis = async () => {
  if (!selectedAlgorithm.value) {
    ElMessage.warning(t('imageAnalysis.selectAlgorithmFirst'))
    return
  }
  if (uploadedFiles.value.length === 0) {
    ElMessage.warning(t('imageAnalysis.uploadImageFirst'))
    return
  }

  analyzing.value = true
  results.value = new Array(uploadedFiles.value.length).fill(null)

  // 1. 确保任务已创建（加载模型、初始化流水线）
  if (!await ensureTaskCreated()) {
    analyzing.value = false
    return
  }

  // 2. 逐张分析图片
  for (let i = 0; i < uploadedFiles.value.length; i++) {
    uploadedFiles.value[i].analyzing = true
    try {
      const params = {
        algorithmCode: selectedAlgorithm.value,
        imageBase64: uploadedFiles.value[i].base64,
        needRetImg: true
      }
      const res = await $API.imageAnalysis(params)
      results.value[i] = res?.resData || {}
      uploadedFiles.value[i].analyzing = false

      // Draw overlay if needed
      await nextTick()
      drawOverlay(i)
    } catch (err) {
      console.error('Analysis failed for image:', uploadedFiles.value[i].name, err)
      results.value[i] = { error: true }
      uploadedFiles.value[i].analyzing = false
    }
  }

  // 3. 所有图片分析完成后，主动销毁后端的任务，释放庞大的大模型显存！
  try {
    const cancelParams = {
      mvDebug: 'MiniV-Debug',
      algorithmCode: selectedAlgorithmInfo.value.algorithmId
    }
    await $API.pTaskCancle(cancelParams)
    taskCreated.value = false // 标记为已销毁，下次再点开始时重新创建
  } catch (err) {
    console.warn('Backend cleanup failed:', err)
  }

  analyzing.value = false
  ElMessage.success(t('imageAnalysis.analysisComplete', { n: uploadedFiles.value.length }))
}

// Canvas overlay drawing for detection results
const drawOverlay = (index) => {
  const result = results.value[index]
  const targets = getTargets(result)
  if (!targets.length) return

  const canvasEl = document.querySelector(`canvas[data-index="${index}"]`)
  if (!canvasEl) return

  const imgEl = canvasEl.parentElement?.querySelector('img')
  if (!imgEl) return

  const imgW = imgEl.naturalWidth || imgEl.width
  const imgH = imgEl.naturalHeight || imgEl.height
  canvasEl.width = imgW
  canvasEl.height = imgH

  const ctx = canvasEl.getContext('2d')
  const colors = ['#3182ce', '#ec4899', '#f59e0b', '#10b981', '#3b82f6', '#ef4444', '#4299e1', '#06b6d4']

  targets.forEach((target, tIdx) => {
    if (!target.box) return
    const color = colors[tIdx % colors.length]
    // 后端返回的是像素坐标
    const { x, y, width: bw, height: bh } = target.box
    // 跳过无效的零尺寸框的矩形绘制（如 Qwen3VL 纯文本结果 box={0,0,0,0}）
    if (bw > 0 || bh > 0) {
      ctx.strokeStyle = color
      ctx.lineWidth = 2
      ctx.strokeRect(x, y, bw, bh)
    }

    const label = `${getTargetLabel(target)} ${getTargetConfidence(target)}`
    if (label) {
      let labelX = x
      let labelY = y
      if (bw === 0 && bh === 0) {
        labelX = 10
        labelY = 30 + tIdx * 25
      }
      
      ctx.font = '14px sans-serif'
      const metrics = ctx.measureText(label)
      const pad = 4
      ctx.fillStyle = color
      ctx.fillRect(labelX, labelY - 20, metrics.width + pad * 2, 20)
      ctx.fillStyle = '#ffffff'
      ctx.fillText(label, labelX + pad, labelY - 5)
    }

    // 绘制关键点 (绿色圆点)
    if (target.landmark && target.landmark.length > 0) {
      ctx.fillStyle = '#00ff00'
      target.landmark.forEach(pt => {
        ctx.beginPath()
        ctx.arc(pt.x, pt.y, 3, 0, Math.PI * 2)
        ctx.fill()
      })
    }
  })
}

// Result helpers
const getTargets = (result) => {
  if (!result) return []
  // Merge areaList targets and top-level targetList
  const targets = []
  if (result.areaList) {
    result.areaList.forEach(area => {
      if (area.targetList) {
        area.targetList.forEach(t => targets.push(t))
      }
    })
  }
  if (result.targetList) {
    result.targetList.forEach(t => targets.push(t))
  }
  return targets
}

const getTargetCount = (result) => {
  return getTargets(result).length
}

const getTargetLabel = (target) => {
  if (!target?.confidence?.length) return t('common.unknown')
  const top = target.confidence.reduce((a, b) =>
    (b.confidence || 0) > (a.confidence || 0) ? b : a
  , target.confidence[0])
  return top?.label || t('common.unknown')
}

const getTargetConfidence = (target) => {
  if (!target?.confidence?.length) return '-'
  const top = target.confidence.reduce((a, b) =>
    (b.confidence || 0) > (a.confidence || 0) ? b : a
  , target.confidence[0])
  return top?.confidence != null ? `${(top.confidence * 100).toFixed(1)}%` : '-'
}

const openPreview = (index) => {
  previewItem.value = displayItems.value[index]
  previewIndex.value = index
  previewVisible.value = true
}

// 预览大图中绘制检测框
const drawPreviewOverlay = () => {
  const result = previewItem.value?.result
  const targets = getTargets(result)
  if (!targets.length) return

  nextTick(() => {
    const canvasEl = previewCanvasRef.value
    const imgEl = previewImageRef.value
    if (!canvasEl || !imgEl) return

    const imgW = imgEl.naturalWidth || imgEl.width
    const imgH = imgEl.naturalHeight || imgEl.height
    // canvas 内部分辨率与原图一致，确保坐标对齐
    canvasEl.width = imgW
    canvasEl.height = imgH

    const ctx = canvasEl.getContext('2d')
    const colors = ['#3182ce', '#ec4899', '#f59e0b', '#10b981', '#3b82f6', '#ef4444', '#4299e1', '#06b6d4']

    targets.forEach((target, tIdx) => {
      if (!target.box) return
      const color = colors[tIdx % colors.length]
      const { x, y, width: bw, height: bh } = target.box
      // 跳过无效的零尺寸框的矩形绘制（如 Qwen3VL 纯文本结果 box={0,0,0,0}）
      if (bw > 0 || bh > 0) {
        ctx.strokeStyle = color
        ctx.lineWidth = Math.max(2, Math.round(imgW / 500))
        ctx.strokeRect(x, y, bw, bh)
      }

      const label = `${getTargetLabel(target)} ${getTargetConfidence(target)}`
      if (label) {
        const fontSize = Math.max(14, Math.round(imgW / 80))
        const pad = 4
        const labelH = fontSize + pad * 2
        
        let labelX = x
        let labelY = y
        if (bw === 0 && bh === 0) {
          labelX = 10
          labelY = labelH + 10 + tIdx * (labelH + 5)
        }

        ctx.font = `${fontSize}px sans-serif`
        const metrics = ctx.measureText(label)
        
        ctx.fillStyle = color
        ctx.fillRect(labelX, labelY - labelH, metrics.width + pad * 2, labelH)
        ctx.fillStyle = '#ffffff'
        ctx.fillText(label, labelX + pad, labelY - pad - 2)
      }

      // 绘制关键点 (绿色圆点)
      if (target.landmark && target.landmark.length > 0) {
        const radius = Math.max(3, Math.round(imgW / 300))
        ctx.fillStyle = '#00ff00'
        target.landmark.forEach(pt => {
          ctx.beginPath()
          ctx.arc(pt.x, pt.y, radius, 0, Math.PI * 2)
          ctx.fill()
        })
      }
    })
  })
}

const onPreviewImageLoad = () => {
  drawPreviewOverlay()
}
</script>

<style lang="scss" scoped>
.image-analysis-page {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 16px;
  gap: 16px;
  overflow: hidden;
}

/* ─ Toolbar ─ */
.toolbar-section {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 20px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-md, 8px);
  box-shadow: var(--shadow-sm);
  flex-shrink: 0;
}

.toolbar-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.algorithm-select {
  width: 280px;
}

.toolbar-right {
  display: flex;
  align-items: center;
}

.file-count {
  font-size: 13px;
  color: var(--text-secondary);
  padding: 4px 12px;
  background: var(--bg-secondary);
  border-radius: 12px;
}

/* ─ Content ─ */
.content-section {
  flex: 1;
  overflow-y: auto;
  padding-bottom: 16px;
}

.image-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;
}

.image-card {
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-md, 8px);
  overflow: hidden;
  box-shadow: var(--shadow-sm);
  transition: all 0.25s ease;
  cursor: pointer;

  &:hover {
    border-color: var(--primary-color);
    box-shadow: 0 4px 16px rgba(49, 130, 206, 0.15);
    transform: translateY(-2px);
  }

  &.has-result {
    border-color: rgba(16, 185, 129, 0.3);
  }
}

.image-wrapper {
  position: relative;
  width: 100%;
  height: 240px;
  background: #f8f9fb;
  overflow: hidden;
}

.analysis-image {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.overlay-canvas {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.image-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  border-top: 1px solid var(--border-color);
}

.image-name {
  font-size: 13px;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 200px;
}

.image-status {
  flex-shrink: 0;
}

/* ─ Result Panel ─ */
.result-panel {
  padding: 10px 14px;
  border-top: 1px solid var(--border-color);
  background: linear-gradient(135deg, rgba(49, 130, 206, 0.03) 0%, rgba(16, 185, 129, 0.03) 100%);
}

.result-section {
  .result-label {
    font-size: 12px;
    font-weight: 600;
    color: var(--text-secondary);
    margin-bottom: 6px;

    &.empty-result {
      color: var(--text-tertiary, #c0c4cc);
      font-weight: 400;
      text-align: center;
      padding: 4px 0;
    }
  }
}

.result-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.target-tag {
  border-radius: 4px;
}

/* ─ Empty State ─ */
.empty-state {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-white);
  border: 1px dashed var(--border-color);
  border-radius: var(--radius-md, 8px);
}

.empty-icon {
  width: 120px;
  height: 120px;
}

/* ─ Preview Dialog ─ */
.preview-dialog {
  :deep(.el-dialog__body) {
    padding: 16px 20px;
    max-height: 75vh;
    overflow-y: auto;
  }
}

.preview-body {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.preview-image-wrap {
  text-align: center;
  background: #f8f9fb;
  border-radius: var(--radius-sm, 4px);
  padding: 8px;
  display: flex;
  justify-content: center;
}

.preview-image-container {
  position: relative;
  display: inline-block;
  line-height: 0;
}

.preview-image {
  max-width: 100%;
  max-height: 50vh;
  object-fit: contain;
  border-radius: 4px;
}

.preview-overlay-canvas {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.preview-details {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.preview-targets-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: 8px;
}

.feature-preview {
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  font-size: 11px;
  color: var(--text-secondary);
  word-break: break-all;
  line-height: 1.4;
}

.feature-preview-empty {
  color: var(--text-tertiary, #c0c4cc);
}
</style>
