<template>
  <div class="pair-panel">
    <p class="pair-hint">{{ $t('imageAnalysis.pairHint') }}</p>
    <div class="pair-images">
      <section v-for="(side, index) in ['A', 'B']" :key="side" class="pair-image">
        <div class="pair-heading">
          <strong>{{ $t(index ? 'imageAnalysis.imageB' : 'imageAnalysis.imageA') }}</strong>
          <el-upload action="#" :auto-upload="false" :show-file-list="false" :disabled="busy"
            accept="image/jpeg,image/png,image/bmp" :on-change="file => $emit('file-change', file, index)">
            <el-button type="primary" :disabled="busy">{{ $t(files[index] ? 'imageAnalysis.replaceImage' : 'imageAnalysis.uploadImage') }}</el-button>
          </el-upload>
        </div>
        <img v-if="files[index]" :ref="el => imageRefs[index] = el" :src="imageSource(index)"
          :alt="files[index].name" class="pair-original" @load="drawCrop(index)" />
        <el-empty v-else :description="$t('imageAnalysis.uploadImageFirst')" />
        <p v-if="files[index]" class="pair-filename">{{ files[index].name }}</p>
        <div v-if="target(index)" class="pair-target">
          <span>{{ $t('imageAnalysis.comparedTarget') }}</span>
          <canvas :ref="el => cropRefs[index] = el" />
        </div>
      </section>
    </div>
    <div v-if="error" class="pair-error" role="alert">
      <strong>{{ $t('imageAnalysis.failed') }}</strong>
      <p>{{ error.side === 'A' ? $t('imageAnalysis.imageA') + ': ' : error.side === 'B' ? $t('imageAnalysis.imageB') + ': ' : '' }}{{ error.message }}</p>
    </div>
    <section v-else-if="hasScore" class="pair-score" aria-live="polite">
      <span>{{ $t('imageAnalysis.pairScore') }}</span>
      <strong>{{ result.comparison.score.toFixed(2) }} / 100</strong>
      <span v-if="Number.isFinite(result.comparison.threshold)">{{ $t('imageAnalysis.pairThreshold') }}: {{ result.comparison.threshold }}</span>
      <el-tag v-if="result.comparison.decision === 'matched'" type="success">{{ $t('imageAnalysis.pairPassed') }}</el-tag>
      <el-tag v-else-if="result.comparison.decision === 'not_matched'" type="warning">{{ $t('imageAnalysis.pairBelow') }}</el-tag>
      <span v-else>{{ $t('imageAnalysis.pairScoreOnly') }}</span>
      <p>{{ $t(result.comparison.featureType === 'body' ? 'imageAnalysis.bodyScoreHint' : 'imageAnalysis.faceScoreHint') }}</p>
    </section>
    <pre v-if="debug && result" class="pair-debug">{{ JSON.stringify(result, null, 2) }}</pre>
  </div>
</template>

<script setup>
import { computed, nextTick, ref, watch } from 'vue'
const props = defineProps({ files: { type: Array, required: true }, result: Object, error: Object, busy: Boolean, debug: Boolean })
defineEmits(['file-change'])
const imageRefs = ref([])
const cropRefs = ref([])
const hasScore = computed(() => Number.isFinite(props.result?.comparison?.score))
const target = index => (index ? props.result?.referenceTargetList : props.result?.targetList)?.find(t => !t.filtered)
const imageSource = index => (index ? props.result?.referencePicture : props.result?.fullPicture) || props.files[index]?.preview
const drawCrop = index => {
  const image = imageRefs.value[index], canvas = cropRefs.value[index], box = target(index)?.box
  if (!image?.naturalWidth || !canvas || !box) return
  const x = Math.max(0, box.x), y = Math.max(0, box.y)
  const width = Math.min(box.width, image.naturalWidth - x), height = Math.min(box.height, image.naturalHeight - y)
  if (!(width > 0 && height > 0)) return
  const scale = Math.min(1, 180 / width, 180 / height)
  canvas.width = Math.max(1, Math.round(width * scale))
  canvas.height = Math.max(1, Math.round(height * scale))
  canvas.getContext('2d').drawImage(image, x, y, width, height, 0, 0, canvas.width, canvas.height)
}
watch(() => props.result, async () => { await nextTick(); drawCrop(0); drawCrop(1) })
</script>

<style scoped>
.pair-panel { overflow: auto; flex: 1; }
.pair-hint { color: var(--text-secondary); margin: 0 0 12px; }
.pair-images { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; }
.pair-image, .pair-score, .pair-error { border: 1px solid var(--border-color); border-radius: 8px; padding: 16px; background: var(--bg-white); }
.pair-heading { display: flex; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 12px; }
.pair-original { width: 100%; height: 280px; object-fit: contain; background: var(--bg-secondary); }
.pair-filename { overflow-wrap: anywhere; color: var(--text-secondary); }
.pair-target { display: flex; gap: 16px; align-items: center; }
.pair-score, .pair-error { margin-top: 16px; }
.pair-score { display: flex; align-items: center; flex-wrap: wrap; gap: 12px; }
.pair-score strong { font-size: 28px; color: var(--primary-color); }
.pair-score p { flex-basis: 100%; margin: 0; color: var(--text-secondary); }
.pair-error { color: var(--el-color-danger); }
.pair-debug { white-space: pre-wrap; overflow-wrap: anywhere; }
@media (max-width: 720px) { .pair-images { grid-template-columns: 1fr; } }
</style>
