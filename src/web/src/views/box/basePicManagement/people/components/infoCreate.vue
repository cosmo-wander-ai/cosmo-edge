<template>
  <el-dialog class="dialogtype ui-admin-dialog" :title="title" v-model="dialogVisible" @close="handleClose" width="560px" center>
    <!-- 人员新增 -->
    <div>
      <el-form class="form-wrap" :model="ruleFormData" :rules="rules" ref="ruleForm" :label-width="currentLocale === 'en-US' ? '160px' : '125px'">
        <el-form-item prop="photo" :label="t('basePic.photo')">
          <Upload :fileList="fileList" :limit="3" @click="onProgress" @delete="onDelete" />
          <div class="upload-tip">{{ t('basePic.uploadFaceTip') }}
            <el-icon class="pic_tip" @click="dialogVisibleother=true"><QuestionFilled /></el-icon>
          </div>
        </el-form-item>
        <el-form-item v-if="isAxera && nativePreview" :label="currentLocale === 'en-US' ? 'Face area' : '人脸区域'">
          <div class="native-face-field">
            <p>{{ currentLocale === 'en-US' ? 'Use one 1920×1080 baseline JFIF JPEG (4:2:0, without EXIF/ICC). Drag a box around the face. Quality and pose are not checked.' : '请上传一张 1920×1080 的基线 JFIF JPEG（4:2:0，不含 EXIF/ICC），在图片上拖框标出人脸。本方式不检查画质和角度。' }}</p>
            <div class="native-face-image" @pointerdown.prevent="startNativeBox" @pointermove.prevent="moveNativeBox" @pointerup.prevent="finishNativeBox" @pointercancel="cancelNativeBox">
              <img :src="nativePreview" draggable="false" @load="checkNativeDimensions" />
              <div v-if="nativeBox" class="native-face-box" :style="nativeBoxStyle"></div>
            </div>
            <p v-if="nativeBox">{{ nativeBox.join(', ') }}</p>
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
    <el-dialog :title="t('basePic.photoExample')" :append-to-body="true" v-model="dialogVisibleother" width="480px" center class="ui-admin-dialog">
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
import { ref, reactive, computed, watch, onMounted, getCurrentInstance, nextTick } from 'vue'
import { QuestionFilled } from '@element-plus/icons-vue'
import Upload from '@/components/Upload.vue'
import miniInput from '@/components/miniInput.vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { uploadFileInChunks, UploadPurpose } from '@/utils/chunkUpload'

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
const pendingFiles = new Map()
const isAxera = ref(false)
const backendKnown = ref(false)
const nativeDimensionsOk = ref(false)
const nativeBox = ref(null)
const nativeStart = ref(null)
const nativePreview = computed(() => fileList.value.find(item => pendingFiles.has(item)) || '')
const nativeBoxStyle = computed(() => {
  if (!nativeBox.value) return {}
  const [x0, y0, x1, y1] = nativeBox.value
  return { left: `${x0 / 1920 * 100}%`, top: `${y0 / 1080 * 100}%`, width: `${(x1-x0) / 1920 * 100}%`, height: `${(y1-y0) / 1080 * 100}%` }
})
function pointOnNativeImage(event) {
  const rect = event.currentTarget.getBoundingClientRect()
  if (!rect.width || !rect.height) return null
  return [Math.round(Math.max(0, Math.min(1, (event.clientX-rect.left)/rect.width))*1920), Math.round(Math.max(0, Math.min(1, (event.clientY-rect.top)/rect.height))*1080)]
}
function startNativeBox(event) {
  if (!nativeDimensionsOk.value) return
  nativeStart.value = pointOnNativeImage(event)
  if (nativeStart.value) { nativeBox.value = [...nativeStart.value, ...nativeStart.value]; event.currentTarget.setPointerCapture(event.pointerId) }
}
function moveNativeBox(event) {
  if (!nativeStart.value) return
  const end = pointOnNativeImage(event)
  if (end) nativeBox.value = [Math.min(nativeStart.value[0],end[0]), Math.min(nativeStart.value[1],end[1]), Math.max(nativeStart.value[0],end[0]), Math.max(nativeStart.value[1],end[1])]
}
function finishNativeBox(event) { moveNativeBox(event); nativeStart.value = null }
function cancelNativeBox() { nativeStart.value = null; nativeBox.value = null }
function checkNativeDimensions(event) { nativeDimensionsOk.value = event.target.naturalWidth === 1920 && event.target.naturalHeight === 1080 }

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
  clearPendingFiles()
  ruleFormData.serialNumber = ''
  ruleFormData.personName = ''
  ruleFormData.faceLibId = []
  fileList.value = []
  emit('update:visible', false)
}

function hidclose() {
  clearPendingFiles()
  ruleFormData.serialNumber = ''
  ruleFormData.personName = ''
  ruleFormData.faceLibId = []
  fileList.value = []
  dialogVisible.value = false
}

