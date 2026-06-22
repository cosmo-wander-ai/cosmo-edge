<template>
  <div class="mv-upload">
    <div class="file-list" v-for="(img, index) in propsFileList" :key="index">
      <div class="img-style">
        <img v-if="imgKey" :src="img[imgKey]" />
        <img v-else :src="img" />
      </div>
      <div class="delete-style">
        <el-button
          class="upload-btn btn1"
          type="primary"
          size="small"
          @click="resetUpload(index, img)"
        >{{ t('action.reUpload') }}</el-button>
        <el-button
          class="upload-btn btn2"
          type="primary"
          size="small"
          @click="handleRemove(index, img)"
        >{{ t('action.delete') }}</el-button>
      </div>
    </div>
    <div class="file-list add-flie" v-if="propsFileList.length<limit" @click="uploadFun">
      <el-icon><Plus /></el-icon>
    </div>
    <input ref="hideFile" type="file" v-show="false" @change="selectFileFun($event)" />
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { t } from '@/i18n'

// 定义组件名称
defineOptions({
  name: 'Upload'
})

// Props
const props = defineProps({
  fileList: {
    type: Array,
    default: () => []
  },
  limit: {
    type: Number,
    default: () => 0
  },
  imgKey: {
    type: String,
    default: null
  }
})

// Emits
const emit = defineEmits(['click', 'delete'])

// Refs
const hideFile = ref(null)

// Reactive data
const propsFileList = ref(props.fileList)
const uploadAgainIndex = ref(null)

// Methods
function uploadFun() {
  hideFile.value.click()
}

function selectFileFun(event) {
  const file = event.target.files[0]
  emit('click', file, uploadAgainIndex.value)
  event.target.value = ''
  uploadAgainIndex.value = null
}

function resetUpload(index) {
  uploadAgainIndex.value = index
  hideFile.value.click()
}

function handleRemove(index, img) {
  propsFileList.value.splice(index, 1)
  emit('delete', img)
}

// Watch
watch(() => props.fileList, (newValue) => {
  propsFileList.value = newValue
}, { deep: true })
</script>

<style scoped lang="scss">
.mv-upload {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;

  .file-list {
    position: relative;
    width: 100px;
    height: 120px;
    border-radius: 12px;
    overflow: hidden;
    cursor: pointer;
    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    border: 2px solid #e4e7ed;
    background: #fff;

    &:hover {
      transform: translateY(-4px);
      box-shadow: 0 8px 25px rgba(0, 0, 0, 0.15);
      border-color: #409eff;

      .delete-style {
        opacity: 1;
        visibility: visible;
      }
    }

    .img-style {
      width: 100%;
      height: 100%;
      position: relative;
      overflow: hidden;

      > img {
        width: 100%;
        height: 100%;
        object-fit: cover;
        transition: all 0.3s;
      }
    }

    .delete-style {
      opacity: 0;
      visibility: hidden;
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: linear-gradient(135deg, rgba(0, 0, 0, 0.7) 0%, rgba(0, 0, 0, 0.5) 100%);
      backdrop-filter: blur(2px);
      z-index: 2;
      transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      gap: 8px;

      .upload-btn {
        margin: 0;
        padding: 6px 12px;
        width: auto;
        height: auto;
        font-size: 12px;
        border-radius: 6px;
        font-weight: 500;
        transition: all 0.3s;
        background: rgba(255, 255, 255, 0.95);
        border: 1px solid rgba(255, 255, 255, 0.2);
        color: #409eff;
        backdrop-filter: blur(4px);

        &:hover {
          transform: scale(1.05);
          box-shadow: 0 4px 12px rgba(64, 158, 255, 0.3);
          background: #fff;
        }

        &.btn2 {
          color: #f56c6c;

          &:hover {
            box-shadow: 0 4px 12px rgba(245, 108, 108, 0.3);
          }
        }
      }
    }
  }

  .add-flie {
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 32px;
    color: #c0ccda;
    border: 2px dashed #d3dce6;
    background: linear-gradient(135deg, #fafbfc 0%, #f5f7fa 100%);
    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    position: relative;
    overflow: hidden;

    &::before {
      content: '';
      position: absolute;
      top: 0;
      left: -100%;
      width: 100%;
      height: 100%;
      background: linear-gradient(90deg, transparent, rgba(64, 158, 255, 0.1), transparent);
      transition: left 0.5s;
    }

    &:hover {
      border-color: #409eff;
      color: #409eff;
      background: linear-gradient(135deg, #f0f9ff 0%, #e6f4ff 100%);
      transform: translateY(-2px);
      box-shadow: 0 4px 15px rgba(64, 158, 255, 0.2);

      &::before {
        left: 100%;
      }

      .el-icon {
        transform: scale(1.1);
      }
    }

    .el-icon {
      transition: all 0.3s;
      filter: drop-shadow(0 2px 4px rgba(0, 0, 0, 0.1));
    }
  }
}

// 响应式设计
@media (max-width: 768px) {
  .mv-upload {
    gap: 12px;

    .file-list {
      width: 80px;
      height: 96px;

      .delete-style .upload-btn {
        padding: 4px 8px;
        font-size: 11px;
      }
    }
  }
}

// 动画效果
@keyframes fadeInUp {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.file-list {
  animation: fadeInUp 0.3s ease-out;
}

// 加载状态
.file-list.loading {
  .img-style::after {
    content: '';
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: rgba(255, 255, 255, 0.8);
    display: flex;
    align-items: center;
    justify-content: center;
  }
}
</style>