import { defineConfig } from 'vitepress'

const guideZh = [
  { text: '构建指南', link: '/guide/build' },
  { text: 'CV186X 快速开始', link: '/guide/cv186x-quick-start' },
  { text: 'RK3576 / RKNN 集成', link: '/guide/rk3576-rknn-development' },
  { text: 'macOS Docker Preview', link: '/guide/macos-docker-preview' },
  { text: '部署指南', link: '/guide/deployment' },
  { text: '运行配置', link: '/guide/configuration' },
  { text: 'GB28181 设备接入', link: '/guide/gb28181-access' },
  { text: 'ONVIF 视频接入', link: '/guide/onvif-access' },
  { text: '故障排查', link: '/guide/troubleshooting' },
  { text: '架构概览', link: '/guide/architecture' },
  { text: '测试范围与用例', link: '/guide/test-cases' }
]

const referenceZh = [
  { text: 'API 概览', link: '/reference/api' },
  { text: '图片检测 API 接入', link: '/reference/image-detection-api' },
  { text: '字段级 API 参考', link: '/reference/api-fields' },
  { text: 'MQTT 接入参考', link: '/reference/mqtt' },
  { text: 'HTTP Webhook 参考', link: '/reference/webhook' },
  { text: '模型与资源', link: '/reference/models' }
]

const developmentZh = [
  { text: '贡献者上手路径', link: '/development/contributing' },
  { text: '前端工程', link: '/development/frontend' },
  { text: '后端开发', link: '/development/backend' },
  { text: 'CI 与质量检查', link: '/development/ci' },
  { text: '智能体辅助二次开发', link: '/development/agent-assisted-development' }
]

const i18nZh = [
  { text: 'I18N Glossary', link: '/i18n/GLOSSARY' },
  { text: 'Short Scope Rules', link: '/i18n/SHORT-SCOPES' }
]

const benchmarksZh = [
  { text: 'ScenarioBench v1.1', link: '/benchmarks/scenario-bench/v1.1/report.zh-CN.html' },
  { text: 'ScenarioBench v1.0', link: '/benchmarks/scenario-bench/v1.0/' },
  { text: '当前滚动刷新', link: '/benchmarks/scenario-bench/current/' }
]

