import { existsSync, readFileSync } from 'node:fs'
import { dirname, extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

import { tutorialPages, tutorialContext, pageRoute, navigationLinks, displayText } from './tutorial-pages.mjs'

const repositoryRoot = resolve(process.argv[2] ?? resolve(dirname(fileURLToPath(import.meta.url)), '..'))
const context = await tutorialContext(repositoryRoot)
const docsRoot = join(repositoryRoot, 'docs')

const placeholders = [
  { pattern: /待填写/u, label: '待填写' },
  { pattern: /待补充/u, label: '待补充' },
  { pattern: /待完善/u, label: '待完善' },
  { pattern: /\bTODO\b/u, label: 'TODO' },
  { pattern: /\bTBD\b/u, label: 'TBD' },
  { pattern: /\bComing soon\b/iu, label: 'Coming soon' },
  { pattern: /\bDraft for review\b/iu, label: 'Draft for review' }
]

const editorialResidues = [
  { pattern: /卷[一二三四五]/u, label: 'volume-style Chinese title' },
  { pattern: /\bVolume\s+[1-5]\b/iu, label: 'volume-style English title' },
  { pattern: /事间中心/u, label: 'misspelling 事间中心' }
]

const requiredIntro = {
  zh: ['适合谁', '完成后能做什么', '使用前提', '预计时间', '是否需要设备', '最终验收结果'],
  en: [
    'Who this is for',
    'What you will accomplish',
    'Prerequisites',
    'Estimated time',
    'Device required',
    'Final acceptance result'
  ]
}

const failures = []

function fail(file, message) {
  failures.push(`${file}: ${message}`)
}

function removeFrontmatter(markdown) {
  const match = markdown.match(/^---\r?\n[\s\S]*?\r?\n---(?:\r?\n|$)/u)
  return match ? markdown.slice(match[0].length) : markdown
}

function stripNonProse(markdown) {
  return removeFrontmatter(markdown)
    .replace(/```[\s\S]*?```/gu, '')
    .replace(/~~~[\s\S]*?~~~/gu, '')
    .replace(/<!--[\s\S]*?-->/gu, '')
}

function linkTarget(rawTarget) {
  let target = rawTarget.trim()
  if (target.startsWith('<') && target.endsWith('>')) {
    target = target.slice(1, -1)
  } else {
    const optionalTitle = target.match(/^(\S+)(?:\s+["'(].*)$/u)
    if (optionalTitle) target = optionalTitle[1]
  }
  return decodeURIComponent(target.split('#', 1)[0].split('?', 1)[0])
}

function resolveLocalTarget(sourcePath, rawTarget, isImage) {
  const target = linkTarget(rawTarget)
  if (!target || target.startsWith('#')) return null
  if (/^(?:https?:|mailto:|tel:|data:)/iu.test(target)) return null

  let absolute
  if (target.startsWith('/')) {
    const sitePath = target.replace(/^\/cosmo-edge\/?/u, '/').replace(/^\//u, '')
    absolute = join(docsRoot, sitePath)
  } else {
    absolute = resolve(dirname(join(repositoryRoot, sourcePath)), target)
  }

  if (isImage || extname(absolute)) return [normalize(absolute)]
  return [normalize(absolute), normalize(`${absolute}.md`), normalize(join(absolute, 'index.md'))]
}

function checkLinksAndImages(file, markdown) {
  const imagePattern = /!\[([^\]]*)\]\(([^)]+)\)/gu
  for (const match of markdown.matchAll(imagePattern)) {
    const alt = match[1].trim()
    if (!alt) fail(file, `image ${match[2]} has an empty alt description`)

    const candidates = resolveLocalTarget(file, match[2], true)
    if (candidates && !candidates.some((candidate) => existsSync(candidate))) {
      fail(file, `image target does not exist: ${match[2]}`)
    }
  }

  const withoutImages = markdown.replace(imagePattern, '')
  const linkPattern = /(?<!!)\[[^\]]+\]\(([^)]+)\)/gu
  for (const match of withoutImages.matchAll(linkPattern)) {
    const candidates = resolveLocalTarget(file, match[1], false)
    if (candidates && !candidates.some((candidate) => existsSync(candidate))) {
      fail(file, `link target does not exist: ${match[1]}`)
    }
  }
}

function checkPage(page) {
  const { path: file, language, type } = page
  const absolute = join(repositoryRoot, file)
  if (!existsSync(absolute)) {
    fail(file, 'required page is missing')
    return
  }

  let parsed
  try { parsed = context.read(page) } catch (error) { fail(file, error.message); return }
  const { markdown, title, html } = parsed
  const prose = stripNonProse(markdown)
  const h1 = [...html.matchAll(/<h1\b[^>]*>([\s\S]*?)<\/h1>/gu)]
  if (h1.length !== 1) {
    fail(file, `expected exactly one H1 outside code blocks, found ${h1.length}`)
  } else if (displayText(h1[0][1]) !== title) {
    fail(file, `H1 must match frontmatter title "${title}"`)
  }

  for (const placeholder of placeholders) {
    if (placeholder.pattern.test(prose)) fail(file, `contains forbidden placeholder "${placeholder.label}"`)
  }
  for (const residue of editorialResidues) {
    if (residue.pattern.test(prose)) fail(file, `contains ${residue.label}`)
  }

  if (/<!--[\s\S]*?(?:OCR|ocr|截图|screenshot)[\s\S]*?-->/u.test(markdown)) {
    fail(file, 'contains an OCR or screenshot editorial HTML comment')
  }

  if (type === 'tutorial') {
    for (const label of requiredIntro[language]) {
      if (!prose.includes(label)) fail(file, `missing introduction field "${label}"`)
    }
  }

  checkLinksAndImages(file, markdown)
}

for (const page of tutorialPages) {
  checkPage(page)
  const route = pageRoute(page)
  const links = navigationLinks(context.config, page.language, route)
  if (!links.some((link) => link.link.replace(/\.md$/u, '') === route && typeof link.text === 'string' && link.text.trim())) {
    fail('docs/.vitepress/config.mts', `missing nonempty ${page.language} navigation to ${route}`)
  }
}

// Preview documentation boundaries stay in the docs gate, independent of Docker availability.
for (const [file, repeat] of [
  ['docs/guide/macos-docker-preview.md', /两次/u],
  ['docs/en/guide/macos-docker-preview.md', /two\s+consecutive/iu]
]) {
  const text = stripNonProse(readFileSync(join(repositoryRoot, file), 'utf8'))
  for (const term of ['Preview', 'linux/amd64', '127.0.0.1', 'Model Guard', 'CEMC']) {
    if (!text.includes(term)) fail(file, `missing Preview compatibility boundary: ${term}`)
  }
  if (!repeat.test(text)) fail(file, 'missing repeated Preview acceptance requirement')
}
for (const file of ['README.md', 'README.zh-CN.md']) {
  const text = readFileSync(join(repositoryRoot, file), 'utf8')
  if (!text.includes('scripts/macos-docker-preview.sh')) {
    fail(file, 'missing macOS Preview launcher reference')
  }
}

if (failures.length > 0) {
  console.error(`Tutorial documentation check failed with ${failures.length} issue(s):`)
  for (const failure of failures) console.error(`- ${failure}`)
  process.exit(1)
}

console.log(
  `Tutorial documentation check passed: ${tutorialPages.length} pages including indexes, bilingual pairs, links, images, and navigation.`
)
