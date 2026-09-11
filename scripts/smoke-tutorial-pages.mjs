import { existsSync, readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

import { tutorialPages, tutorialContext, pageHtml, pageRoute, displayText } from './tutorial-pages.mjs'

const repositoryRoot = resolve(process.argv[3] ?? resolve(dirname(fileURLToPath(import.meta.url)), '..'))
const context = await tutorialContext(repositoryRoot)
const distRoot = resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist')

const failures = []

const decodeHtml = displayText

for (const source of tutorialPages) {
  let parsed
  try { parsed = context.read(source) } catch (error) { failures.push(error.message); continue }
  const page = { html: pageHtml(source), title: parsed.title, lang: source.language === 'zh' ? 'zh-CN' : 'en-US' }
  const file = join(distRoot, page.html)
  if (!existsSync(file)) {
    failures.push(`${page.html}: rendered page is missing`)
    continue
  }

  const html = readFileSync(file, 'utf8')
  if (!new RegExp(`<html[^>]+lang="${page.lang}"`, 'u').test(html)) {
    failures.push(`${page.html}: expected html lang="${page.lang}"`)
  }

  const title = html.match(/<title>([\s\S]*?)<\/title>/u)
  if (!title || !(decodeHtml(title[1]) === page.title || decodeHtml(title[1]).startsWith(`${page.title} | `))) {
    failures.push(`${page.html}: document title does not start with "${page.title}"`)
  }

  const route = pageRoute(source)
  const expectedHref = `${context.config.base ?? '/'}${route.slice(1)}`
  const anchors = [...html.matchAll(/<a\b[^>]*\bhref="([^"]+)"[^>]*>([\s\S]*?)<\/a>/gu)]
  if (!anchors.some((anchor) => decodeHtml(anchor[1]) === expectedHref && decodeHtml(anchor[2]))) {
    failures.push(`${page.html}: rendered navigation to ${route} is missing`)
  }

  const doc = html.match(
    /<div class="content-container"[^>]*>([\s\S]*?)<footer class="VPDocFooter/u
  )
  if (!doc) {
    failures.push(`${page.html}: VitePress document body was not found`)
    continue
  }

  const headings = [...doc[1].matchAll(/<h([1-6])\b[^>]*>([\s\S]*?)<\/h\1>/gu)]
  const h1 = headings.filter((heading) => heading[1] === '1')
  if (h1.length !== 1) {
    failures.push(`${page.html}: expected one rendered H1, found ${h1.length}`)
  } else if (decodeHtml(h1[0][2]) !== page.title) {
    failures.push(`${page.html}: rendered H1 is not "${page.title}"`)
  }

  if (headings.length === 0 || headings[0][1] !== '1') {
    failures.push(`${page.html}: first rendered heading is not H1; frontmatter may have leaked into content`)
  }
  if (
    headings.some(
      (heading) =>
        heading[1] === '2' && /^(?:title|description|prev|next)\s*:/iu.test(decodeHtml(heading[2]))
    )
  ) {
    failures.push(`${page.html}: frontmatter keys leaked into rendered headings`)
  }

  for (const match of doc[1].matchAll(/<(a|img)\b[^>]*?\b(href|src)="([^"]+)"[^>]*>/gu)) {
    const target = decodeHtml(match[3]).split(/[?#]/u)[0]
    if (!target || /^(?:[a-z]+:|\/\/)/iu.test(target)) continue
    const relative = target.startsWith('/') ? target.replace(context.config.base ?? '/', '').replace(/^\//u, '') : null
    const local = relative === null ? resolve(dirname(file), target) : resolve(distRoot, relative)
    const candidates = [local, `${local}.html`, join(local, 'index.html')]
    if (!candidates.some(existsSync)) failures.push(`${page.html}: rendered link target does not exist: ${target}`)
    if (match[1] === 'img' && !/\balt="[^"\s][^"]*"/u.test(match[0])) failures.push(`${page.html}: rendered image alt is empty`)
  }

}

if (failures.length > 0) {
  console.error(`Tutorial page smoke test failed with ${failures.length} issue(s):`)
  for (const failure of failures) console.error(`- ${failure}`)
  process.exit(1)
}

console.log(`Tutorial page smoke test passed: ${tutorialPages.length} rendered bilingual pages.`)
