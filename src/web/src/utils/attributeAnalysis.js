export const emptyAttributeSchema = () => ({ objectType: 'person', attributes: [] })
export const parseAttributeSchema = (value) => {
  try {
    const schema = typeof value === 'string' ? JSON.parse(value) : JSON.parse(JSON.stringify(value || emptyAttributeSchema()))
    return schema && Array.isArray(schema.attributes) ? schema : emptyAttributeSchema()
  }
  catch { return emptyAttributeSchema() }
}
const identifier = (value) => typeof value === 'string' && /^[A-Za-z0-9_.-]{1,64}$/.test(value)
export function validAttributeSchema(schema) {
  if (!identifier(schema?.objectType) || !Array.isArray(schema.attributes) || !schema.attributes.length || schema.attributes.length > 32) return false
  if (new TextEncoder().encode(JSON.stringify(schema)).length > 65536) return false
  const keys = new Set()
  return schema.attributes.every(a => {
    if (!identifier(a.key) || keys.has(a.key) || !a.name || a.name.length > 128 || !identifier(a.sourceNode) || !identifier(a.modelCode)) return false
    keys.add(a.key)
    if (!['single', 'multiple', 'binary'].includes(a.type) || !Number.isFinite(a.threshold) || a.threshold < 0 || a.threshold > 1 || !Number.isFinite(a.minRatio) || a.minRatio <= 0.5 || a.minRatio > 1) return false
    if (!Array.isArray(a.options) || !a.options.length || a.options.length > 64) return false
    const binary = a.type === 'binary'
    if (binary && (a.options.length !== 2 || !a.options[0].label || a.options[1].label !== '')) return false
    const labels = new Set(), values = new Set()
    return a.options.every(o => {
      if (typeof o.label !== 'string' || (!binary && !o.label) || o.label.length > 128 || !identifier(o.value) || !o.name || o.name.length > 128 || labels.has(o.label) || values.has(o.value)) return false
      labels.add(o.label); values.add(o.value)
      return true
    })
  })
}
export const attributeParam = (node, key) => node?.configObject?.params?.find(p => p.key === key)?.value
export function attributeNodes(process) {
  try {
    const nodes = typeof process === 'string' ? JSON.parse(process) : process
    return (nodes || []).filter(n => n.actionId === 'BA_20003' && attributeParam(n, 'inputMode') === 'attributes')
  } catch { return [] }
}
export function validAttributeFlow(process) {
  try {
    const nodes = typeof process === 'string' ? JSON.parse(process) : process
    const accumulators = attributeNodes(nodes)
    if (!accumulators.length) return true
    if (accumulators.length !== 1) return false
    const node = accumulators[0], schema = parseAttributeSchema(attributeParam(node, 'attributeSchema'))
    if (!validAttributeSchema(schema)) return false
    const byId = new Map(nodes.map(n => [String(n.flowActionId), n]))
    const ancestors = (current, seen = new Set()) => {
      for (const id of String(current?.preFlowActionId || '').split(',')) {
        if (!id || seen.has(id) || !byId.has(id)) continue
        seen.add(id); ancestors(byId.get(id), seen)
      }
      return seen
    }
    const upstream = ancestors(node)
    if ([node, ...[...upstream].map(id => byId.get(id))].some(n => String(n.preFlowActionId || '').includes(','))) return false
    return [...upstream].some(id => byId.get(id).actionId === 'AA_00003') &&
      schema.attributes.every(a => upstream.has(a.sourceNode) && byId.get(a.sourceNode)?.actionId === 'AA_00002' && String(attributeParam(byId.get(a.sourceNode), 'atomicCode')) === a.modelCode && [...ancestors(byId.get(a.sourceNode))].some(id => byId.get(id).actionId === 'AA_00003')) &&
      nodes.some(n => n.actionId === 'BA_00004' && ancestors(n).has(String(node.flowActionId)))
  } catch { return false }
}
export function attributeRecord(row) {
  try { return (typeof row.property === 'string' ? JSON.parse(row.property) : row.property)?.attributes || null }
  catch { return null }
}
export function attributeDisplay(record, definition, unknown, failed, notApplicable) {
  const value = record?.attributes?.find(a => a.key === definition.key)
  if (!value) return notApplicable
  if (value.status !== 'valid') return value.status === 'failed' ? failed : unknown
  const original = record.schema.attributes.find(a => a.key === definition.key)
  return value.values.map(v => original?.options.find(o => o.value === v)?.name || v).join(' / ')
}

export function attributeClassifierSources(nodes, edges, targetId) {
  const upstream = new Set()
  const visit = id => edges.filter(edge => String(edge.target) === String(id)).forEach(edge => {
    const parent = String(edge.source)
    if (!upstream.has(parent)) { upstream.add(parent); visit(parent) }
  })
  visit(targetId)
  return nodes.filter(node => upstream.has(String(node.id)) && node.data?.actionId === 'AA_00002')
    .map(node => ({ ...node.data?.configObject?.webConfig?.atomic, position: String(node.id) }))
    .filter(source => source.atomicCode)
}
