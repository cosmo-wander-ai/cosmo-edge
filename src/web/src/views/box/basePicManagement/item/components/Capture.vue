<template>
  <el-dialog v-if="dialogVisible" class="dialogtype" :title="t('basePic.captureAddTitle')" v-model="dialogVisible" @close="handleClose" width="1046px" center>
    <div class="container">
      <div class="wrap-left">
        <div class="cardleft">
          <!-- 摄像机列表 -->
          <div class="select-area" :class="{ 'expanded': cameraDrawerVisible }">
            <div v-if="cameraDrawerVisible" class="camera-list-wrap">
              <div class="title">
                <span>{{ t('basePic.channelList') }}</span>
              </div>
              <div class="exseach">
                <el-input size="small" :placeholder="t('basePic.searchKeyword')" maxlength="32" v-model="cameraFilterText">
                  <template #suffix>
                    <el-icon><Search /></el-icon>
                  </template>
                </el-input>

                <el-tree ref="treeRef" class="filter-tree" :data="cameraList" :highlight-current="true" node-key="id" default-expand-all :filter-node-method="filterNode">
                  <template #default="{ node, data }">
                    <div class="custom-tree-node" :class="{'padding-left-18': data.id !== -1}" @dblclick="handleCameraNodeClick(data)">
                      <div v-if="data.channelType == 0 && data.status == 0" class="stnode">
                        <img src="@/assets/close-circle.png" />
                        <span>{{ node.label }}</span>
                      </div>
                      <div v-else-if="data.id !== -1" class="stnode">
                        <img src="@/assets/check-circle.png" />
                        <span>{{ node.label }}</span>
                      </div>
                      <div v-else>
                        <span>{{ node.label }}</span>
                      </div>
                    </div>
                  </template>
                </el-tree>
              </div>
            </div>
            <div class="el-icon-d-arrow-right" :class="{ 'expanded': cameraDrawerVisible }" @click="toggleCameraDrawer"></div>
          </div>

          <div class="video-body">
            <div v-if="openImgtable || !channelId" class="nodataImg">
              <img src="@/assets/zanwu1.png" alt="">
              <p>{{ t('basePic.noCamera') }}</p>
            </div>
            <flv v-else :channelId="channelId" class="flv-body"></flv>
            <m-v-canvas v-if="channelId" style="z-index:99; " ref="mvCanvasRef" :width="width" :height="height" v-model="allPoints" :active-index="activeIndex" :is-rect="isRect" :size-params="referenceAreaSizeParams" :is-show-min="isShowMin" :is-show-max="isShowMax" @update-reference="updateReferenceAreaParams"></m-v-canvas>
          </div>
        </div>

        <div class="capture-btn">
          <el-button @click="captureFn" type="primary" size="small">{{ t('basePic.capture') }}</el-button>
        </div>
      </div>

      <div class="wrap-right">
        <div class="right-title">{{ t('basePic.itemPhoto') }}</div>
        <div class="right-content">
          <div v-for="(picture, index) in pictureUrlList" :key="index" class="picture">
            <img src="@/assets/auth_fail.png" class="close" @click="removePicture(index)">
            <img :src="picture.pictureUrl">
          </div>
        </div>
      </div>
    </div>
    <div class="footer-btn">
      <el-button type="primary" @click="save" size="small">{{ t('action.save') }}</el-button>
      <el-button @click="handleClose" size="small">{{ t('action.cancel') }}</el-button>
    </div>
  </el-dialog>
</template>

<script setup>
import { ref, reactive, watch, getCurrentInstance } from 'vue'
import { Search } from '@element-plus/icons-vue'
import moment from 'moment'
import flv from '@/components/flvjs.vue'
import MVCanvas from './mVCanvas.vue'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()

// Props
const props = defineProps({
  visible: Boolean,
  selectList: Array,
  thingsLibId: [String, Number]
})

// Emits
const emit = defineEmits(['update:visible', 'updateCapture'])

// Refs
const treeRef = ref(null)
const mvCanvasRef = ref(null)

