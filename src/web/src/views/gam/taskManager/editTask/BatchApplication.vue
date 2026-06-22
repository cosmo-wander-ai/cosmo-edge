<template>
  <div>
    <el-dialog :title="t('glossary.batchApply')" v-model="showDialog" width="30%" center :before-close="close">
      <div class="BatchApplicationClass">
        <span>{{ t('field.description') }}{{ localeColon }}</span>
        <div style="text-align: center;margin:5px 0px 10px 3px; ">
          {{ t('glossary.batchApplyDesc') }}
        </div>
      </div>
      <el-tree ref="tree" :data="hatchway" node-key="id" :props="defaultProps" @node-click="handleNodeClick" show-checkbox check-on-click-node default-expand-all></el-tree>

      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" @click="confirm">{{ t('action.ok') }}</el-button>
          <el-button @click="close">{{ t('action.cancel') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, getCurrentInstance } from 'vue'
import { t, localeColon } from '@/i18n'

const props = defineProps({
  BatchApplication: Boolean,
  algorithmId: [String, Number],
  channelId: [String, Number]
})

const emit = defineEmits(['update:BatchApplication', 'confirm'])

const { proxy } = getCurrentInstance()

const showDialog = ref(props.BatchApplication)
const hatchway = ref([])
const defaultProps = ref({
  children: 'children',
  label: 'channelName',
  value: 'id'
})
const tree = ref(null)

watch(() => props.BatchApplication, (newValue) => {
  showDialog.value = newValue
  if (newValue) {
    init()
  }
})

const init = () => {
  hatchway.value = []
  const param = {
    algorithmId: props.algorithmId,
  }
  proxy.$API.listChannel(param).then(res => {
    const resData = res.resData || res.data?.resData || []
    let arr = {
      channelName: t('common.all'),
      id: '',
      children: []
    }
    hatchway.value.push(arr)
    resData.forEach(element => {
      if (element.id != props.channelId) {
        hatchway.value[0].children.push(element)
      }
    })
    if (hatchway.value[0].children.length == 0) {
      hatchway.value = []
    }
  }).catch(() => {
    hatchway.value = []
  })
}

const handleNodeClick = (data) => {
  console.log(data)
}

const confirm = () => {
  let arr = []
  let taskCustId = window.localStorage.getItem('taskCustId') ? window.localStorage.getItem('taskCustId') : window.localStorage.getItem('currentCustId')
  let nodes = tree.value.getCheckedNodes()
  let obj = nodes.find(item => item.id == '')
  if (obj) {
    nodes.forEach(el => {
      if (el.id != '') {
        arr.push(el.id)
      }
    })
  } else {
    nodes.forEach(el => {
      arr.push(el.id)
    })
  }
  const param = {
    algorithmId: props.algorithmId,
    sourceChannelId: props.channelId,
    custId: taskCustId ? taskCustId : '',
    targetChannelIds: arr
  }
  if (param.targetChannelIds.length == 0) {
    return proxy.$message.error(t('validate.noAvailableChannel'))
  }
  emit('confirm', param)
  emit('update:BatchApplication', false)
}

const close = () => {
  emit('update:BatchApplication', false)
}

onMounted(() => {
  if (props.BatchApplication) {
    init()
  }
})
</script>

<style scoped lang="scss">
.BatchApplicationClass {
  color: #0000ff;
}
</style>
