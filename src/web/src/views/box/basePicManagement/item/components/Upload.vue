<template>
  <div class="mv-upload">
    <div class="file-list" v-for="(item, index) in propsFileList" :key="index">
      <div class="img-style">
        <img :src="'data:image/jpeg;base64,'+ item.pictureBase64" />
      </div>
      <div class="delete-style">
        <el-button class="upload-btn btn1" type="primary" size="small" @click="resetUpload(index, item)">{{ t('action.reUpload') }}</el-button>
        <el-button class="upload-btn btn2" type="primary" size="small" @click="handleRemove(index, item)">{{ t('action.delete') }}</el-button>
      </div>
    </div>
    <div class="file-list add-flie" v-if="propsFileList.length<limit" @click="uploadFun">
      <el-icon><Plus /></el-icon>
    </div>
    <input ref="hideFileRef" type="file" v-show="false" @change="selectFileFun($event)" />
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { t } from '@/i18n'

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
  imgKey: null
})

// Emits
const emit = defineEmits(['click', 'delete'])

// Refs
const hideFileRef = ref(null)

// Reactive data
const propsFileList = ref(props.fileList)
const uploadAgainIndex = ref(null)

// Methods
const uploadFun = () => {
  hideFileRef.value.click()
}

const selectFileFun = (event) => {
  const file = event.target.files[0]
  emit('click', file, uploadAgainIndex.value)
  event.target.value = ''
  uploadAgainIndex.value = null
}

const resetUpload = (index, item) => {
  uploadAgainIndex.value = index
  hideFileRef.value.click()
}

const handleRemove = (index, item) => {
  propsFileList.value.splice(index, 1)
  emit('delete', item)
}

// Watchers
watch(() => props.fileList, (n) => {
  propsFileList.value = n
}, { deep: true })
</script>

<style scoped lang="scss">
.mv-upload {
  display: flex;
  flex-wrap: wrap;
  .file-list {
    position: relative;
    margin: 0 10px 10px 0;
    width: 84px;
    height: 96px;
    border-radius: 4px;
    overflow: hidden;
    cursor: pointer;
    &:hover {
      .delete-style {
        display: block;
      }
    }
    .img-style {
      width: 100%;
      height: 100%;
      > img {
        width: 100%;
        height: 100%;
      }
    }
    .delete-style {
      display: none;
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background-color: rgba($color: #000000, $alpha: 0.5);
      z-index: 1;
      transition: all 0.3s;
      .upload-btn {
        position: absolute;
        margin: 0;
        padding: 0;
        width: 65px;
        height: 30px;
        left: 50%;
        transform: translateX(-50%) scale(0.83);
        &.btn1 {
          top: 15px;
        }
        &.btn2 {
          top: 50px;
        }
      }
    }
  }
  .add-flie {
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 40px;
    color: #c0ccda;
    border: 1px dashed #c0ccda;
    &:hover {
      border: 1px dashed #409eff;
    }
  }
}
</style>