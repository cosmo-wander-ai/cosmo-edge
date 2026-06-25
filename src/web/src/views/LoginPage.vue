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

    // try {
    //   const modeRes = await proxy.$API.queryRunModeParam({})
    //   localStorage.setItem('runMode', modeRes.resData.runMode)
    // } catch {
      localStorage.setItem('runMode', '0')
    // }

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
</style>
