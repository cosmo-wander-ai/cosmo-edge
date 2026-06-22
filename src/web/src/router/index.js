import { createRouter, createWebHashHistory } from 'vue-router'
import LoginPage from '../views/LoginPage.vue'
import MainLayout from '../views/main/index.vue'

const routes = [
  {
    path: '/boxLogin',
    name: 'boxLogin',
    component: LoginPage
  },
  // 大屏页面 - 独立路由，不包含主布局
  {
    path: '/bigScreen/warnningScreen',
    name: 'WarnningScreen',
    component: () => import('../views/box/bigScreen/warnningScreen/index.vue')
  },
  {
    path: '/',
    component: MainLayout,
    redirect: '/home',
    children: [
      {
        path: '/home',
        name: 'Home',
        component: () => import('../views/home/index.vue')
      },
      {
        path: '/imageAnalysis',
        name: 'ImageAnalysis',
        component: () => import('../views/gam/imageAnalysis/index.vue')
      },
      {
        path: '/eventQuery/alarmRecord',
        name: 'AlarmRecord',
        component: () => import('../views/box/eventQuery/alarmRecord/index.vue')
      },
      {
        path: '/eventQuery/faceBody',
        name: 'FaceBody',
        component: () => import('../views/box/eventQuery/faceBody/index.vue')
      },
      {
        path: '/eventQuery/carStatistics',
        name: 'CarStatistics',
        component: () => import('../views/box/eventQuery/carStatistics/index.vue')
      },
      {
        path: '/eventQuery/trafficStatistics',
        name: 'TrafficStatistics',
        component: () => import('../views/box/eventQuery/trafficStatistics/index.vue')
      },
      {
        path: '/gam/taskManager/taskmanagement',
        name: 'TaskManagement',
        component: () => import('../views/gam/taskManager/index.vue')
      },
      {
        path: '/videoAccess',
        name: 'VideoAccess',
        component: () => import('../views/box/videoAccess/wrapper.vue')
      },
      {
        path: '/gam/taskManager/realEditingTask',
        name: 'RealEditingTask',
        component: () => import('../views/gam/taskManager/editTask/serviceConfig.vue')
      },
      {
        path: '/dataDocking/index',
        name: 'DataDocking',
        component: () => import('../views/box/dataDocking/index.vue')
      },
      {
        path: '/gam/countManagement',
        name: 'CountManagement',
        component: () => import('../views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue')
      },
      {
        path: '/gam/atomicModel',
        name: 'AtomicModel',
        component: () => import('../views/gam/countManagement/atomicModel/index.vue')
      },
      {
        path: '/gam/arrangeDetail',
        name: 'ArrangeDetail',
        component: () => import('../views/gam/countManagement/arrangeDetail/index.vue')
      }, {
        path: '/gam/modelConfig',
        name: 'ModelConfig',
        component: () => import('../views/gam/countManagement/modelConfig/index.vue')
      },
      {
        path: '/basePicManagement/people',
        name: 'PeopleBase',
        component: () => import('@/views/box/basePicManagement/people/index.vue')
      },
      {
        path: '/basePicManagement/workClothes',
        name: 'WorkClothes',
        component: () => import('@/views/box/basePicManagement/workClothes/index.vue')
      },
      {
        path: '/basePicManagement/item',
        name: 'ItemBase',
        component: () => import('@/views/box/basePicManagement/item/index.vue')
      },
      {
        path: '/audioManagement/audio',
        name: 'AudioManagement',
        component: () => import('@/views/box/audioManagement/audio/index.vue')
      },
      {
        path: '/peripheralManagement/sound',
        name: 'SoundManagement',
        component: () => import('@/views/box/peripheralManagement/sound/index.vue')
      },
      {
        path: '/strategyManagement/linkageStrategy',
        name: 'LinkageStrategy',
        component: () => import('@/views/box/strategyManagement/linkageStrategy/index.vue')
      },
      // 系统管理
      // 系统设置
      {
        path: '/systemManagement/systemConfig',
        name: 'systemConfig',
        component: () => import('@/views/box/systemManagement/systemConfig/index.vue')
      },
      // 网络设置
      {
        path: '/systemManagement/netConfig',
        name: 'netConfig',
        component: () => import('@/views/box/systemManagement/netConfig/index.vue')
      },
      // 系统维护
      {
        path: '/systemManagement/systemMaintain',
        name: 'systemMaintain',
        component: () => import('@/views/box/systemManagement/systemMaintain/index.vue')
      },
      // 个性化设置
      {
        path: '/systemManagement/customConfig',
        name: 'customConfig',
        component: () => import('@/views/box/systemManagement/customConfig/index.vue')
      },
    ]
  },
  // 404 捕获路由 - 必须放在最后
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    redirect: (to) => {
      // 如果是访问根路径，重定向到登录页
      if (to.path === '/') {
        return '/boxLogin'
      }
      // 其他未匹配的路由重定向到首页
      return '/home'
    }
  }
]

const router = createRouter({
  history: createWebHashHistory(),
  routes
})

// 添加路由守卫
router.beforeEach((to, from, next) => {
  console.log('路由跳转:', from.path, '->', to.path)

  const token = localStorage.getItem('token')

  // 如果访问登录页，直接放行
  if (to.path === '/boxLogin') {
    // 如果已经登录，重定向到首页
    if (token) {
      next('/home')
    } else {
      next()
    }
    return
  }

  // 如果访问其他页面但没有 token，重定向到登录页
  if (!token) {
    next('/boxLogin')
    return
  }

  // 有 token，正常访问
  next()
})

// 添加错误处理
router.onError((error) => {
  console.error('路由错误:', error)
})

export default router
