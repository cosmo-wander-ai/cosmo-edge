<template>
  <el-dialog class="dialogtype ui-admin-dialog" v-if="dialogVisible" :title="title" v-model="dialogVisible" @close="handleClose" width="560px" center>
    <div>
      <el-form class="form-wrap" :model="ruleForm" :rules="rules" ref="ruleFormRef" :label-width="currentLocale === 'en-US' ? '160px' : '125px'">
        <el-form-item prop="photo" :label="t('basePic.photo')">
          <Upload :fileList="fileList" :limit="5" @click="onProgress" @delete="onDelete" />
        </el-form-item>
      </el-form>
      <span style="font-size: 12px; color: #1890FF; margin-left: 125px;">{{ t('basePic.photoSizeTip') }}</span>

      <div class="obsBtoon">
        <el-button @click="handleClose" size="small">{{ t('action.cancel') }}</el-button>
        <el-button @click="submitForm('ruleForm')" type="primary" size="small">{{ t('action.save') }}</el-button>
      </div>
    </div>
  </el-dialog>
</template>

<script setup>
import { ref, reactive, watch, getCurrentInstance } from 'vue'
import Upload from './Upload.vue'
import { t, currentLocale } from '@/i18n'
import { uploadFileInChunks, UploadPurpose } from '@/utils/chunkUpload'

const { proxy } = getCurrentInstance()

// Props
const props = defineProps({
  title: String,
  visible: Boolean,
  data: Object,
  selectList: Array
})

// Emits
const emit = defineEmits(['update:visible', 'updateLib'])

// Refs
const ruleFormRef = ref(null)

// Reactive data
const dialogVisibleother = ref(false)
const datasuccess = ref(false)
const dialogVisible = ref(false)

const ruleForm = reactive({
  serialNumber: '',
  personName: '',
  faceLibId: []
})

const fileList = ref([])
const personLibList = ref([])

const rules = {
  personName: [
    { required: true, message: t('basePic.nameRequired'), trigger: 'blur' },
    {
      min: 1,
      max: 32,
      message: t('basePic.nameMaxLength'),
      trigger: 'change'
    }
  ],
  faceLibId: [
    { required: true, message: t('basePic.selectFaceLib'), trigger: ['blur'] }
  ],
  photo: [
    {
      validator: validatePass,
      trigger: ['blur', 'change'],
      required: true
    }
  ],
  serialNumber: [
    { max: 32, message: t('basePic.codeMaxLength'), trigger: 'change' },
    { validator: validateSerial, trigger: 'blur' }
  ],
  thingsLibId: [
    {
      required: true,
      message: t('basePic.selectWorkClothesLib'),
      trigger: ['blur', 'change']
    }
  ]
}

// Watchers
watch(() => props.visible, (newValue) => {
  ruleFormRef.value && ruleFormRef.value.resetFields()
  datasuccess.value = false
  dialogVisible.value = newValue
}, { deep: true })

watch(() => props.data, (newValue) => {
  if (newValue.id) {
    initData()
  }
}, { deep: true })

// Methods
function validatePersonID(rule, value, callback) {
  if (!value) {
    callback(new Error(t('basePic.codeRequired')))
  } else {
    var len = 0
    for (var i = 0; i < value.length; i++) {
      var c = value.charCodeAt(i)
      //单字节加1
      if ((c >= 0x0001 && c <= 0x007e) || (0xff60 <= c && c <= 0xff9f)) {
        len++
      } else {
        len += 2
      }
    }
    if (len > 32) {
      callback(new Error(t('basePic.codeMaxLength')))
    } else {
      callback()
    }
  }
}

function validateSerial(rule, value, callback) {
  var reg = /^[a-zA-Z0-9]+$/
  if (!value || reg.test(value)) {
    callback()
  } else {
    callback(new Error(t('basePic.codeAlphanumeric')))
  }
}

function handleClose() {
  clearPendingPreviews()
  Object.assign(ruleForm, {
    serialNumber: '',
    personName: '',
    faceLibId: []
  })
  fileList.value = []
  emit('update:visible', false)
}

function validatePass(rule, value, callback) {
  if (!fileList.value.length) {
    callback(new Error(t('basePic.photoRequired')))
  } else {
    callback()
  }
}

function initData() {
  clearPendingPreviews()
  ruleForm.serialNumber = props.data.serialNumber
  ruleForm.personName = props.data.name
  const faceLibId = []
  props.data.faceLibIdList.forEach((element) => {
    faceLibId.push(element.id)
  })
  ruleForm.faceLibId = faceLibId
  const fileListData = []
  props.data.pictureList.forEach((element) => {
    fileListData.push({
      pictureUrl: element.url,
      pictureName: element.pictureName || ''
    })
  })
  fileList.value = fileListData
}

