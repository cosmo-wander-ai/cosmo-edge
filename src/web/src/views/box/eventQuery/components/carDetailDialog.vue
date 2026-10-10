<template>
  <div>
    <el-dialog :model-value="show" :title="t('action.details')" width="800px" center @close="close" class="ui-admin-dialog ui-scroll-dialog event-detail-dialog">
      <div v-if="show" class="content-body">
        <div class="info-item-big">
          <span class="info-item-title">{{ t('event.eventType') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ resolveResourceAlgorithmName(detailData) }}</span>
        </div>

        <div class="info-item info-item-spacer"></div>

        <!-- 检测照 -->
        <div class="info-item" v-if="detailData.detectedPicture">
          <span class="info-item-title">{{ t('event.detectedImage') }}{{ localeColon }}</span>
          <el-image 
            class="capture-img3" 
            :src="detailData.detectedPicture" 
            fit="contain" 
            :preview-src-list="[detailData.detectedPicture]"
          >
            <template #error>
              <div class="image-slot">
                <ImageLoader 
                  width="176px" 
                  height="100px" 
                  :timestamp="Number(detailData.timestamp)" 
                />
              </div>
            </template>
          </el-image>
        </div>

        <!-- 全景照 -->
        <div class="info-item" v-if="detailData.fullPicture">
          <span class="info-item-title">{{ t('event.fullImage') }}{{ localeColon }}</span>
          <el-image 
            class="capture-img4" 
            :src="detailData.fullPicture" 
            fit="contain" 
            :preview-src-list="[detailData.fullPicture]"
          >
            <template #error>
              <div class="image-slot">
                <ImageLoader 
                  width="280px" 
                  height="156px" 
                  :timestamp="Number(detailData.timestamp)" 
                />
              </div>
            </template>
          </el-image>
        </div>

        <!-- 车辆信息 -->
        <template v-if="checkObj(vehicleData)">
          <div class="info-item">
            <span class="info-item-title">{{ t('event.vehiclePlateNumber') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(vehicleData, null, 'plate') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.vehicleType') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(vehicleData, null, 'vehicleClass') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.vehiclePlateColor') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(vehicleData, null, 'plateColor') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.vehicleBodyColor') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(vehicleData, null, 'vehicleColor') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.vehicleOrientation') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(vehicleData, null, 'orientation') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.enterTime') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(null, targetData, 'inAreaTime') }}</span>
          </div>

          <div class="info-item">
            <span class="info-item-title">{{ t('event.leaveTime') }}{{ localeColon }}</span>
            <span class="info-item-content">{{ returnBaseInfo(null, targetData, 'outAreaTime') }}</span>
          </div>
        </template>

        <div class="info-item info-item-spacer"></div>

        <!-- 所属通道 -->
        <div class="info-item">
          <span class="info-item-title">{{ t('event.channel') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ detailData.channelName }}</span>
        </div>

        <div class="info-item info-item-spacer"></div>

        <!-- 告警时间 -->
        <div class="info-item">
          <span class="info-item-title">{{ t('event.captureTime') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ dateFormat(Number(detailData.timestamp)) }}</span>
        </div>
      </div>

      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" @click="close" class="mv-el-button ui-secondary-button">{{ t('action.close') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import moment from 'moment'
import ImageLoader from '@/components/ImageLoader.vue'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  detailData: {
    type: Object,
    default: () => ({})
  },
  vehicleDictMap: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['update:visible'])

const show = ref(false)
const vehicleData = ref({})
const targetData = ref({})

const close = () => {
  emit('update:visible', false)
}

const dateFormat = (value) => {
  if (!value) return ''
  return moment(Number(value)).format('YYYY-MM-DD HH:mm:ss')
}

const checkObj = (obj) => {
  if (obj) {
    return Object.keys(obj).length
  }
  return 0
}

