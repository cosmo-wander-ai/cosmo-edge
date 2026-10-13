<template>
  <el-table :data="rows" row-key="request_id" :empty-text="emptyText || t('visualReview.empty')" class="visual-audit-table">
    <el-table-column type="expand" width="42">
      <template #default="{ row }">
        <div class="audit-details">
          <dl class="audit-identity">
            <div class="audit-identity-wide"><dt>{{ t('visualReview.requestId') }}</dt><dd>{{ row.request_id }}</dd></div>
            <div><dt>{{ t('visualReview.run') }}</dt><dd>{{ row.request.run_epoch ?? '—' }}</dd></div>
            <div><dt>{{ t('visualReview.frame') }}</dt><dd>{{ row.request.frame_id ?? '—' }}</dd></div>
            <div><dt>{{ t('visualReview.config') }}</dt><dd>{{ row.request.config_revision ?? '—' }}</dd></div>
            <div><dt>{{ t('visualReview.alarms') }}</dt><dd>{{ row.events?.join(' / ') || '—' }}</dd></div>
            <div class="audit-identity-wide"><dt>{{ t('visualReview.model') }}</dt><dd>{{ row.request.manifest_sha256 || '—' }}</dd></div>
            <div><dt>{{ t('visualQuestions.acceptedPolicy') }}</dt><dd>{{ row.response?.decision?.profile_id || '—' }}</dd></div>
            <div><dt>{{ t('visualReview.decisionLabel') }}</dt><dd>{{ label('decision', row.response?.decision?.verdict) }}</dd></div>
          </dl>
          <el-table :data="row.items" :empty-text="t('visualReview.noQuestions')" class="audit-questions" size="small">
            <el-table-column :label="t('visualReview.question')" min-width="160">
              <template #default="{ row: item }">{{ item.identity.question_id }} v{{ item.identity.question_version }}</template>
            </el-table-column>
            <el-table-column :label="t('visualReview.modelResult')" min-width="120">
              <template #default="{ row: item }">{{ item.result?.status === 'completed' ? item.result.top1 : label('result', item.result?.status || 'pending') }}</template>
            </el-table-column>
            <el-table-column :label="t('visualReview.scores')" min-width="250">
              <template #default="{ row: item }">{{ visualReviewScores(item.result) }}</template>
            </el-table-column>
            <el-table-column :label="t('visualReview.reasonLabel')" min-width="180">
              <template #default="{ row: item }">{{ label('reason', item.result?.reason) }}</template>
            </el-table-column>
          </el-table>
        </div>
      </template>
    </el-table-column>
    <el-table-column :label="t('visualReview.time')" min-width="150"><template #default="{ row }"><span class="audit-time">{{ new Date(row.created_ms).toLocaleString() }}</span></template></el-table-column>
    <el-table-column prop="request.task_id" :label="t('visualReview.task')" min-width="125" show-overflow-tooltip />
    <el-table-column prop="request.roi_id" :label="t('visualReview.roi')" min-width="110" show-overflow-tooltip />
    <el-table-column :label="t('visualReview.modelResult')" min-width="132"><template #default="{ row }"><span class="audit-result" :class="resultClass(row.response?.status)">{{ label('result', row.response?.status || 'pending') }}</span></template></el-table-column>
    <el-table-column :label="t('visualReview.filterAction')" min-width="110"><template #default="{ row }"><span :class="{ 'audit-action': row.response?.decision }">{{ row.response?.decision ? t(row.response.decision.filter_applied ? 'visualReview.filtered' : 'visualReview.retained') : '—' }}</span></template></el-table-column>
    <el-table-column :label="t('visualReview.reasonLabel')" min-width="170"><template #default="{ row }">{{ label('reason', row.response?.reason || row.response?.decision?.reason) }}</template></el-table-column>
    <el-table-column :label="t('visualReview.deliveryLabel')" min-width="150"><template #default="{ row }"><span :class="{ 'audit-record-error': ['alarm_store_failed', 'interrupted'].includes(row.delivery) }">{{ label('delivery', row.delivery) }}</span></template></el-table-column>
  </el-table>
</template>
<script setup>
import { t } from '@/i18n'
import { visualReviewLabel, visualReviewScores } from '@/utils/visualReview'
defineProps({ rows: { type: Array, default: () => [] }, emptyText: { type: String, default: '' } })
const label = (group, code) => visualReviewLabel(t, group, code)
const resultClass = code => code === 'completed' ? 'is-completed' : code === 'partial' ? 'is-partial' : 'is-unknown'
</script>
<style scoped>
.audit-details { padding: 14px 20px 18px; overflow-wrap: anywhere; background: var(--bg-subtle); }
.audit-identity { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 24px; margin: 0 0 14px; color: var(--text-secondary); font-size: 12px; }
.audit-identity > div { display: grid; grid-template-columns: 128px minmax(0, 1fr); gap: 10px; }
.audit-identity dt { color: var(--text-secondary); }
.audit-identity dd { margin: 0; color: var(--text-primary); font-family: var(--font-mono, ui-monospace, monospace); }
.audit-identity .audit-identity-wide { grid-column: 1 / -1; }
.audit-result { display: inline-flex; align-items: baseline; gap: 7px; }
.audit-result::before { content: ''; display: inline-block; flex: none; width: 6px; height: 6px; border-radius: 50%; background: currentColor; }
.is-completed { color: var(--success-color); }
.is-partial { color: var(--warning-color); }
.is-unknown { color: var(--text-secondary); }
.audit-action { color: var(--primary-color); }
.audit-record-error { color: var(--danger-color); }
.audit-time { font-variant-numeric: tabular-nums; }
.audit-questions { border: 1px solid var(--border-color); border-radius: 4px; }
.visual-audit-table :deep(.cell) { line-height: 1.6; overflow-wrap: anywhere; }
@media (max-width: 850px) {
  .audit-identity { grid-template-columns: minmax(0, 1fr); }
  .audit-details { padding: 12px; }
}
</style>
