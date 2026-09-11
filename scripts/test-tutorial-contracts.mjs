import assert from 'node:assert/strict'
import { cpSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { createMarkdownRenderer } from 'vitepress'
import { tutorialContext, tutorialPages, pageRoute, pageHtml } from './tutorial-pages.mjs'

const toolRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const root = resolve(process.argv[2] ?? toolRoot)
const fixture = mkdtempSync(join(tmpdir(), 'cosmo-tutorial-contracts-'))
function run(script, args, expected, reason) {
  const result = spawnSync(process.execPath, [join(toolRoot, 'scripts', script), ...args], { encoding: 'utf8' })
  const output = result.stdout + result.stderr
  assert.equal(result.status === 0, expected, `${script}: ${output}`)
  if (reason) assert.match(output, reason)
}
function mutate(path, from, to) {
  const original = readFileSync(path, 'utf8')
  assert.ok(original.includes(from), `mutation was not applied: ${from}`)
  const changed = original.replace(from, to)
  assert.notEqual(changed, original, 'mutation must change the fixture')
  writeFileSync(path, changed)
  return () => writeFileSync(path, original)
}
try {
  cpSync(join(root, 'docs'), join(fixture, 'docs'), { recursive: true, filter: (path) => !/[/\\]\.vitepress[/\\](dist|cache)([/\\]|$)/u.test(path) })
  for (const file of ['README.md', 'README.zh-CN.md']) cpSync(join(root, file), join(fixture, file))
  symlinkSync(join(root, 'node_modules'), join(fixture, 'node_modules'), 'dir')
  writeFileSync(join(fixture, 'package.json'), '{"type":"module"}')
  const selected = tutorialPages.find(page => page.language === 'zh' && page.type === 'tutorial')
  const page = join(fixture, selected.path)
  const config = join(fixture, 'docs/.vitepress/config.mts')
  const original = readFileSync(page, 'utf8')
  const context = await tutorialContext(fixture)
  const parsed = context.read(selected)
  const markdown = await createMarkdownRenderer(join(fixture, 'docs'))
  const heading = markdown.parse(parsed.content, {}).find(token => token.type === 'heading_open' && token.tag === 'h1')
  assert.ok(heading?.map, 'source H1 must have a Markdown location')
  const lines = parsed.content.split('\n')
  const inlineTitle = parsed.title.replace(/[\\`*_[\]<>]/gu, '\\$&')
  function pageVariant(title, headingText) {
    const frontmatter = { ...parsed.frontmatter, title }
    const yaml = JSON.stringify(frontmatter)
    return `---\n${yaml}\n---\n${[...lines.slice(0, heading.map[0]), headingText, ...lines.slice(heading.map[1])].join('\n')}`
  }
  const configOriginal = readFileSync(config, 'utf8')
  function navigationVariant(change) {
    const value = structuredClone({ locales: context.config.locales, base: context.config.base })
    let changed = 0
    function visit(node) {
      if (!node || typeof node !== 'object') return
      if (node.link === pageRoute(selected)) { change(node); changed++ }
      for (const child of Object.values(node)) visit(child)
    }
    visit(value)
    assert.ok(changed > 0, 'navigation mutation must apply to the selected route')
    writeFileSync(config, `export default ${JSON.stringify(value)}\n`)
    return () => writeFileSync(config, configOriginal)
  }
  run('check-tutorial-docs.mjs', [fixture], true)
  let restore = navigationVariant(node => { node.text = node.text === 'Intro' ? 'Guide' : 'Intro' })
  run('check-tutorial-docs.mjs', [fixture], true)
  restore()
  // JSON frontmatter is equivalent YAML and handles arbitrary title quoting.
  writeFileSync(page, pageVariant(parsed.title, `# ${inlineTitle}`))
  run('check-tutorial-docs.mjs', [fixture], true)
  writeFileSync(page, pageVariant(parsed.title, `**${inlineTitle}**\n================================`))
  run('check-tutorial-docs.mjs', [fixture], true)
  writeFileSync(page, original)
  const preview = join(fixture, 'docs/en/guide/macos-docker-preview.md')
  const repeated = readFileSync(preview, 'utf8').match(/two\s+consecutive/iu)?.[0]
  assert.ok(repeated, 'Preview repeated acceptance text must exist')
  restore = mutate(preview, repeated, repeated === 'two\nconsecutive' ? 'two consecutive' : 'two\nconsecutive')
  run('check-tutorial-docs.mjs', [fixture], true)
  restore()
  const previewOriginal = readFileSync(preview, 'utf8')
  const missingGuard = previewOriginal.replaceAll('Model Guard', 'Protection')
  assert.notEqual(missingGuard, previewOriginal)
  writeFileSync(preview, missingGuard)
  run('check-tutorial-docs.mjs', [fixture], false, /missing Preview compatibility boundary: Model Guard/u)
  writeFileSync(preview, previewOriginal)
  const readme = join(fixture, 'README.md')
  const readmeOriginal = readFileSync(readme, 'utf8')
  const missingLauncher = readmeOriginal.replaceAll('scripts/macos-docker-preview.sh', 'scripts/not-the-launcher.sh')
  assert.notEqual(missingLauncher, readmeOriginal)
  writeFileSync(readme, missingLauncher)
  run('check-tutorial-docs.mjs', [fixture], false, /missing macOS Preview launcher reference/u)
  writeFileSync(readme, readmeOriginal)
  const changedTitle = `${parsed.title} — Contract variant`
  const renamed = pageVariant(changedTitle, `# ${inlineTitle} — Contract variant`)
  assert.notEqual(renamed, original)
  writeFileSync(page, renamed)
  run('check-tutorial-docs.mjs', [fixture], true)
  run('smoke-tutorial-pages.mjs', [join(root, 'docs/.vitepress/dist'), fixture], false, /rendered H1|document title/u)
  const build = spawnSync(process.execPath, [join(root, 'node_modules/vitepress/bin/vitepress.js'), 'build', join(fixture, 'docs')], { encoding: 'utf8' })
  assert.equal(build.status, 0, build.stdout + build.stderr)
  // These static reports are produced by the normal docs:build postprocessors.
  cpSync(join(root, 'docs/.vitepress/dist/benchmarks'), join(fixture, 'docs/.vitepress/dist/benchmarks'), { recursive: true })
  run('smoke-tutorial-pages.mjs', [join(fixture, 'docs/.vitepress/dist'), fixture], true)
  writeFileSync(page, original)
  writeFileSync(page, pageVariant(parsed.title, '# Incorrect contract heading'))
  run('check-tutorial-docs.mjs', [fixture], false, /H1 must match/u)
  writeFileSync(page, pageVariant('', `# ${inlineTitle}`))
  run('check-tutorial-docs.mjs', [fixture], false, /nonempty string/u)
  writeFileSync(page, original)
  restore = navigationVariant(node => { node.link = '/missing-page' })
  run('check-tutorial-docs.mjs', [fixture], false, /missing nonempty zh navigation/u)
  restore()
  writeFileSync(page, original + '\n[Broken link](./missing-contract-target)\n')
  run('check-tutorial-docs.mjs', [fixture], false, /link target does not exist/u)
  writeFileSync(page, renamed)
  const htmlPath = join(fixture, 'docs/.vitepress/dist', pageHtml(selected))
  restore = mutate(htmlPath, '</h1>', '</h1><a href="./missing-contract-target">Broken</a>')
  run('smoke-tutorial-pages.mjs', [join(fixture, 'docs/.vitepress/dist'), fixture], false, /rendered link target does not exist/u)
  restore()
  rmSync(htmlPath)
  run('smoke-tutorial-pages.mjs', [join(fixture, 'docs/.vitepress/dist'), fixture], false, /rendered page is missing/u)
  rmSync(page)
  run('check-tutorial-docs.mjs', [fixture], false, /required page is missing/u)
  console.log('Tutorial contracts passed: navigation abbreviation, synchronized title rename and real rebuilt output; rejected stale output, mismatched/empty titles, missing navigation/pages and broken source/rendered links.')
} finally {
  rmSync(fixture, { recursive: true, force: true })
}
