<script>
export default {
  name: 'MainLayout'
}
</script>

<script setup>
import { computed, ref, watch, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { SwitchButton, Menu, House, View, Document, VideoCamera, Connection, Cpu, Picture, Headset, Iphone, Link, Setting, DataBoard, Monitor, Box } from '@element-plus/icons-vue'
import EventBus from '@/components/eventBus.js'
import OnboardingGuide from '@/components/OnboardingGuide.vue'
import menuData from './menu.js'
import md5 from 'js-md5'
import { currentLocale, localeOptions, setLocale, t, tShort } from '@/i18n'

const router = useRouter()
const route = useRoute()
const { proxy } = getCurrentInstance()

const isCollapse = ref(false)
const activeMenu = ref('/home')
const openedMenus = ref([])
const username = computed(() => localStorage.getItem('account') || t('common.admin'))
const userMenuOpen = ref(false)
const changePwdVisible = ref(false)
const changePwdForm = ref({
  oldPassword: '',
  newPassword: '',
  confirmPassword: ''
})
const DEFAULT_PLATFORM_NAME = computed(() => t('system.defaultPlatformName'))
const normalizePlatformName = (name) => {
  const defaults = ['智能终端管理平台', '智能终端管理系统', '智能盒子', '边缘智能中枢', 'Edge Intelligence Box', 'Edge Intelligence Hub']
  if (!name || defaults.includes(name)) {
    return DEFAULT_PLATFORM_NAME.value
  }
  return name
}
const platformName = ref(normalizePlatformName(localStorage.getItem('platformName') || window.getGlobalConfig?.()?.platformName))
const defaultLogo = new URL('@/assets/logo.png', import.meta.url).href
const logoSrc = ref(localStorage.getItem('logoUrl') || defaultLogo)
const rebooting = ref(false)
const rebootSeconds = ref(60)
let rebootTimer = null

// 按 section 分组菜单
const sectionLabels = { core: '', display: 'nav.sectionDisplay', task: 'nav.sectionTask', resource: 'nav.sectionResource', system: 'nav.sectionSystem' }
const sectionOrder = ['core', 'display', 'task', 'resource', 'system']
const sectionedMenu = sectionOrder.map(section => ({
  section,
  label: sectionLabels[section],
  items: menuData.filter(item => (item.section || 'core') === section)
}))

const currentLanguage = currentLocale
const changeLanguage = (locale) => {
  setLocale(locale)
}

// When locale changes, re-evaluate the default platform name for sidebar + title
watch(currentLocale, () => {
  // Only update if using the default (not a user-customized system name)
  const stored = localStorage.getItem('platformName')
  const normalized = normalizePlatformName(stored || window.getGlobalConfig?.()?.platformName)
  platformName.value = normalized
  document.title = normalized
})
const menuTitle = (item) => {
  return item.titleKey ? tShort(item.titleKey, 'sidebar.menu') : ''
}

// 根据当前路由初始化激活菜单和展开的父菜单
const initMenuState = () => {
  const currentPath = route.path
  activeMenu.value = currentPath
  
  // 查找并展开包含当前路由的父菜单
  menuData.forEach(item => {
    if (item.children && item.children.length > 0) {
      const hasActiveChild = item.children.some(child => child.index === currentPath)
      if (hasActiveChild && !openedMenus.value.includes(item.index)) {
        openedMenus.value.push(item.index)
      }
    }
  })
}

// 记录进入编排页前侧边栏的原始状态
let sidebarStateBeforeArrange = null

// 监听路由变化，同步菜单状态
watch(() => route.path, (newPath, oldPath) => {
  activeMenu.value = newPath
  
  // 自动展开包含当前路由的父菜单
  menuData.forEach(item => {
    if (item.children && item.children.length > 0) {
      const hasActiveChild = item.children.some(child => child.index === newPath)
      if (hasActiveChild && !openedMenus.value.includes(item.index)) {
        openedMenus.value.push(item.index)
      }
    }
  })

  // 进入编排页面时自动收起侧边栏，增大画幅
  const isArrangePage = newPath.includes('/arrangeDetail')
  const wasArrangePage = oldPath && oldPath.includes('/arrangeDetail')

  if (isArrangePage && !wasArrangePage) {
    sidebarStateBeforeArrange = isCollapse.value
    if (!isCollapse.value) {
      isCollapse.value = true
      EventBus.$emit('layout:resize')
    }
  } else if (!isArrangePage && wasArrangePage && sidebarStateBeforeArrange !== null) {
    // 离开编排页时恢复侧边栏原始状态
    isCollapse.value = sidebarStateBeforeArrange
    sidebarStateBeforeArrange = null
    EventBus.$emit('layout:resize')
  }
})

// 切换侧边栏折叠状态
const toggleCollapse = () => {
  isCollapse.value = !isCollapse.value
  EventBus.$emit('layout:resize')
}

// 处理菜单点击
const handleMenuClick = (item) => {
  if (item.children && item.children.length > 0) {
    // 有子菜单，切换展开/收起
    const index = openedMenus.value.indexOf(item.index)
    if (index > -1) {
      openedMenus.value.splice(index, 1)
    } else {
      openedMenus.value.push(item.index)
    }
  } else {
    // 无子菜单，直接跳转
    activeMenu.value = item.index
    router.push(item.index)
  }
}

// 处理子菜单点击
const handleSubMenuClick = (subItem) => {
  activeMenu.value = subItem.index
  router.push(subItem.index)
}

// 判断菜单是否展开
const isMenuOpen = (index) => {
  return openedMenus.value.includes(index)
}

// 判断菜单是否激活
const isMenuActive = (index) => {
  return activeMenu.value === index || activeMenu.value.startsWith(index + '/')
}

// 菜单图标映射到 Element Plus 图标组件
const iconMap = {
  'el-icon-s-home': House,
  'el-icon-data-board': DataBoard,
  'el-icon-monitor': Monitor,
  'el-icon-box': Box,
  'el-icon-view': View,
  'el-icon-document': Document,
  'el-icon-video-camera': VideoCamera,
  'el-icon-connection': Connection,
  'el-icon-cpu': Cpu,
  'el-icon-picture-outline': Picture,
  'el-icon-headset': Headset,
  'el-icon-mobile': Iphone,
  'el-icon-link': Link,
  'el-icon-setting': Setting
}
const getIconComp = (icon) => iconMap[icon] || Menu

// 组件挂载时初始化菜单状态
onMounted(() => {
  initMenuState()
  document.addEventListener('click', handleGlobalClick)
  getSystemConfig()

  // 如果初始路由就是编排页，立即收起侧边栏
  if (route.path.includes('/arrangeDetail')) {
    sidebarStateBeforeArrange = false
    isCollapse.value = true
  }

  // 子页面可通过事件控制侧边栏收起/展开
  EventBus.$on('sidebar:collapse', () => {
    if (!isCollapse.value) {
      isCollapse.value = true
      EventBus.$emit('layout:resize')
    }
  })
  EventBus.$on('sidebar:expand', () => {
    if (isCollapse.value) {
      isCollapse.value = false
      EventBus.$emit('layout:resize')
    }
  })
})

onBeforeUnmount(() => {
  document.removeEventListener('click', handleGlobalClick)
  if (rebootTimer) {
    clearInterval(rebootTimer)
    rebootTimer = null
  }
  EventBus.$off && EventBus.$off('sidebar:collapse')
  EventBus.$off && EventBus.$off('sidebar:expand')
})

const toggleUserMenu = (e) => {
  userMenuOpen.value = !userMenuOpen.value
  e?.stopPropagation?.()
}

const handleGlobalClick = () => {
  userMenuOpen.value = false
}

const handleLogout = () => {
  userMenuOpen.value = false
  localStorage.removeItem('mtk')
  localStorage.removeItem('token')
  localStorage.removeItem('account')
  localStorage.removeItem('platformType')
  localStorage.removeItem('runMode')
  localStorage.removeItem('playedCameraList')
  localStorage.removeItem('dialog')
  router.replace('/boxLogin')
}

const openChangePwd = (e) => {
  e?.stopPropagation?.()
  userMenuOpen.value = false
  changePwdVisible.value = true
}

const handleResetOnboarding = async (e) => {
  e?.stopPropagation?.()
  userMenuOpen.value = false
  try {
    await proxy.$API.resetOnboarding({})
    localStorage.removeItem('onboarding_completed')
    localStorage.setItem('onboarding_active', 'true')
    proxy.$message.success(t('onboarding.resetSucceeded'))
  } catch {
    proxy.$message.error(t('onboarding.resetFailed'))
  }
}

const submitChangePwd = () => {
  if (!changePwdForm.value.oldPassword || !changePwdForm.value.newPassword) {
    proxy.$message.warning(t('validate.oldNewPasswordRequired'))
    return
  }
  if (changePwdForm.value.newPassword !== changePwdForm.value.confirmPassword) {
    proxy.$message.warning(t('validate.passwordMismatch'))
    return
  }
  const params = {
    passwdOld: md5(changePwdForm.value.oldPassword),
    passwdNew: md5(changePwdForm.value.newPassword),
    mtk: localStorage.getItem('mtk') || ''
  }
  proxy.$API.boxModifyPassword(params).then(() => {
    proxy.$message.success(t('system.passwordChangedLoginAgain'))
    changePwdVisible.value = false
    handleLogout()
  })
}

const getSystemConfig = async () => {
  try {
    const res = await proxy.$API.boxGetLogo({})
    const { resData } = res || {}
    if (resData?.logoUrl) {
      const url = resData.logoUrl
      logoSrc.value = url
      localStorage.setItem('logoUrl', url)
      // 更新 favicon
      const ts = Date.now()
      const link = document.querySelector("link[rel*='icon']") || document.createElement('link')
      link.type = 'image/x-icon'
      link.rel = 'icon'
      link.href = `${url}?v=${ts}`
      if (!link.parentNode) document.head.appendChild(link)
    }
    if (resData?.systemName) {
      const name = normalizePlatformName(resData.systemName)
      platformName.value = name
      localStorage.setItem('platformName', name)
      document.title = name
    }
  } catch (e) {
    // 忽略获取失败，使用本地缓存或默认值
  }
}

const handleReboot = () => {
  proxy.$confirm(t('system.rebootConfirm'), t('common.notice'), {
    type: 'warning',
    confirmButtonText: t('action.reboot'),
    cancelButtonText: t('action.cancel')
  }).then(() => {
    proxy.$API.boxResetSystem({ resetOperation: 0 }).then(() => {
      rebooting.value = true
      rebootSeconds.value = 60
      if (rebootTimer) clearInterval(rebootTimer)
      rebootTimer = setInterval(() => {
        if (rebootSeconds.value > 0) {
          rebootSeconds.value -= 1
        } else {
          clearInterval(rebootTimer)
          rebootTimer = null
          localStorage.removeItem('token')
          localStorage.removeItem('mtk')
          window.location.hash = '#/boxLogin'
        }
      }, 1000)
    })
  })
}
</script>

<template>
  <div class="main-container">
    <!-- 侧边栏 -->
    <aside class="sidebar" :class="{ 'is-collapse': isCollapse }">
      <div class="sidebar-header">
        <div class="logo">
          <img class="logo-image" :src="logoSrc" alt="logo" />
          <span v-show="!isCollapse" class="logo-text">{{ platformName }}</span>
        </div>
        <button class="collapse-btn" @click="toggleCollapse">
          <svg v-if="!isCollapse" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 19l-7-7 7-7m8 14l-7-7 7-7" />
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 5l7 7-7 7M5 5l7 7-7 7" />
          </svg>
        </button>
      </div>

      <nav class="menu-container">
        <template v-for="(group, gi) in sectionedMenu" :key="group.section">
          <!-- 分区标签 -->
          <div v-if="group.label && !isCollapse" class="menu-section-label">{{ t(group.label) }}</div>
          <div v-if="gi > 0" class="menu-section-divider"></div>

          <template v-for="item in group.items" :key="item.index">
            <div
              v-if="!item.hidden"
              class="menu-item-wrapper"
              :id="'onboarding-menu-' + item.index.replace(/\//g, '-').replace(/^-/, '')"
            >
              <!-- 主菜单项 -->
              <div
                class="menu-item"
                :class="{ 
                  'is-active': isMenuActive(item.index) && !item.children,
                  'has-children': item.children && item.children.length > 0
                }"
                @click="handleMenuClick(item)"
              >
                <el-icon class="menu-icon">
                  <component :is="getIconComp(item.icon)" />
                </el-icon>
                <span v-show="!isCollapse" class="menu-title">{{ menuTitle(item) }}</span>
                <svg
                  v-if="item.children && item.children.length > 0 && !isCollapse"
                  class="menu-arrow"
                  :class="{ 'is-open': isMenuOpen(item.index) }"
                  viewBox="0 0 24 24"
                  fill="none"
                  stroke="currentColor"
                >
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                </svg>
              </div>

              <!-- 子菜单 -->
              <transition name="submenu">
                <div
                  v-if="item.children && item.children.length > 0 && isMenuOpen(item.index) && !isCollapse"
                  class="submenu-container"
                >
                  <template v-for="subItem in item.children" :key="subItem.index">
                    <div
                      v-if="!subItem.hidden"
                      class="submenu-item"
                      :class="{ 'is-active': activeMenu === subItem.index }"
                      @click.stop="handleSubMenuClick(subItem)"
                    >
                      <span class="submenu-title">{{ menuTitle(subItem) }}</span>
                    </div>
                  </template>
                </div>
              </transition>
            </div>
          </template>
        </template>
      </nav>
    </aside>

    <!-- 主内容区 -->
    <div class="main-content">
      <header class="main-header">
        <div class="header-left">
          <!-- <h2 class="page-title">边缘智能中枢</h2> -->
        </div>
        <div class="header-right">
          <button class="header-btn" @click="handleReboot">
              <el-icon><SwitchButton /></el-icon>
          </button>
          <el-select
            v-model="currentLanguage"
            class="locale-select"
            size="small"
            @change="changeLanguage"
          >
            <el-option
              v-for="item in localeOptions"
              :key="item.value"
              :label="item.label || t(item.labelKey)"
              :value="item.value"
            />
          </el-select>
          <div class="user-info" @click.stop="toggleUserMenu">
            <div class="avatar">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
              </svg>
            </div>
            <span class="username">{{ username }}</span>
            <svg class="user-caret" viewBox="0 0 24 24" fill="none" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
            </svg>
            <div v-if="userMenuOpen" class="user-dropdown" @click.stop>
              <div class="dropdown-item" @click="openChangePwd">{{ t('action.changePassword') }}</div>
              <div class="dropdown-item" @click="handleResetOnboarding">{{ t('action.resetOnboarding') }}</div>
              <div class="dropdown-divider"></div>
              <div class="dropdown-item danger" @click="handleLogout">{{ t('action.logout') }}</div>
            </div>
          </div>
        </div>
      </header>

      <main class="content-area" id="content-area">
        <router-view v-slot="{ Component }">
          <keep-alive include="ImageAnalysis">
            <component :is="Component" />
          </keep-alive>
        </router-view>
        <!-- Onboarding guide overlay -->
        <OnboardingGuide ref="onboardingGuideRef" />
      </main>
      <div v-if="rebooting" class="reboot-mask">
        <div class="reboot-dialog">
          <div class="reboot-title">{{ t('system.rebooting') }}</div>
          <div class="reboot-sub">{{ t('system.remainingSeconds', { n: rebootSeconds }) }}</div>
          <div class="reboot-spinner"></div>
        </div>
      </div>

      <el-dialog v-model="changePwdVisible" :title="t('action.changePassword')" width="420px" center>
        <el-form :label-width="currentLocale === 'en-US' ? '180px' : '90px'" size="small">
          <el-form-item :label="t('field.oldPassword')">
            <el-input v-model="changePwdForm.oldPassword" type="password" show-password></el-input>
          </el-form-item>
          <el-form-item :label="t('field.newPassword')">
            <el-input v-model="changePwdForm.newPassword" type="password" show-password></el-input>
          </el-form-item>
          <el-form-item :label="t('field.confirmPassword')">
            <el-input v-model="changePwdForm.confirmPassword" type="password" show-password></el-input>
          </el-form-item>
        </el-form>
        <template #footer>
          <span class="dialog-footer">
            <el-button @click="changePwdVisible = false" size="small">{{ t('action.cancel') }}</el-button>
            <el-button type="primary" @click="submitChangePwd" size="small">{{ t('action.save') }}</el-button>
          </span>
        </template>
      </el-dialog>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.main-container {
  display: flex;
  height: 100vh;
  background: #f5f7fa;
}

