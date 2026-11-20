const behaviorKeyPattern = /^filter\.attr\.(smoking|sleep|fall|normal)\.confidence\.min$/

export const behaviorFilterOptions = (atomicList, edges, currentNode) => {
  const scopeToUpstream = Array.isArray(edges) && currentNode
  const upstream = new Set()
  if (scopeToUpstream) {
    const pending = [String(currentNode)]
    while (pending.length) {
      const target = pending.pop()
      for (const edge of edges) {
        const source = String(edge.source)
        if (String(edge.target) === target && !upstream.has(source)) {
          upstream.add(source)
          pending.push(source)
        }
      }
    }
  }
  const options = new Map()
  for (const atomic of atomicList) {
    if (scopeToUpstream && !upstream.has(String(atomic.position))) continue
    for (const score of atomic.behaviorScores || []) {
      if (score?.nameCN && behaviorKeyPattern.test(`filter.attr.${score.class_name}.confidence.min`)) {
        options.set(score.class_name, score)
      }
    }
  }
  return [...options.values()]
}

export const readBehaviorFilters = (params) => [
  ...new Set((Array.isArray(params) ? params : []).flatMap(param => {
    const match = behaviorKeyPattern.exec(param?.key)
    return match ? [match[1]] : []
  }))
]

export const buildBehaviorFilterParams = (selected, options, sceneParams, nodeParams, t) =>
  selected.map((behavior) => {
    const key = `filter.attr.${behavior}.confidence.min`
    const option = options.find(({ class_name }) => class_name === behavior)
    const scene = sceneParams.find((param) => param.key === key)
    const node = nodeParams.find((param) => param.key === key)
    const value = String(scene?.defaultValue ?? scene?.value ?? node?.value ?? '').trim() || '0.70'
    const name = option?.nameCN || behavior
    return {
      key,
      value,
      defaultValue: value,
      name: t('glossary.labelConfidence', { name }),
      description: t('glossary.labelConfidenceDesc', { name }),
      type: 'text',
      regexpr: '/^(0(\\.\\d{1,3})?|1(\\.0{1,3})?)$/',
      failedTip: t('validate.enterConfidence01'),
      level: '2',
      senior: 1
    }
  })
