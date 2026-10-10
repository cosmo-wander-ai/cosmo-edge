<template>
  <div class="login-container">
    <div class="login-appearance"><ThemeSwitcher /></div>
    <div class="login-background" aria-hidden="true"></div>
    
    <div class="login-card">
      <div class="logo-section">
        <img class="logo-icon" :src="logoSrc" alt="logo" />
        <h1 class="logo-title">{{ platformName }}</h1>
        <p class="logo-subtitle">{{ t('login.subtitle') }}</p>
      </div>

      <form @submit.prevent="handleLogin" class="login-form">
        <div class="form-group">
          <label for="username" class="form-label">{{ t('field.username') }}</label>
          <div class="input-wrapper">
            <svg class="input-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
            </svg>
            <input
              id="username"
              v-model.trim="username"
              type="text"
              autocomplete="username"
              :placeholder="t('placeholder.username')"
              required
              class="form-input"
              @keyup.enter="handleLogin"
            />
          </div>
        </div>

        <div class="form-group">
          <label for="password" class="form-label">{{ t('field.password') }}</label>
          <div class="input-wrapper">
            <svg class="input-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
            </svg>
            <input
              id="password"
              v-model.trim="password"
              type="password"
              autocomplete="current-password"
              :placeholder="t('placeholder.password')"
              required
              class="form-input"
              @keyup.enter="handleLogin"
            />
          </div>
        </div>

        <button type="submit" class="submit-button" :disabled="isLoading">
          <span v-if="!isLoading">{{ t('action.login') }}</span>
          <span v-else class="loading-spinner">
            <svg class="spinner-icon" viewBox="0 0 24 24">
              <circle class="spinner-circle" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" fill="none" />
            </svg>
            {{ t('action.loggingIn') }}
          </span>
        </button>
      </form>
    </div>

    <!-- Password Change Dialog (forced on default password) -->
    <div v-if="showPasswordDialog" class="dialog-overlay" @click.self="() => {}">
      <div class="dialog-card">
        <div class="dialog-header">
          <svg class="dialog-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-2.5L13.732 4c-.77-.833-1.964-.833-2.732 0L4.082 16.5c-.77.833.192 2.5 1.732 2.5z" />
          </svg>
          <h2 class="dialog-title">{{ t('login.passwordChangeTitle') }}</h2>
        </div>
        <p class="dialog-desc">{{ t('login.passwordChangeDesc') }}</p>
        <form @submit.prevent="handleChangePassword" class="login-form">
          <!-- 隐藏的用户名输入框，用于辅助功能和浏览器密码管理器 -->
          <input type="text" autocomplete="username" :value="username" style="display:none;" aria-hidden="true" />
          <div class="form-group">
            <label for="oldPwd" class="form-label">{{ t('field.oldPassword') }}</label>
            <div class="input-wrapper">
              <svg class="input-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
              </svg>
              <input id="oldPwd" v-model.trim="oldPwd" type="password" autocomplete="current-password" :placeholder="t('placeholder.password')" required class="form-input" />
            </div>
          </div>
          <div class="form-group">
            <label for="newPwd" class="form-label">{{ t('field.newPassword') }}</label>
            <div class="input-wrapper">
              <svg class="input-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
              </svg>
              <input id="newPwd" v-model.trim="newPwd" type="password" autocomplete="new-password" :placeholder="t('placeholder.enter', { field: t('field.newPassword') })" required class="form-input" />
            </div>
          </div>
          <div class="form-group">
            <label for="confirmPwd" class="form-label">{{ t('field.confirmPassword') }}</label>
            <div class="input-wrapper">
              <svg class="input-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
              </svg>
              <input id="confirmPwd" v-model.trim="confirmPwd" type="password" autocomplete="new-password" :placeholder="t('placeholder.enter', { field: t('field.confirmPassword') })" required class="form-input" />
            </div>
          </div>
          <button type="submit" class="submit-button" :disabled="isChangingPwd">
            <span v-if="!isChangingPwd">{{ t('action.confirm') }}</span>
            <span v-else class="loading-spinner">
              <svg class="spinner-icon" viewBox="0 0 24 24">
                <circle class="spinner-circle" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" fill="none" />
              </svg>
            </span>
          </button>
        </form>
      </div>
    </div>

    <div class="copyright">{{ copyRight }}</div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, getCurrentInstance } from 'vue'