// 侧边栏样式
.sidebar {
  width: 200px;
  background: #1d2b3a;
  color: rgba(255, 255, 255, 0.65);
  transition: width 0.3s ease;
  display: flex;
  flex-direction: column;

  &.is-collapse {
    width: 62px;

    .menu-title,
    .submenu-container,
    .menu-arrow {
      display: none;
    }
  }
}

.sidebar-header {
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 12px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
}

.logo {
  display: flex;
  align-items: center;
  gap: 10px;
  flex: 1;
  min-width: 0;
}

.logo-icon {
  width: 28px;
  height: 28px;
  color: #4299e1;
  flex-shrink: 0;
}

.logo-image {
  width: 34px;
  height: 34px;
  border-radius: 8px;
  object-fit: cover;
  flex-shrink: 0;
}

.logo-text {
  font-size: 15px;
  font-weight: 600;
  color: #ffffff;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 140px;
}

.collapse-btn {
  width: 26px;
  height: 26px;
  border: none;
  background: rgba(255, 255, 255, 0.08);
  border-radius: 6px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s ease;
  flex-shrink: 0;

  svg {
    width: 14px;
    height: 14px;
    color: rgba(255, 255, 255, 0.5);
  }

  &:hover {
    background: rgba(255, 255, 255, 0.15);
  }
}

