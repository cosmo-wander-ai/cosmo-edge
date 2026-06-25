<template>
  <div>
    <el-dialog v-model="show" :title="t('event.eventDetails')" width="800px" center @close="close">
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
              {{ workClothesRecognitionData.matchDegree ? workClothesRecognitionData.matchDegree : '' }}
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
              {{ machineMaterialData.matchDegree ? machineMaterialData.matchDegree  : '' }}
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
          <el-button type="primary" @click="close" class="mv-el-button">{{ t('action.close') }}</el-button>
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
:deep(.el-dialog) {
  margin: 0 !important;
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
}

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
  // align-items: baseline;
  margin-bottom: 20px;
}

.info-item-title {
  display: inline-block;
  align-self: baseline;
}

.info-item-content {
  display: inline-block;
  align-self: baseline;
  width: 80%;
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
}

.capture-img3 {
  width: 176px;
  height: 100px;
  object-fit: cover;
}

.info-item-big {
  width: 100%;
  display: flex;
  // align-items: baseline;
  margin-bottom: 20px;

  .capture-img3 {
    width: 320px !important;
    height: 180px !important;
  }
}

.video-bg {
  position: relative;
  width: 320px;
  height: 180px;
  background: black;

  .video-bg-img {
    position: relative;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
  }

  .el-icon-video-play {
    position: absolute;
    display: block;
    top: 50%;
    left: 50%;
    font-size: 60px;
    color: white;
    transform: translate(-50%, -50%);
    cursor: pointer;
  }
}

.wran-video:deep(.el-dialog__header) {
  background-image: linear-gradient(180deg, #1d4070 0%, #152345 100%);
  color: white;

  .mini-video-play-download {
    display: none;
  }

  .el-dialog__headerbtn {
    top: 15px;
  }

  .el-dialog__close {
    color: white;
  }
}
</style>
