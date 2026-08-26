import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

const taskCardPath = fileURLToPath(new URL(
  '../src/views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue',
  import.meta.url
))
const source = readFileSync(taskCardPath, 'utf8')

const ruleBody = (selector) => {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]+)\\}`))
  assert.ok(match, `missing ${selector} style rule`)
  return match[1]
}

const pageRule = ruleBody('.task-page')
assert.match(pageRule, /min-height:\s*0\s*;/)
assert.match(pageRule, /overflow:\s*hidden\s*;/)

const toolbarRule = ruleBody('.task-toolbar')
assert.match(toolbarRule, /flex-shrink:\s*0\s*;/)

const gridRule = ruleBody('.task-grid')
assert.match(gridRule, /grid-auto-rows:\s*minmax\(260px,\s*auto\)\s*;/)
assert.match(gridRule, /min-height:\s*0\s*;/)
assert.match(gridRule, /overflow-y:\s*auto\s*;/)
assert.match(gridRule, /overflow-x:\s*hidden\s*;/)

const cardRule = ruleBody('.task-card')
assert.match(cardRule, /min-height:\s*260px\s*;/)
assert.match(cardRule, /height:\s*auto\s*;/)

const footerRule = ruleBody('.card-footer')
assert.match(footerRule, /flex-shrink:\s*0\s*;/)

const paginationRule = ruleBody('.pagination-container')
assert.match(paginationRule, /flex-shrink:\s*0\s*;/)

console.log('Task card layout checks passed')
