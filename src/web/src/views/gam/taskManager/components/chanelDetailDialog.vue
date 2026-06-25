<template>
  <div>
    <el-dialog v-model="show" :title="t('glossary.cameraDetails')" width="600px" center @close="emit('update:visible', false)">
      <el-form :label-width="currentLocale === 'en-US' ? '190px' : '140px'" :model="props.detailChannel">
        <el-form-item :label="t('field.channelNo') + localeColon">
          <el-input v-model="props.detailChannel.videoChannelId" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('glossary.accessType') + localeColon">
          <el-input v-model="props.detailChannel.channelTypeLabel" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('field.channelName') + localeColon">
          <el-input v-model="props.detailChannel.channelName" size="small" disabled />
        </el-form-item>
        <!-- <el-form-item label="IP：">
          <el-input v-model="detailChannel.ip" size="small" disabled />
        </el-form-item> -->
        <el-form-item :label="t('field.resolution') + localeColon">
          <el-input v-model="props.detailChannel.resolution" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('field.videoCodec') + localeColon">
          <el-input v-model="props.detailChannel.codec" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('glossary.frameRate') + localeColon">
          <el-input v-model="props.detailChannel.fps" size="small" disabled />
        </el-form-item>
        <el-form-item :label="'RTSP' + localeColon">
          <el-input v-model="props.detailChannel.url" size="small" disabled />
        </el-form-item>
        <el-form-item v-if="false" :label="t('field.externalChannelNo') + localeColon">
          <el-input v-model="props.detailChannel.externalChannelNo" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('glossary.algorithmService') + localeColon">
          <span v-for="(item, idx) in props.detailChannel.algorithms" :key="idx" style="margin-right:8px;">
            {{ resolveResourceAlgorithmName(item) }}
            <span v-if="item.status === 1" style="color: #00b300;">{{ t('status.running') }}</span>
            <span v-else style="color: #999;">{{item.statusText}}</span>
            <span v-if="item.workday" style="margin-left:4px;">{{item.workday}}</span>
          </span>
        </el-form-item>
        <el-form-item :label="t('field.status') + localeColon">
          <span :style="{color: props.detailChannel.channelStatus === 1 ? '#00b300' : '#999'}">
            {{ props.detailChannel.channelStatus === 1 ? t('status.online') : t('status.offline') }}
          </span>
        </el-form-item>
      </el-form>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  detailChannel: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['update:visible'])
const show = ref(false)

watch(() => props.visible, (val) => {
  show.value = val
})
</script>

<style lang="scss" scoped>
.el-form {
  padding-right: 60px;
}

.el-form-item {
  margin-bottom: 10px;
}
</style>