.menu-container {
  flex: 1;
  overflow-y: auto;
  padding: 8px 8px;

  &::-webkit-scrollbar {
    width: 4px;
  }

  &::-webkit-scrollbar-thumb {
    background: rgba(255, 255, 255, 0.15);
    border-radius: 2px;
  }
}

.menu-section-label {
  font-size: 12px;
  font-weight: 500;
  color: rgba(255, 255, 255, 0.35);
  letter-spacing: 0.5px;
  padding: 10px 12px 4px;
}

.menu-section-divider {
  display: none;
}

.menu-item-wrapper {
  margin-bottom: 2px;
}

.menu-item {
  display: flex;
  align-items: center;
  padding: 10px 12px;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s ease;
  position: relative;
  gap: 10px;

  &:hover {
    background: rgba(255, 255, 255, 0.06);
  }

  &.is-active {
    background: #2d6fb6;

    .menu-icon {
      color: #ffffff;
    }

    .menu-title {
      color: #ffffff;
      font-weight: 600;
    }
  }

  &.has-children {
    &:hover {
      background: rgba(255, 255, 255, 0.06);
    }
  }
}

.menu-icon {
  width: 18px;
  height: 18px;
  font-size: 18px;
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: rgba(255, 255, 255, 0.6);
}

.menu-title {
  flex: 1;
  font-size: 14px;
  font-weight: 400;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: rgba(255, 255, 255, 0.65);
}

