<template>
  <div class="start-node">
    <!-- <button type="button" class="node-delete" @click.stop="handleDelete">×</button> -->
    <Handle type="source" :position="Position.Right" />
    <div class="node-icon">
      <svg viewBox="0 0 24 24" width="30" height="30" fill="#fff">
        <polygon points="6,4 20,12 6,20" />
      </svg>
    </div>
    <span class="start-label">{{ t('glossary.start') }}</span>
  </div>
</template>

<script setup>
import { Handle, Position, useVueFlow } from '@vue-flow/core'
import { t } from '@/i18n'

const props = defineProps({
  id: {
    type: String,
    required: true,
  },
})

const { getEdges, setEdges, setNodes } = useVueFlow()

const handleDelete = () => {
  const edges = getEdges && 'value' in getEdges ? getEdges.value : getEdges
  const incoming = edges.filter((e) => e.target === props.id)
  const outgoing = edges.filter((e) => e.source === props.id)

  const newEdges = []

  incoming.forEach((i) => {
    outgoing.forEach((o) => {
      if (i.source && o.target && i.source !== o.target) {
        newEdges.push({
          id: `${i.id}-${o.id}-bridge`,
          source: i.source,
          target: o.target,
          type: o.type || 'default',
          data: o.data,
        })
      }
    })
  })

  setEdges((current) =>
    current
      .filter((e) => e.source !== props.id && e.target !== props.id)
      .concat(newEdges),
  )

  setNodes((nodes) => nodes.filter((n) => n.id !== props.id))
}
</script>

<style scoped>
.start-node {
  position: relative;
  width: 76px;
  height: 96px;
  box-sizing: border-box;
  border-radius: 12px;
  background: #3182ce;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  color: #ffffff;
  box-shadow: none;
}

.node-icon {
  display: flex;
  align-items: center;
  justify-content: center;
}

.start-label {
  font-size: 11px;
  margin-top: 2px;
  letter-spacing: 1px;
}

.node-delete {
  position: absolute;
  top: -6px;
  right: -6px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: 1px solid #f56c6c;
  background-color: #ffffff;
  color: #f56c6c;
  font-size: 12px;
  line-height: 1;
  padding: 0;
  cursor: pointer;
}

.node-delete:hover {
  background-color: #fef0f0;
}
</style>