const tutorialsZh = [
  { text: '系统使用总览', link: '/tutorials/' },
  {
    text: '基础使用',
    items: [
      { text: '快速开始：部署、登录与首次检测', link: '/tutorials/01-quickstart/quickstart' },
      { text: '场景任务配置：通道、区域、参数与告警', link: '/tutorials/02-scenario-config/scenario-config' }
    ]
  },
  {
    text: 'AI 能力配置',
    items: [
      { text: 'VLM 与 DINO：提示词驱动的视觉任务', link: '/tutorials/03-vlm-guide/vlm-guide' }
    ]
  },
  {
    text: '高级扩展',
    items: [
      { text: '算法编排：修改与创建 Pipeline', link: '/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
      { text: '第三方模型接入：转换、上传与验证', link: '/tutorials/05-model-porting/model-porting' }
    ]
  }
]

const communityZh = [
  { text: '社区总览', link: '/community/' },
  { text: '操作案例', link: '/community/cases/' },
  { text: '案例模板', link: '/community/cases/template' }
]

const guideEn = [
  { text: 'Build Guide', link: '/en/guide/build' },
  { text: 'CV186X Quick Start', link: '/en/guide/cv186x-quick-start' },
  { text: 'RK3576 / RKNN Integration', link: '/en/guide/rk3576-rknn-development' },
  { text: 'macOS Docker Preview', link: '/en/guide/macos-docker-preview' },
  { text: 'Deployment Guide', link: '/en/guide/deployment' },
  { text: 'Runtime Configuration', link: '/en/guide/configuration' },
  { text: 'GB28181 Device Access', link: '/en/guide/gb28181-access' },
  { text: 'ONVIF Video Access', link: '/en/guide/onvif-access' },
  { text: 'Troubleshooting', link: '/en/guide/troubleshooting' },
  { text: 'Architecture Overview', link: '/en/guide/architecture' },
  { text: 'Test Scope and Cases', link: '/en/guide/test-cases' }
]

const referenceEn = [
  { text: 'API Overview', link: '/en/reference/api' },
  { text: 'Image Detection API', link: '/en/reference/image-detection-api' },
  { text: 'API Fields', link: '/en/reference/api-fields' },
  { text: 'MQTT Reference', link: '/en/reference/mqtt' },
  { text: 'HTTP Webhook Reference', link: '/en/reference/webhook' },
  { text: 'Models and Resources', link: '/en/reference/models' }
]

const developmentEn = [
  { text: 'Contributor Guide', link: '/en/development/contributing' },
  { text: 'Frontend Development', link: '/en/development/frontend' },
  { text: 'Backend Development', link: '/en/development/backend' },
  { text: 'CI and Quality Checks', link: '/en/development/ci' },
  { text: 'Agent-Assisted Development', link: '/en/development/agent-assisted-development' }
]

const tutorialsEn = [
  { text: 'Using CosmoEdge Overview', link: '/en/tutorials/' },
  {
    text: 'Basic Use',
    items: [
      { text: 'Quick Start: Deployment, Sign-In, and First Detection', link: '/en/tutorials/01-quickstart/quickstart' },
      { text: 'Scenario Task Configuration: Channels, Regions, Parameters, and Alarms', link: '/en/tutorials/02-scenario-config/scenario-config' }
    ]
  },
  {
    text: 'AI Capability Configuration',
    items: [
      { text: 'VLM and DINO: Prompt-Driven Vision Tasks', link: '/en/tutorials/03-vlm-guide/vlm-guide' }
    ]
  },
  {
    text: 'Advanced Extensions',
    items: [
      { text: 'Pipeline Orchestration: Modify and Create Pipelines', link: '/en/tutorials/04-pipeline-orchestration/pipeline-orchestration' },
      { text: 'Third-Party Model Integration: Convert, Upload, and Validate', link: '/en/tutorials/05-model-porting/model-porting' }
    ]
  }
]

const benchmarksEn = [
  { text: 'ScenarioBench v1.1', link: '/benchmarks/scenario-bench/v1.1/report.html' },
  { text: 'ScenarioBench v1.0', link: '/benchmarks/scenario-bench/v1.0/README' },
  { text: 'Current Rolling Refresh', link: '/benchmarks/scenario-bench/current/README' }
]

const communityEn = [
  { text: 'Community Overview', link: '/en/community/' },
  { text: 'Recipes and Cases', link: '/en/community/cases/' },
  { text: 'Case Template', link: '/en/community/cases/template' }
]

export default defineConfig({
  title: 'CosmoEdge',
  description: 'CosmoEdge documentation and tutorials',
  base: '/cosmo-edge/',
  cleanUrls: true,
  lastUpdated: true,
  // Benchmark reports are checked in as static HTML files next to their
  // Markdown indexes. VitePress dead-link checking treats them as page routes.
  ignoreDeadLinks: [
    /^(?:\.\/)?(?:(?:\.\.\/)?current\/)?(vlm-77175-npu|vlm-55009-npu|helmet-7463-npu|pedestrian-45626-npu|pedestrian-helmet-mixed-npu|helmet-7463-x86)\/report(?:\.zh-CN)?$/,
    /^\.\/report(?:\.zh-CN)?$/,
    /^\/benchmarks\/scenario-bench\/v1\.1\/report(?:\.zh-CN)?$/,
    /^\.\/results\/(bm1688|cv186x|rk3576)\/(single-detector|dual-detector|vlm-observation)\/report$/
  ],

  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      title: 'CosmoEdge',
      description: 'CosmoEdge 文档与教程',
      themeConfig: {
        nav: [
          { text: '系统使用', link: '/tutorials/' },
          { text: '指南', link: '/guide/build' },
          { text: '社区', link: '/community/' },
          { text: '性能报告', link: '/benchmarks/scenario-bench/v1.1/report.zh-CN.html' },
          { text: '参考', link: '/reference/api' },
          { text: '开发', link: '/development/frontend' },
          { text: 'GitHub', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
        ],
        sidebar: {
          '/guide/': [{ text: '指南', items: guideZh }],
          '/reference/': [{ text: '参考', items: referenceZh }],
          '/development/': [{ text: '开发', items: developmentZh }],
          '/i18n/': [{ text: 'I18N', items: i18nZh }],
          '/tutorials/': [{ text: 'CosmoEdge 系统使用指南', items: tutorialsZh }],
          '/community/': [{ text: '社区', items: communityZh }],
          '/benchmarks/': [{ text: '性能报告', items: benchmarksZh }],
          '/': [
            { text: '开始', items: [{ text: '文档首页', link: '/' }, ...guideZh] },
            { text: 'CosmoEdge 系统使用指南', items: tutorialsZh },
            { text: '社区', items: communityZh },
            { text: '性能报告', items: benchmarksZh },
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
          { text: 'Using CosmoEdge', link: '/en/tutorials/' },
          { text: 'Guide', link: '/en/guide/build' },
          { text: 'Community', link: '/en/community/' },
          { text: 'Benchmarks', link: '/benchmarks/scenario-bench/v1.1/report.html' },
          { text: 'Reference', link: '/en/reference/api' },
          { text: 'Development', link: '/en/development/frontend' },
          { text: 'GitHub', link: 'https://github.com/cosmo-wander-ai/cosmo-edge' }
        ],
        sidebar: {
          '/en/guide/': [{ text: 'Guide', items: guideEn }],
          '/en/reference/': [{ text: 'Reference', items: referenceEn }],
          '/en/development/': [{ text: 'Development', items: developmentEn }],
          '/en/tutorials/': [{ text: 'Using CosmoEdge', items: tutorialsEn }],
          '/en/community/': [{ text: 'Community', items: communityEn }],
          '/benchmarks/': [{ text: 'Benchmarks', items: benchmarksEn }],
          '/en/': [
            { text: 'Start', items: [{ text: 'Documentation Home', link: '/en/' }, ...guideEn] },
            { text: 'Using CosmoEdge', items: tutorialsEn },
            { text: 'Community', items: communityEn },
            { text: 'Benchmarks', items: benchmarksEn },
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