// Reactive data
const dialogVisible = ref(false)
const rules = {
  personLibId: [
    {
      required: true,
      message: t('basePic.selectWorkClothesLib'),
      trigger: ['blur', 'change']
    }
  ]
}
const ruleForm = ref({})
const data = ref([])
const defaultProps = {
  children: 'children',
  label: 'label'
}
const cameraFilterText = ref('')
const channelId = ref('')
const openImgtable = ref(true)
const checkbutton = ref(1)
const pictureUrlList = ref([])
const success = ref(false)
const cameraDrawerVisible = ref(false)
const cameraList = ref([])
const allPoints = ref([])
const activeIndex = ref(0)
const isRect = ref(true)
const referenceAreaSizeParams = reactive({
  minWidth: 10,
  minHeight: 10,
  maxWidth: 256,
  maxHeight: 256
})
const isShowMin = ref(false)
const isShowMax = ref(false)
const width = ref(684)
const height = ref(389)

// Watchers
watch(() => props.visible, (newValue) => {
  dialogVisible.value = newValue
  pictureUrlList.value = []
  if (newValue) {
    channelId.value = ''
    initCameraList()
    width.value = 684
    height.value = 389
    setBlockListDefault()
  }
})

watch(cameraFilterText, () => {
  const tree = treeRef.value
  console.log(tree, 'aaaaa')
  tree.filter(cameraFilterText.value)
})

// Methods
const handleClose = () => {
  success.value = false
  dialogVisible.value = false
  emit('update:visible', false)
}

const filterNode = (value, data) => {
  if (!value) return true
  return data.label.indexOf(value) !== -1
}

const initCameraList = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.boxQueryCameraList(params).then((res) => {
    const { resData } = res
    let childCameras = []
    childCameras = resData.rows.map((item) => {
      const { videoChannelId, channelName, channelStatus, channelType } = item
      return {
        id: videoChannelId,
        label: channelName,
        status: channelStatus,
        channelType: channelType
      }
    })
    cameraList.value = [
      {
        id: -1,
        label: t('common.all'),
        children: childCameras
      }
    ]
    console.log(cameraList.value, '===========')
  })
}

const handleCameraNodeClick = (e) => {
  if (e.channelType == 0 && e.status == 0)
    return proxy.$message.error(t('api.cameraOffline'))
  cameraDrawerVisible.value = false
  openImgtable.value = true
  if (e.status == 1 && e.id) {
    channelId.value = e.id
    setTimeout(() => {
      openImgtable.value = false
    }, 100)
  } else if (e.status != 1 || !e.id) {
    openImgtable.value = true
  }
}

const toggleCameraDrawer = () => {
  cameraDrawerVisible.value = !cameraDrawerVisible.value
}

const captureFn = () => {
  let rect = {
    xRatio: pointsToRadio(allPoints.value[0])[0].xRatio,
    yRatio: pointsToRadio(allPoints.value[0])[0].yRatio,
    wRatio: +(
      pointsToRadio(allPoints.value[0])[1].xRatio -
      pointsToRadio(allPoints.value[0])[0].xRatio
    ).toFixed(6),
    hRatio: +(
      pointsToRadio(allPoints.value[0])[2].yRatio -
      pointsToRadio(allPoints.value[0])[1].yRatio
    ).toFixed(6)
  }
  if (pictureUrlList.value.length >= 5)
    return proxy.$message.error(t('basePic.maxPhotosWarning'))
  const params = {
    cameraId: channelId.value,
    rect: rect
  }
  proxy.$API.getThingsPicture(params).then((res) => {
    pictureUrlList.value.push({
      pictureUrl: res.resData.pictureUrl,
      pictureName: ''
    })
    console.log(pictureUrlList.value)
  })
}

const removePicture = (index) => {
  pictureUrlList.value.splice(index, 1)
}

const save = () => {
  if (pictureUrlList.value.length === 0)
    return proxy.$message.warning(t('basePic.addCaptureWarning'))
  const params = {
    thingsOperation: 1,
    thingsLibId: props.thingsLibId,
    thingsList: pictureUrlList.value
  }
  proxy.$API.addLibThings(params).then(() => {
    success.value = true
    dialogVisible.value = false
    emit('updateCapture')
    proxy.$message.success(t('common.operationSucceeded'))
  })
}

const updateReferenceAreaParams = (params) => {
  Object.assign(referenceAreaSizeParams, params)
}

