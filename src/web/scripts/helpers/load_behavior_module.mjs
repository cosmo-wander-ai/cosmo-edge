import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import vm from 'node:vm'
import { parse, compileScript, compileTemplate, rewriteDefault } from 'vue/compiler-sfc'

// Load production ESM without rewriting its source or exposing test-only APIs.
// The override lets the same checks exercise a saved parent source tree.
export const sourceRoot = process.env.WEB_BEHAVIOR_SOURCE_ROOT ||
  fileURLToPath(new URL('../../src/', import.meta.url))

export const loadBehaviorModule = async (entry, { mocks = {}, globals = {}, env = {}, readSource = id => readFile(id, 'utf8') } = {}) => {
  const context = vm.createContext({ console, ...globals })
  const modules = new Map()
  const moduleSource = async (id) => {
    const source = await readSource(id)
    if (!id.endsWith('.vue')) return source
    const { descriptor, errors } = parse(source, { filename: id })
    if (errors.length) throw errors[0]
    const script = compileScript(descriptor, { id, inlineTemplate: true })
    if (descriptor.scriptSetup) return script.content
    const template = compileTemplate({ source: descriptor.template.content, filename: id, id })
    if (template.errors.length) throw template.errors[0]
    return `${rewriteDefault(script.content, '__component')}\n${template.code}\n__component.render = render; export default __component`
  }
  const load = async (specifier, parent = path.join(sourceRoot, 'entry.js')) => {
    const mocked = Object.hasOwn(mocks, specifier)
    let id = mocked ? specifier : specifier.startsWith('@/')
      ? path.join(sourceRoot, specifier.slice(2))
      : path.resolve(path.dirname(parent), specifier)
    if (!mocked && !path.extname(id)) id += '.js'
    if (modules.has(id)) return modules.get(id)
    // Cache construction before any asynchronous source read. Diamond imports
    // must share one module (and therefore one exported Symbol/provide key).
    const pending = Promise.resolve().then(async () => mocked
      ? new vm.SyntheticModule(Object.keys(mocks[id]), function () {
        for (const [name, value] of Object.entries(mocks[id])) this.setExport(name, value)
      }, { context, identifier: id })
      : new vm.SourceTextModule(await moduleSource(id), {
        context,
        identifier: id,
        initializeImportMeta(meta) { meta.env = env; meta.url = pathToFileURL(id).href }
      }))
    modules.set(id, pending)
    return pending
  }
  const module = await load(`./${entry}`)
  // The VM recursively links the graph, including cycles. Independently linking
  // dependencies can expose modules whose own link operation is still pending.
  await module.link((dependency, importer) => load(dependency, importer.identifier))
  await module.evaluate()
  return module.namespace
}
