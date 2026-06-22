<template>
  <div>
    <div class="warning-set">
      <div class="warning-set-form">
        <el-form :label-width="currentLocale === 'en-US' ? '200px' : '140px'" label-position="right" :model="formData">
          <!-- 告警图片设置 -->
          <div class="section-title">{{ t('systemManage.alarmImageSettings') }}</div>
          <el-form-item>
            <template #label>
              <span>{{ t('systemManage.panoramaQuality') }}
                <el-tooltip effect="dark" placement="top">
                  <template #content>
                    <p>{{ t('systemManage.panoramaQualityTooltip') }}</p>
                  </template>
                  <i class="el-icon-question" />
                </el-tooltip>
              </span>
              {{ localeColon }}
            </template>
            <el-input-number v-model="formData.fullPictureQuality" :min="1" :max="99" size="small" style="width: 120px" :controls="false" @change="handleQualityChange" />
          </el-form-item>

          <el-form-item :label="t('systemManage.panoramaOverlay')">
            <el-checkbox :value="isAllChecked" @change="handleCheckAll">{{ t('systemManage.selectAll') }}</el-checkbox>
            <div class="checkbox-group">
              <el-checkbox v-model="formData.alarmNameOverlay" :true-value="1" :false-value="0">{{ t('systemManage.overlayAlarmType') }}</el-checkbox>
              <el-checkbox v-model="formData.areaOverlay" :true-value="1" :false-value="0">{{ t('systemManage.overlayAreaDetection') }}</el-checkbox>
              <el-checkbox v-model="formData.targetBoxOverlay" :true-value="1" :false-value="0">{{ t('systemManage.overlayTargetDetection') }}</el-checkbox>
              <el-checkbox v-model="formData.targetSizeOverlay" :true-value="1" :false-value="0">{{ t('systemManage.overlayTargetSize') }}</el-checkbox>
            </div>
          </el-form-item>

          <!-- 告警录像设置 -->
          <div class="section-title">{{ t('systemManage.alarmVideoSettings') }}</div>
          <el-form-item>
            <template #label>
              <span>{{ t('systemManage.alarmVideo') }}
                <el-tooltip effect="dark" placement="top">
                  <template #content>
                    <p>{{ t('systemManage.alarmVideoDefaultOn') }}</p>
                  </template>
                  <i class="el-icon-question" />
                </el-tooltip>
              </span>
              {{ localeColon }}
            </template>
            <el-switch v-model="formData.switch" />
          </el-form-item>

          <el-form-item v-if="formData.switch">
            <template #label>
              <span>{{ t('systemManage.alarmVideoDuration') }}
                <el-tooltip effect="dark" placement="top">
                  <template #content>
                    <p>{{ t('systemManage.alarmVideoDurationTooltip') }}</p>
                  </template>
                  <i class="el-icon-question" />
                </el-tooltip>
              </span>
              {{ localeColon }}
            </template>
            <div class="time-input">
              <span>{{ t('systemManage.preSeconds') }}</span>
              <el-input-number v-model="formData.videoPreTime" :min="1" :max="9" :controls="false" size="small" style="width: 100px" />
              <span>{{ t('systemManage.postSeconds') }}</span>
              <el-input-number v-model="formData.videoPostTime" :min="1" :max="9" :controls="false" size="small" style="width: 100px" />
              <span>{{ t('systemManage.secondsUnit') }}</span>
            </div>
          </el-form-item>
        </el-form>
      </div>

      <div class="form-footer">
        <el-button @click="handleReset" size="small">{{ t('action.reset') }}</el-button>
        <el-button type="primary" @click="handleSave" size="small">{{ t('action.save') }}</el-button>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, computed, onMounted, getCurrentInstance } from 'vue'
import { ElMessage } from 'element-plus'
import { t, localeColon, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const formData = ref({
  fullPictureQuality: 75,
  alarmNameOverlay: 1,
  areaOverlay: 1,
  targetBoxOverlay: 1,
  targetSizeOverlay: 1,
  switch: 0,
  videoPreTime: 5,
  videoPostTime: 5,
  alarmOutputs: []
})

const isAllChecked = computed(() => {
  return (
    formData.value.alarmNameOverlay === 1 &&
    formData.value.areaOverlay === 1 &&
    formData.value.targetBoxOverlay === 1 &&
    formData.value.targetSizeOverlay === 1
  )
})

const init = () => {
  queryPictureQuality()
  queryAlarmVideoDuration()
}

const queryPictureQuality = () => {
  $API.queryPictureQuality().then((res) => {
    const { resData } = res
    formData.value.fullPictureQuality = resData.fullPictureQuality
    formData.value.alarmNameOverlay = resData.alarmNameOverlay
    formData.value.areaOverlay = resData.areaOverlay
    formData.value.targetBoxOverlay = resData.targetBoxOverlay
    formData.value.targetSizeOverlay = resData.targetSizeOverlay
  })
}

const queryAlarmVideoDuration = () => {
  $API.queryAlarmVideoDuration().then((res) => {
    const { resData } = res
    formData.value.switch = resData.switch
    formData.value.videoPreTime = resData.videoPreTime
    formData.value.videoPostTime = resData.videoPostTime
  })
}

const handleCheckAll = (val) => {
  const value = val ? 1 : 0
  formData.value.alarmNameOverlay = value
  formData.value.areaOverlay = value
  formData.value.targetBoxOverlay = value
  formData.value.targetSizeOverlay = value
}

const handleSave = () => {
  const params1 = {
    fullPictureQuality: formData.value.fullPictureQuality,
    alarmNameOverlay: formData.value.alarmNameOverlay,
    areaOverlay: formData.value.areaOverlay,
    targetBoxOverlay: formData.value.targetBoxOverlay,
    targetSizeOverlay: formData.value.targetSizeOverlay
  }
  const params2 = {
    switch: formData.value.switch,
    videoPreTime: formData.value.videoPreTime,
    videoPostTime: formData.value.videoPostTime
  }

  if (!params2.switch) {
    delete params2.videoPreTime
    delete params2.videoPostTime
  } else {
    if (params2.videoPreTime + params2.videoPostTime > 10) {
      return ElMessage.warning(t('systemManage.alarmVideoMaxDuration'))
    }
  }
  const res1 = $API.setPictureQuality(params1)
  const res2 = $API.setAlarmVideoDuration(params2)
  Promise.all([res1, res2])
    .then(() => {
      ElMessage.success(t('common.operationSucceeded'))
    })
    .catch(() => {
      ElMessage.error(t('common.failed'))
    })
}

const handleQualityChange = (value) => {
  const num = parseInt(value)
  if (isNaN(num)) {
    formData.value.fullPictureQuality = 1
  } else {
    formData.value.fullPictureQuality = num
  }
}

const handleReset = () => {
  const res1 = $API.resetPictureQuality()
  const res2 = $API.resetAlarmVideoDuration()
  Promise.all([res1, res2])
    .then(() => {
      ElMessage.success(t('common.operationSucceeded'))
      init()
    })
    .catch(() => {
      ElMessage.error(t('common.failed'))
    })
}

onMounted(() => {
  init()
})
</script>

<style lang="scss" scoped>
.warning-set {
  display: flex;  
  flex-wrap: wrap;

  .warning-set-form {
    width: 900px;
  }

  .section-title {
    font-size: 14px;
    color: #606266;
    margin: 20px 0;
    padding-left: 10px;
    border-left: 4px solid #409eff;
  }

  .checkbox-group {
    display: flex;
    align-items: center;
  }

  .time-input {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .form-footer {
    margin-top: 20px;
    text-align: center;
  }
}
</style>