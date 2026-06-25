import { defineConfig } from 'vitepress'

const guideZh = [
  { text: '构建指南', link: '/guide/build' },
  { text: '部署指南', link: '/guide/deployment' },
  { text: '运行配置', link: '/guide/configuration' },
  { text: '故障排查', link: '/guide/troubleshooting' },
  { text: '架构概览', link: '/guide/architecture' },
  { text: '测试范围与用例', link: '/guide/test-cases' }
]

const referenceZh = [
  { text: 'API 概览', link: '/reference/api' },
  { text: '字段级 API 参考', link: '/reference/api-fields' },
  { text: 'MQTT 接入参考', link: '/reference/mqtt' },
  { text: 'HTTP Webhook 参考', link: '/reference/webhook' },
  { text: '模型与资源', link: '/reference/models' }
]

const developmentZh = [
  { text: '前端工程', link: '/development/frontend' },
  { text: '后端开发', link: '/development/backend' },
  { text: 'CI 与质量检查', link: '/development/ci' }
]

const i18nZh = [
  { text: 'I18N Glossary', link: '/i18n/GLOSSARY' },
  { text: 'Short Scope Rules', link: '/i18n/SHORT-SCOPES' }
]

const tutorialsZh = [
  { text: '教程总览', link: '/tutorials/' },
  { text: '卷一：快速上手', link: '/tutorials/01-quickstart/quickstart' },
  { text: '卷二：场景配置', link: '/tutorials/02-scenario-config/scenario-config' },
  { text: '卷三：VLM / DINO 指南', link: '/tutorials/03-vlm-guide/vlm-guide' },
  { text: '卷四：Pipeline 编排', link: '/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
  { text: '卷五：模型移植', link: '/tutorials/05-model-porting/model-porting' }
]

const guideEn = [
  { text: 'Build Guide', link: '/en/guide/build' },
  { text: 'Deployment Guide', link: '/en/guide/deployment' },
  { text: 'Runtime Configuration', link: '/en/guide/configuration' },
  { text: 'Troubleshooting', link: '/en/guide/troubleshooting' },
  { text: 'Architecture Overview', link: '/en/guide/architecture' },
  { text: 'Test Scope and Cases', link: '/en/guide/test-cases' }
]

const referenceEn = [
  { text: 'API Overview', link: '/en/reference/api' },
  { text: 'API Fields', link: '/en/reference/api-fields' },
  { text: 'MQTT Reference', link: '/en/reference/mqtt' },
  { text: 'HTTP Webhook Reference', link: '/en/reference/webhook' },
  { text: 'Models and Resources', link: '/en/reference/models' }
]

const developmentEn = [
  { text: 'Frontend Development', link: '/en/development/frontend' },
  { text: 'Backend Development', link: '/en/development/backend' },
  { text: 'CI and Quality Checks', link: '/en/development/ci' }
]

const tutorialsEn = [
  { text: 'Tutorials Overview', link: '/en/tutorials/' },
  { text: 'Volume 1: Quick Start', link: '/en/tutorials/01-quickstart/quickstart' },
  { text: 'Volume 2: Scenario Configuration', link: '/en/tutorials/02-scenario-config/scenario-config' },
  { text: 'Volume 3: VLM / DINO Guide', link: '/en/tutorials/03-vlm-guide/vlm-guide' },
  { text: 'Volume 4: Pipeline Orchestration', link: '/en/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
  { text: 'Volume 5: Model Porting', link: '/en/tutorials/05-model-porting/model-porting' }
]

export default defineConfig({
  title: 'CosmoEdge',
  description: 'CosmoEdge documentation and tutorials',
  base: '/cosmo-edge/',
  cleanUrls: true,
  lastUpdated: true,

  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      title: 'CosmoEdge',
      description: 'CosmoEdge 文档与教程',
      themeConfig: {
        nav: [
          { text: '教程', link: '/tutorials/' },
          { text: '指南', link: '/guide/build' },
          { text: '参考', link: '/reference/api' },
          { text: '开发', link: '/development/frontend' },
          { text: 'GitHub', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
        ],
        sidebar: {
          '/guide/': [{ text: '指南', items: guideZh }],
          '/reference/': [{ text: '参考', items: referenceZh }],
          '/development/': [{ text: '开发', items: developmentZh }],
          '/i18n/': [{ text: 'I18N', items: i18nZh }],
          '/tutorials/': [{ text: '教程', items: tutorialsZh }],
          '/': [
            { text: '开始', items: [{ text: '文档首页', link: '/' }, ...guideZh] },
            { text: '五卷教程', items: tutorialsZh },
            { text: '参考', items: referenceZh },
            { text: '开发', items: developmentZh },
            { text: 'I18N', items: i18nZh }
          ]
        },
        outline: { label: '本页目录' },
        docFooter: { prev: '上一页', next: '下一页' },
        lastUpdated: { text: '最后更新' },
        editLink: {
          pattern: 'https://github.com/cosmo-wander-ai/cosmo-edge/edit/main/docs/:path',
          text: '在 GitHub 上编辑此页'
        },
        langMenuLabel: '语言',
        returnToTopLabel: '返回顶部',
        sidebarMenuLabel: '菜单',
        darkModeSwitchLabel: '深色模式',
        lightModeSwitchTitle: '切换到浅色模式',
        darkModeSwitchTitle: '切换到深色模式'
      }
    },
    en: {
      label: 'English',
      lang: 'en-US',
      title: 'CosmoEdge',
      description: 'CosmoEdge documentation and tutorials',
      themeConfig: {
        nav: [
          { text: 'Tutorials', link: '/en/tutorials/' },
          { text: 'Guide', link: '/en/guide/build' },
          { text: 'Reference', link: '/en/reference/api' },
          { text: 'Development', link: '/en/development/frontend' },
          { text: 'GitHub', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
        ],
        sidebar: {
          '/en/guide/': [{ text: 'Guide', items: guideEn }],
          '/en/reference/': [{ text: 'Reference', items: referenceEn }],
          '/en/development/': [{ text: 'Development', items: developmentEn }],
          '/en/tutorials/': [{ text: 'Tutorials', items: tutorialsEn }],
          '/en/': [
            { text: 'Start', items: [{ text: 'Documentation Home', link: '/en/' }, ...guideEn] },
            { text: 'Tutorials', items: tutorialsEn },
            { text: 'Reference', items: referenceEn },
            { text: 'Development', items: developmentEn }
          ]
        },
        outline: { label: 'On This Page' },
        docFooter: { prev: 'Previous page', next: 'Next page' },
        lastUpdated: { text: 'Last updated' },
        editLink: {
          pattern: 'https://github.com/cosmo-wander-ai/cosmo-edge/edit/main/docs/:path',
          text: 'Edit this page on GitHub'
        }
      }
    }
  },

  themeConfig: {
    search: {
      provider: 'local'
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
    ],
    outline: {
      level: [2, 3]
    },
    lastUpdated: {
      formatOptions: {
        dateStyle: 'medium',
        timeStyle: 'short'
      }
    }
  }
})
