import { defineConfig } from 'vitepress'

export default defineConfig({
  lang: 'zh-CN',
  title: 'CosmoEdge',
  description: 'CosmoEdge documentation and tutorials',
  base: '/cosmo-edge/',
  cleanUrls: true,
  lastUpdated: true,

  themeConfig: {
    search: {
      provider: 'local'
    },

    nav: [
      { text: '教程', link: '/tutorials/' },
      { text: 'GitHub', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
    ],

    sidebar: {
      '/tutorials/': [
        {
          text: '教程',
          items: [
            { text: '教程总览', link: '/tutorials/' },
            { text: '卷一：快速上手', link: '/tutorials/01-quickstart/quickstart' },
            { text: '卷二：场景配置', link: '/tutorials/02-scenario-config/scenario-config' },
            { text: '卷三：VLM / DINO 指南', link: '/tutorials/03-vlm-guide/vlm-guide' },
            { text: '卷四：Pipeline 编排', link: '/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
            { text: '卷五：模型移植', link: '/tutorials/05-model-porting/model-porting' }
          ]
        }
      ],
      '/': [
        {
          text: '开始',
          items: [
            { text: '文档首页', link: '/' },
            { text: '教程总览', link: '/tutorials/' }
          ]
        },
        {
          text: '五卷教程',
          items: [
            { text: '卷一：快速上手', link: '/tutorials/01-quickstart/quickstart' },
            { text: '卷二：场景配置', link: '/tutorials/02-scenario-config/scenario-config' },
            { text: '卷三：VLM / DINO 指南', link: '/tutorials/03-vlm-guide/vlm-guide' },
            { text: '卷四：Pipeline 编排', link: '/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
            { text: '卷五：模型移植', link: '/tutorials/05-model-porting/model-porting' }
          ]
        }
      ]
    },

    outline: {
      level: [2, 3],
      label: '本页目录'
    },

    docFooter: {
      prev: '上一页',
      next: '下一页'
    },

    lastUpdated: {
      text: '最后更新',
      formatOptions: {
        dateStyle: 'medium',
        timeStyle: 'short'
      }
    },

    editLink: {
      pattern: 'https://github.com/cosmo-wander-ai/cosmo-edge/edit/main/docs/:path',
      text: '在 GitHub 上编辑此页'
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
    ]
  }
})
