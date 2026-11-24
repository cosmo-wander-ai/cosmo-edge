<template>
  <svg
    class="flow-node-icon"
    viewBox="0 0 24 24"
    xmlns="http://www.w3.org/2000/svg"
    aria-hidden="true"
    focusable="false"
  >
    <!-- Scene glyphs are from Lucide 1.17.0. See public/licenses/lucide.txt. -->
    <g v-if="scene && kind === 'video'">
      <path d="M15.033 9.44a.647.647 0 0 1 0 1.12l-4.065 2.352a.645.645 0 0 1-.968-.56V7.648a.645.645 0 0 1 .967-.56z" />
      <path d="M12 17v4" />
      <path d="M8 21h8" />
      <rect x="2" y="3" width="20" height="14" rx="2" />
    </g>
    <g v-else-if="scene && kind === 'target'">
      <path d="M3 7V5a2 2 0 0 1 2-2h2" />
      <path d="M17 3h2a2 2 0 0 1 2 2v2" />
      <path d="M21 17v2a2 2 0 0 1-2 2h-2" />
      <path d="M7 21H5a2 2 0 0 1-2-2v-2" />
    </g>
    <g v-else-if="scene && kind === 'tracking'">
      <circle cx="6" cy="19" r="3" />
      <path d="M9 19h8.5a3.5 3.5 0 0 0 0-7h-11a3.5 3.5 0 0 1 0-7H15" />
      <circle cx="18" cy="5" r="3" />
    </g>
    <path v-else-if="scene && kind === 'filter'" d="M10 20a1 1 0 0 0 .553.895l2 1A1 1 0 0 0 14 21v-7a2 2 0 0 1 .517-1.341L21.74 4.67A1 1 0 0 0 21 3H3a1 1 0 0 0-.742 1.67l7.225 7.989A2 2 0 0 1 10 14z" />
    <path v-else-if="scene && kind === 'judge'" d="M2.7 10.3a2.41 2.41 0 0 0 0 3.41l7.59 7.59a2.41 2.41 0 0 0 3.41 0l7.59-7.59a2.41 2.41 0 0 0 0-3.41l-7.59-7.59a2.41 2.41 0 0 0-3.41 0Z" />
    <g v-else-if="scene && kind === 'send'">
      <path d="M12 3v12" />
      <path d="m17 8-5-5-5 5" />
      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
    </g>
    <g v-else-if="kind === 'video'">
      <rect x="3" y="4" width="18" height="13" rx="1.5" />
      <path d="m10 8 5 3-5 3ZM12 17v4M8 21h8" />
    </g>
    <g v-else-if="kind === 'person'">
      <circle cx="12" cy="7" r="3.5" />
      <path d="M5 21v-2a7 7 0 0 1 14 0v2" />
    </g>
    <g v-else-if="kind === 'vehicle'">
      <path d="m5 9 2-5h10l2 5M3 10l2-1h14l2 1v8H3ZM5 18v3M19 18v3M3 14h4M17 14h4" />
    </g>
    <g v-else-if="kind === 'count'">
      <path d="M5 3v18M11 3v18M17 3v18M2 17 21 7" />
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
  kind: { type: String, default: 'default' },
  scene: Boolean
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
