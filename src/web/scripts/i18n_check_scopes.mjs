import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findI18nRepoRoot } from './i18n_repo_root.mjs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = findI18nRepoRoot(__dirname)
const glossaryPath = path.join(repoRoot, 'docs', 'i18n', 'GLOSSARY.md')
const scopesPath = path.join(repoRoot, 'docs', 'i18n', 'SHORT-SCOPES.md')

const readFile = (file) => fs.readFileSync(file, 'utf8')

const scopeContent = readFile(scopesPath)
const scopeSection = scopeContent.split('## Scope 一览')[1]?.split('## 使用规则')[0] || ''
const allowedScopes = new Set(
  [...scopeSection.matchAll(/^\|\s*`([^`]+)`\s*\|/gm)].map(match => match[1])
)

const glossaryContent = readFile(glossaryPath)
const invalid = []
let activeGlossaryTable = false

for (const line of glossaryContent.split(/\r?\n/)) {
  const cells = line.split('|').slice(1, -1).map(cell => cell.trim())
  if (!line.startsWith('|')) {
    activeGlossaryTable = false
    continue
  }
  if (cells[0] === '中文' && cells[1] === 'EN' && cells[2] === 'Short' && cells[3] === 'Short scope') {
    activeGlossaryTable = true
    continue
  }
  if (!activeGlossaryTable || line.includes('---')) continue
  if (cells.length < 4) continue
  const term = cells[0]
  const scopeCell = cells[3]
  if (!scopeCell || scopeCell === '-') continue
  const scopes = scopeCell
    .replace(/`/g, '')
    .split(',')
    .map(scope => scope.trim())
    .filter(Boolean)
  for (const scope of scopes) {
    if (!allowedScopes.has(scope)) {
      invalid.push(`${term}: ${scope}`)
    }
  }
}

if (invalid.length) {
  console.error('Invalid I18N short scopes:')
  invalid.forEach(item => console.error(`- ${item}`))
  process.exit(1)
}

console.log(`I18N short scopes OK (${allowedScopes.size} allowed scopes).`)
