import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { findI18nRepoRoot } from './i18n_repo_root.mjs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const workspaceRoot = findI18nRepoRoot(__dirname)
const glossaryPath = path.join(workspaceRoot, 'docs', 'i18n', 'GLOSSARY.md')
const glossaryModulePath = path.resolve(__dirname, '..', 'src', 'i18n', 'glossary.js')

const keyOverrides = new Map([
  ['批量删除', 'action.bulkDelete'],
  ['详情', 'action.details'],
  ['点击上传', 'action.clickUpload'],
  ['说明', 'field.description'],
  ['地址', 'field.address'],
  ['更新时间', 'field.updateTime'],
  ['请输入', 'placeholder.enter'],
  ['请选择', 'placeholder.select'],
  ['请上传', 'placeholder.upload']
])

const navKeyOverrides = new Map([
  ['运行总览', 'nav.home'],
  ['实时展示', 'nav.liveDisplay'],
  ['图片分析', 'nav.imageAnalysis'],
  ['视频接入', 'nav.videoAccess'],
  ['场景任务', 'nav.sceneTasks'],
  ['事件中心', 'nav.eventCenter'],
  ['检测/分析', 'nav.detectionAnalysis'],
  ['人脸人体', 'nav.faceBody'],
  ['计数统计', 'nav.countingStats'],
  ['模型仓库', 'nav.modelRepository'],
  ['底库管理', 'nav.baseLibrary'],
  ['人脸底库', 'nav.faceLibrary'],
  ['工服底库', 'nav.uniformLibrary'],
  ['物品底库', 'nav.itemLibrary'],
  ['数据对接', 'nav.dataDocking'],
  ['音频管理', 'nav.audioManagement'],
  ['外设管理', 'nav.peripheralManagement'],
  ['网络音柱', 'nav.networkSpeaker'],
  ['联动管理', 'nav.linkageManagement'],
  ['系统管理', 'nav.systemManagement'],
  ['系统设置', 'nav.systemSettings'],
  ['网络设置', 'nav.networkSettings'],
  ['系统维护', 'nav.systemMaintenance'],
  ['个性化设置', 'nav.personalization']
])

const toCamel = (value) => value
  .replace(/[^A-Za-z0-9]+(.)/g, (_, char) => char.toUpperCase())
  .replace(/^[A-Z]/, char => char.toLowerCase())

const inferKey = (term, english) => {
  if (keyOverrides.has(term)) return keyOverrides.get(term)
  return `glossary.${toCamel(english.replace(/\s*\.\s*$/, ''))}`
}

const parseGlossaryShorts = () => {
  const content = fs.readFileSync(glossaryPath, 'utf8')
  const rows = []
  let activeGlossaryTable = false
  let inMenuSection = false
  for (const line of content.split(/\r?\n/)) {
    if (line.startsWith('## ')) {
      inMenuSection = line.startsWith('## 11.')
      activeGlossaryTable = false
    }
    if (!line.startsWith('|')) {
      activeGlossaryTable = false
      continue
    }
    const cells = line.split('|').slice(1, -1).map(cell => cell.trim())
    const isGlossaryHeader = cells[0] === '中文' && cells[1] === 'EN' && cells[2] === 'Short' && cells[3] === 'Short scope'
    if (isGlossaryHeader) {
      activeGlossaryTable = true
      continue
    }
    if (!activeGlossaryTable || line.includes('---')) continue
    if (cells.length < 4) continue
    const [term, english, short, scopeCell] = cells
    if (!short || short === '-' || !scopeCell || scopeCell === '-') continue
    const scopes = scopeCell
      .replace(/`/g, '')
      .split(',')
      .map(scope => scope.trim())
      .filter(Boolean)
      .sort()
    const key = inMenuSection ? navKeyOverrides.get(term) : inferKey(term, english)
    rows.push({ term, key, scopes })
  }
  return rows
}

const { shortGlossary } = await import(pathToFileURL(glossaryModulePath).href)
const glossaryRows = parseGlossaryShorts()
const errors = []

for (const row of glossaryRows) {
  const entry = shortGlossary[row.key]
  if (!entry) {
    errors.push(`${row.term}: missing ${row.key}`)
    continue
  }
  const actualScopes = [...(entry.scopes || [])].sort()
  if (actualScopes.join(',') !== row.scopes.join(',')) {
    errors.push(`${row.term}: ${row.key} scopes ${actualScopes.join(',') || '-'} != ${row.scopes.join(',')}`)
  }
}

const glossaryKeys = new Set(glossaryRows.map(row => row.key))
for (const [key, entry] of Object.entries(shortGlossary)) {
  if (!glossaryKeys.has(key)) {
    errors.push(`${key}: declared in glossary.js but missing in GLOSSARY.md`)
  }
  if (entry.fullKey !== key) {
    errors.push(`${key}: fullKey ${entry.fullKey} must match key`)
  }
}

if (errors.length) {
  console.error('GLOSSARY.md and src/i18n/glossary.js are out of sync:')
  errors.forEach(error => console.error(`- ${error}`))
  process.exit(1)
}

console.log(`I18N glossary sync OK (${glossaryRows.length} short terms).`)
