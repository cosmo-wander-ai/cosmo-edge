export default [
  // ── 核心（无标题） ──────────────
  {
    index: '/home',
    titleKey: 'nav.home',
    icon: 'el-icon-data-board',
    section: 'core',
  },

  // ── 分析展示 ──────────────────
  {
    index: '/bigScreen/warnningScreen',
    titleKey: 'nav.liveDisplay',
    icon: 'el-icon-view',
    section: 'display',
  },
  {
    index: '/imageAnalysis',
    titleKey: 'nav.imageAnalysis',
    icon: 'el-icon-picture-outline',
    section: 'display',
  },

  // ── 任务配置 ──────────────────
  {
    index: '/videoAccess',
    titleKey: 'nav.videoAccess',
    icon: 'el-icon-video-camera',
    section: 'task'
  },
  {
    index: '/gam/countManagement',
    titleKey: 'nav.sceneTasks',
    icon: 'el-icon-monitor',
    section: 'task',
  },
  {
    index: '/eventQuery',
    titleKey: 'nav.eventCenter',
    icon: 'el-icon-document',
    section: 'task',
    children: [
      {
        index: '/eventQuery/alarmRecord',
        titleKey: 'nav.detectionAnalysis',
      },
      {
        index: '/eventQuery/faceBody',
        titleKey: 'nav.faceBody',
      },
      {
        index: '/eventQuery/trafficStatistics',
        titleKey: 'nav.countingStats',
      },
    ]
  },

  // ── 资源 ──────────────────────
  {
    index: '/gam/atomicModel',
    titleKey: 'nav.modelRepository',
    icon: 'el-icon-box',
    section: 'resource',
  },
  {
    index: '/basePicManagement',
    titleKey: 'nav.baseLibrary',
    icon: 'el-icon-picture-outline',
    section: 'resource',
    runMode: 1,
    children: [
      {
        index: '/basePicManagement/people',
        titleKey: 'nav.faceLibrary',
      },
      {
        index: '/basePicManagement/workClothes',
        titleKey: 'nav.uniformLibrary'
      },
      {
        index: '/basePicManagement/item',
        titleKey: 'nav.itemLibrary',
        hidden: true
      }
    ]
  },

  // ── 系统 ──────────────────────
  {
    index: '/dataDocking/index',
    titleKey: 'nav.dataDocking',
    icon: 'el-icon-connection',
    section: 'system',
  },
  {
    index: '/audioManagement/audio',
    titleKey: 'nav.audioManagement',
    icon: 'el-icon-headset',
    section: 'system',
  },
  {
    index: '/peripheralManagement',
    titleKey: 'nav.peripheralManagement',
    icon: 'el-icon-mobile',
    section: 'system',
    runMode: 1,
    hidden: true,
    children: [
      {
        index: '/peripheralManagement/sound',
        titleKey: 'nav.networkSpeaker'
      }
    ]
  },
  {
    index: '/strategyManagement/linkageStrategy',
    titleKey: 'nav.linkageManagement',
    icon: 'el-icon-link',
    section: 'system',
    runMode: 1,
    hidden: true
  },
  {
    index: '/systemManagement',
    titleKey: 'nav.systemManagement',
    icon: 'el-icon-setting',
    section: 'system',
    children: [
      {
        index: '/systemManagement/systemConfig',
        titleKey: 'nav.systemSettings'
      },
      {
        index: '/systemManagement/netConfig',
        titleKey: 'nav.networkSettings'
      },
      {
        index: '/systemManagement/systemMaintain',
        titleKey: 'nav.systemMaintenance'
      },
      {
        index: '/systemManagement/customConfig',
        titleKey: 'nav.personalization'
      }
    ]
  }
]
