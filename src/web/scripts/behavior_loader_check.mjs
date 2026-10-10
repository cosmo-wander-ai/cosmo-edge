import assert from 'node:assert/strict'
import path from 'node:path'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'

const deferred = () => {
  let resolve
  const promise = new Promise(done => { resolve = done })
  return { promise, resolve }
}

const diamond = async () => {
  const sharedSource = deferred()
  const reads = new Map()
  const sources = {
    'entry.js': `import { left } from './left.js'; import { right } from './right.js'; export { left, right }`,
    'left.js': `import { key } from './shared.js'; export const left = key`,
    'right.js': `import { key } from './shared.js'; import './release.js'; export const right = key`,
    'shared.js': `export const key = Symbol('flowEditor')`,
    'release.js': `export {}`
  }
  const namespace = await loadBehaviorModule('__loader_fixture__/entry.js', {
    readSource: async id => {
      const name = path.basename(id)
      assert.ok(Object.hasOwn(sources, name), `unexpected fixture import: ${name}`)
      reads.set(name, (reads.get(name) || 0) + 1)
      // Both branches request shared.js before release.js lets its source
      // resolve. This exposes duplicate construction without sleeps or retries.
      if (name === 'shared.js') await sharedSource.promise
      if (name === 'release.js') sharedSource.resolve()
      return sources[name]
    }
  })
  assert.equal(reads.get('shared.js'), 1, 'diamond imports read and construct the shared module once')
  assert.equal(namespace.left, namespace.right, 'provider and consumer receive the same exported Symbol')
  assert.equal(reads.size, Object.keys(sources).length, 'the complete diamond graph was linked')
  return namespace.left
}

// Caches belong to a single graph. Concurrent mounts must not share module
// instances, while imports within either mount must retain singleton identity.
const keys = await Promise.all([diamond(), diamond()])
assert.notEqual(keys[0], keys[1], 'separate loader calls keep isolated module graphs')

const cycleSources = {
  'entry.js': `import { key, readThroughCycle } from './a.js'; export const same = key === readThroughCycle()`,
  'a.js': `import { readKey } from './b.js'; export const key = Symbol('cycle'); export function readThroughCycle() { return readKey() }`,
  'b.js': `import { key } from './a.js'; export function readKey() { return key }`
}
const cycle = await loadBehaviorModule('__loader_cycle__/entry.js', {
  readSource: async id => {
    const name = path.basename(id)
    assert.ok(Object.hasOwn(cycleSources, name), `unexpected cycle import: ${name}`)
    return cycleSources[name]
  }
})
assert.equal(cycle.same, true, 'the VM links cycles without independent in-flight link operations')

console.log('Behavior module loader checks passed')