import { useRouter } from 'vue-router'
import md5 from 'js-md5'
import { t } from '@/i18n'
import ThemeSwitcher from '@/components/ThemeSwitcher.vue'

const router = useRouter()
const { proxy } = getCurrentInstance()

const username = ref('')
const password = ref('')
const isLoading = ref(false)
const platformName = computed(() => {
  const DEFAULT_PLATFORM_NAMES = ['智能终端管理平台', '智能终端管理系统', '智能盒子', '边缘智能中枢']
  const raw = window.getGlobalConfig?.()?.platformName
  if (!raw || DEFAULT_PLATFORM_NAMES.includes(raw)) {
    return t('system.defaultPlatformName')
  }
  return raw
})
const copyRight = ref(window.getGlobalConfig?.()?.copyRight || '')
const defaultLogo = new URL('@/assets/logo.png', import.meta.url).href
const logoSrc = ref(localStorage.getItem('logoUrl') || defaultLogo)

// ── Password change dialog state ──
const showPasswordDialog = ref(false)
const oldPwd = ref('')
const newPwd = ref('')
const confirmPwd = ref('')
const isChangingPwd = ref(false)
const savedToken = ref('')   // token from the login response, used for ChangePasswd

onMounted(() => {
  localStorage.removeItem('mtk')
  localStorage.removeItem('token')
  localStorage.removeItem('platformType')
})

const handleLogin = async () => {
  if (!username.value || !password.value) {
    proxy.$message.warning(t('validate.usernamePasswordRequired'))
    return
  }

  isLoading.value = true

  try {
    const params = {
      account: username.value,
      pwd: md5(password.value)
    }

    const res = await proxy.$API.dologin(params)
    const { resData } = res

    localStorage.removeItem('playedCameraList')
    localStorage.removeItem('dialog')
    localStorage.setItem('mtk', resData.mtk)
    localStorage.setItem('token', resData.mtk)
    localStorage.setItem('account', resData?.account || params.account || '')

    // ── SEC-001: Force password change when factory-default password is active ──
    if (resData.passwordChangeRequired) {
      savedToken.value = resData.mtk
      oldPwd.value = ''
      newPwd.value = ''
      confirmPwd.value = ''
      showPasswordDialog.value = true
      proxy.$message.warning(t('login.passwordChangeRequired'))
      return
    }

    localStorage.setItem('runMode', '0')

    // Check onboarding status before navigating to home
    try {
      const onboardingRes = await proxy.$API.queryOnboardingStatus({})
      if (onboardingRes?.resData?.onboardingCompleted) {
        localStorage.setItem('onboarding_completed', 'true')
        localStorage.removeItem('onboarding_active')
      } else {
        localStorage.removeItem('onboarding_completed')
        localStorage.setItem('onboarding_active', 'true')
      }
    } catch {
      localStorage.removeItem('onboarding_completed')
      localStorage.setItem('onboarding_active', 'true')
    }

    router.replace({ path: '/home' })
  } catch (error) {
    console.error('Login failed:', error)
  } finally {
    isLoading.value = false
  }
}

const handleChangePassword = async () => {
  if (!oldPwd.value || !newPwd.value) {
    proxy.$message.warning(t('validate.oldNewPasswordRequired'))
    return
  }
  if (newPwd.value !== confirmPwd.value) {
    proxy.$message.warning(t('validate.passwordMismatch'))
    return
  }

  isChangingPwd.value = true
  try {
    await proxy.$API.boxModifyPassword({
      mtk: savedToken.value,
      passwdOld: md5(oldPwd.value),
      passwdNew: md5(newPwd.value)
    })
    proxy.$message.success(t('login.passwordChanged'))
    showPasswordDialog.value = false
    // Clear session — user must re-login with new credentials
    localStorage.removeItem('mtk')
    localStorage.removeItem('token')
    username.value = ''
    password.value = ''
  } catch (error) {
    console.error('Password change failed:', error)
    proxy.$message.error(t('login.passwordChangeFailed'))
  } finally {
    isChangingPwd.value = false
  }
}
</script>

<style lang="scss" scoped>
.login-appearance {
  position: absolute;
  top: 20px;
  right: 24px;
  z-index: 2;
}
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 64px 24px;
  position: relative;
  color: var(--text-primary);
  background: var(--bg-primary);
}

.login-background {
  position: absolute;
  inset: 0;
  overflow: hidden;
  pointer-events: none;
}

