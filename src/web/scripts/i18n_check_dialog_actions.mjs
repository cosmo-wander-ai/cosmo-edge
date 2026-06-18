import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const srcDir = path.resolve(__dirname, '..', 'src')

const readFiles = (dir) => {
  const entries = fs.readdirSync(dir, { withFileTypes: true })
  return entries.flatMap((entry) => {
    const fullPath = path.join(dir, entry.name)
    if (entry.isDirectory()) return readFiles(fullPath)
    return /\.(vue|js)$/.test(entry.name) ? [fullPath] : []
  })
}

const offenders = []

// TODO(i18n): extend Phase 2 coverage to ElMessageBox.confirm,
// el-popconfirm, and dialog-like warning/error components.
for (const file of readFiles(srcDir)) {
  const content = fs.readFileSync(file, 'utf8')
  const confirmBlocks = content.match(/\$confirm\([\s\S]*?\}\)/g) || []
  for (const block of confirmBlocks) {
    if (/type:\s*['"](warning|error)['"]/.test(block) && /confirmButtonText:\s*t\(['"]action\.ok['"]\)/.test(block)) {
      offenders.push(path.relative(process.cwd(), file))
    }
  }
}

if (offenders.length) {
  console.error('warning/error confirms must not use action.ok:')
  ;[...new Set(offenders)].forEach(file => console.error(`- ${file}`))
  process.exit(1)
}

console.log('I18N dialog action keys OK.')
