<template>
  <el-dialog v-model="visible" title="安全帽复核记录" width="90%" destroy-on-close>
    <el-alert title="试验功能：固定安全帽问题，业务验收前不自动过滤。不确定、超时或服务异常时正常报警。" type="info" :closable="false" />
    <p>复核服务：{{ runtimeStates[runtime.state] || runtime.state }} · 自动过滤：{{ runtime.automatic_filtering ? '已开启' : '未开启' }}</p>
    <p>保留最近 {{ retention }} 条候选记录，包含已过滤记录。复核状态与告警推送状态分别记录。</p>
    <el-alert v-if="error" :title="error" type="error" :closable="false" />
    <el-table v-loading="loading" :data="rows" row-key="event_id">
      <el-table-column type="expand">
        <template #default="{ row }">
          <el-descriptions :column="1" border>
            <el-descriptions-item label="告警候选 ID">{{ row.event_id }}</el-descriptions-item>
            <el-descriptions-item label="任务 / 运行批次">{{ row.task_id }} / {{ row.run_epoch }}</el-descriptions-item>
            <el-descriptions-item label="问题 / 策略">{{ row.question_id }} v{{ row.question_version }} / {{ row.policy_version }}</el-descriptions-item>
            <el-descriptions-item label="概率（戴帽 / 未戴帽 / 不确定）">{{ probabilities(row) }}</el-descriptions-item>
            <el-descriptions-item label="精度状态">{{ row.result?.numeric_status || '无模型结果' }}</el-descriptions-item>
          </el-descriptions>
        </template>
      </el-table-column>
      <el-table-column label="时间" min-width="165"><template #default="{ row }">{{ new Date(row.created_ms).toLocaleString() }}</template></el-table-column>
      <el-table-column prop="channel_name" label="通道" />
      <el-table-column label="模式"><template #default="{ row }">{{ row.mode === 'review' ? '限时复核' : '观察' }}</template></el-table-column>
      <el-table-column label="结果"><template #default="{ row }">{{ decisions[row.result?.decision] || '等待结果' }}</template></el-table-column>
      <el-table-column label="原因" min-width="210"><template #default="{ row }">{{ reasons[row.result?.reason] || row.result?.reason || '处理中' }}</template></el-table-column>
      <el-table-column label="告警记录"><template #default="{ row }">{{ publications[row.publication] || row.publication }}</template></el-table-column>
    </el-table>
    <el-pagination v-model:current-page="page" :page-size="20" :total="total" layout="prev, pager, next, total" @current-change="load" />
    <template #footer><el-button @click="load">刷新</el-button><el-button @click="visible = false">关闭</el-button></template>
  </el-dialog>
</template>
<script setup>
import { computed, getCurrentInstance, ref, watch } from 'vue'
const props = defineProps({ modelValue: Boolean, eventId: { type: String, default: '' } })
const emit = defineEmits(['update:modelValue'])
const visible = computed({ get: () => props.modelValue, set: v => emit('update:modelValue', v) })
const { proxy } = getCurrentInstance()
const rows = ref([]), total = ref(0), page = ref(1), loading = ref(false), error = ref(''), retention = ref(10000)
let generation = 0
const runtime = ref({ state: 'unavailable', automatic_filtering: false })
const runtimeStates = { ready: '就绪', loading: '加载中', stopped: '已停止', unavailable: '不可用' }
const decisions = { unknown: '不确定（放行）', confirm: '确认告警', reject: '建议过滤' }
const publications = { published: '已写入告警', filtered: '已过滤', interrupted: '发布中断（需核对告警）', pending: '待发布', alarm_store_failed: '告警落库失败' }
const reasons = { business_qualification_pending: '等待业务样本验收', worker_unavailable: '服务不可用', deadline_exceeded: '超过复核时限', queue_full: '复核队列已满', stale_task_run: '任务已停止或重启', engine_restarted: '引擎已重启', unsupported_or_empty_roi: '图像不可用或包含多个目标', model_identity_mismatch: '模型版本不匹配', helmet_present: '检测到安全帽', helmet_absent: '未检测到安全帽', uncertain_or_low_confidence: '结果不确定或置信度不足' }
const probabilities = row => row.result?.probabilities?.map(v => `${(v * 100).toFixed(1)}%`).join(' / ') || '—'
async function load() {
  const current = ++generation
  loading.value = true; error.value = ''
  try {
    const response = await proxy.$API.boxQueryLayaReview({ eventId: props.eventId, pageNum: page.value, pageSize: 20 })
    if (current !== generation) return
    if (response.resCode !== 1 || !response.resData) throw new Error('query failed')
    runtime.value = response.resData.runtime || { state: 'unavailable', automatic_filtering: false }; rows.value = response.resData.rows; total.value = response.resData.total; retention.value = response.resData.retention_limit
  } catch {
    if (current === generation) { rows.value = []; error.value = '复核记录查询失败，请检查服务后重试。' }
  } finally { if (current === generation) loading.value = false }
}
watch(() => [props.modelValue, props.eventId], () => {
  ++generation
  if (props.modelValue) { page.value = 1; load() }
})
</script>
