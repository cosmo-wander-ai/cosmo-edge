<template>
  <svg viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">
    <g v-if="type === 'detect'">
      <rect x="4" y="5" width="24" height="22" rx="1.5" />
      <rect x="8" y="9" width="5" height="7" rx="0.5" />
      <rect x="18" y="16" width="6" height="7" rx="0.5" />
    </g>
    <g v-else-if="type === 'classify'">
      <circle cx="6" cy="16" r="3" />
      <path d="M9 16h5M14 8v16M14 8h6M14 16h6M14 24h6" />
      <rect x="20" y="5" width="8" height="6" rx="1" />
      <rect x="20" y="13" width="8" height="6" rx="1" />
      <rect x="20" y="21" width="8" height="6" rx="1" />
    </g>
    <g v-else-if="type === 'text-detection'">
      <path d="M5 12H4v15h24V12h-3" />
      <rect x="8" y="4" width="16" height="10" rx="1" />
      <path d="M11 7h10M11 11h6" />
      <rect x="16" y="18" width="7" height="5" rx="0.5" />
    </g>
    <g v-else-if="type === 'feature'">
      <rect x="2.5" y="8" width="12" height="16" rx="1" />
      <circle cx="6" cy="12" r="1" />
      <path d="m3 21 4-5 3 3 4-2M14.5 16h6m-2-2 2 2-2 2M24 8h-2v16h2M29 8h2v16h-2M25 11h3M25 16h2M25 21h3" />
    </g>
    <g v-else-if="type === 'keypoints'">
      <circle cx="16" cy="5" r="2.5" />
      <circle cx="16" cy="12" r="1.5" />
      <circle cx="8" cy="15" r="1.5" />
      <circle cx="24" cy="15" r="1.5" />
      <circle cx="16" cy="20" r="1.5" />
      <circle cx="10" cy="28" r="1.5" />
      <circle cx="22" cy="28" r="1.5" />
      <path d="M16 7.5v3M14.5 12.5l-5 2M17.5 12.5l5 2M16 13.5v5M15 21l-4 5.5M17 21l4 5.5" />
    </g>
    <g v-else-if="type === 'ocr'">
      <path d="M9 4H4v5M23 4h5v5M28 23v5h-5M9 28H4v-5M8 22l5-13 5 13M10 18h6M21 12h4M21 17h4M21 22h3" />
    </g>
    <g v-else-if="type === 'segmentation'">
      <rect x="4" y="4" width="24" height="24" rx="1.5" />
      <path d="m9 11 6-3 7 5-2 9-8 2-5-6Z" />
      <path d="m10 14 4-4M10 20l9-9M14 22l7-7" />
    </g>
    <g v-else-if="type === 'multimodal' || type === 'foundation'">
      <rect x="3" y="5" width="17" height="15" rx="1.5" />
      <circle cx="8" cy="10" r="1.5" />
      <path d="m4 18 5-5 4 4 3-3 3 3M24 8h5M24 13h5M24 18h5M8 25h21M8 29h14" />
    </g>
    <g v-else>
      <rect x="5" y="5" width="22" height="22" rx="2" />
      <path d="M5 12h22M12 5v7M12 20h8" />
    </g>
  </svg>
</template>

<script setup>
defineProps({ type: { type: String, default: 'other' } })
</script>
