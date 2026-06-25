<template>
  <div class="main-container">
    <div class="left-config">
      <div class="btn-tools">
        <el-button @click="reloadImage" type="primary" size="small">{{ t('action.reloadImage') }}</el-button>
        <el-button v-if="retroDirectParamIndex !== -1" :disabled="isDrawingLine" @click="changeRetroDirection"
          type="primary" size="small">{{ t('action.adjustArrowDirection') }}</el-button>
      </div>
      <detection-canvas id="onboarding-detection-canvas" ref="canvasRef" :width="width" :height="height" :imageSrc="imgSrc" :allPoints="allPoints"
        :activeIndex="activeIndex" :shieldActiveIndex="shieldActiveIndex" :regionType="regionType"
        :isDrawingLine="isDrawingLine" :associatedAreaConfig="associatedAreaConfig"
        :retroDirectType="retroDirectType"></detection-canvas>
    </div>
    <div class="right-config">
      <!-- 检测线 -->
      <div v-if="regionType == 'cordon' || regionType == 'oneWayCordon'" class="area-content">
        <div class="area-top">
          <div>{{ t('glossary.detectionLineList') }}</div>
        </div>
        <div class="area-tools">
          <el-button @click="drawLine" :type="isDrawingLine ? 'success' : 'primary'" size="small">{{ isDrawingLine ?
            t('action.finishDrawing') : t('action.draw') }}</el-button>
          <el-button v-if="regionType === 'cordon'" :disabled="!isDrawingLine && activeIndex === null"
            @click="bidirectionalClick" type="primary" size="small">{{ t('action.toggleDirectionMode') }}</el-button>
          <el-button @click="changeDirection" :disabled="!isDrawingLine && activeIndex === null" type="primary"
            size="small">{{ t('action.switchDirection') }}</el-button>
          <el-button @click="deleteDrawingLine" :disabled="!isDrawingLine" type="primary" size="small">{{ t('action.delete') }}</el-button>
        </div>
        <el-table :data="props.config?.taskAreaRows || []" style="width: 100%" :key="`detection-table-${tableKey}`"
          @row-click="chooseRow($event, 'detection')" :row-class-name="setRowIndex" :row-style="hightlight">
          <el-table-column :label="t('field.name')" show-overflow-tooltip>
            <template #default="scope">
              <span :style="{ color: colors[scope.row.index] }">{{ scope.row.name }}</span>
            </template>
          </el-table-column>
          <el-table-column :label="t('field.actions')" min-width="90">
            <template #default="scope">
              <div class="tool-btns">
                <el-button link @click="handleEdit(scope.$index)">{{ t('action.edit') }}</el-button>
                <el-button class="delete-btn" link @click="handleDelete(scope.$index)">{{ t('action.delete') }}</el-button>
              </div>
            </template>
          </el-table-column>
        </el-table>
      </div>

      <!-- 检测区域 -->
      <div v-else-if="props.config?.taskAreaHeader && props.config.taskAreaHeader.length !== 0" class="area-content">
        <div class="area-top">
          <div>{{ t('glossary.detectionAreaList') }}</div>
          <el-button id="onboarding-add-area" @click="addArea('detection')" type="primary" size="small">{{ t('glossary.addArea') }}</el-button>
        </div>
        <el-table :data="props.config?.taskAreaRows || []" style="width: 100%"
          @row-click="chooseRow($event, 'detection')" :row-class-name="setRowIndex" :row-style="hightlight">
          <el-table-column v-for="(item, index) in props.config?.taskAreaHeader || []" :key="index" :label="resolveHeaderName(item)"
            show-overflow-tooltip>
            <template #header="scope">{{ scope.column.label }}</template>
            <template #default="scope">
              <span :style="{ color: colors[scope.row.index] }">{{ scope.row[item.key] }}</span>
            </template>
          </el-table-column>
          <el-table-column :label="t('field.actions')" min-width="90">
            <template #default="scope">
              <div class="tool-btns">
                <el-button link @click="handleEdit(scope.$index, 'detection')">{{ t('action.edit') }}</el-button>
                <el-button class="delete-btn" link
                  @click="handleDelete(scope.$index, 'detection')">{{ t('action.delete') }}</el-button>
              </div>
            </template>
          </el-table-column>
        </el-table>
      </div>

      <!-- 屏蔽区域 -->
      <div v-if="props.config?.shieldAreaHeader && props.config.shieldAreaHeader.length !== 0" class="area-content">
        <div class="area-top">
          <div>{{ t('glossary.shieldAreaList') }}</div>
          <el-button @click="addArea('shield')" type="primary" size="small">{{ t('glossary.addArea') }}</el-button>
        </div>
        <el-table :data="props.config?.shieldAreaRows || []" style="width: 100%"
          @row-click="chooseRow($event, 'shield')" :row-class-name="setRowIndex" :row-style="shieldHightlight">
          <el-table-column v-for="(item, index) in props.config?.shieldAreaHeader || []" :key="index" :label="resolveHeaderName(item)"
            show-overflow-tooltip>
            <template #header="scope">{{ scope.column.label }}</template>
            <template #default="scope">
              <span>{{ scope.row[item.key] }}</span>
            </template>
          </el-table-column>
          <el-table-column :label="t('field.actions')" min-width="90">
            <template #default="scope">
              <div class="tool-btns">
                <el-button link @click="handleEdit(scope.$index, 'shield')">{{ t('action.edit') }}</el-button>
                <el-button class="delete-btn" link @click="handleDelete(scope.$index, 'shield')">{{ t('action.delete') }}</el-button>
              </div>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>

    <el-dialog id="onboarding-area-dialog" :title="dialogTitle" v-model="addDialogVisible" width="560px" center>
      <dynamicform v-model="addAreaDialogConfig" :labelWidth="'200px'" :algorithmCode="props.algorithmCode" ref="submitFormRef" @update:modelValue="handleFormUpdate"></dynamicform>
      <template #footer>
        <span class="dialog-footer">
          <el-button size="small" @click="cancelAddClick">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" size="small" class="mv-el-button" @click="sureAddClick">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <el-dialog :title="t('common.notice')" v-model="sureDeleteClick" center width="402px">
      <div style="text-align: center">{{ t('action.confirmDelete') }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button size="small" @click="sureDeleteClick = false">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" size="small" class="mv-el-button" @click="deleteArea">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, getCurrentInstance, nextTick } from 'vue'
import { t } from '@/i18n'
import { resolveResourceParamText } from '@/utils/i18nResource'
import DetectionCanvas from './DetectionCanvas.vue'
import dynamicform from './dynamicForm.vue'
import { getDefaultPointsRatio } from './defaultConfig.js'
import { v4 } from 'uuid'
import CatchPhoto from '@/assets/CatchPhoto.png'

const { proxy } = getCurrentInstance()

const props = defineProps({
  config: {
    type: Object,
    default: () => ({
      taskAreaRows: [],
      shieldAreaRows: [],
      taskAreaHeader: [],
      shieldAreaHeader: [],
      taskParam: [],
      channelId: '',
      regionType: '',
      maxAreaCount: 4,
      defaultFullScreen: false,
      areasTitle: []
    })
  },
  algorithmCode: [String, Number]
})

const emit = defineEmits(['update:config'])
const resolveHeaderName = (item) =>
  resolveResourceParamText(item, props.algorithmCode, 'name')

const platformType = localStorage.getItem('platformType') || ''
const width = ref(550)
const height = ref(550 / 1.8)
const imgSrc = ref('')
const isLoadingImage = ref(false) // 添加加载状态
const lastChannelId = ref('') // 缓存上次的channelId
let imageLoadTimer = null // 防抖定时器
const allPoints = ref([])
const activeIndex = ref(null)
const shieldActiveIndex = ref(null)
const colors = ['rgb(24,144,255)', 'rgb(255,115,24)', 'rgb(24,255,89)', 'rgb(255,239,24)']
const areaDialogMode = ref('add-area')
const dialogTitle = computed(() => {
  const map = {
    'add-area': t('glossary.addArea'),
    'edit-area': t('glossary.editArea'),
    'add-line': t('glossary.addDetectionLine')
  }
  return map[areaDialogMode.value] || t('glossary.addArea')
})
const addDialogVisible = ref(false)
const sureDeleteClick = ref(false)
const addAreaType = ref('detection')
const addAreaDialogConfig = ref([])
const regionType = ref('')
const retroDirectParamIndex = ref(null)
const retroDirectType = ref(null)
const isDrawingLine = ref(false)
const associatedAreaConfig = ref(null)
const currentAlgorithmCode = ref(props.algorithmCode)
const tableKey = ref(0) // 用于强制重新渲染表格

// 本地响应式数据，确保表格实时更新
const localTaskAreaRows = ref([])

const canvasRef = ref(null)
const submitFormRef = ref(null)

// 临时存储当前绘制的检测线名称
const currentDrawingLineName = ref('')

const handlePoints = () => {
  allPoints.value = []
  props.config?.taskAreaRows?.forEach((element, index) => {
    const area = {
      points: [],
      associatedAreas: [],
      shieldPoints: [],
      linePoints: [],
      directionType: element.directionType,
      index: index
    }
    element.points?.forEach((pointRatio) => {
      area.points.push(ratioToPoints(pointRatio))
    })
    element.associatedAreas?.forEach((pointRatio) => {
      area.associatedAreas.push(ratioToPoints(pointRatio))
    })
    element.linePoints?.forEach((pointRatio) => {
      area.linePoints.push(ratioToPoints(pointRatio))
    })
    allPoints.value.push(area)
  })

  props.config?.shieldAreaRows?.forEach((element, index) => {
    if (allPoints.value[index]) {
      allPoints.value[index].shieldPoints = []
      element.shieldPoints?.forEach((pointRatio) => {
        allPoints.value[index].shieldPoints.push(ratioToPoints(pointRatio))
      })
    } else {
      const area = {
        points: [],
        associatedAreas: [],
        shieldPoints: [],
        linePoints: [],
        directionType: element.directionType,
        index: index
      }
      element.shieldPoints?.forEach((pointRatio) => {
        area.shieldPoints.push(ratioToPoints(pointRatio))
      })
      allPoints.value.push(area)
    }
  })

  if (activeIndex.value === null && shieldActiveIndex.value === null && (props.config?.taskAreaRows?.length || 0) !== 0) {
    activeIndex.value = (props.config?.taskAreaRows?.length || 1) - 1
  }
  if (activeIndex.value !== null) return
  if (shieldActiveIndex.value === null && (props.config?.shieldAreaRows?.length || 0) !== 0) {
    shieldActiveIndex.value = (props.config?.shieldAreaRows?.length || 1) - 1
  }
}

const reloadImage = () => {
  getImage(true) // 传递true参数强制刷新
}

const getImage = (forceRefresh = false) => {
  // 确保channelId存在才获取图片
  if (!props.config?.channelId) {
    imgSrc.value = CatchPhoto
    lastChannelId.value = ''
    return
  }
  
  // 如果channelId没有变化且不是强制刷新，直接返回
  if (lastChannelId.value === props.config.channelId && imgSrc.value && !forceRefresh) {
    return
  }
  
  // 如果正在加载中，避免重复请求
  if (isLoadingImage.value) {
    return
  }
  
  // 清除之前的定时器
  if (imageLoadTimer) {
    clearTimeout(imageLoadTimer)
  }
  
  // 使用防抖，避免短时间内多次调用
  imageLoadTimer = setTimeout(() => {
    isLoadingImage.value = true
    lastChannelId.value = props.config.channelId
    
    let params = {
      channelId: props.config.channelId,
      cacheEnable: true
    }
    
    if (platformType == '15') {
      proxy.$API.boxRecaptureImage({ videoChannelId: params.channelId }).then((res) => {
        const { resData } = res
        imgSrc.value = resData.url
      }).catch(() => {
        imgSrc.value = CatchPhoto
      }).finally(() => {
        isLoadingImage.value = false
      })
    } else {
      proxy.$API.recaptureImage(params).then((res) => {
        const { resData } = res
        if (resData == 'DEFAULT_EMPTY_SNAP') {
          imgSrc.value = CatchPhoto
        } else {
          imgSrc.value = resData
        }
      }).catch(() => {
        imgSrc.value = CatchPhoto
      }).finally(() => {
        isLoadingImage.value = false
      })
    }
  }, forceRefresh ? 0 : 100) // 强制刷新时立即执行，否则延迟100ms
}

const changeRetroDirection = () => {
  retroDirectType.value = retroDirectType.value == '0' ? '1' : '0'
  if (props.config?.taskParam && retroDirectParamIndex.value !== -1) {
    props.config.taskParam[retroDirectParamIndex.value].value = retroDirectType.value
  }
}

const ratioToPoints = (ratio) => {
  return [width.value * ratio.xRatio, height.value * ratio.yRatio]
}

const chooseRow = (row, type) => {
  if (isDrawingLine.value) return
  if (type == 'shield') {
    activeIndex.value = null
    shieldActiveIndex.value = row.index
  } else {
    shieldActiveIndex.value = null
    activeIndex.value = row.index
  }
}

const setRowIndex = ({ row, rowIndex }) => {
  row.index = rowIndex
}

const hightlight = ({ rowIndex }) => {
  if (activeIndex.value == rowIndex) {
    return { 'background-color': 'rgba(24,144,255,.2)' }
  }
}

const shieldHightlight = ({ rowIndex }) => {
  if (shieldActiveIndex.value == rowIndex) {
    return { 'background-color': 'rgba(24,144,255,.2)' }
  }
}

// 处理表单数据更新
const handleFormUpdate = (newData) => {
  console.log('表单数据更新:', newData)
  if (newData && Array.isArray(newData)) {
    // 确保响应式更新
    addAreaDialogConfig.value = [...newData]
    
    // 特别记录 name 字段的更新
    const nameItem = newData.find(item => item.key === 'name')
    if (nameItem) {
      console.log('检测线/区域名称已更新为:', nameItem.value)
      
      // 如果是编辑模式，实时更新显示
      if (areaDialogMode.value === 'edit-area' && activeIndex.value !== null) {
        console.log('编辑模式下实时更新名称显示')
      }
    }
  }
}

const addArea = (type) => {
  addAreaType.value = type
  areaDialogMode.value = 'add-area'
  if (addAreaType.value == 'shield') {
    if ((props.config?.shieldAreaRows?.length || 0) >= (props.config?.maxAreaCount || 0)) {
      return proxy.$message.error(t('validate.areaLimitReached'))
    }
    addAreaDialogConfig.value = (props.config?.shieldAreaHeader || []).map((item) => ({
      ...item,
      value: item.defaultValue || '' // 确保有默认值
    }))
  } else {
    if ((props.config?.taskAreaRows?.length || 0) >= (props.config?.maxAreaCount || 0)) {
      return proxy.$message.error(t('validate.areaLimitReached'))
    }
    addAreaDialogConfig.value = (props.config?.taskAreaHeader || []).map((item) => ({
      ...item,
      value: item.defaultValue || '' // 确保有默认值
    }))
  }
  console.log('addAreaDialogConfig设置为:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
  
  // 等待下一个tick，确保组件已经渲染
  nextTick(() => {
    addDialogVisible.value = true
  })
}

const handleEdit = (index, type) => {
  addAreaType.value = type
  if (isDrawingLine.value) return proxy.$message.warning(t('validate.completeDrawingFirst'))
  areaDialogMode.value = 'edit-area'

  if (type == 'shield') {
    shieldActiveIndex.value = index
    activeIndex.value = null
    addAreaDialogConfig.value = (props.config?.shieldAreaHeader || []).map((item) => {
      let obj = { ...item }
      obj.value = props.config?.shieldAreaRows?.[index]?.[item.key] || item.defaultValue || ''
      return obj
    })
  } else {
    activeIndex.value = index
    shieldActiveIndex.value = null
    
    // 检查是否是检测线（通过regionType判断）
    const isDetectionLine = props.config?.regionType === 'cordon' || props.config?.regionType === 'oneWayCordon'
    
    if (isDetectionLine) {
      // 为检测线创建专门的编辑配置
      const currentRow = props.config?.taskAreaRows?.[index]
      addAreaDialogConfig.value = [
        {
          key: 'name',
          name: t('field.name'),
          defaultValue: '',
          description: t('validate.areaNameDescription'),
          type: 'text',
          regexpr: '/^\\S{1,32}$/',
          failedTip: t('validate.areaNameMaxNoSpaces'),
          value: currentRow?.name || '',
          isColumn: true
        }
      ]
      console.log('检测线编辑配置:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
    } else {
      // 普通检测区域的编辑配置
      addAreaDialogConfig.value = (props.config?.taskAreaHeader || []).map((item) => {
        let obj = { ...item }
        obj.value = props.config?.taskAreaRows?.[index]?.[item.key] || item.defaultValue || ''
        return obj
      })
    }
  }
  console.log('编辑时addAreaDialogConfig设置为:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
  
  // 等待下一个tick，确保数据已经设置
  nextTick(() => {
    addDialogVisible.value = true
  })
}

const handleDelete = (index, type) => {
  if (isDrawingLine.value) return proxy.$message.warning(t('validate.completeDrawingFirst'))
  if (type === 'shield') {
    shieldActiveIndex.value = index
    activeIndex.value = null
  } else {
    activeIndex.value = index
    shieldActiveIndex.value = null
  }
  sureDeleteClick.value = true
}

const sureAddClick = async () => {
  const canSubmit = submitFormRef.value.submitForm()
  if (!canSubmit) return

  if (areaDialogMode.value === 'add-line') {
    console.log('当前addAreaDialogConfig:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
    
    // 第二步：获取检测线的名称并开始绘制
    const lineName = addAreaDialogConfig.value.find(item => item.key === 'name')?.value || t('glossary.detectionLine')
    
    console.log('获取到的检测线名称:', lineName)
    
    if (!lineName.trim()) {
      return proxy.$message.error(t('validate.areaNameRequired'))
    }
    
    // 存储检测线名称，用于完成绘制时使用
    currentDrawingLineName.value = lineName.trim()
    
    // 开始绘制模式
    isDrawingLine.value = true
    activeIndex.value = null
    shieldActiveIndex.value = null
    addDialogVisible.value = false
    
    // 设置canvas的方向类型并清理之前的绘制状态
    await nextTick()
    if (canvasRef.value) {
      canvasRef.value.directionType = props.regionType == 'cordon' ? '1' : '0'
      // 清理之前的绘制点
      if (canvasRef.value.drawingLinePoints) {
        canvasRef.value.drawingLinePoints.length = 0 // 清空数组但保持引用
        console.log('已清空之前的绘制点')
      }
      // 清理选中的多边形
      canvasRef.value.selectedPolygon = null
      console.log('绘制状态已初始化，方向类型:', canvasRef.value.directionType)
    }
    
    console.log('开始绘制检测线，名称:', lineName)
    return
  }

  // 处理其他类型的区域添加/编辑
  savePointsBeforeChange()

  // 创建config的深拷贝以确保响应式更新
  const updatedConfig = { ...props.config }
  
  // 确保数组也是深拷贝
  if (updatedConfig.taskAreaRows) {
    updatedConfig.taskAreaRows = updatedConfig.taskAreaRows.map(row => ({ ...row }))
  }
  if (updatedConfig.shieldAreaRows) {
    updatedConfig.shieldAreaRows = updatedConfig.shieldAreaRows.map(row => ({ ...row }))
  }

  addAreaDialogConfig.value.forEach((item) => {
    if (addAreaType.value == 'detection') {
      if (areaDialogMode.value == 'add-area') {
        const newArea = { ...(updatedConfig?.taskAreaRows?.[0] || {}) }
        newArea.areaId = v4()
        newArea.points = getDefaultPointsRatio(width.value, height.value, updatedConfig?.regionType, updatedConfig?.defaultFullScreen)
        if (associatedAreaConfig.value) {
          newArea.associatedAreas = getDefaultPointsRatio(width.value, height.value, associatedAreaConfig.value[1].regionType, false, 150)
        }
        if (!updatedConfig?.taskAreaRows) {
          updatedConfig.taskAreaRows = []
        }
        // 设置新区域的属性
        newArea[item.key] = item.value
        updatedConfig.taskAreaRows.push(newArea)
        shieldActiveIndex.value = null
        activeIndex.value = updatedConfig.taskAreaRows.length - 1
      } else if (areaDialogMode.value == 'edit-area') {
        // 编辑现有区域（包括检测线）
        console.log(`编辑检测区域/线，索引: ${activeIndex.value}, 字段: ${item.key}, 新值: ${item.value}`)
        if (updatedConfig?.taskAreaRows?.[activeIndex.value]) {
          updatedConfig.taskAreaRows[activeIndex.value] = {
            ...updatedConfig.taskAreaRows[activeIndex.value],
            [item.key]: item.value
          }
          console.log('更新后的数据:', updatedConfig.taskAreaRows[activeIndex.value])
        }
      }
    } else if (addAreaType.value == 'shield') {
      if (areaDialogMode.value == 'add-area') {
        const regionTypeValue = updatedConfig?.regionType
        const newArea = { name: '', areaId: v4(), shieldPoints: [] }
        newArea.shieldPoints = getDefaultPointsRatio(
          width.value,
          height.value,
          regionTypeValue == 'hexagon' || regionTypeValue == 'quadrilateral' ? regionTypeValue : 'quadrilateral',
          updatedConfig?.defaultFullScreen
        )
        // 设置新区域的属性
        newArea[item.key] = item.value
        if (!updatedConfig?.shieldAreaRows) {
          updatedConfig.shieldAreaRows = []
        }
        updatedConfig.shieldAreaRows.push(newArea)
        activeIndex.value = null
        shieldActiveIndex.value = updatedConfig.shieldAreaRows.length - 1
      } else {
        // 编辑现有区域
        if (updatedConfig?.shieldAreaRows?.[shieldActiveIndex.value]) {
          updatedConfig.shieldAreaRows[shieldActiveIndex.value] = {
            ...updatedConfig.shieldAreaRows[shieldActiveIndex.value],
            [item.key]: item.value
          }
        }
      }
    }
  })
  
  // 触发更新事件通知父组件
  console.log('触发config更新:', JSON.parse(JSON.stringify(updatedConfig)))
  emit('update:config', updatedConfig)
  
  // 强制重新渲染表格
  tableKey.value++
  
  // 强制更新表格数据显示
  nextTick(() => {
    // 触发 handlePoints 重新处理数据
    handlePoints()
  })
  
  // 延迟关闭对话框，确保数据更新完成
  await nextTick()
  addDialogVisible.value = false
}

// 保存绘制的检测线数据
const saveDrawingLineData = () => {
  console.log('开始保存绘制数据...')
  
  if (!canvasRef.value) {
    console.error('canvasRef 不存在')
    proxy.$message.error(t('validate.canvasMissing'))
    return
  }
  
  if (!currentDrawingLineName.value) {
    console.error('检测线名称为空')
    proxy.$message.error(t('validate.areaNameEmpty'))
    return
  }
  
  // 获取canvas中的绘制数据
  let result
  try {
    result = canvasRef.value.submit()
    console.log('canvas.submit() 返回的数据:', JSON.parse(JSON.stringify(result)))
  } catch (e) {
    console.error('调用 canvas.submit() 失败:', e)
    proxy.$message.error(t('validate.drawingDataFailed'))
    return
  }
  
  if (!result || result.length === 0) {
    console.error('submit() 返回空数据')
    proxy.$message.error(t('validate.noDrawingData'))
    return
  }
  
  // 创建config的深拷贝
  const updatedConfig = { ...props.config }
  if (updatedConfig.taskAreaRows) {
    updatedConfig.taskAreaRows = [...updatedConfig.taskAreaRows]
  } else {
    updatedConfig.taskAreaRows = []
  }
  
  // 查找最新绘制的线条数据（通常是最后一个）
  const latestDrawing = result[result.length - 1]
  console.log('最新的绘制数据:', latestDrawing)
  
  if (latestDrawing && latestDrawing.linePoints && latestDrawing.linePoints.length >= 2) {
    // 创建新的检测线数据
    const newLineArea = {
      name: currentDrawingLineName.value,
      areaId: v4(),
      points: [],
      linePoints: latestDrawing.linePoints,
      directionType: latestDrawing.directionType || (props.regionType == 'cordon' ? '1' : '0'),
      index: updatedConfig.taskAreaRows.length
    }
    
    // 如果有关联区域配置，也添加关联区域
    if (latestDrawing.associatedAreas && latestDrawing.associatedAreas.length > 0) {
      newLineArea.associatedAreas = latestDrawing.associatedAreas
    }
    
    updatedConfig.taskAreaRows.push(newLineArea)
    
    console.log('保存检测线数据:', newLineArea)
    console.log('更新后的 taskAreaRows:', updatedConfig.taskAreaRows)
    
    // 触发更新事件
    emit('update:config', updatedConfig)
    
    // 强制重新渲染表格
    tableKey.value++
    
    // 清空当前绘制的名称
    currentDrawingLineName.value = ''
    
    proxy.$message.success(t('common.detectionLineSaved', { name: newLineArea.name }))
  } else {
    console.error('linePoints 数据无效:', latestDrawing?.linePoints)
    proxy.$message.error(t('validate.invalidLinePoints'))
  }
}

const savePointsBeforeChange = () => {
  const result = canvasRef.value.submit()
  
  // 创建config的深拷贝
  const updatedConfig = { ...props.config }
  if (updatedConfig.taskAreaRows) {
    updatedConfig.taskAreaRows = [...updatedConfig.taskAreaRows]
  }
  if (updatedConfig.shieldAreaRows) {
    updatedConfig.shieldAreaRows = [...updatedConfig.shieldAreaRows]
  }
  
  result.forEach((item, index) => {
    if (item.points.length > 0 || item.linePoints.length > 0) {
      if (updatedConfig?.taskAreaRows?.[index]) {
        updatedConfig.taskAreaRows[index] = {
          ...updatedConfig.taskAreaRows[index],
          points: item.points,
          associatedAreas: item.associatedAreas,
          linePoints: item.linePoints,
          directionType: item.directionType,
          index: index
        }
      } else {
        const newArea = {
          name: addAreaDialogConfig.value[0]?.value || '',
          areaId: v4(),
          points: item.points,
          shieldPoints: item.shieldPoints,
          associatedAreas: item.associatedAreas,
          linePoints: item.linePoints,
          directionType: item.directionType,
          index: index
        }
        if (!updatedConfig.taskAreaRows) {
          updatedConfig.taskAreaRows = []
        }
        updatedConfig.taskAreaRows.push(newArea)
      }
    }

    if (item.shieldPoints && item.shieldPoints.length > 0) {
      if (updatedConfig?.shieldAreaRows?.[index]) {
        updatedConfig.shieldAreaRows[index] = {
          ...updatedConfig.shieldAreaRows[index],
          shieldPoints: item.shieldPoints,
          index: index
        }
      } else {
        const newArea = {
          name: addAreaDialogConfig.value[0]?.value || '',
          areaId: v4(),
          shieldPoints: item.shieldPoints,
          index: index
        }
        if (!updatedConfig.shieldAreaRows) {
          updatedConfig.shieldAreaRows = []
        }
        updatedConfig.shieldAreaRows.push(newArea)
      }
    }
  })
  
  // 触发更新事件
  emit('update:config', updatedConfig)
}

const cancelAddClick = () => {
  addDialogVisible.value = false
}

const deleteArea = () => {
  savePointsBeforeChange()
  sureDeleteClick.value = false
  
  // 创建config的深拷贝
  const updatedConfig = { ...props.config }
  
  if (shieldActiveIndex.value !== null) {
    if (updatedConfig?.shieldAreaRows) {
      updatedConfig.shieldAreaRows = [...updatedConfig.shieldAreaRows]
      updatedConfig.shieldAreaRows.splice(shieldActiveIndex.value, 1)
    }
  } else {
    if (updatedConfig?.taskAreaRows) {
      updatedConfig.taskAreaRows = [...updatedConfig.taskAreaRows]
      updatedConfig.taskAreaRows.splice(activeIndex.value, 1)
    }
  }
  
  // 触发更新事件
  console.log('删除后触发config更新:', updatedConfig)
  emit('update:config', updatedConfig)
  
  // 强制重新渲染表格
  tableKey.value++
  
  activeIndex.value = null
  shieldActiveIndex.value = null
}

const drawLine = () => {
  if (!isDrawingLine.value) {
    // 开始新的绘制
    if ((props.config?.taskAreaRows?.length || 0) >= (props.config?.maxAreaCount || 0)) {
      return proxy.$message.error(t('validate.areaLimitReached'))
    }
    areaDialogMode.value = 'add-line'
    
    // 创建表单配置
    const formConfig = {
      key: 'name',
      name: t('field.name'),
      defaultValue: '',
      description: t('validate.areaNameDescription'),
      type: 'text',
      regexpr: '/^\\S{1,32}$/',
      failedTip: t('validate.areaNameMaxNoSpaces'),
      value: '', // 确保初始值为空字符串
      isColumn: true
    }
    
    addAreaDialogConfig.value = [formConfig]
    console.log('drawLine设置addAreaDialogConfig:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
    
    // 等待下一个tick，确保数据已经设置
    nextTick(() => {
      addDialogVisible.value = true
      activeIndex.value = null
    })
  } else {
    // 完成当前绘制
    console.log('完成绘制，保存检测线数据')
    
    // 检查是否有绘制的线条数据
    if (!canvasRef.value) {
      return proxy.$message.error(t('validate.canvasMissing'))
    }
    
    const drawingPoints = canvasRef.value.drawingLinePoints
    console.log('当前绘制的点数:', drawingPoints?.length)
    
    if (!drawingPoints || drawingPoints.length < 2) {
      return proxy.$message.warning(t('validate.drawAtLeastTwoPoints'))
    }
    
    // 保存当前绘制的线条数据
    saveDrawingLineData()
    
    // 结束绘制模式
    isDrawingLine.value = false
    
    // 设置活动索引为新添加的检测线
    nextTick(() => {
      if (activeIndex.value === null) {
        activeIndex.value = (props.config?.taskAreaRows?.length || 1) - 1
      }
    })
    
    console.log('绘制完成，检测线已保存')
  }
}

const bidirectionalClick = () => {
  if (isDrawingLine.value) {
    const directionTypeValue = canvasRef.value?.directionType
    canvasRef.value.directionType = directionTypeValue === '0' ? '1' : '0'
  } else {
    const directionTypeValue = allPoints.value[activeIndex.value].directionType
    allPoints.value[activeIndex.value].directionType = directionTypeValue === '0' ? '1' : '0'
  }
  canvasRef.value?.drawLineOperation('type')
}

const changeDirection = () => {
  if (isDrawingLine.value) {
    canvasRef.value.isReversed = !canvasRef.value.isReversed
    canvasRef.value.drawingLinePoints.reverse()
  } else {
    const linePoints = allPoints.value[activeIndex.value].linePoints
    linePoints.reverse()
  }
  canvasRef.value?.drawLineOperation('direction')
}

const deleteDrawingLine = () => {
  if (isDrawingLine.value) {
    // 清理绘制数据
    if (canvasRef.value) {
      canvasRef.value.drawLineOperation('delete')
      canvasRef.value.drawingLinePoints = []
      canvasRef.value.selectedPolygon = null
    }
    
    // 结束绘制模式
    isDrawingLine.value = false
    
    // 清空当前绘制的名称
    currentDrawingLineName.value = ''
    
    console.log('已取消当前绘制')
  }
}

onMounted(() => {
  handlePoints()
  // 只在有channelId时才获取图片
  if (props.config?.channelId) {
    getImage()
  }
})

watch(() => props.config, (newVal, oldVal) => {
  if (!newVal) return
  regionType.value = newVal.regionType
  if (isDrawingLine.value) {
    canvasRef.value.drawingLinePoints = []
    isDrawingLine.value = false
    canvasRef.value.selectedPolygon = null
  }

  retroDirectParamIndex.value = newVal?.taskParam?.findIndex((item) => item.type === 'retroDirect') ?? -1
  if (retroDirectParamIndex.value !== -1) {
    retroDirectType.value = newVal?.taskParam?.[retroDirectParamIndex.value]?.value
  } else {
    retroDirectType.value = null
  }
  associatedAreaConfig.value = newVal?.areasTitle
  
  // 只在channelId真正发生变化时获取图片
  if (!oldVal || (newVal.channelId && newVal.channelId !== oldVal.channelId)) {
    getImage()
  }
}, { deep: true, immediate: true })

watch(() => props.algorithmCode, (newVal) => {
  activeIndex.value = null
  shieldActiveIndex.value = null
  currentAlgorithmCode.value = newVal
  getImage()
})

watch(() => props.config?.taskAreaRows, (newVal) => {
  if (newVal) {
    handlePoints()
  }
}, { deep: true })

watch(() => props.config?.shieldAreaRows, (newVal) => {
  if (newVal) {
    handlePoints()
  }
}, { deep: true })

// 监听 addAreaDialogConfig 的变化，用于调试
watch(() => addAreaDialogConfig.value, (newVal) => {
  console.log('addAreaDialogConfig变化:', JSON.parse(JSON.stringify(newVal)))
  // 特别关注 name 字段的变化
  const nameItem = newVal.find(item => item.key === 'name')
  if (nameItem) {
    console.log('检测线名称字段变化:', nameItem.value)
  }
}, { deep: true })

// 监听对话框显示状态，确保数据同步
watch(() => addDialogVisible.value, (newVal) => {
  if (newVal) {
    console.log('对话框打开时的addAreaDialogConfig:', JSON.parse(JSON.stringify(addAreaDialogConfig.value)))
  }
})
</script>

<style lang="scss" scoped>
.main-container {
  margin-top: 20px;
  padding-bottom: 30px;
  display: flex;
}

.left-config {
  width: 550px;

  .btn-tools {
    margin-bottom: 10px;
  }
}

.right-config {
  flex: 1;
  display: flex;
  flex-direction: column;
  margin-left: 15px;
  min-width: 200px;

  .area-content {
    margin-bottom: 20px;
  }

  .area-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 10px;
  }

  .area-tools {
    margin-bottom: 10px;
  }

  .tool-btns {
    .el-button {
      padding: 0;
    }

    .delete-btn {
      color: #f56c6c;
    }
  }
}
</style>
