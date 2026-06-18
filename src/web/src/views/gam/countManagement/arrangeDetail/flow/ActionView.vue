<template>
  <div class="action-container">
    <div v-if="Object.keys(actions).length === 0" class="empty-block">
      <el-empty :description="t('glossary.noActions')" />
    </div>
    <div v-else v-for="group in Object.keys(actions)" :key="group">
      <div
        class="group-title"
        role="button"
        tabindex="0"
        :aria-expanded="expandedGroups[group]"
        @click="toggleGroup(group)"
        @keydown.enter="toggleGroup(group)"
        @keydown.space.prevent="toggleGroup(group)"
      >
        <el-icon class="group-arrow" :class="{ 'is-expand': expandedGroups[group] }">
          <ArrowRight />
        </el-icon>
        <span class="group-name">{{ groupDict[group] || t('glossary.uncategorized') }}</span>
        <span class="group-count">{{ actions[group]?.length || 0 }}</span>
      </div>
      <Transition name="collapse">
        <div class="action-body" v-show="expandedGroups[group]">
          <div
            class="item-action"
            v-for="item in actions[group]"
            :key="item.id"
            role="button"
            tabindex="0"
            @click="actionClick(item)"
            @keydown.enter="actionClick(item)"
            @keydown.space.prevent="actionClick(item)"
          >
            <span>{{ resolveResourceActionName(item) }}</span>
          </div>
        </div>
      </Transition>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import _ from 'lodash'
import { ArrowRight } from '@element-plus/icons-vue'
import { t } from '@/i18n'
import { resolveResourceActionName } from '@/utils/i18nResource'

const props = defineProps({
  actionList: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['onAction'])

const actions = ref({})
const expandedGroups = ref({})
const groupDict = {
  1: t('glossary.algorithmActions'),
  2: t('glossary.businessProcessing'),
  3: t('glossary.otherComponents')
}

const rebuildGroups = (list) => {
  const grouped = _.groupBy(Array.isArray(list) ? list : [], 'actionType')
  const nextExpanded = {}
  Object.keys(grouped).forEach((group) => {
    nextExpanded[group] = expandedGroups.value[group] ?? true
  })
  actions.value = grouped
  expandedGroups.value = nextExpanded
}

watch(
  () => props.actionList,
  (val) => {
    rebuildGroups(val)
  },
  { immediate: true, deep: true }
)

const actionClick = (obj) => {
  emit('onAction', obj)
}

const toggleGroup = (group) => {
  expandedGroups.value[group] = !expandedGroups.value[group]
}
</script>

<style lang="scss" scoped>
.action-container {
  padding: 12px 16px;
}

.group-title {
  padding: 8px 0;
  cursor: pointer;
  display: flex;
  align-items: center;
  color: var(--text-primary);
  font-weight: 600;
  user-select: none;

  .group-arrow {
    margin-right: 6px;
    transition: transform 0.2s ease;
    color: var(--text-secondary);
  }

  .group-arrow.is-expand {
    transform: rotate(90deg);
  }
  
  .group-name {
    flex: none;
    font-size: 14px;
    margin-right: 8px;
  }

  .group-count {
    flex: none;
    min-width: 22px;
    height: 20px;
    padding: 0 6px;
    border-radius: 10px;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-size: 12px;
    line-height: 20px;
    text-align: center;
  }
}

.action-body {
  display: flex;
  flex-wrap: wrap;
  margin-bottom: 10px;
  cursor: pointer;
}

.item-action {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 200px;
  height: 40px;
  margin-right: 15px;
  margin-bottom: 15px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  text-align: center;
  transition: all 0.2s ease;
  outline: none;

  span {
    font-size: 14px;
    color: var(--text-primary);
  }
}

.item-action:hover {
  border-color: var(--primary-color);
  box-shadow: var(--shadow-sm);
  transform: translateY(-1px);
}

.item-action:focus-visible {
  border-color: var(--primary-color);
  box-shadow: 0 0 0 3px rgba(49, 130, 206, 0.15);
}

.collapse-enter-active,
.collapse-leave-active {
  transition: all 0.2s ease;
}
.collapse-enter-from,
.collapse-leave-to {
  opacity: 0;
  transform: translateY(-2px);
}

.empty-block {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px 0;
  background: var(--bg-white);
  border: 1px dashed var(--border-color);
  border-radius: var(--radius-sm);
}
</style>
