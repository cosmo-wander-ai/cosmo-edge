<template>
  <div>
    <el-dialog v-model="show" :title="t('action.details')" width="800px" center @close="close">
      <div v-if="show" class="content-body">
        <div class="info-item-big">
          <span class="info-item-title">
              {{ t('event.eventType') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ resolveResourceAlgorithmName(detailData) }}
          </span>
        </div>

        <!-- 人脸比对-->
        <template v-if="checkObj(recognitionData)">
          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.captureImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="detailData.detectedPicture" fit="contain" :preview-src-list="[detailData.detectedPicture]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>

          <div class="info-item" v-if="recognitionData.LibImage">
            <span class="info-item-title">
              {{ t('event.faceBaseImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="recognitionData.LibImage" fit="contain" :preview-src-list="[recognitionData.LibImage]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>

          <div class="info-item" v-if="recognitionData.matchName">
            <span class="info-item-title">
              {{ t('event.personName') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ recognitionData.matchName }}
            </span>
          </div>

          <div class="info-item" v-if="recognitionData.personCode">
            <span class="info-item-title">
              {{ t('event.personCode') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ recognitionData.personCode }}
            </span>
          </div>

          <div class="info-item" v-if="recognitionData.matchLibName">
            <span class="info-item-title">
              {{ t('event.faceLibrary') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ recognitionData.matchLibName }}
            </span>
          </div>

          <div class="info-item" v-if="recognitionData.matchDegree  && recognitionData.matchDegree != '-1'">
            <span class="info-item-title">
              {{ t('event.similarity') }}{{ localeColon }}
            </span>
            <span class="info-item-content">
              {{ recognitionData.matchDegree ? recognitionData.matchDegree  : '' }}
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

        <!-- 人脸检测 -->
        <template v-else-if="checkObj(faceData)">
          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.captureImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="faceData.image" fit="contain" :preview-src-list="[faceData.image]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>
        </template>

        <!-- 人体检测 -->
        <template v-if="checkObj(bodyData)">
          <div class="info-item">
            <span class="info-item-title">
              {{ t('event.captureImage') }}{{ localeColon }}
            </span>
            <el-image class="capture-img3" :src="bodyData.image" fit="contain" :preview-src-list="[bodyData.image]">
              <template #error>
                <div class="image-slot">
                  <ImageLoader width="176px" height="100px" :timestamp="Number(detailData.timestamp)" />
                </div>
              </template>
            </el-image>
          </div>
        </template>

        <!-- 所属通道 -->
        <div class="info-item-big">
          <span class="info-item-title">
            {{ t('field.channelName') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ detailData.channelName }}
          </span>
        </div>

        <!-- 告警时间 -->
        <div class="info-item-big">
          <span class="info-item-title">
            {{ t('event.captureTime') }}{{ localeColon }}
          </span>
          <span class="info-item-content">
            {{ dateFormat(Number(detailData.timestamp)) }}
          </span>
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
  }
})

const emit = defineEmits(['update:visible'])

const show = ref(false)
const faceData = ref({})
const bodyData = ref({})
const recognitionData = ref({})

watch(() => props.visible, (val) => {
  show.value = val
  console.log('========detailData========', props.detailData)
  if (val && props.detailData?.property) {
    faceData.value = props.detailData?.property?.face || {}
    bodyData.value = props.detailData?.property?.body || {}
    recognitionData.value = props.detailData?.property?.recognition || {}
  } else {
    faceData.value = {}
    bodyData.value = {}
    recognitionData.value = {}
  }
})

const close = () => {
  emit('update:visible', false)
}

const dateFormat = (value) => {
  if (!value) return ''
  return moment(value).format('YYYY-MM-DD HH:mm:ss')
}

const checkObj = (obj) => {
  if (obj) {
    return Object.keys(obj).length
  }
  return 0
}
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
  // white-space: nowrap;
  // text-overflow: ellipsis;
  // overflow: hidden;
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
</style>
