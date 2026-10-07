<template>
  <el-table :data="rows" row-key="request_id" :empty-text="t('visualReview.empty')">
    <el-table-column type="expand">
      <template #default="{ row }">
        <div class="audit-details">
          <el-descriptions :column="1" border>
            <el-descriptions-item :label="t('visualReview.requestId')">{{ row.request_id }}</el-descriptions-item>
            <el-descriptions-item :label="t('visualReview.run')">{{ row.request.run_epoch }}</el-descriptions-item>
            <el-descriptions-item :label="t('visualReview.frame')">{{ row.request.frame_id }}</el-descriptions-item>
            <el-descriptions-item :label="t('visualReview.config')">{{ row.request.config_revision }}</el-descriptions-item>
            <el-descriptions-item :label="t('visualReview.model')">{{ row.request.manifest_sha256 || '—' }}</el-descriptions-item>
            <el-descriptions-item :label="t('visualReview.alarms')">{{ row.events?.join(' / ') || '—' }}</el-descriptions-item>
          </el-descriptions>
          <el-table :data="row.items" :empty-text="t('visualReview.noQuestions')">
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
    <el-table-column :label="t('visualReview.time')" min-width="170"><template #default="{ row }">{{ new Date(row.created_ms).toLocaleString() }}</template></el-table-column>
    <el-table-column prop="request.task_id" :label="t('visualReview.task')" min-width="150" show-overflow-tooltip />
    <el-table-column prop="request.roi_id" :label="t('visualReview.roi')" min-width="160" show-overflow-tooltip />
    <el-table-column :label="t('visualReview.status')" min-width="130"><template #default="{ row }">{{ label('result', row.response?.status || 'pending') }}</template></el-table-column>
    <el-table-column :label="t('visualReview.reasonLabel')" min-width="180"><template #default="{ row }">{{ label('reason', row.response?.reason) }}</template></el-table-column>
    <el-table-column :label="t('visualReview.deliveryLabel')" min-width="160"><template #default="{ row }">{{ label('delivery', row.delivery) }}</template></el-table-column>
  </el-table>
</template>
<script setup>
import { t } from '@/i18n'
import { visualReviewLabel, visualReviewScores } from '@/utils/visualReview'
defineProps({ rows: { type: Array, default: () => [] } })
const label = (group, code) => visualReviewLabel(t, group, code)
</script>
<style scoped>
.audit-details { padding: 12px 24px; overflow-wrap: anywhere; }
</style>
