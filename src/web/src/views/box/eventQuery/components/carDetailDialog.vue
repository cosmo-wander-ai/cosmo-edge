<template>
  <div>
    <el-dialog :model-value="show" :title="t('action.details')" width="800px" center @close="close">
      <div v-if="show" class="content-body">
        <div class="info-item">
          <span class="info-item-title">{{ t('event.eventType') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ resolveResourceAlgorithmName(detailData) }}</span>
        </div>

        <div class="info-item"></div>

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

        <div class="info-item"></div>

        <!-- 所属通道 -->
        <div class="info-item">
          <span class="info-item-title">{{ t('event.channel') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ detailData.channelName }}</span>
        </div>

        <div class="info-item"></div>

        <!-- 告警时间 -->
        <div class="info-item">
          <span class="info-item-title">{{ t('event.captureTime') }}{{ localeColon }}</span>
          <span class="info-item-content">{{ dateFormat(Number(detailData.timestamp)) }}</span>
        </div>
      </div>

      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" @click="close" class="mv-el-button">{{ t('action.close') }}</el-button>
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
.mv-el-button {
  width: 77px;
  height: 32px;
  padding: 0px;
}

.content-body {
  display: flex;
  flex-wrap: wrap;
  padding: 0 20px;
}

.info-item {
  width: 50%;
  display: flex;
  margin-bottom: 20px;
}

.info-item-title {
  display: inline-block;
  align-self: baseline;
  flex-shrink: 0;
  min-width: 77px;
  white-space: nowrap;
  margin-right: 8px;
}

.info-item-content {
  display: inline-block;
  align-self: baseline;
}

.capture-img3 {
  width: 176px;
  height: 100px;
  object-fit: cover;
}

.capture-img4 {
  width: 280px;
  height: 156px;
}
</style>
