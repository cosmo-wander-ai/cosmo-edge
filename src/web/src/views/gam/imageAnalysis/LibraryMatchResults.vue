<template>
  <div v-if="matches.length" class="library-matches">
    <div class="match-heading">{{ $t('imageAnalysis.libraryMatches') }}</div>
    <div v-for="(target, index) in matches" :key="target.targetId || index" class="library-match" @click.stop>
      <el-image v-if="target.matchInfo.baseImageUrl" :src="target.matchInfo.baseImageUrl"
        :preview-src-list="[target.matchInfo.baseImageUrl]" preview-teleported fit="contain" class="reference-image">
        <template #error><span>{{ $t('imageAnalysis.referenceUnavailable') }}</span></template>
      </el-image>
      <span v-else class="reference-image">{{ $t('imageAnalysis.referenceUnavailable') }}</span>
      <div class="match-details">
        <div>{{ $t('imageAnalysis.matchedName') }}: {{ target.matchInfo.name || target.matchInfo.personId || target.matchInfo.matchId || '-' }}</div>
        <div>{{ $t('imageAnalysis.matchedLibrary') }}: {{ target.matchInfo.groupName || target.matchInfo.groupId || '-' }}</div>
        <div>{{ $t('imageAnalysis.matchScore') }}: {{ formatScore(target.matchInfo.matchDegree) }}</div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
const props = defineProps({ targets: { type: Array, default: () => [] } })
const matches = computed(() => props.targets.filter(target => !target.filtered && target.matchInfo?.matched === true))
// Similarity is already on the service's 0–100 scale; it is not detector confidence.
const formatScore = score => typeof score === 'number' && Number.isFinite(score) && score >= 0 ? score.toFixed(2) : '-'
</script>

<style scoped>
.library-matches { margin: 10px 0; }
.match-heading { font-weight: 600; margin-bottom: 8px; }
.library-match { display: flex; gap: 10px; padding: 8px 0; align-items: center; }
.reference-image { width: 72px; height: 88px; flex-shrink: 0; font-size: 12px; }
.match-details { min-width: 0; overflow-wrap: anywhere; line-height: 1.7; font-size: 13px; }
</style>