.login-background::before, .login-background::after {
  content: '';
  position: absolute;
  width: 340px;
  height: 340px;
  border: 1px solid var(--border-color);
  border-radius: 44px;
  transform: rotate(30deg);
}

.login-background::before {
  left: -190px;
  top: 12%;
}

.login-background::after {
  right: -190px;
  bottom: 12%;
}

.login-card {
  position: relative;
  width: 100%;
  max-width: 420px;
  padding: 36px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  box-shadow: var(--shadow-md);
}

.logo-section {
  text-align: left;
  margin-bottom: 32px;
}

.logo-icon {
  width: 48px;
  height: 48px;
  object-fit: contain;
  display: block;
  margin-bottom: 20px;
  border-radius: 8px;
}

.logo-title {
  font-size: 25px;
  font-weight: 650;
  color: var(--text-primary);
  margin: 0 0 8px;
  line-height: 1.4;
  letter-spacing: -.5px;
  overflow-wrap: anywhere;
}

.logo-subtitle {
  font-size: 13px;
  color: var(--text-secondary);
  margin: 0;
  line-height: 1.6;
}

.login-form {
  margin: 0;
}

.form-group {
  margin-bottom: 20px;
}

.form-label {
  display: block;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-primary);
  margin-bottom: 8px;
}

.input-wrapper {
  position: relative;
}

.input-icon {
  position: absolute;
  left: 13px;
  top: 50%;
  transform: translateY(-50%);
  width: 18px;
  height: 18px;
  color: var(--text-secondary);
  pointer-events: none;
}

.form-input {
  width: 100%;
  min-height: 44px;
  padding: 11px 13px 11px 40px;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  font-size: 14px;
  font-family: inherit;
  line-height: 20px;
  color: var(--text-primary);
  background: var(--bg-white);
  transition: border-color .15s, box-shadow .15s;
}

.form-input::placeholder {
  color: var(--text-muted);
}

.form-input:hover {
  border-color: #bfc7d3;
}

.form-input:focus {
  outline: none;
  border-color: var(--primary-color);
  box-shadow: 0 0 0 3px var(--primary-soft-bg);
}

.submit-button {
  display: block;
  width: 100%;
  min-height: 44px;
  margin-top: 26px;
  padding: 11px 16px;
  background: var(--primary-button-bg, var(--primary-color));
  color: white;
  border: 1px solid var(--primary-button-bg, var(--primary-color));
  border-radius: 6px;
  font-size: 14px;
  line-height: 20px;
  font-weight: 500;
  font-family: inherit;
  cursor: pointer;
  transition: background .15s;
}

.submit-button:hover:not(:disabled) {
  background: var(--primary-dark);
  border-color: var(--primary-dark);
}

.submit-button:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 3px;
}

.submit-button:disabled {
  opacity: .65;
  cursor: not-allowed;
}

.loading-spinner {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}

.spinner-icon {
  width: 18px;
  height: 18px;
  animation: spin 1s linear infinite;
}

.spinner-circle {
  stroke-dasharray: 60;
  stroke-dashoffset: 45;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.copyright {
  position: absolute;
  bottom: 24px;
  left: 24px;
  right: 24px;
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.5;
  text-align: center;
}

.dialog-overlay {
  position: fixed;
  inset: 0;
  padding: 24px;
  overflow-y: auto;
  background: rgba(20, 24, 36, .5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.dialog-card {
  width: 100%;
  max-width: 440px;
  max-height: calc(100vh - 48px);
  overflow-y: auto;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 10px;
  padding: 28px;
  box-shadow: 0 16px 48px #20222d26;
}

.dialog-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.dialog-icon {
  width: 24px;
  height: 24px;
  color: var(--warning-color);
  flex-shrink: 0;
}

.dialog-title {
  font-size: 18px;
  font-weight: 600;
  line-height: 1.4;
  color: var(--text-primary);
  margin: 0;
}

.dialog-desc {
  font-size: 13px;
  color: var(--text-secondary);
  margin: 0 0 24px;
  line-height: 1.6;
}

@media (max-width: 480px) {
  .login-container {
    padding: 48px 16px 72px;
  }
  .login-card {
    padding: 28px 24px;
  }
  .logo-title {
    font-size: 23px;
  }
  .dialog-card {
    padding: 24px;
  }
}
</style>
