<template>
  <el-dialog class="dialogtype" v-if="dialogVisible" :title="title" v-model="dialogVisible" @close="handleClose" width="560px" center>
    <div>
      <el-form class="form-wrap" :model="ruleForm" :rules="rules" ref="ruleFormRef" :label-width="currentLocale === 'en-US' ? '130px' : '100px'">
        <el-form-item prop="photo" :label="t('basePic.photo')">
          <Upload :fileList="fileList" :limit="5" @click="onProgress" />
        </el-form-item>
      </el-form>
      <div style="margin-left: 100px;">
        <span style="font-size: 12px; color: #1890FF;">{{ t('basePic.workClothesPhotoTip') }}</span>
      </div>
      <div class="obsBtoon">
        <el-button @click="submitForm" type="primary" size="small">{{ t('action.save') }}</el-button>
        <el-button @click="handleClose" size="small">{{ t('action.cancel') }}</el-button>
      </div>
    </div>
  </el-dialog>
</template>

<script setup>
import { ref, reactive, watch, getCurrentInstance } from 'vue'
import Upload from './Upload.vue'
import { t, currentLocale } from '@/i18n'

// 定义组件名称
defineOptions({
  name: 'InfoCreate'
})

// Props
const props = defineProps({
  title: String,
  visible: Boolean,
  data: Object,
  selectList: Array,
  personLibId: [String, Number]
})

// Emits
const emit = defineEmits(['update:visible', 'updateLib'])

const { proxy } = getCurrentInstance()

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
const ImageBase64 = ref('')

const rules = reactive({
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
  personLibId: [
    {
      required: true,
      message: t('basePic.selectWorkClothesLib'),
      trigger: ['blur', 'change']
    }
  ]
})

// Watch
watch(() => props.visible, (newValue) => {
  ruleFormRef.value && ruleFormRef.value.resetFields()
  datasuccess.value = false
  dialogVisible.value = newValue
}, { deep: true })

watch(() => props.data, (newValue) => {
  if (newValue?.id) {
    initData()
  }
}, { deep: true })

// Methods
function queryPersonLibInfo() {
  const params = {
    personLibName: '',
    personLibType: 1
  }
  proxy.$API.queryPersonLibInfo(params).then((res) => {
    personLibList.value = res.resData.personLibList
  })
}

function validatePersonID(rule, value, callback) {
  if (!value) {
    callback(new Error(t('basePic.codeRequired')))
  } else {
    let len = 0
    for (let i = 0; i < value.length; i++) {
      const c = value.charCodeAt(i)
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
  const reg = /^[a-zA-Z0-9]+$/
  if (!value || reg.test(value)) {
    callback()
  } else {
    callback(new Error(t('basePic.codeAlphanumeric')))
  }
}

function handleClose() {
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
  ruleForm.serialNumber = props.data.serialNumber
  ruleForm.personName = props.data.name
  const faceLibId = []
  props.data.faceLibIdList.forEach((element) => {
    faceLibId.push(element.id)
  })
  ruleForm.faceLibId = faceLibId
  const newFileList = []
  props.data.pictureList.forEach((element) => {
    newFileList.push(element.url)
  })
  fileList.value = newFileList
}

function onProgress(file, index) {
  console.log(file, 'file')
  console.log(index, 'index')
  const isJPG = file.type === 'image/jpeg'
  const isPNG = file.type === 'image/png'
  const isBMP = file.type === 'image/jpg'
  const isLt2M = file.size / 1024 / 1024 < 3

  if (!isJPG && !isPNG && !isBMP) {
    return proxy.$message.error(t('basePic.photoFormatError'))
  }
  if (!isLt2M) {
    return proxy.$message.error(t('basePic.photoSizeError'))
  }

  const reader = new FileReader()
  reader.readAsDataURL(file)
  reader.onload = (e) => {
    let base64
    if (e.target.result.indexOf(',') > -1) {
      base64 = e.target.result.substring(e.target.result.indexOf(',') + 1)
    } else {
      base64 = e.target.result
    }

    proxy.$API.detectPerson({ imageBase64: base64 }).then((res) => {
      console.log(res, 'res')

      if (index || index == 0) {
        fileList.value[index] = {
          pictureUrl: res.resData.pictureUrl,
          pictureName: ''
        }
      } else {
        fileList.value.push({
          pictureUrl: res.resData.pictureUrl,
          pictureName: ''
        })
      }
    })
  }
}

function submitForm() {
  ruleFormRef.value.validate((valid) => {
    if (valid) {
      const params = {
        personOperation: 1,
        personLibId: props.personLibId,
        personList: fileList.value
      }

      proxy.$API.addLibPerson(params).then(() => {
        datasuccess.value = true
        dialogVisible.value = false
        emit('updateLib')
        proxy.$message.success(t('common.operationSucceeded'))
        Object.assign(ruleForm, {
          serialNumber: '',
          personName: '',
          faceLibId: []
        })
        fileList.value = []
      })
    }
  })
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
    border-left: 3px solid #409eff;
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
  border-color: #409eff;
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
          color: #303133;
        }
        &:last-child {
          font-size: 14px;
          color: #909399;
        }
      }
    }
  }
  .error-example-wrap {
    padding: 20px 55px;
    .error-title {
      font-size: 16px;
      font-weight: bold;
      color: #303133;
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
          color: #303133;
        }
      }
    }
  }
}
</style>