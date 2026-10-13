<template>
  <el-dialog v-model="show" :title="t('glossary.cameraDetails')" width="min(720px, calc(100vw - 48px))" @close="emit('update:visible', false)" class="ui-admin-dialog ui-scroll-dialog channel-detail-dialog">
    <dl class="channel-detail-grid">
      <div class="channel-detail-wide"><dt>{{ t('field.channelName') }}</dt><dd class="channel-detail-name">{{ props.detailChannel.channelName || '—' }}</dd></div>
      <div><dt>{{ t('field.channelNo') }}</dt><dd class="channel-detail-code">{{ props.detailChannel.videoChannelId ?? '—' }}</dd></div>
      <div><dt>{{ t('glossary.accessType') }}</dt><dd>{{ props.detailChannel.channelTypeLabel || '—' }}</dd></div>
      <div><dt>{{ t('field.resolution') }}</dt><dd>{{ props.detailChannel.resolution || '—' }}</dd></div>
      <div><dt>{{ t('field.videoCodec') }}</dt><dd>{{ props.detailChannel.codec || '—' }}</dd></div>
      <div><dt>{{ t('glossary.frameRate') }}</dt><dd>{{ props.detailChannel.fps || '—' }}</dd></div>
      <div><dt>{{ t('field.status') }}</dt><dd><span class="channel-detail-status" :class="{ 'is-online': props.detailChannel.channelStatus === 1 }"><i aria-hidden="true" />{{ props.detailChannel.channelStatus === 1 ? t('status.online') : t('status.offline') }}</span></dd></div>
      <div class="channel-detail-wide"><dt>{{ t('glossary.streamAddress') }}</dt><dd class="channel-detail-code">{{ props.detailChannel.url || '—' }}</dd></div>
      <div class="channel-detail-wide"><dt>{{ t('glossary.algorithmService') }}</dt><dd>
        <div v-for="(item, idx) in props.detailChannel.algorithms" :key="idx" class="channel-detail-task">
          <span>{{ resolveResourceAlgorithmName(item) }}</span>
          <span class="channel-detail-status" :class="{ 'is-online': item.status === 1 }">{{ item.status === 1 ? t('status.running') : item.statusText }}</span>
          <span v-if="item.workday" class="channel-detail-schedule">{{ item.workday }}</span>
        </div>
        <span v-if="!props.detailChannel.algorithms?.length">—</span>
      </dd></div>
    </dl>
    <template #footer><el-button @click="show = false">{{ t('action.close') }}</el-button></template>
  </el-dialog>
</template>

<script setup>
import { ref, watch } from 'vue'
import { t } from '@/i18n'
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

<style scoped>
.channel-detail-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 0 24px; margin: 0; }
.channel-detail-grid > div { padding: 12px 0; min-width: 0; border-bottom: 1px solid var(--border-light); }
.channel-detail-grid > div:last-child { border-bottom: 0; }
.channel-detail-grid dt { margin: 0 0 6px; color: var(--text-secondary); font-size: 12px; line-height: 18px; }
.channel-detail-grid dd { margin: 0; color: var(--text-primary); font-size: 13px; line-height: 21px; overflow-wrap: anywhere; }
.channel-detail-wide { grid-column: 1 / -1; }
.channel-detail-grid .channel-detail-name { font-size: 16px; font-weight: 600; }
.channel-detail-code { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-variant-numeric: tabular-nums; }
.channel-detail-status { display: inline-flex; align-items: center; gap: 6px; color: var(--text-secondary); font-size: 12px; }
.channel-detail-status.is-online { color: var(--theme-success, var(--el-color-success)); }
.channel-detail-status i { width: 6px; height: 6px; border-radius: 50%; background: currentColor; }
.channel-detail-task { display: flex; align-items: baseline; flex-wrap: wrap; gap: 4px 12px; padding: 4px 0; }
.channel-detail-schedule { color: var(--text-secondary); font-size: 12px; }
@media (max-width: 600px) { .channel-detail-grid { grid-template-columns: minmax(0, 1fr); } }
</style>
