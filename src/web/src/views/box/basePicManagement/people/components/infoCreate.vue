<template>
  <el-dialog class="dialogtype" :title="title" v-model="dialogVisible" @close="handleClose" width="560px" center>
    <!-- 人员新增 -->
    <div>
      <el-form class="form-wrap" :model="ruleFormData" :rules="rules" ref="ruleForm" :label-width="currentLocale === 'en-US' ? '160px' : '125px'">
        <el-form-item prop="photo" :label="t('basePic.photo')">
          <Upload :fileList="fileList" :limit="3" @click="onProgress" />
          <div class="upload-tip">{{ t('basePic.uploadFaceTip') }}
            <el-icon class="pic_tip" @click="dialogVisibleother=true"><QuestionFilled /></el-icon>
          </div>
        </el-form-item>
        <el-form-item :label="t('basePic.personName') + localeColon" prop="personName">
          <miniInput class="mv-el-input" v-model="ruleFormData.personName" :placeholder="t('placeholder.enter')" />
        </el-form-item>
        <el-form-item :label="t('basePic.personCode') + localeColon" prop="serialNumber">
          <miniInput class="mv-el-input" v-model="ruleFormData.serialNumber" :placeholder="t('placeholder.enter')" />
        </el-form-item>

        <!-- <el-form-item label="脸库：" prop="faceLibId">
          <el-select clearable multiple v-model="ruleForm.faceLibId" placeholder="请选择" size="small">
            <el-option v-for="item in selectList" :key="item.id" :label="item.name" :value="item.id"></el-option>
          </el-select>
        </el-form-item> -->
      </el-form>
      <div class="obsBtoon">
        <el-button @click="hidclose" size="small">{{ t('action.cancel') }}</el-button>
        <el-button @click="submitForm('ruleForm')" type="primary" size="small">{{ t('action.save') }}</el-button>
      </div>
    </div>
    <el-dialog :title="t('basePic.photoExample')" :append-to-body="true" v-model="dialogVisibleother" width="480px" center>
      <div class="pic-example-wrap">
        <el-alert :title="t('basePic.photoSizeTip')" type="warning" show-icon :closable="false"></el-alert>
        <div class="right-example-wrap">
          <div class="right-pic">
            <img width="100%" height="100%" src="@/assets/person/img1.png" />
          </div>
          <div class="right-title">
            <p>{{ t('basePic.correctExample') }}</p>
            <p>{{ t('basePic.correctExampleTip') }}</p>
          </div>
        </div>
        <div class="error-example-wrap">
          <p class="error-title">{{ t('basePic.errorExample') }}</p>
          <div class="error-pic">
            <div>
              <span>
                <img src="@/assets/person/img2.png" />
              </span>
              <label>{{ t('basePic.blurry') }}</label>
            </div>
            <div>
              <span>
                <img src="@/assets/person/img3.png" />
              </span>
              <label>{{ t('basePic.multipleFaces') }}</label>
            </div>
            <div>
              <span>
                <img src="@/assets/person/img4.png" />
              </span>
              <label>{{ t('basePic.occlusion') }}</label>
            </div>
            <div>
              <span>
                <img src="@/assets/person/img5.png" />
              </span>
              <label>{{ t('basePic.animalLike') }}</label>
            </div>
          </div>
        </div>
      </div>
      <div class="obsBtoon">
        <el-button @click="dialogVisibleother=false" size="small">{{ t('action.cancel') }}</el-button>
        <el-button type="primary" @click="dialogVisibleother=false" size="small">{{ t('action.confirm') }}</el-button>
      </div>
    </el-dialog>
  </el-dialog>
</template>

<script setup>
import { ref, reactive, watch, onMounted, getCurrentInstance, nextTick } from 'vue'
import { QuestionFilled } from '@element-plus/icons-vue'
import Upload from '@/components/Upload.vue'
import miniInput from '@/components/miniInput.vue'
import { t, localeColon, currentLocale } from '@/i18n'

// 定义组件名称
defineOptions({
  name: 'InfoCreate'
})

// Props
const props = defineProps({
  title: String,
  visible: Boolean,
  data: Object,
  selectList: Array
})

// Emits
const emit = defineEmits(['update:visible', 'updatePage'])

const { proxy } = getCurrentInstance()

// Refs
const ruleForm = ref(null)

// Reactive data
const dialogVisibleother = ref(false)
const datasuccess = ref(false)
const dialogVisible = ref(false)

const ruleFormData = reactive({
  serialNumber: '',
  personName: '',
  faceLibId: []
})

const fileList = ref([])
const ImageBase64 = ref('')

// Validation functions
function validatePass(rule, value, callback) {
  if (!fileList.value.length) {
    callback(new Error(t('basePic.photoRequired')))
  } else {
    callback()
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
  ]
})

// Watch
watch(() => props.visible, (newValue) => {
  console.log(props.data, '=============')
  ruleForm.value && ruleForm.value.resetFields()
  datasuccess.value = false
  dialogVisible.value = newValue
  newValue && initData()
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

function handleClose() {
  ruleFormData.serialNumber = ''
  ruleFormData.personName = ''
  ruleFormData.faceLibId = []
  fileList.value = []
  emit('update:visible', false)
}

function hidclose() {
  ruleFormData.serialNumber = ''
  ruleFormData.personName = ''
  ruleFormData.faceLibId = []
  fileList.value = []
  dialogVisible.value = false
}

function initData() {
  ruleFormData.faceLibId = props.data.faceLibId
  if (props.data?.personId) {
    ruleFormData.serialNumber = props.data.serialNumber
    ruleFormData.personName = props.data.personName
    const newFileList = []
    props.data.pictureList.forEach((element) => {
      newFileList.push(element.url)
    })
    fileList.value = newFileList
  }
}

function onProgress(file, index) {
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
  reader.onload = function () {
    if (index || index == 0) {
      fileList.value[index] = this.result
    } else {
      fileList.value.push(this.result)
    }
    nextTick(() => {
      ruleForm.value.validateField('photo')
    })
  }
}

function submitForm(formName) {
  ruleForm.value.validate((valid) => {
    if (valid) {
      const pictureBase64 = []
      fileList.value.forEach((element) => {
        if (element.indexOf(',') > -1) {
          pictureBase64.push(element.substring(element.indexOf(',') + 1))
        } else {
          pictureBase64.push(element)
        }
      })
      let params = {
        personId: props.data.personId ? props.data.personId : '',
        personOperation: !props.data?.personId ? 1 : 2, // 1：添加，2更新
        faceLibId: ruleFormData.faceLibId,
        serialNumber: ruleFormData.serialNumber,
        personName: ruleFormData.personName
      }
      !props.data?.personId && delete params.personId
      let old = [],
        arr = []
      for (let index = 0; index < pictureBase64.length; index++) {
        const element = pictureBase64[index]
        if (element.length > 100) {
          arr.push(element)
        } else {
          old.push(element)
        }
      }
      params.pictureBase64 = arr
      params.retainPictureId = old
      proxy.$API.modifyFacePicLib(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        datasuccess.value = true
        dialogVisible.value = false
        emit('updatePage')
        fileList.value = []
        ruleFormData.serialNumber = ''
        ruleFormData.personName = ''
        ruleFormData.faceLibId = []
      })
    } else {
      return false
    }
  })
}

// Lifecycle
onMounted(() => {
  initData()
})
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