const returnBaseInfo = (vehicleDataParam, targetDataParam, propertyKey) => {
  console.log(targetDataParam)
  if (vehicleDataParam) {
    switch (propertyKey) {
      case 'orientation':
        return (
          props.vehicleDictMap.vehicleorientation?.find(
            (item) => item.value === vehicleDataParam[propertyKey]
          )?.label || ''
        )
      case 'vehicleColor':
        return (
          props.vehicleDictMap.vehiclecolor?.find(
            (item) => item.value === vehicleDataParam[propertyKey]
          )?.label || ''
        )
      case 'vehicleClass':
        return (
          props.vehicleDictMap.vehicleclass?.find(
            (item) => item.value === vehicleDataParam[propertyKey]
          )?.label || ''
        )
      case 'plateColor':
        return (
          props.vehicleDictMap.vehicleplatecolor?.find(
            (item) => item.value === vehicleDataParam[propertyKey]
          )?.label || ''
        )
      case 'plate':
        return vehicleDataParam[propertyKey] || ''
      default:
        return ''
    }
  } else if (targetDataParam && targetDataParam[propertyKey] && targetDataParam[propertyKey] != '0') {
    return moment(Number(targetDataParam[propertyKey])).format('YYYY-MM-DD HH:mm:ss')
  }
  return ''
}

watch(() => props.visible, (val) => {
  show.value = val
  console.log('========detailData========', props.detailData)
  if (val && props.detailData?.property) {
    const result = JSON.parse(props.detailData.property)
    if (result) {
      vehicleData.value = result?.vehicle || {}
      targetData.value = result?.target || {}
    }
  } else {
    vehicleData.value = {}
    targetData.value = {}
  }
})
</script>

<style lang="scss" scoped>
:deep(.el-dialog.event-detail-dialog) {
  padding: 0;
  max-width: calc(100vw - 32px);
  color: var(--text-primary);
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 10px;
  box-shadow: var(--shadow-lg);
}
:deep(.event-detail-dialog > .el-dialog__header) {
  margin-right: 0;
  padding: 18px 56px 16px 24px;
  border-bottom: 1px solid var(--border-light);
  text-align: left;
}
:deep(.event-detail-dialog > .el-dialog__header .el-dialog__title) {
  color: var(--text-primary);
  font-size: 18px;
  font-weight: 650;
  line-height: 1.4;
  overflow-wrap: anywhere;
}
:deep(.event-detail-dialog > .el-dialog__body) { padding: 20px 24px; }
:deep(.event-detail-dialog > .el-dialog__footer) {
  padding: 12px 24px;
  border-top: 1px solid var(--border-light);
  text-align: right;
}
.mv-el-button { min-width: 80px; height: 32px; }
.content-body {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 18px 24px;
  padding: 0;
}
.info-item,
.info-item-big {
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 0;
}
.info-item-big { grid-column: 1 / -1; }
.info-item-title {
  color: var(--text-secondary);
  font-size: 12px;
  font-weight: 500;
  line-height: 1.5;
  overflow-wrap: anywhere;
}
.info-item-content {
  min-width: 0;
  color: var(--text-primary);
  font-size: 14px;
  font-weight: 500;
  line-height: 1.6;
  overflow-wrap: anywhere;
}
.capture-img3,
.capture-img4,
.image-missing {
  box-sizing: border-box;
  width: 100%;
  max-width: 100%;
  height: clamp(160px, 24dvh, 220px);
  border: 1px solid var(--border-color);
  border-radius: 7px;
  background: var(--bg-primary);
}
.capture-img3 :deep(.el-image__inner),
.capture-img4 :deep(.el-image__inner) { object-fit: contain; }
.info-item-big > .capture-img3,
.info-item-big > .capture-img4 { height: clamp(200px, 40dvh, 360px); }
.image-slot,
.image-missing { display: grid; place-items: center; color: var(--text-secondary); }
.image-slot { width: 100%; height: 100%; overflow: hidden; }
.image-slot :deep(.main-body) { max-width: 100%; }
.image-missing { border-style: dashed; }
@media (max-width: 640px) {
  :deep(.event-detail-dialog > .el-dialog__header) { padding: 16px 48px 14px 16px; }
  :deep(.event-detail-dialog > .el-dialog__body) { padding: 16px; }
  :deep(.event-detail-dialog > .el-dialog__footer) { padding: 12px 16px; }
  .content-body { grid-template-columns: minmax(0, 1fr); gap: 16px; }
}
.info-item-spacer { display: none; }
</style>
