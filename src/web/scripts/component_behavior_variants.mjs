import assert from 'node:assert/strict'
import { cp, mkdtemp, readFile, writeFile, rm } from 'node:fs/promises'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { parse, compileScript } from 'vue/compiler-sfc'

const web = fileURLToPath(new URL('../', import.meta.url))
const baseline = process.env.WEB_BEHAVIOR_SOURCE_ROOT || path.join(web, 'src')
const parameter = 'views/gam/countManagement/arrangeDetail/flow/ParameterSetting.vue'
const walk = (node, visit) => {
  if (!node || typeof node !== 'object') return
  if (Array.isArray(node)) return node.forEach((child) => walk(child, visit))
  visit(node)
  for (const [key, child] of Object.entries(node)) {
    if (!['loc', 'parent'].includes(key)) walk(child, visit)
  }
}
const editSource = (source, edits) => {
  assert.ok(edits.length, 'variant must actually apply')
  return edits.sort((a, b) => b.start - a.start).reduce((text, edit) =>
    text.slice(0, edit.start) + edit.text + text.slice(edit.end), source)
}
const binding = (node, name) => node.props?.find((p) => p.type === 7 && p.name === 'bind' && p.arg?.content === name)
const templateEdits = (source, visitor) => {
  const { descriptor, errors } = parse(source)
  assert.equal(errors.length, 0)
  const edits = []
  walk(descriptor.template.ast, (node) => visitor(node, edits))
  return editSource(source, edits)
}
const replaceLoc = (loc, text) => ({ start: loc.start.offset, end: loc.end.offset, text })

// Locate the stable public exposed method through the compiler AST. Neither
// its local name nor shorthand-vs-explicit object syntax is part of the test.
const rewriteSave = (source, hidden) => {
  const { descriptor } = parse(source)
  const compiled = compileScript(descriptor, { id: 'f01-variant' })
  const offset = descriptor.scriptSetup.loc.start.offset
  const edits = []
  walk(compiled.scriptSetupAst, (node) => {
    if (node.type !== 'CallExpression' || node.callee?.name !== 'defineExpose') return
    const property = node.arguments[0]?.properties?.find((p) => (p.key?.name || p.key?.value) === 'saveParamConfig')
    if (!property) return
    const original = source.slice(offset + property.value.start, offset + property.value.end)
    const result = hidden
      ? `(...args) => (${original})(...args).map(param => ({...param, senior: 2, channelEditable: false}))`
      : `(...args) => (${original})(...args)`
    edits.push({ start: offset + property.start, end: offset + property.end, text: `saveParamConfig: ${result}` })
  })
  return editSource(source, edits)
}
const mutateRequest = (source) => {
  const { descriptor } = parse(source)
  const compiled = compileScript(descriptor, { id: 'f01-variant' })
  const offset = descriptor.scriptSetup.loc.start.offset
  const edits = []
  walk(compiled.scriptSetupAst, (node) => {
    if (node.type !== 'CallExpression') return
    const name = node.callee?.property?.name || node.callee?.property?.value
    if (name !== 'selectAllAlgorithmInfo') return
    const arg = node.arguments[0]
    const original = source.slice(offset + arg.start, offset + arg.end)
    edits.push({start: offset + arg.start, end: offset + arg.end, text: `({...(${original}), channelId: 'wrong-channel'})`})
  })
  return editSource(source, edits)
}
const variants = [
  ['checkbox attribute order', parameter, (source) => templateEdits(source, (node, edits) => {
    if (node.tag !== 'el-checkbox') return
    const yes = binding(node, 'true-value'), no = binding(node, 'false-value')
    if (yes && no) edits.push(replaceLoc(yes.loc, no.loc.source), replaceLoc(no.loc, yes.loc.source))
  }), true],
  ['exposed save helper rewrite', parameter, (source) => rewriteSave(source, false), true],
  ['reversed ownership', parameter, (source) => templateEdits(source, (node, edits) => {
    if (node.tag !== 'el-checkbox') return
    const yes = binding(node, 'true-value'), no = binding(node, 'false-value')
    if (yes && no) edits.push(replaceLoc(yes.exp.loc, no.exp.content), replaceLoc(no.exp.loc, yes.exp.content))
  }), false],
  ['saved defaults hidden', parameter, (source) => rewriteSave(source, true), false],
  ['dependency disabled regression', 'views/gam/taskManager/editTask/dynamicForm.vue', (source) => templateEdits(source, (node, edits) => {
    const disabled = node.tag === 'el-form' && binding(node, 'disabled')
    if (disabled) edits.push(replaceLoc(disabled.exp.loc, `!(${disabled.exp.content})`))
  }), false],
  ['dependency visibility regression', 'views/gam/taskManager/editTask/dynamicForm.vue', (source) => templateEdits(source, (node, edits) => {
    if (node.tag !== 'el-form' || !binding(node, 'disabled')) return
    const condition = node.props.find((p) => p.type === 7 && p.name === 'if')
    if (condition) edits.push(replaceLoc(condition.exp.loc, 'false'))
  }), false],
  ['device capability leak', 'views/box/systemManagement/systemConfig/components/DeviceInfo.vue', (source) => {
    const { descriptor } = parse(source)
    return editSource(source, [{start:descriptor.template.loc.end.offset,end:descriptor.template.loc.end.offset,text:'<span>hidden-capability</span>'}])
  }, false],
  ['route parameter lost', 'views/gam/taskManager/editTask/serviceConfig.vue', mutateRequest, false]
]

for (const [name, file, mutate, pass] of variants) {
  const root = await mkdtemp(path.join(tmpdir(), 'cosmo-component-'))
  try {
    await cp(baseline, root, {recursive:true})
    const target = path.join(root, file)
    const source = await readFile(target, 'utf8')
    const changed = mutate(source)
    assert.notEqual(changed, source, `variant did not apply: ${name}`)
    await writeFile(target, changed)
    const env = {...process.env, WEB_BEHAVIOR_SOURCE_ROOT:root}
    const result = spawnSync(process.execPath, ['--experimental-vm-modules', path.join(web, 'scripts/component_behavior_check.mjs')], {encoding:'utf8', env, timeout:30000})
    assert.equal(result.status === 0, pass, `${name}\n${result.stdout}\n${result.stderr}`)
    if (!pass) assert.match(result.stderr, /AssertionError/, `variant failed for an unrelated reason: ${name}`)
    // An equivalent source must pass the whole gate, including generation of
    // every other variant. Bound recursion to one level.
    if (pass && !process.env.F01_VARIANT_NESTED) {
      const nested = spawnSync(process.execPath, [fileURLToPath(import.meta.url)], {encoding:'utf8', env:{...env,F01_VARIANT_NESTED:'1'}, timeout:90000})
      assert.equal(nested.status, 0, `equivalent source broke variant generation: ${name}\n${nested.stdout}\n${nested.stderr}`)
    }
    console.log(`PASS: ${name} ${pass ? 'accepted by complete gate' : 'rejected'}`)
  } finally { await rm(root, {recursive:true,force:true}) }
}
