<template>
  <svg
    class="flow-node-icon"
    viewBox="0 0 24 24"
    xmlns="http://www.w3.org/2000/svg"
    aria-hidden="true"
    focusable="false"
  >
    <g v-if="kind === 'video'">
      <rect x="3" y="4" width="18" height="13" rx="1.5" />
      <path d="m10 8 5 3-5 3ZM12 17v4M8 21h8" />
    </g>
    <g v-else-if="kind === 'target'">
      <path d="M8 3H3v5M16 3h5v5M21 16v5h-5M8 21H3v-5" />
      <rect x="7" y="8.5" width="10" height="7" rx="1" />
    </g>
    <g v-else-if="kind === 'tracking'">
      <path d="M6 16.2c1.1-2.4 2.5-4.1 4.4-5.5M13.8 8.4c1.3-.9 2.7-1.6 4.2-2.1" />
      <circle cx="5" cy="18" r="2" />
      <circle cx="12" cy="9.5" r="1.8" />
      <circle cx="20" cy="5.5" r="2" />
    </g>
    <g v-else-if="kind === 'ai' || kind === 'multimodal' || kind === 'foundation'">
      <rect x="2" y="3" width="13" height="12" rx="1" />
      <circle cx="6" cy="7" r="1" />
      <path d="m3 13 4-4 4 4 3-3M18 5h4M18 9h4M18 13h4M5 19h17M5 22h11" />
    </g>
    <g v-else-if="kind === 'grid'">
      <rect x="3" y="3" width="7" height="7" rx="1" />
      <rect x="14" y="3" width="7" height="7" rx="1" />
      <rect x="3" y="14" width="7" height="7" rx="1" />
      <rect x="14" y="14" width="7" height="7" rx="1" />
    </g>
    <path v-else-if="kind === 'filter'" d="M3 3h18l-7 10v8h-4v-8Z" />
    <g v-else-if="kind === 'judge' || kind === 'classify'">
      <path d="m11 2 8 8-8 8-8-8ZM11 18v4M17 20h4v2" />
      <circle cx="21" cy="20" r="1" />
    </g>
    <g v-else-if="kind === 'timer'">
      <circle cx="12" cy="14" r="8" />
      <path d="M9 2h6M12 2v4M18 7l2-2M12 10v4l3 2" />
    </g>
    <path v-else-if="kind === 'send'" d="M7 12H3v9h18v-9h-4M12 16V3m-4 4 4-4 4 4" />
    <g v-else-if="kind === 'text-detection'">
      <path d="M4 9H3v12h18V9h-1" />
      <rect x="6" y="3" width="12" height="8" rx="1" />
      <path d="M8.5 6h7M8.5 8.5h4" />
      <rect x="12" y="14" width="6" height="4" rx="0.5" />
    </g>
    <g v-else-if="kind === 'feature'">
      <rect x="2" y="6" width="9" height="12" rx="1" />
      <circle cx="5" cy="9" r="0.75" />
      <path d="m2.5 16 3-4 2 2 3-1M11 12h4m-2-2 2 2-2 2M18 5h-1v14h1M22 5h1v14h-1M19.5 8h1M19.5 12h1M19.5 16h1" />
    </g>
    <g v-else-if="kind === 'keypoints'">
      <circle cx="12" cy="4" r="2" />
      <circle cx="12" cy="9" r="1" />
      <circle cx="6" cy="12" r="1" />
      <circle cx="18" cy="12" r="1" />
      <circle cx="12" cy="15" r="1" />
      <circle cx="7" cy="21" r="1" />
      <circle cx="17" cy="21" r="1" />
      <path d="M12 6v2M11 9.5l-4 2M13 9.5l4 2M12 10v4M11.5 16l-4 4M12.5 16l4 4" />
    </g>
    <g v-else-if="kind === 'ocr'">
      <path d="M7 3H3v4M17 3h4v4M21 17v4h-4M7 21H3v-4M6 17l4-10 4 10M7.5 14h5M17 8h1M17 12h1M17 16h1" />
    </g>
    <g v-else-if="kind === 'segmentation'">
      <rect x="3" y="3" width="18" height="18" rx="1" />
      <path d="m7 8 5-2 5 4-2 7-6 1-4-5ZM7 11l4-4M7 15l7-7M10 17l6-6" />
    </g>
    <g v-else>
      <path d="M3 6h4m4 0h10M3 12h10m4 0h4M3 18h4m4 0h10" />
      <circle cx="9" cy="6" r="2" />
      <circle cx="15" cy="12" r="2" />
      <circle cx="9" cy="18" r="2" />
    </g>
  </svg>
</template>

<script setup>
defineProps({
  kind: { type: String, default: 'default' }
})
</script>

<style scoped>
.flow-node-icon {
  display: block;
  width: 24px;
  height: 24px;
  flex-shrink: 0;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.6;
  stroke-linecap: round;
  stroke-linejoin: round;
}
</style>