.menu-arrow {
  width: 14px;
  height: 14px;
  transition: transform 0.3s ease;
  flex-shrink: 0;
  color: rgba(255, 255, 255, 0.3);

  &.is-open {
    transform: rotate(180deg);
  }
}

.submenu-container {
  margin-top: 2px;
  margin-left: 28px;
  overflow: hidden;
  border-left: 1px solid rgba(255, 255, 255, 0.08);
  padding-left: 0;
}

.submenu-item {
  padding: 7px 12px;
  border-radius: 0 6px 6px 0;
  cursor: pointer;
  transition: all 0.2s ease;
  position: relative;
  margin-bottom: 1px;

  &:hover {
    background: rgba(255, 255, 255, 0.06);
  }

  &.is-active {
    background: rgba(49, 130, 206, 0.2);
    border-left: 2px solid #4299e1;
    margin-left: -1px;

    .submenu-title {
      color: #ffffff;
      font-weight: 500;
    }
  }
}

.submenu-title {
  font-size: 13px;
  padding-left: 10px;
  color: rgba(255, 255, 255, 0.55);
}

// 子菜单动画
.submenu-enter-active,
.submenu-leave-active {
  transition: all 0.25s ease;
}

.submenu-enter-from,
.submenu-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}

// 主内容区样式
.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.main-header {
  height: 64px;
  background: #fff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
  z-index: 10;
}