function onProgress(file, index) {
  const supportedTypes = new Set(['image/jpeg', 'image/png', 'image/bmp'])
  if (!file || !supportedTypes.has(file.type)) {
    return proxy.$message.error(t('basePic.photoFormatError'))
  }
  if (!Number.isSafeInteger(file.size) || file.size <= 0) {
    return proxy.$message.error(t('api.error.UpLoadDataEmpty'))
  }

  const entry = {
    rawFile: file,
    previewUrl: URL.createObjectURL(file),
    pictureName: ''
  }
  if (index || index === 0) {
    releasePreview(fileList.value[index])
    fileList.value[index] = entry
  } else {
    fileList.value.push(entry)
  }
}

function releasePreview(entry) {
  if (entry?.previewUrl) URL.revokeObjectURL(entry.previewUrl)
}

function clearPendingPreviews() {
  fileList.value.forEach(releasePreview)
}

function onDelete(entry) {
  releasePreview(entry)
}

async function submitForm() {
  try {
    await ruleFormRef.value.validate()
  } catch (_) {
    return
  }

  const stagedUploads = []
  try {
    const thingsList = []
    for (const entry of fileList.value) {
      if (!entry.rawFile) {
        thingsList.push({
          pictureUrl: entry.pictureUrl,
          pictureName: entry.pictureName || ''
        })
        continue
      }
      const staged = await uploadFileInChunks(entry.rawFile, {
        purpose: UploadPurpose.IMAGE,
        uploadChunk: formData => proxy.$API.uploadAtomicModelTemp(formData),
        cancelUpload: data => proxy.$API.cancelAtomicModelUpload(data),
        getCapabilities: () => proxy.$API.getUploadCapabilities()
      })
      stagedUploads.push(staged)
      thingsList.push({
        pictureUploadId: staged.uploadId,
        pictureName: entry.pictureName || ''
      })
    }

    await proxy.$API.addLibThings({
      thingsOperation: 1,
      thingsLibId: props.data.thingsLibId,
      thingsList
    })
    datasuccess.value = true
    dialogVisible.value = false
    emit('updateLib')
    proxy.$message.success(t('common.operationSucceeded'))
    Object.assign(ruleForm, {
      serialNumber: '',
      personName: '',
      faceLibId: []
    })
    clearPendingPreviews()
    fileList.value = []
  } catch (error) {
    await Promise.allSettled(stagedUploads.map(upload =>
      proxy.$API.cancelAtomicModelUpload({ uploadId: upload.uploadId })
    ))
    throw error
  }
}
</script>

<style scoped lang="scss">
.pro {
  padding-left: 110px;
  font-size: 14px;
  color: #09aaff;
  span {
    cursor: pointer;
  }
}
.dialogFrom {
  width: 100%;
}
.form-wrap {
  padding: 0 30px 0 10px;
  > p {
    border-left: 3px solid var(--primary-color);
    margin-left: 15px;
    padding-left: 10px;
    line-height: 14px;
  }
  .mv-el-input {
    width: 300px;
  }
  .el-select {
    width: 300px;
  }
}
.obsBtoon {
  text-align: center;
  margin-top: 30px;
  .el-button + .el-button {
    margin-left: 20px;
  }
}
.avatar-uploader .el-upload {
  border: 1px dashed #d9d9d9;
  border-radius: 6px;
  cursor: pointer;
  position: relative;
  overflow: hidden;
}
.avatar-uploader .el-upload:hover {
  border-color: var(--primary-color);
}
.avatar-uploader-icon {
  font-size: 28px;
  color: #3598ff;
  width: 100px;
  height: 140px;
  line-height: 140px;
  text-align: center;
  border: 1px solid #f2f6fc;
  background-color: #f2f6fc;
  border-radius: 4px;
}
.avatar {
  width: 100px;
  height: 140px;
  display: block;
  border-radius: 4px;
  object-fit: cover;
}
.upload-tip {
  color: #cccccc;
  font-size: 12px;
  line-height: 12px;
  .pic_tip {
    font-size: 16px;
    cursor: pointer;
  }
}
.pic-example-wrap {
  .right-example-wrap {
    margin-top: 20px;
    padding: 20px 55px;
    display: flex;
    .right-pic {
      flex-shrink: 0;
      width: 80px;
      height: 96px;
      border-radius: 4px;
      margin-right: 30px;
    }
    .right-title {
      display: flex;
      flex-direction: column;
      justify-content: center;
      > p {
        margin: 5px;
        line-height: 22px;
        &:first-child {
          font-size: 16px;
          font-weight: bold;
          color: var(--text-primary);
        }
        &:last-child {
          font-size: 14px;
          color: var(--text-secondary);
        }
      }
    }
  }
  .error-example-wrap {
    padding: 20px 55px;
    .error-title {
      font-size: 16px;
      font-weight: bold;
      color: var(--text-primary);
    }
    .error-pic {
      margin-top: 30px;
      display: flex;
      justify-content: space-between;
      > div {
        display: flex;
        flex-direction: column;
        align-items: center;
        > span {
          width: 60px;
          height: 71px;
          border-radius: 4px;
          > img {
            object-fit: cover;
          }
        }
        > label {
          margin-top: 10px;
          font-size: 14px;
          color: var(--text-primary);
        }
      }
    }
  }
}
</style>