function initData() {
  clearPendingFiles()
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
  const supportedTypes = new Set(['image/jpeg', 'image/png', 'image/bmp'])
  if (!file || !supportedTypes.has(file.type)) {
    return proxy.$message.error(t('basePic.photoFormatError'))
  }
  if (!Number.isSafeInteger(file.size) || file.size <= 0) {
    return proxy.$message.error(t('api.error.UpLoadDataEmpty'))
  }

  if (isAxera.value && file.type !== 'image/jpeg') return proxy.$message.error(currentLocale.value === 'en-US' ? 'Choose a JFIF JPEG image.' : '请选择 JFIF JPEG 图片。')
  const existingNew = fileList.value.filter(item => pendingFiles.has(item)).length
  const replacingNew = Number.isInteger(index) && pendingFiles.has(fileList.value[index])
  if (isAxera.value && existingNew && !replacingNew) return proxy.$message.error(currentLocale.value === 'en-US' ? 'Upload one new face image at a time.' : '每次只能上传一张新的人脸图片。')
  nativeBox.value = null
  nativeDimensionsOk.value = false
  const previewUrl = URL.createObjectURL(file)
  if (index || index === 0) {
    releasePendingFile(fileList.value[index])
    fileList.value[index] = previewUrl
  } else {
    fileList.value.push(previewUrl)
  }
  pendingFiles.set(previewUrl, file)
  nextTick(() => {
    ruleForm.value.validateField('photo')
  })
}

function releasePendingFile(previewUrl) {
  if (!pendingFiles.has(previewUrl)) return
  URL.revokeObjectURL(previewUrl)
  pendingFiles.delete(previewUrl)
}

function clearPendingFiles() {
  for (const previewUrl of pendingFiles.keys()) {
    URL.revokeObjectURL(previewUrl)
  }
  pendingFiles.clear()
  nativeBox.value = null
  nativeStart.value = null
  nativeDimensionsOk.value = false
}

function onDelete(previewUrl) {
  releasePendingFile(previewUrl)
  nativeBox.value = null
  nativeDimensionsOk.value = false
}

async function submitForm() {
  try {
    await ruleForm.value.validate()
  } catch (_) {
    return
  }

  if (!backendKnown.value) return proxy.$message.error(currentLocale.value === 'en-US' ? 'Device capabilities are still loading.' : '设备能力信息尚未加载完成。')
  const newPictures = fileList.value.filter(item => pendingFiles.has(item))
  if (isAxera.value && newPictures.length) {
    const box = nativeBox.value
    if (newPictures.length !== 1 || !nativeDimensionsOk.value || !box || box[2]-box[0] < 2 || box[3]-box[1] < 2) return proxy.$message.error(currentLocale.value === 'en-US' ? 'Use one 1920×1080 JFIF JPEG and draw a face box.' : '请上传一张 1920×1080 的 JFIF JPEG 并框选人脸。')
  }
  const stagedUploads = []
  try {
    for (const previewUrl of fileList.value) {
      const file = pendingFiles.get(previewUrl)
      if (!file) continue
      const staged = await uploadFileInChunks(file, {
        purpose: UploadPurpose.IMAGE,
        uploadChunk: formData => proxy.$API.uploadAtomicModelTemp(formData),
        cancelUpload: data => proxy.$API.cancelAtomicModelUpload(data),
        getCapabilities: () => proxy.$API.getUploadCapabilities()
      })
      stagedUploads.push(staged)
    }

    const params = {
      personId: props.data.personId ? props.data.personId : '',
      personOperation: !props.data?.personId ? 1 : 2,
      faceLibId: ruleFormData.faceLibId,
      serialNumber: ruleFormData.serialNumber,
      personName: ruleFormData.personName,
      pictureUploadIds: stagedUploads.map(upload => upload.uploadId),
      retainPictureId: fileList.value.filter(item => !pendingFiles.has(item))
    }
    if (isAxera.value && newPictures.length) params.nativeFaceRoi = nativeBox.value
    if (!props.data?.personId) delete params.personId

    await proxy.$API.modifyFacePicLib(params)
    proxy.$message.success(t('common.operationSucceeded'))
    datasuccess.value = true
    dialogVisible.value = false
    emit('updatePage')
    clearPendingFiles()
    fileList.value = []
    ruleFormData.serialNumber = ''
    ruleFormData.personName = ''
    ruleFormData.faceLibId = []
  } catch (error) {
    await Promise.allSettled(stagedUploads.map(upload =>
      proxy.$API.cancelAtomicModelUpload({ uploadId: upload.uploadId })
    ))
    throw error
  }
}

// Lifecycle
onMounted(() => {
  initData()
  if (!proxy.$API.queryDeviceInfo) { backendKnown.value = true; return }
  proxy.$API.queryDeviceInfo().then(res => {
    const list = res?.resData?.devInfoList || []
    const backend = (list.find(item => item.key === 'acceleratorBackend')?.value || '').toLowerCase()
    const device = (list.find(item => item.key === 'deviceType')?.value || '').toLowerCase()
    isAxera.value = backend === 'axera' || device.includes('ax650')
    backendKnown.value = true
  }).catch(() => { backendKnown.value = false })
})
</script>

<style scoped lang="scss">
.native-face-field { width: 100%; line-height: 1.5; }
.native-face-image { position: relative; width: 100%; touch-action: none; cursor: crosshair; user-select: none; }
.native-face-image img { display: block; width: 100%; pointer-events: none; }
.native-face-box { position: absolute; box-sizing: border-box; border: 2px solid #e84b46; background: rgba(232, 75, 70, 0.12); pointer-events: none; }

.pro {
  padding-left: 110px;
  font-size: 14px;
  color: var(--theme-accent, #09aaff);
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
  border: 1px dashed var(--theme-border, #d9d9d9);
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
  color: var(--theme-accent, #3598ff);
  width: 100px;
  height: 140px;
  line-height: 140px;
  text-align: center;
  border: 1px solid var(--theme-border-light, #f2f6fc);
  background-color: var(--theme-surface-soft, #f2f6fc);
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
  color: var(--theme-text-muted, #cccccc);
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
