export const emptyCatalog = () => ({ questions: [], default: [] })

export function readCatalog(value) {
  if (!value) return emptyCatalog()
  const catalog = typeof value === 'string' ? JSON.parse(value) : value
  if (!Array.isArray(catalog.questions) || !Array.isArray(catalog.default)) throw new Error('catalog')
  return JSON.parse(JSON.stringify(catalog))
}

export function validCatalog(value) {
  try {
    const { questions, default: selected } = readCatalog(value)
    const ids = questions.map(q => q.id)
    if (questions.length > 32 || new Set(ids).size !== ids.length || selected.length > 8 ||
        (questions.length > 0 && selected.length === 0) || new Set(selected).size !== selected.length ||
        selected.some(id => !ids.includes(id))) return false
    return questions.every(q => {
      if (!/^[A-Za-z0-9_.:-]{1,96}$/.test(q.id) || !Number.isInteger(q.version) || q.version < 1 ||
          !q.instructions?.trim() || !['noul', 'choice'].includes(q.type)) return false
      if (q.type === 'noul') return true
      const criteria = Array.isArray(q.criteria) ? q.criteria : Object.entries(q.criteria || {}).map(([label, description]) => ({ label, description }))
      return criteria.length >= 2 && criteria.length <= 16 && criteria.every(x => x.label?.trim()) &&
        new Set(criteria.map(x => x.label)).size === criteria.length
    })
  } catch { return false }
}

export function questionsFromParams(params = []) {
  const result = new Map()
  const visit = list => list.forEach(p => {
    const key = String(p.key || '').replace(/^param\./, '')
    try {
      if (key === 'visual.catalog') readCatalog(p.value).questions.forEach(q => result.set(q.id, q))
      else if (key.startsWith('visual.question.')) {
        const q = JSON.parse(p.value); result.set(q.id, q)
      }
    } catch { /* The task field reports malformed catalog data. */ }
    if (Array.isArray(p.children)) visit(p.children)
  })
  visit(params)
  return [...result.values()]
}

const roiQuestionKeys = new Set(['visual.questions', 'keywords', 'prompt', 'advanced_mode'])
export function readRoiQuestions(params = []) {
  const values = Object.fromEntries(params.map(p => [p.key, p.value]))
  if (values['visual.questions']) return { mode: 'select', selected: JSON.parse(values['visual.questions']), prompt: '' }
  if (values.prompt !== undefined || values.keywords !== undefined) return { mode: 'prompt', selected: [], prompt: values.prompt ?? values.keywords, advanced: values.advanced_mode ?? '1' }
  return { mode: 'inherit', selected: [], prompt: '' }
}
export function writeRoiQuestions(params = [], draft) {
  const result = params.filter(p => !roiQuestionKeys.has(p.key))
  if (draft.mode === 'select') result.push({ key: 'visual.questions', value: JSON.stringify(draft.selected) })
  if (draft.mode === 'prompt') result.push({ key: 'prompt', value: draft.prompt }, { key: 'advanced_mode', value: draft.advanced ?? '1' })
  return result
}
export function regionParams(row) {
  return [...(row.params || []).filter(p => !['name', 'directionType'].includes(p.key)),
    { key: 'name', value: row.name }, { key: 'directionType', value: row.directionType ?? '' }]
}
