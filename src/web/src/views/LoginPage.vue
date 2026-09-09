<template>
  <div class="login-container">
    <div class="background-gradient"></div>
    
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
    localStorage.setItem('platformType', '15')
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
@import url('https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@300;400;500;600;700&display=swap');

.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 1.5rem;
  position: relative;
  overflow: hidden;
  font-family: 'Plus Jakarta Sans', sans-serif;
}

.background-gradient {
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, #1a365d 0%, #2b6cb0 50%, #4299e1 100%);
  
  &::before {
    content: '';
    position: absolute;
    top: -50%;
    left: -50%;
    width: 200%;
    height: 200%;
    background: radial-gradient(circle, rgba(255,255,255,0.1) 1px, transparent 1px);
    background-size: 50px 50px;
    animation: moveBackground 20s linear infinite;
  }
}

@keyframes moveBackground {
  0% { transform: translate(0, 0); }
  100% { transform: translate(50px, 50px); }
}

.login-card {
  position: relative;
  width: 100%;
  max-width: 440px;
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(20px);
  border-radius: 24px;
  padding: 1rem 2rem;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.3);
  animation: slideUp 0.6s ease-out;

  @media (prefers-reduced-motion: reduce) {
    animation: none;
  }
}

@keyframes slideUp {
  from {
    opacity: 0;
    transform: translateY(30px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.logo-section {
  text-align: center;
  margin-bottom: 1.5rem;
}

.logo-icon {
  width: 80px;
  height: 80px;
  object-fit: cover;
  margin: 0 auto 0.75rem;
  display: block;
  border-radius: 18px;
  box-shadow: 0 4px 16px rgba(29, 43, 58, 0.3);
}

.logo-title {
  font-size: 2rem;
  font-weight: 700;
  color: #1a202c;
  margin: 0 0 0.5rem;
}

.logo-subtitle {
  font-size: 0.95rem;
  color: #64748B;
  margin: 0;
  font-weight: 400;
}

.login-form {
  margin-bottom: 1rem;
}

.form-group {
  margin-bottom: 1.5rem;
}

.form-label {
  display: block;
  font-size: 0.875rem;
  font-weight: 600;
  color: #1a202c;
  margin-bottom: 0.5rem;
}

.input-wrapper {
  position: relative;
}

.input-icon {
  position: absolute;
  left: 1rem;
  top: 50%;
  transform: translateY(-50%);
  width: 20px;
  height: 20px;
  color: #94A3B8;
  pointer-events: none;
}

.form-input {
  width: 100%;
  padding: 0.875rem 1rem 0.875rem 3rem;
  border: 2px solid #E2E8F0;
  border-radius: 12px;
  font-size: 0.95rem;
  font-family: inherit;
  color: #1a202c;
  background: rgba(255, 255, 255, 0.8);
  transition: all 0.2s ease;

  &::placeholder {
    color: #94A3B8;
  }

  &:focus {
    outline: none;
    border-color: #3182ce;
    background: rgba(255, 255, 255, 1);
    box-shadow: 0 0 0 4px rgba(49, 130, 206, 0.1);
  }
}

.form-options {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.5rem;
}

.checkbox-label {
  display: flex;
  align-items: center;
  cursor: pointer;
  user-select: none;
}

.checkbox-input {
  width: 18px;
  height: 18px;
  margin-right: 0.5rem;
  cursor: pointer;
  accent-color: #3182ce;
}

.checkbox-text {
  font-size: 0.875rem;
  color: #475569;
}

.forgot-link {
  font-size: 0.875rem;
  color: #3182ce;
  text-decoration: none;
  font-weight: 600;
  transition: color 0.2s ease;

  &:hover {
    color: #2b6cb0;
  }
}

.submit-button {
  width: 100%;
  padding: 1rem;
  background: linear-gradient(135deg, #3182ce 0%, #4299e1 100%);
  color: white;
  border: none;
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 600;
  font-family: inherit;
  cursor: pointer;
  transition: all 0.2s ease;
  box-shadow: 0 4px 12px rgba(49, 130, 206, 0.3);

  &:hover:not(:disabled) {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(49, 130, 206, 0.4);
  }

  &:active:not(:disabled) {
    transform: translateY(0);
  }

  &:disabled {
    opacity: 0.7;
    cursor: not-allowed;
  }
}

.loading-spinner {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
}

.spinner-icon {
  width: 20px;
  height: 20px;
  animation: spin 1s linear infinite;
}

.spinner-circle {
  stroke-dasharray: 60;
  stroke-dashoffset: 45;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.copyright {
  position: absolute;
  bottom: 1.5rem;
  left: 50%;
  transform: translateX(-50%);
  font-size: 0.875rem;
  color: rgba(255, 255, 255, 0.9);
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.2);
  text-align: center;
  z-index: 10;
}

@media (max-width: 768px) {
  .login-card {
    padding: 2rem 1.5rem;
  }

  .logo-title {
    font-size: 1.75rem;
  }

  .copyright {
    font-size: 0.75rem;
    padding: 0 1rem;
  }
}

@media (max-width: 375px) {
  .login-container {
    padding: 1rem;
  }

  .login-card {
    padding: 1.5rem 1.25rem;
  }
}

// ── Password change dialog ──
.dialog-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  backdrop-filter: blur(4px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  animation: fadeIn 0.3s ease;
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

.dialog-card {
  width: 100%;
  max-width: 440px;
  background: rgba(255, 255, 255, 0.98);
  backdrop-filter: blur(20px);
  border-radius: 24px;
  padding: 2rem;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.3);
  animation: slideUp 0.4s ease-out;
}

.dialog-header {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}

.dialog-icon {
  width: 28px;
  height: 28px;
  color: #D97706;
  flex-shrink: 0;
}

.dialog-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: #1a202c;
  margin: 0;
}

.dialog-desc {
  font-size: 0.875rem;
  color: #64748B;
  margin: 0 0 1.5rem;
  line-height: 1.5;
}
</style>