.header-left {
  display: flex;
  align-items: center;
}

.page-title {
  font-size: 20px;
  font-weight: 600;
  color: #1a1f3a;
  margin: 0;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.header-btn {
  width: 40px;
  height: 40px;
  border: none;
  background: #f5f7fa;
  border-radius: 10px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s ease;
  .el-icon {
    font-size: 20px;  
  }


  &:hover {
    background: #e2e8f0;
  }
}

.locale-select {
  width: 112px;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  background: #f5f7fa;
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.2s ease;
  position: relative;

  &:hover {
    background: #e2e8f0;
  }
}

.avatar {
  width: 36px;
  height: 36px;
  background: linear-gradient(135deg, #3182ce 0%, #4299e1 100%);
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;

  svg {
    width: 20px;
    height: 20px;
    color: #fff;
  }
}

.username {
  font-size: 14px;
  font-weight: 500;
  color: #1a1f3a;
}

.user-caret {
  width: 16px;
  height: 16px;
  color: #64748b;
}

.user-dropdown {
  position: absolute;
  right: 0;
  top: 52px;
  background: #fff;
  border-radius: 10px;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.12);
  padding: 8px 0;
  width: 140px;
  z-index: 2000;
}

.dropdown-item {
  padding: 8px 14px;
  font-size: 14px;
  color: #1f2937;
  cursor: pointer;
  transition: background 0.2s;

  &:hover {
    background: #f3f4f6;
  }

  &.danger {
    color: #ef4444;
  }
}

.dropdown-divider {
  height: 1px;
  background: #e5e7eb;
  margin: 6px 0;
}

.content-area {
  height:calc(100vh - 64px);
  padding: 20px;
  background: #f5f7fa;
  overflow: auto;
}

.reboot-mask {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 3000;
}
.reboot-dialog {
  width: 320px;
  padding: 24px 20px;
  background: #fff;
  border-radius: 12px;
  text-align: center;
  box-shadow: 0 8px 24px rgba(0,0,0,0.2);
}
.reboot-title {
  font-size: 18px;
  font-weight: 600;
  color: #1f2937;
}
.reboot-sub {
  margin-top: 8px;
  font-size: 14px;
  color: #4b5563;
}
.reboot-spinner {
  margin: 14px auto 0;
  width: 24px;
  height: 24px;
  border: 3px solid #e5e7eb;
  border-top-color: #3182ce;
  border-radius: 50%;
  animation: spin 1s linear infinite;
}
@keyframes spin {
  to { transform: rotate(360deg); }
}

// 响应式设计
@media (max-width: 768px) {
  .sidebar {
    position: fixed;
    left: 0;
    top: 0;
    bottom: 0;
    z-index: 1000;
  }

  .main-content {
    margin-left: 0;
  }

  .page-title {
    font-size: 16px;
  }

  .username {
    display: none;
  }
}
</style>
