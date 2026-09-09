import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import vm from 'node:vm'

// Load production ESM without rewriting its source or exposing test-only APIs.
// The override lets the same checks exercise a saved parent source tree.
export const sourceRoot = process.env.WEB_BEHAVIOR_SOURCE_ROOT ||
  fileURLToPath(new URL('../../src/', import.meta.url))

export const loadBehaviorModule = async (entry, { mocks = {}, globals = {}, env = {} } = {}) => {
  const context = vm.createContext({ console, ...globals })
  const modules = new Map()
  const load = async (specifier, parent = path.join(sourceRoot, 'entry.js')) => {
    const mocked = Object.hasOwn(mocks, specifier)
    let id = mocked ? specifier : specifier.startsWith('@/')
      ? path.join(sourceRoot, specifier.slice(2))
      : path.resolve(path.dirname(parent), specifier)
    if (!mocked && !path.extname(id)) id += '.js'
    if (modules.has(id)) return modules.get(id)
    const module = mocked
      ? new vm.SyntheticModule(Object.keys(mocks[id]), function () {
        for (const [name, value] of Object.entries(mocks[id])) this.setExport(name, value)
      }, { context, identifier: id })
      : new vm.SourceTextModule(await readFile(id, 'utf8'), {
        context,
        identifier: id,
        initializeImportMeta(meta) { meta.env = env }
      })
    modules.set(id, module)
    await module.link((dependency, importer) => load(dependency, importer.identifier))
    return module
  }
  const module = await load(`./${entry}`)
  await module.evaluate()
  return module.namespace
}
