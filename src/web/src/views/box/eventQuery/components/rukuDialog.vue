<template>
  <div>
    <el-dialog 
      :title="t('event.captureImageStorage')" 
      :model-value="show" 
      center 
      @close="emit('update:visible', false)" 
      width="500px"
    >
      <el-form :model="formData" label-width="120px" label-position="right">
        <el-form-item :label="t('event.detectedImage') + localeColon">
          <el-image 
            class="detected-img" 
            :src="rukuData.detectedPicture" 
            fit="fit" 
            @click="handleImageClick"
          />
        </el-form-item>
        <el-form-item :label="rukuLabel + localeColon">
          <el-select v-model="formData.libId" size="small" clearable>
            <el-option 
              v-for="item in options" 
              :label="item.name" 
              :value="item.id" 
              :key="item.id"
            />
          </el-select>
        </el-form-item>
      </el-form>

      <template #footer>
        <span class="dialog-footer">
          <el-button @click="emit('update:visible', false)" size="small">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="sureClick" size="small">{{ t('action.confirm') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch, getCurrentInstance } from 'vue'
import { t, localeColon } from '@/i18n'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  rukuData: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['update:visible'])

const { proxy } = getCurrentInstance()

const show = ref(false)
const options = ref([])
const formData = ref({
  libId: ''
})
const rukuLabel = ref(t('event.selectWorkClothesLib'))
const currentProperty = ref({})

const handleImageClick = () => {
  // 如果有图片预览方法，可以在这里调用
  // proxy.$imgView([props.rukuData.detectedPicture])
}

const queryPersonLibInfo = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.queryPersonLibInfo(params).then((res) => {
    const { resData } = res
    options.value = resData.personLibList || []
  })
}

const queryThingsLibInfo = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.queryThingsLibInfo(params).then((res) => {
    const { resData } = res
    options.value = resData.thingsLibList || []
  })
}

const sureClick = () => {
  if (currentProperty.value?.workClothesRecognition) {
    addWorkClothes()
  } else {
    addThings()
  }
}

const addWorkClothes = () => {
  if (!formData.value.libId) {
    proxy.$message.warning(t('event.selectWorkClothesLibWarning'))
    return
  }
  const params = {
    personOperation: 1,
    personLibId: formData.value.libId,
    personList: [{
      pictureUrl: props.rukuData.detectedPicture
    }]
  }
  proxy.$API.addLibPerson(params).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    emit('update:visible', false)
  })
}

const addThings = () => {
  if (!formData.value.libId) {
    proxy.$message.warning(t('event.selectThingsLibWarning'))
    return
  }
  const params = {
    thingsOperation: 1,
    thingsLibId: formData.value.libId,
    thingsList: [{
      pictureUrl: props.rukuData.detectedPicture
    }]
  }
  proxy.$API.addLibThings(params).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    emit('update:visible', false)
  })
}

watch(() => props.visible, (newVal) => {
  show.value = newVal
  if (newVal) {
    formData.value.libId = ''
    const result = JSON.parse(props.rukuData.property)
    currentProperty.value = result
    if (result?.workClothesRecognition) {
      rukuLabel.value = t('event.selectWorkClothesLib')
      queryPersonLibInfo()
    } else {
      rukuLabel.value = t('event.selectMachineThingsLib')
      queryThingsLibInfo()
    }
  }
})
</script>

<style lang="scss" scoped>
.detected-img {
  margin-top: 10px;
  max-width: 200px;
  max-height: 200px;
  cursor: pointer;
}
</style>