const pointsToRadio = (blocks) => {
  const points = blocks.reduce((p, t) => {
    let obj = {
      xRatio: 0,
      yRatio: 0
    }
    obj.xRatio = +(t.x / width.value).toFixed(6)
    obj.yRatio = +(t.y / height.value).toFixed(6)
    return [...p, obj]
  }, [])
  return points
}

const setBlockListDefault = () => {
  // 生成一个单例默认坐标数组
  const radius = 100
  let defaultPos = []
  if (isRect.value) {
    defaultPos = [
      {
        x: width.value / 2 - radius,
        y: height.value / 2 - radius
      },
      {
        x: width.value / 2 + radius,
        y: height.value / 2 - radius
      },
      {
        x: width.value / 2 + radius,
        y: height.value / 2 + radius
      },
      {
        x: width.value / 2 - radius,
        y: height.value / 2 + radius
      }
    ]
  }
  allPoints.value = [defaultPos]
}
</script>

<style lang="scss" scoped>
.container {
  margin-bottom: 50px;
  display: flex;
}
.footer-btn {
  display: flex;
  justify-content: center;
}
.wrap-left {
  position: relative;
  width: 704px;
  height: 430px;
  margin-right: 20px;
}
.wrap-right {
  width: 282px;
  .right-title {
    height: 33px;
    line-height: 33px;
    margin-bottom: 18px;
  }
  .right-content {
    display: flex;
    flex-wrap: wrap;
    .picture {
      position: relative;
      width: 84px;
      height: 100px;
      margin: 0 10px 16px 0;
      img {
        width: 100%;
        height: 100%;
      }
      .close {
        position: absolute;
        top: -4px;
        right: -4px;
        width: 14px;
        height: 14px;
        cursor: pointer;
      }
    }
  }
}

// 预览
.nodataImg {
  width: 684px;
  height: 389px;
  display: flex;
  justify-content: center;
  flex-direction: column;
  align-items: center;
  img {
    width: 100px;
    height: 90px;
  }
  p {
    margin: 0 0;
    color: #3598ff;
    font-size: 14px;
    letter-spacing: 2px;
    margin-top: 10px;
  }
}

.cardleft {
  position: relative;
  width: 704px;
  height: 391px;
  background: #fff;
  border: 1px solid lightgray;
  box-sizing: border-box;
}

.video-body {
  position: absolute;
  top: 0;
  left: 20px;
  width: 684px;
  height: 389px;
  box-sizing: border-box;
}

.capture-btn {
  position: absolute;
  bottom: 0;
  right: 0;
}

.select-area {
  width: 20px;
  position: absolute;
  top: 0;
  height: 100%;
  background: #101938;
  transition: width 0.3s ease;
  z-index: 200;
  padding: 10px;
  color: white;
  box-sizing: border-box;

  &.expanded {
    width: 200px;
  }

  .el-icon-d-arrow-right {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    cursor: pointer;
    transition: all 0.3s ease;
    z-index: 100;

    &.expanded {
      transform: rotate(180deg);
      right: 0;
    }
  }
}

.exseach {
  margin-top: 10px;
  :deep(.el-input) {
    .el-input__inner {
      height: 32px;
      border-radius: 0;
      background: url(@/assets/big_screen_input_bg.png) center no-repeat;
      background-size: 100% 100%;
      color: #94d0ff;
      background-color: initial;
      border: none;
    }
    .el-input__suffix-inner,
    .el-input__inner::placeholder {
      color: #94d0ff;
    }
  }

  .filter-tree {
    margin-top: 20px;
    box-sizing: border-box;
    overflow: scroll;
    max-height: 295px;
  }

  :deep(.el-tree) {
    background: inherit;
    font-size: 14px;
    color: #94d0ff;
  }
}

.custom-tree-node {
  .stnode {
    display: flex;
    align-items: center;
  }

  img {
    margin-right: 5px;
  }

  span {
    display: inline-block;
    width: 120px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
}

.filter-tree {
  max-height: 389px;
  overflow-y: scroll;
}

.flv-body {
  :deep(.video-container) {
    width: 100%;
    height: 100%;
  }
  :deep(.video) {
    border-radius: 0;
  }
}

::-webkit-scrollbar {
  width: 8px;
  height: 0px;
}
</style>


