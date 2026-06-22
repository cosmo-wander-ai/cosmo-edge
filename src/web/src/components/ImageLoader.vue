<template>
  <div ref="bodyRef" class="main-body">
    <div v-if="isUpload && imgUrl">
      <img class="bg" src="@/assets/loader_bg.png" alt="">
      <img class="loading" src="@/assets/loader_loading.svg" alt="">
      <div class="upload-text">{{ t('common.uploading') }}</div>
    </div>
    <div v-else>
      <img class="bg-fit" src="@/assets/error-image.png" alt="">
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import moment from 'moment'
import { t } from '@/i18n'

const props = defineProps({
  timestamp: {
    type: Number
  },
  startTime: {
    type: String
  },
  width: {
    type: String,
    default: '48px'
  },
  height: {
    type: String,
    default: '48px'
  },
  imgUrl: {
    type: String,
    default: ''
  }
})

const bodyRef = ref(null)
const isUpload = ref(false)

onMounted(() => {
  if (bodyRef.value) {
    bodyRef.value.style.width = props.width
    bodyRef.value.style.height = props.height
  }

  let start
  if (props.timestamp) {
    start = moment(props.timestamp).format('YYYY-MM-DD HH:mm:ss')
  } else {
    start = props.startTime
  }
  const end = moment(new Date()).format('YYYY-MM-DD HH:mm:ss')
  const nextDay = moment(end).diff(start, 'day')
  isUpload.value = nextDay < 1
})
</script>

<style lang="scss" scoped>
.main-body {
  position: relative;
  width: 48px;
  height: 48px;
}

.bg {
  position: absolute;
  width: 100%;
  height: 100%;
  object-fit: fill;
}

.bg-fit {
  position: absolute;
  width: 100%;
  height: 100%;
  object-fit: none;
}

.loading {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -100%);
}

.upload-text {
  width: 100%;
  position: absolute;
  top: 50%;
  font-size: 12px;
  color: white;
  text-align: center;
}
</style>
