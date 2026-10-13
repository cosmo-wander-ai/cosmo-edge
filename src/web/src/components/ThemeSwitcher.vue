<script setup>
import { computed } from 'vue'
import { Sunny, Moon, Monitor, Check } from '@element-plus/icons-vue'
import { t } from '@/i18n'
import { useAppearance } from '@/composables/useAppearance'

const { appearance, setAppearance, setPalette } = useAppearance()
const options = [
  { value: 'light', label: 'appearance.light', icon: Sunny },
  { value: 'dark', label: 'appearance.dark', icon: Moon },
  { value: 'system', label: 'appearance.system', icon: Monitor }
]
const palettes = [
  { value: 'modern', label: 'appearance.modern' },
  { value: 'classic-blue', label: 'appearance.classicBlue' }
]
const active = computed(() => options.find((option) => option.value === appearance.value.preference) || options[0])
const activePalette = computed(() => palettes.find((option) => option.value === appearance.value.palette) || palettes[0])
// A cached pre-palette controller can still switch modes without breaking render.
const paletteAvailable = typeof setPalette === 'function'
const description = computed(() => `${t('appearance.switch')} · ${t(active.value.label)} · ${t(activePalette.value.label)}`)
const choiceLabel = (group, option, selected) => `${t(group)} · ${t(option.label)}${selected ? ` · ${t('appearance.selected')}` : ''}`

const selectAppearance = (command) => {
  if (command.kind === 'mode') setAppearance(command.value)
  else if (command.kind === 'palette' && paletteAvailable) setPalette(command.value)
}
</script>

<template>
  <el-dropdown trigger="click" placement="bottom-end" popper-class="appearance-menu" @command="selectAppearance">
    <button type="button" class="appearance-trigger" :aria-label="description" :title="description">
      <el-icon><component :is="active.icon" /></el-icon>
      <span class="appearance-label">{{ t(active.label) }}</span>
    </button>
    <template #dropdown>
      <el-dropdown-menu :aria-label="t('appearance.switch')">
        <li class="appearance-section" role="presentation">{{ t('appearance.mode') }}</li>
        <el-dropdown-item v-for="option in options" :key="option.value" :command="{ kind: 'mode', value: option.value }" :aria-label="choiceLabel('appearance.mode', option, appearance.preference === option.value)" :aria-current="appearance.preference === option.value ? 'true' : undefined" :class="{ 'is-selected': appearance.preference === option.value }">
          <el-icon><component :is="option.icon" /></el-icon>
          <span>{{ t(option.label) }}</span>
          <el-icon class="appearance-check" aria-hidden="true" :class="{ 'is-visible': appearance.preference === option.value }"><Check /></el-icon>
        </el-dropdown-item>
        <li class="appearance-divider" role="separator"></li>
        <li class="appearance-section" role="presentation">{{ t('appearance.palette') }}</li>
        <el-dropdown-item v-for="option in palettes" :key="option.value" :command="{ kind: 'palette', value: option.value }" :disabled="!paletteAvailable" :aria-label="choiceLabel('appearance.palette', option, activePalette.value === option.value)" :aria-current="activePalette.value === option.value ? 'true' : undefined" :class="{ 'is-selected': activePalette.value === option.value }">
          <span class="appearance-swatch" :class="`is-${option.value}`" aria-hidden="true"></span>
          <span>{{ t(option.label) }}</span>
          <el-icon class="appearance-check" aria-hidden="true" :class="{ 'is-visible': activePalette.value === option.value }"><Check /></el-icon>
        </el-dropdown-item>
      </el-dropdown-menu>
    </template>
  </el-dropdown>
</template>

<style lang="scss">
.appearance-trigger {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-width: 32px;
  height: 32px;
  padding: 0 8px;
  border: 1px solid transparent;
  border-radius: 6px;
  color: var(--secondary-color);
  background: transparent;
  font: inherit;
  font-size: 13px;
  white-space: nowrap;
  cursor: pointer;
  .el-icon { font-size: 17px; }
  &:hover { background: var(--bg-secondary); color: var(--primary-color); }
  &:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; }
}
.appearance-menu .el-dropdown-menu__item {
  display: flex;
  gap: 8px;
  min-width: 150px;
  .el-icon { margin: 0; }
  &.is-selected { color: var(--primary-color); background: var(--el-color-primary-light-9); }
  .appearance-check { margin-left: auto; visibility: hidden; }
  .appearance-check.is-visible { visibility: visible; }
}
.appearance-menu {
  .appearance-section {
    padding: 7px 16px 4px;
    color: var(--text-secondary);
    font-size: 12px;
    line-height: 18px;
    list-style: none;
  }
  .appearance-divider {
    height: 1px;
    margin: 5px 0 0;
    background: var(--border-light);
    list-style: none;
  }
  .appearance-swatch {
    width: 14px;
    height: 14px;
    border-radius: 4px;
    flex-shrink: 0;
    &.is-modern { background: #6254cd; }
    &.is-classic-blue { background: #2f75b5; }
  }
}
@media (max-width: 1024px) { .appearance-trigger .appearance-label { display: none; } }
</style>
