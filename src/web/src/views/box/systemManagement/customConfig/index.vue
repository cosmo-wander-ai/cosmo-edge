<template>
  <div class="custom-config">
    <div class="page-header">
      <div class="title">{{ t('systemManage.customSettings') }}</div>
      <div class="actions">
        <el-button @click="handleReset" size="small">{{ t('action.reset') }}</el-button>
        <el-button type="primary" size="small" @click="handleSave">{{ t('action.save') }}</el-button>
      </div>
    </div>

    <el-form ref="formRef" :model="formData" :label-width="currentLocale === 'en-US' ? '160px' : '100px'">
      <el-form-item :label="t('systemManage.systemName')" prop="systemName" :rules="[
          { required: true, message: t('systemManage.enterSystemName'), trigger: 'blur'},
          { max: 32, message: t('systemManage.systemNameMaxLength'), trigger: 'blur' }
        ]">
        <el-input v-model.trim="formData.systemName" class="item-content" size="small" :placeholder="t('systemManage.enterSystemName')"></el-input>
      </el-form-item>

      <el-form-item :label="t('systemManage.uploadIcon')">
        <el-upload class="logo-uploader" action="#" :show-file-list="false" :auto-upload="false" :on-change="handleLogoChange" :before-upload="beforeLogoUpload" accept=".jpg,.jpeg,.bmp,.png">
          <img v-if="formData.logoUrl" :src="formData.logoUrl" class="logo">
          <i v-else class="el-icon-plus logo-uploader-icon"></i>
        </el-upload>
        <div class="upload-tip">
          <i class="el-icon-info"></i>
          {{ t('systemManage.uploadIconTip') }}
        </div>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, getCurrentInstance } from 'vue'
import defaultLogo from '@/assets/logo.png'
import { t, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()

const formRef = ref(null)
const defaultLogoUrl = new URL('@/assets/logo.png', import.meta.url).href
const defaultLogoBase64 = ref('')

const formData = reactive({
  systemName: window.getGlobalConfig().platformName,
  logoUrl: localStorage.getItem('logoUrl') || ''
})

// 将图片转换为 Base64
const handleImageToBase64 = async (imageUrl) => {
  try {
    const response = await fetch(imageUrl)
    const blob = await response.blob()
    return new Promise((resolve) => {
      const reader = new FileReader()
      reader.onload = () => {
        resolve(reader.result)
      }
      reader.readAsDataURL(blob)
    })
  } catch (error) {
    console.error('Image to base64 error:', error)
    return null
  }
}

// 修改 favicon
const changeFavicon = (iconUrl) => {
  const timestamp = new Date().getTime()
  const cacheBustUrl = `${iconUrl}?v=${timestamp}`
  
  const link =
    document.querySelector("link[rel*='icon']") ||
    document.createElement('link')
  link.type = 'image/x-icon'
  link.rel = 'icon'
  link.href = cacheBustUrl

  if (!link.parentNode) {
    document.head.appendChild(link)
  }
  localStorage.setItem('logoUrl', iconUrl)
}

// 获取 Logo
const getLogo = async () => {
  try {
    const res = await proxy.$API.boxGetLogo({})
    const { resData } = res
    
    if (resData?.logoUrl) {
      const base64Url = await handleImageToBase64(resData.logoUrl)
      if (base64Url) {
        formData.logoUrl = base64Url
        changeFavicon(resData.logoUrl)
        localStorage.setItem('logoUrl', resData.logoUrl)
        const logoImgElement = document.querySelector('.logo-img')
        if (logoImgElement) {
          logoImgElement.src = base64Url
        }
      }
    }
    
    if (resData?.systemName) {
      window.getGlobalConfig().platformName = resData.systemName
      localStorage.setItem('platformName', resData.systemName)
      document.title = resData.systemName
      const logoTextElement = document.querySelector('.logo-text')
      if (logoTextElement) {
        logoTextElement.textContent = resData.systemName
      }
    }
  } catch (error) {
    console.error('Get logo error:', error)
  }
}

// 设置 Logo
const setLogo = () => {
  const params = {
    systemName: formData.systemName,
    logoBase64: formData.logoUrl ? formData.logoUrl.split(',')[1] : '',
    logoFileType: '.png'
  }
  
  formRef.value?.validate((valid) => {
    if (valid) {
      proxy.$API.boxSetLogo(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        getLogo()
      })
    }
  })
}

// Logo 文件改变
const handleLogoChange = (file) => {
  beforeLogoUpload(file.raw).then(() => {
    const reader = new FileReader()
    reader.onload = (e) => {
      formData.logoUrl = e.target.result
    }
    reader.readAsDataURL(file.raw)
  }).catch(() => {
    // 校验失败，不做处理
  })
}

// Logo 上传前校验
const beforeLogoUpload = (file) => {
  const isValidFormat = ['image/jpeg', 'image/bmp', 'image/png'].includes(file.type)
  
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.src = URL.createObjectURL(file)
    img.onload = () => {
      URL.revokeObjectURL(img.src)
      const isValidSize = img.width === 100 && img.height === 100

      if (!isValidFormat) {
        proxy.$message.error(t('systemManage.uploadIconFormatError'))
        reject(false)
      } else if (!isValidSize) {
        proxy.$message.error(t('systemManage.uploadIconSizeError'))
        reject(false)
      } else {
        resolve(true)
      }
    }
    img.onerror = () => {
      URL.revokeObjectURL(img.src)
      proxy.$message.error(t('systemManage.uploadIconLoadError'))
      reject(false)
    }
  })
}

// 保存
const handleSave = () => {
  setLogo()
}

// 重置
const handleReset = () => {
  formData.systemName = t('system.defaultPlatformName')
  formData.logoUrl = defaultLogoBase64.value
  setLogo()
}

// 生命周期
onMounted(async () => {
  const base64 = await handleImageToBase64(defaultLogo)
  if (base64) {
    formData.logoUrl = base64
    defaultLogoBase64.value = base64
  }
  getLogo()
})
</script>

<style lang="scss" scoped>
.custom-config {
  padding: 20px;
  background: #fff;
  border-radius: 4px;

  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
    padding-bottom: 15px;
    border-bottom: 1px solid #ebeef5;

    .title {
      font-size: 16px;
      font-weight: 500;
      color: #303133;
    }

    .actions {
      .el-button + .el-button {
        margin-left: 12px;
      }
    }
  }

  .logo-uploader {
    padding-top: 8px;
    :deep(.el-upload) {
      border: 1px dashed #d9d9d9;
      border-radius: 6px;
      cursor: pointer;
      position: relative;
      overflow: hidden;

      &:hover {
        border-color: #409eff;
      }
    }
  }

  .logo-uploader-icon {
    font-size: 28px;
    color: #8c939d;
    width: 100px;
    height: 100px;
    line-height: 100px;
    text-align: center;
  }

  .logo {
    width: 100px;
    height: 100px;
    display: block;
  }

  .upload-tip {
    color: #909399;
    font-size: 12px;

    .el-icon-info {
      margin-right: 4px;
    }
  }
}

.item-content {
  width: 300px;
}
</style>
