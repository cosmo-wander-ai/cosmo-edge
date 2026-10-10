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
    <g v-else-if="kind === 'ai'">
      <path d="m10.8 7.2-4.6 9.5M13.2 7.2l4.6 9.5M7.5 19h9" />
      <circle cx="12" cy="5" r="2.5" />
      <circle cx="5" cy="19" r="2.5" />
      <circle cx="19" cy="19" r="2.5" />
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
