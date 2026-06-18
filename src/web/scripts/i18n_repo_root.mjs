import fs from 'node:fs'
import path from 'node:path'

const requiredFiles = [
  path.join('docs', 'i18n', 'GLOSSARY.md'),
  path.join('docs', 'i18n', 'SHORT-SCOPES.md')
]

const hasI18nDocs = (dir) =>
  requiredFiles.every((file) => fs.existsSync(path.join(dir, file)))

export const findI18nRepoRoot = (startDir) => {
  const envRoot = process.env.COSMO_REPO_ROOT || process.env.WORKSPACE_ROOT
  if (envRoot) {
    const resolved = path.resolve(envRoot)
    if (hasI18nDocs(resolved)) return resolved
    throw new Error(`I18N docs not found under ${resolved}`)
  }

  let current = path.resolve(startDir)
  while (true) {
    if (hasI18nDocs(current)) return current
    const parent = path.dirname(current)
    if (parent === current) break
    current = parent
  }

  throw new Error(
    `Unable to locate i18n docs from ${startDir}. ` +
      'Set COSMO_REPO_ROOT to the repository root.'
  )
}
