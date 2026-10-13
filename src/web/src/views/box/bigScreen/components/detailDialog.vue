<template>
  <div>
    <el-dialog v-model="show" :title="t('event.eventDetails')" width="800px" center @close="close" class="ui-admin-dialog ui-scroll-dialog event-detail-dialog">
      <div v-if="show" class="content-body">
        <div class="info-item">
          <span class="info-item-title">
            {{ t('event.alarmType') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ resolveResourceAlgorithmName(detailData) }}
          </span>
        </div>

        <!-- 所属通道 -->
        <div class="info-item">
          <span class="info-item-title">
            {{ t('event.channel') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ detailData.channelName }}
          </span>
        </div>

        <!-- 区域名称 -->
        <div class="info-item">
          <span class="info-item-title">
            {{ t('event.areaName') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ detailData.areaName }}
          </span>
        </div>

        <!-- 告警时间 -->
        <div class="info-item">
          <span class="info-item-title">
            {{ t('event.alarmTime') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ dateFormat(Number(detailData.timestamp)) }}
          </span>
        </div>

        <!-- 检测照 -->
        <div class="info-item" v-if="detailData.detectedPicture">
          <span class="info-item-title">
            {{ t('event.detectedImage') }}{{ localeColon }}
          </span>
          <el-image class="capture-img3" :src="detailData.detectedPicture" fit="contain" :preview-src-list="[detailData.detectedPicture]">
            <template #error>
              <div class="image-slot">
                <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
              </div>
            </template>
          </el-image>
        </div>

        <!-- 工服底库照 -->
        <template v-if="checkObj(workClothesRecognitionData)">
          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.baseImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="workClothesRecognitionData.baseImageUrl" fit="contain" :preview-src-list="[workClothesRecognitionData.baseImageUrl]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>

          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.workClothesLibrary') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ workClothesRecognitionData.groupName }}
            </span>
          </div>

          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.similarity') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ formatSimilarity(workClothesRecognitionData.matchDegree) }}
            </span>
          </div>
        </template>

        <!-- 机物 -->
        <template v-if="checkObj(machineMaterialData)">
          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.baseImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="machineMaterialData.baseImageUrl" fit="contain" :preview-src-list="[machineMaterialData.baseImageUrl]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>

          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.machineMaterialLibrary') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ machineMaterialData.groupName }}
            </span>
          </div>

          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.similarity') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ formatSimilarity(machineMaterialData.matchDegree) }}
            </span>
          </div>
        </template>

        <!-- 全景照 -->
        <div class="info-item-big" v-if="detailData.fullPicture">
          <span class="info-item-title">
            {{ t('event.fullImage') }}{{ localeColon }}
          </span>
          <el-image class="capture-img3" :src="detailData.fullPicture" fit="contain" :preview-src-list="[detailData.fullPicture]">
            <template #error>
              <div class="image-slot">
                <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
              </div>
            </template>
          </el-image>
        </div>

        <div class="info-item-big" v-if="detailData.video">
          <span class="info-item-title">
            {{ t('event.video') }}{{ localeColon }}
          </span>
          <div class="video-bg">
            <el-image class="capture-img3" :src="detailData.fullPicture"></el-image>
            <i class="el-icon-video-play" @click="videoDialogVisiable = true"></i>
          </div>
        </div>

      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" @click="close" class="mv-el-button ui-secondary-button">{{ t('action.close') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <video-frequency class="wran-video" v-model="videoDialogVisiable" :closable="false" :url="detailData.video" :structureDataUrl="detailData.videostructured"></video-frequency>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import moment from 'moment'
import ImageLoader from '@/components/ImageLoader.vue'
import VideoFrequency from '@/components/videoPlaying265.vue'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import { formatSimilarity } from '@/utils/format'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  detailData: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['update:visible'])

const show = ref(false)
const videoDialogVisiable = ref(false)
const machineMaterialData = ref({})
const workClothesRecognitionData = ref({})

watch(() => props.visible, (val) => {
  show.value = val
  console.log('========detailData========', props.detailData)
  if (val && props.detailData?.property) {
    machineMaterialData.value =
      props.detailData?.property?.machineMaterial || {}
    workClothesRecognitionData.value =
      props.detailData?.property?.workClothesRecognition || {}
  } else {
    machineMaterialData.value = {}
    workClothesRecognitionData.value = {}
  }
})

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
.video-bg {
  position: relative;
  width: 100%;
  max-width: 100%;
  height: clamp(200px, 40dvh, 360px);
  border: 1px solid var(--border-color);
  border-radius: 7px;
  background: #171c24;
  overflow: hidden;
}
.video-bg .capture-img3 { width: 100%; height: 100%; border: 0; border-radius: 0; }
.video-bg .el-icon-video-play {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  display: block;
  font-size: 48px;
  color: #fff;
  cursor: pointer;
}
.wran-video :deep(.mini-video-play-download) { display: none; }
</style>
