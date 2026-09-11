import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { createMarkdownRenderer, resolveUserConfig } from 'vitepress'

// Only page identity belongs here. Editorial titles come from frontmatter.
const tutorials = [
  '01-quickstart/quickstart',
  '02-scenario-config/scenario-config',
  '03-vlm-guide/vlm-guide',
  '04-pipeline-orchestration/pipeline-orchestration',
  '05-model-porting/model-porting'
]
export const tutorialPages = ['zh', 'en'].flatMap((language) =>
  [...tutorials, 'index'].map((name) => ({
    path: `docs/${language === 'en' ? 'en/' : ''}tutorials/${name}.md`,
    language,
    type: name === 'index' ? 'index' : 'tutorial'
  }))
)
export const pageRoute = (page) => '/' + page.path.replace(/^docs\//u, '').replace(/index\.md$/u, '').replace(/\.md$/u, '')
export const pageHtml = (page) => page.path.replace(/^docs\//u, '').replace(/\.md$/u, '.html')
export function displayText(value) {
  return value.replace(/<[^>]*>/gu, '')
    .replace(/&#x([\da-f]+);/giu, (_, number) => String.fromCodePoint(parseInt(number, 16)))
    .replace(/&#(\d+);/gu, (_, number) => String.fromCodePoint(Number(number)))
    .replace(/&quot;/gu, '"').replace(/&apos;/gu, "'")
    .replace(/&lt;/gu, '<').replace(/&gt;/gu, '>').replace(/&amp;/gu, '&')
    .replace(/&ZeroWidthSpace;|\u200b/gu, '').replace(/\s+/gu, ' ').trim()
}
export async function tutorialContext(repositoryRoot) {
  const root = join(repositoryRoot, 'docs')
  const [config] = await resolveUserConfig(root, 'build', 'production')
  const renderer = await createMarkdownRenderer(root, config.markdown, config.base)
  return {
    config,
    read(page) {
      const markdown = readFileSync(join(repositoryRoot, page.path), 'utf8')
      const env = {}
      const html = renderer.render(markdown, env)
      const title = env.frontmatter?.title
      if (typeof title !== 'string' || !title.trim()) throw new Error(`${page.path}: frontmatter.title must be a nonempty string`)
      return { markdown, html, title: displayText(title), content: env.content, frontmatter: env.frontmatter }
    }
  }
}
export function navigationLinks(config, language, route) {
  const locale = config.locales?.[language === 'zh' ? 'root' : 'en']
  const theme = { ...config.themeConfig, ...locale?.themeConfig }
  const links = []
  function visit(value) {
    if (Array.isArray(value)) return value.forEach(visit)
    if (!value || typeof value !== 'object') return
    if (typeof value.link === 'string') links.push(value)
    for (const child of Object.values(value)) if (typeof child === 'object') visit(child)
  }
  visit(theme.nav)
  if (Array.isArray(theme.sidebar)) visit(theme.sidebar)
  else if (theme.sidebar && route) {
    const prefix = Object.keys(theme.sidebar).filter((key) => route.startsWith(key)).sort((a, b) => b.length - a.length)[0]
    if (prefix) visit(theme.sidebar[prefix])
  }
  return links
}
