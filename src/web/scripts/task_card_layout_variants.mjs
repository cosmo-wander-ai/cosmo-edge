import assert from 'node:assert/strict'
import { cp, mkdtemp, readFile, writeFile, rm, readdir } from 'node:fs/promises'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
const dist = fileURLToPath(new URL('../node_modules/.cache/f01-layout/dist/', import.meta.url))
for (const [name, suffix, expected] of [
  ['CSS custom property extraction', null, true],
  ['hidden pagination', '.pagination-container{display:none!important}', false],
  ['covered card actions', '.card-actions:after{content:"";position:fixed;inset:0;z-index:999999;background:white}', false],
  ['unreachable last card', '.task-grid{overflow:clip!important}', false]
]) {
  const root = await mkdtemp(path.join(tmpdir(), 'cosmo-layout-variant-'))
  try {
    await cp(dist, root, {recursive:true})
    const files = (await readdir(path.join(root, 'assets'))).filter((file) => file.endsWith('.css'))
    const styles = await Promise.all(files.map(async (name) => ({
      file: path.join(root, 'assets', name),
      source: await readFile(path.join(root, 'assets', name), 'utf8')
    })))
    const taskStyle = styles.find(({ source }) => /\.task-card(?:\[[^\]]+\])?\{[^}]*?min-height:/.test(source))
    assert.ok(taskStyle, 'CSS extraction variant needs an actual task-card declaration')
    const { file, source: before } = taskStyle
    // Mutate the actual CSS artifact. A custom property is layout-equivalent;
    // the negative cases independently violate user-visible geometry.
    const height = before.match(/\.task-card(?:\[[^\]]+\])?\{[^}]*?min-height:([^;}]+)/)
    if (!suffix) assert.ok(height, 'CSS extraction variant did not apply')
    const after = suffix ? before + suffix : `:root{--fixture-min-height:${height[1]}}` + before.replace(height[0], height[0].replace(/min-height:[^;}]*/, 'min-height:var(--fixture-min-height)'))
    assert.notEqual(after, before)
    await writeFile(file, after)
    const result = spawnSync(process.execPath, [fileURLToPath(new URL('./task_card_layout_check.mjs', import.meta.url))], {encoding:'utf8', timeout:60000, env:{...process.env, LAYOUT_FIXTURE_DIST:root}})
    assert.equal(result.status === 0, expected, `${name}\n${result.stdout}\n${result.stderr}`)
    if (!expected) assert.match(result.stderr, /AssertionError/, 'negative must fail its geometry contract')
    console.log(`PASS: ${name} ${expected ? 'accepted' : 'rejected'}`)
  } finally { await rm(root, {recursive:true,force:true}) }
}
