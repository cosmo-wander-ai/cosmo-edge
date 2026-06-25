<template>
  <teleport to="body">
    <div v-if="active && currentStep" class="onboarding-overlay">
      <!-- Spotlight mask -->
      <div class="onboarding-mask" :style="maskStyle" @click.stop></div>

      <!-- Highlight border -->
      <div v-if="highlightRect" class="onboarding-highlight" :style="highlightStyle"></div>

      <!-- Guide card -->
      <div class="onboarding-card" :style="cardStyle" :class="cardPlacement">
        <!-- Step progress -->
        <div class="card-progress">
          <div class="progress-steps">
            <span v-for="s in majorSteps" :key="s.id"
              class="progress-dot"
              :class="{ active: s.id === currentMajorStep, done: s.id < currentMajorStep }">
            </span>
          </div>
          <span class="progress-text">{{ t('onboarding.stepCounter', { current: stepIndex + 1, total: steps.length }) }}</span>
        </div>

        <!-- Step title -->
        <div class="card-title">
          <span class="step-badge">{{ currentStep.badge }}</span>
          {{ stepTitle }}
        </div>

        <!-- Step description -->
        <div class="card-desc">{{ stepDescription }}</div>

        <!-- Tip -->
        <div v-if="stepTip" class="card-tip">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" width="14" height="14">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          {{ stepTip }}
        </div>

        <!-- Actions -->
        <div class="card-actions">
          <button v-if="canSkip" class="btn-skip" @click="skipOnboarding">{{ t('onboarding.skip') }}</button>
          <button v-if="!currentStep.advanceOn" class="btn-next" @click="nextStep">
            {{ stepIndex >= steps.length - 1 ? t('onboarding.finish') : t('onboarding.next') }}
          </button>
        </div>
      </div>
    </div>
  </teleport>
</template>

<script setup>
import { ref, reactive, computed, watch, onMounted, onBeforeUnmount, nextTick, getCurrentInstance } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()
const router = useRouter()
const route = useRoute()

// ── State ──
const active = ref(false)
const stepIndex = ref(0)
const highlightRect = ref(null)
let resizeObserver = null
let mutationObserver = null
let pollTimer = null

const addLog = (msg, type = 'info') => {
  console.log(`[OnboardingGuide][${type}] ${msg}`)
}

const steps = reactive([
  // ━━ Access Video ━━
  {
    id: '1', major: 1, badge: '1',
    titleKey: 'onboarding.step1Title',
    descKey: 'onboarding.step1Desc',
    route: '/videoAccess',
    selector: '#onboarding-add-channel',
    placement: 'bottom',
    advanceOn: 'click'
  },
  {
    id: '2', major: 1, badge: '2',
    titleKey: 'onboarding.step2Title',
    descKey: 'onboarding.step2Desc',
    selector: '#onboarding-channel-type',
    placement: 'right',
    waitForSelector: true,
    advanceOn: 'dom-appear',
    advanceTrigger: '#onboarding-upload-video'
  },
  {
    id: '3', major: 1, badge: '3',
    titleKey: 'onboarding.step3Title',
    descKey: 'onboarding.step3Desc',
    selector: '#onboarding-channel-name',
    placement: 'right',
    autoFill: { target: 'channelName', text: '安全帽检测演示', delay: 80 },
    advanceOn: 'autofill'
  },
  {
    id: '4', major: 1, badge: '4',
    titleKey: 'onboarding.step4Title',
    descKey: 'onboarding.step4Desc',
    tipKey: 'onboarding.step4Tip',
    selector: '#onboarding-upload-video',
    placement: 'right',
    waitForSelector: true,
    advanceOn: null
  },
  {
    id: '5', major: 1, badge: '5',
    titleKey: 'onboarding.step5Title',
    descKey: 'onboarding.step5Desc',
    selector: '#onboarding-save-channel',
    placement: 'top',
    waitForSelector: true,
    advanceOn: 'click'
  },

  // ━━ Configure Task ━━
  {
    id: '6', major: 2, badge: '6',
    titleKey: 'onboarding.step6Title',
    descKey: 'onboarding.step6Desc',
    route: '/videoAccess',
    selector: '#onboarding-allocate-btn',
    placement: 'top',
    waitForSelector: true,
    advanceOn: 'click'
  },
  {
    id: '7', major: 2, badge: '7',
    titleKey: 'onboarding.step7Title',
    descKey: 'onboarding.step7Desc',
    selector: '#onboarding-algorithm-tree',
    placement: 'right',
    waitForSelector: true,
    advanceOn: 'click'
  },
  {
    id: '8', major: 2, badge: '8',
    titleKey: 'onboarding.step8Title',
    descKey: 'onboarding.step8Desc',
    selector: '#onboarding-add-area',
    placement: 'left',
    waitForSelector: true,
    advanceOn: 'click'
  },
  {
    id: '9', major: 2, badge: '9',
    titleKey: 'onboarding.step9Title',
    descKey: 'onboarding.step9Desc',
    selector: '#onboarding-area-dialog .el-input',
    placement: 'right',
    waitForSelector: true,
    autoFill: { target: 'areaName', text: '检测区域1', delay: 80 },
    advanceOn: 'autofill'
  },
  {
    id: '10', major: 2, badge: '10',
    titleKey: 'onboarding.step10Title',
    descKey: 'onboarding.step10Desc',
    selector: '#onboarding-area-dialog .mv-el-button',
    placement: 'bottom',
    waitForSelector: true,
    advanceOn: 'click'
  },
  {
    id: '11', major: 2, badge: '11',
    titleKey: 'onboarding.step11Title',
    descKey: 'onboarding.step11Desc',
    tipKey: 'onboarding.step11Tip',
    selector: '#onboarding-detection-canvas',
    placement: 'right',
    waitForSelector: true,
    advanceOn: null
  },
  {
    id: '12', major: 2, badge: '12',
    titleKey: 'onboarding.step12Title',
    descKey: 'onboarding.step12Desc',
    selector: '#onboarding-save-service',
    placement: 'bottom',
    waitForSelector: true,
    advanceOn: 'click'
  },

  // ━━ Start Task ━━
  {
    id: '13', major: 3, badge: '13',
    titleKey: 'onboarding.step13Title',
    descKey: 'onboarding.step13Desc',
    route: '/videoAccess',
    selector: '#onboarding-task-switch',
    placement: 'top',
    waitForSelector: true,
    advanceOn: null
  },

  // ━━ Live Display ━━
  {
    id: '14', major: 4, badge: '14',
    titleKey: 'onboarding.step14Title',
    descKey: 'onboarding.step14Desc',
    selector: '#onboarding-menu-bigScreen-warnningScreen',
    placement: 'right',
    advanceOn: 'click'
  },

  // ━━ Play Channel ━━
  {
    id: '15', major: 5, badge: '15',
    titleKey: 'onboarding.step15Title',
    descKey: 'onboarding.step15Desc',
    route: '/bigScreen/warnningScreen',
    selector: '#onboarding-camera-toggle',
    placement: 'right',
    waitForSelector: true,
    advanceOn: 'click'
  },
  {
    id: '16', major: 5, badge: '16',
    titleKey: 'onboarding.step16Title',
    descKey: 'onboarding.step16Desc',
    tipKey: 'onboarding.step16Tip',
    selector: '#onboarding-camera-tree',
    placement: 'right',
    waitForSelector: true,
    advanceOn: null
  },

  // ━━ Algorithm Overlay ━━
  {
    id: '17', major: 6, badge: '17',
    titleKey: 'onboarding.step17Title',
    descKey: 'onboarding.step17Desc',
    selector: '#onboarding-overlay-select',
    placement: 'bottom',
    waitForSelector: true,
    advanceOn: null
  }
])

// ── Major steps ──
const majorSteps = [
  { id: 1, labelKey: 'onboarding.phaseAccessVideo' },
  { id: 2, labelKey: 'onboarding.phaseConfigureTask' },
  { id: 3, labelKey: 'onboarding.phaseStartTask' },
  { id: 4, labelKey: 'onboarding.phaseLiveDisplay' },
  { id: 5, labelKey: 'onboarding.phasePlayChannel' },
  { id: 6, labelKey: 'onboarding.phaseAlgorithmOverlay' }
]

const currentStep = computed(() => steps[stepIndex.value])
const currentMajorStep = computed(() => currentStep.value?.major || 1)
const canSkip = computed(() => true)

// i18n-resolved display values
const stepTitle = computed(() => currentStep.value?.titleKey ? t(currentStep.value.titleKey) : '')
const stepDescription = computed(() => currentStep.value?.descKey ? t(currentStep.value.descKey) : '')
const stepTip = computed(() => currentStep.value?.tipKey ? t(currentStep.value.tipKey) : null)

// ── Spotlight mask clip-path ──
const maskStyle = computed(() => {
  if (!highlightRect.value) {
    return { clipPath: 'none' }
  }
  const r = highlightRect.value
  const pad = 6
  const x1 = r.left - pad
  const y1 = r.top - pad
  const x2 = r.right + pad
  const y2 = r.bottom + pad
  return {
    clipPath: `polygon(
      0% 0%, 100% 0%, 100% 100%, 0% 100%, 0% 0%,
      ${x1}px ${y1}px, ${x1}px ${y2}px, ${x2}px ${y2}px, ${x2}px ${y1}px, ${x1}px ${y1}px
    )`
  }
})

const highlightStyle = computed(() => {
  if (!highlightRect.value) return { display: 'none' }
  const r = highlightRect.value
  const pad = 6
  return {
    top: (r.top - pad) + 'px',
    left: (r.left - pad) + 'px',
    width: (r.width + pad * 2) + 'px',
    height: (r.height + pad * 2) + 'px'
  }
})

const cardPlacement = computed(() => `card-${currentStep.value?.placement || 'bottom'}`)

const cardStyle = computed(() => {
  if (!highlightRect.value) {
    return { top: '50%', left: '50%', transform: 'translate(-50%, -50%)' }
  }
  const r = highlightRect.value
  const gap = 16
  const cardW = 360
  const cardH = 220
  const vw = window.innerWidth
  const vh = window.innerHeight
  const margin = 12

  let top = null, left = null, right = null
  const placement = currentStep.value?.placement || 'bottom'

  switch (placement) {
    case 'bottom':
      top = r.bottom + gap
      left = r.left
      break
    case 'top':
      top = r.top - gap - cardH
      left = r.left
      break
    case 'right':
      top = r.top
      left = r.right + gap
      break
    case 'left':
      top = r.top
      left = r.left - gap - cardW
      break
  }

  if (left !== null) {
    if (left + cardW > vw - margin) left = vw - cardW - margin
    if (left < margin) left = margin
  }

  if (top !== null) {
    if (top + cardH > vh - margin) top = vh - cardH - margin
    if (top < margin) top = margin
  }

  const style = {}
  if (top !== null) style.top = top + 'px'
  if (left !== null) style.left = left + 'px'
  if (right !== null) style.right = right + 'px'

  return style
})

// ── Core logic ──
const locateTarget = () => {
  const step = currentStep.value
  if (!step?.selector) {
    highlightRect.value = null
    return
  }
  const el = document.querySelector(step.selector)
  if (el) {
    const rect = el.getBoundingClientRect()
    highlightRect.value = {
      top: rect.top,
      left: rect.left,
      right: rect.right,
      bottom: rect.bottom,
      width: rect.width,
      height: rect.height
    }
    stopPolling()
    addLog(`Located element: ${step.selector} (${Math.round(rect.width)}x${Math.round(rect.height)})`)
  } else if (step.waitForSelector) {
    highlightRect.value = null
    startPolling()
  } else {
    highlightRect.value = null
    addLog(`Element not found: ${step.selector}`, 'error')
  }
}

const startPolling = () => {
  stopPolling()
  pollTimer = setInterval(() => {
    locateTarget()
  }, 500)
}

const stopPolling = () => {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// ── Typewriter animation ──
const typewriterFill = async (step) => {
  if (!step.autoFill) return

  await new Promise(resolve => {
    const check = () => {
      const el = document.querySelector(step.selector)
      if (el) { resolve(); return }
      setTimeout(check, 300)
    }
    check()
  })

  await new Promise(r => setTimeout(r, 500))

  const { text, delay } = step.autoFill
  const inputEl = document.querySelector(step.selector + ' input') ||
                  document.querySelector(step.selector)

  if (!inputEl) return

  const nativeInputValueSetter = Object.getOwnPropertyDescriptor(
    window.HTMLInputElement.prototype, 'value'
  )?.set

  inputEl.focus()

  for (let i = 0; i <= text.length; i++) {
    if (nativeInputValueSetter) {
      nativeInputValueSetter.call(inputEl, text.substring(0, i))
    } else {
      inputEl.value = text.substring(0, i)
    }
    inputEl.dispatchEvent(new Event('input', { bubbles: true }))
    await new Promise(r => setTimeout(r, delay))
  }

  inputEl.dispatchEvent(new Event('change', { bubbles: true }))

  if (step.advanceOn === 'autofill') {
    addLog('Autofill complete, auto-advancing', 'success')
    await new Promise(r => setTimeout(r, 500))
    nextStep()
  }
}

// ── Auto-advance ──
let autoAdvanceCleanup = null

const setupAutoAdvance = (step) => {
  cleanupAutoAdvance()
  if (!step.advanceOn || step.advanceOn === 'autofill') return

  const waitAndAttach = () => {
    const el = document.querySelector(step.selector)
    if (!el) {
      setTimeout(waitAndAttach, 500)
      return
    }

    if (step.advanceOn === 'click') {
      const handler = () => {
        addLog(`User clicked ${step.selector}, auto-advancing`, 'success')
        el.removeEventListener('click', handler, true)
        setTimeout(() => nextStep(), 600)
      }
      el.addEventListener('click', handler, true)
      autoAdvanceCleanup = () => el.removeEventListener('click', handler, true)
      addLog(`  Click listener bound: ${step.selector}`)
    }

    if (step.advanceOn === 'dom-appear') {
      const triggerSelector = step.advanceTrigger
      addLog(`  Watching for DOM element: ${triggerSelector}`)
      const checkInterval = setInterval(() => {
        const triggerEl = document.querySelector(triggerSelector)
        if (triggerEl) {
          clearInterval(checkInterval)
          addLog(`Element ${triggerSelector} appeared, auto-advancing`, 'success')
          setTimeout(() => nextStep(), 600)
        }
      }, 300)
      autoAdvanceCleanup = () => clearInterval(checkInterval)
    }

    if (step.advanceOn === 'select') {
      const getSelectText = (selectEl) => {
        if (!selectEl) return ''
        const inner = selectEl.querySelector('.el-input__inner')
        if (inner?.value) return inner.value.trim()
        const selected = selectEl.querySelector('.el-select__selected-item')
        if (selected?.textContent) return selected.textContent.trim()
        const input = selectEl.querySelector('input')
        if (input?.value) return input.value.trim()
        const wrapper = selectEl.querySelector('.el-select__wrapper') ||
                       selectEl.querySelector('.el-input')
        if (wrapper?.innerText) return wrapper.innerText.trim()
        return selectEl.innerText?.trim() || ''
      }

      const monitorEl = step.advanceTarget
        ? document.querySelector(step.advanceTarget)
        : el
      if (!monitorEl) {
        addLog(`  advanceTarget ${step.advanceTarget} not found, polling`, 'error')
        setTimeout(waitAndAttach, 500)
        return
      }
      const initialText = getSelectText(monitorEl)
      addLog(`  Watching select change: initial="${initialText}"`)
      const checkInterval = setInterval(() => {
        const currentEl = step.advanceTarget
          ? document.querySelector(step.advanceTarget)
          : document.querySelector(step.selector)
        if (!currentEl) return
        const currentText = getSelectText(currentEl)
        const matched = step.advanceValue
          ? currentText.includes(step.advanceValue)
          : currentText !== initialText && currentText !== ''
        if (matched) {
          clearInterval(checkInterval)
          addLog(`Select value changed to "${currentText}", auto-advancing`, 'success')
          setTimeout(() => nextStep(), 600)
        }
      }, 300)
      autoAdvanceCleanup = () => clearInterval(checkInterval)
    }
  }
  waitAndAttach()
}

const cleanupAutoAdvance = () => {
  if (autoAdvanceCleanup) {
    autoAdvanceCleanup()
    autoAdvanceCleanup = null
  }
}

// ── Step navigation ──
const navigateAndLocate = async () => {
  const step = currentStep.value
  if (!step) return

  cleanupAutoAdvance()
  addLog(`Step ${step.id}: ${t(step.titleKey)} [${step.advanceOn || 'manual'}]`)

  if (step.route && route.path !== step.route) {
    addLog(`  Route: ${route.path} -> ${step.route}`)
    await router.push(step.route)
    await nextTick()
    await new Promise(r => setTimeout(r, 800))
  }

  await nextTick()
  locateTarget()

  setupAutoAdvance(step)

  if (step.autoFill) {
    addLog(`  Autofill: ${step.autoFill.text}`)
    await new Promise(r => setTimeout(r, 600))
    typewriterFill(step)
  }
}

const nextStep = async () => {
  cleanupAutoAdvance()
  if (stepIndex.value >= steps.length - 1) {
    await completeOnboarding()
    return
  }
  stepIndex.value++
  addLog(`Next -> stepIndex=${stepIndex.value}`)
  await navigateAndLocate()
}

const skipOnboarding = async () => {
  await completeOnboarding()
}

const completeOnboarding = async () => {
  active.value = false
  stopPolling()
  cleanupAutoAdvance()
  localStorage.setItem('onboarding_completed', 'true')
  try {
    await proxy.$API.completeOnboarding({})
  } catch { /* ignore */ }
}

const startOnboarding = () => {
  addLog('startOnboarding() called, launching guide!', 'success')
  stepIndex.value = 0
  active.value = true
  navigateAndLocate()
}

// ── Resize / scroll tracking ──
const handleResize = () => {
  if (active.value) {
    locateTarget()
  }
}

const handleScroll = () => {
  if (active.value) {
    locateTarget()
  }
}

// ── Lifecycle ──
const DEBUG_FORCE_ONBOARDING = false

onMounted(async () => {
  window.addEventListener('resize', handleResize)
  window.addEventListener('scroll', handleScroll, true)

  addLog(`Component mounted, route: ${route.path}`)
  addLog(`DEBUG_FORCE_ONBOARDING = ${DEBUG_FORCE_ONBOARDING}`)

  if (DEBUG_FORCE_ONBOARDING) {
    addLog('DEBUG mode: force-start onboarding', 'success')
    localStorage.removeItem('onboarding_completed')
    await new Promise(r => setTimeout(r, 1500))
    startOnboarding()
    return
  }

  // ── Production logic ──
  const completed = localStorage.getItem('onboarding_completed')
  const shouldStart = localStorage.getItem('onboarding_active')
  addLog(`localStorage: completed=${completed}, active=${shouldStart}`)

  if (completed === 'true') {
    addLog('onboarding_completed=true, skipping')
    return
  }

  if (shouldStart === 'true') {
    addLog('onboarding_active=true, launching...')
    localStorage.removeItem('onboarding_active')
    await new Promise(r => setTimeout(r, 1000))
    startOnboarding()
    return
  }

  addLog('No flag set, falling back to API query...')
  try {
    const res = await proxy.$API.queryOnboardingStatus({})
    addLog(`API response: ${JSON.stringify(res?.resData)}`)
    if (res?.resData && !res.resData.onboardingCompleted) {
      addLog('API confirms onboarding not completed, launching...')
      await new Promise(r => setTimeout(r, 1000))
      startOnboarding()
    } else {
      addLog('API reports completed, not launching')
    }
  } catch (err) {
    addLog(`API query failed: ${err?.message || err}`, 'error')
  }
})

// Cross-tab storage listener (LoginPage sets the flag)
const handleStorageChange = (e) => {
  if (e.key === 'onboarding_active' && e.newValue === 'true' && !active.value) {
    localStorage.removeItem('onboarding_active')
    setTimeout(() => startOnboarding(), 1000)
  }
}
if (typeof window !== 'undefined') {
  window.addEventListener('storage', handleStorageChange)
}

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('scroll', handleScroll, true)
  if (typeof window !== 'undefined') {
    window.removeEventListener('storage', handleStorageChange)
  }
  stopPolling()
  cleanupAutoAdvance()
})

// Re-locate on route change
watch(() => route.path, () => {
  if (active.value) {
    nextTick(() => {
      setTimeout(locateTarget, 500)
    })
  }
})

defineExpose({ startOnboarding })
</script>

<style lang="scss" scoped>
.onboarding-overlay {
  position: fixed;
  inset: 0;
  z-index: 99999;
  pointer-events: none;
  contain: layout style;
}

.onboarding-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  pointer-events: auto;
  transition: clip-path 0.15s ease;
  z-index: 100000;
  will-change: clip-path;
}

.onboarding-highlight {
  position: fixed;
  border: 2px solid #4299e1;
  border-radius: 6px;
  box-shadow: 0 0 0 4px rgba(66, 153, 225, 0.25),
              0 0 20px rgba(66, 153, 225, 0.15);
  pointer-events: none;
  z-index: 100001;
  transition: all 0.35s ease;
}

.onboarding-card {
  position: fixed;
  width: 360px;
  max-width: calc(100vw - 32px);
  background: linear-gradient(145deg, #1a2332 0%, #1e2d40 100%);
  border: 1px solid rgba(66, 153, 225, 0.3);
  border-radius: 14px;
  padding: 20px;
  pointer-events: auto;
  z-index: 100002;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.4), 0 0 40px rgba(66, 153, 225, 0.1);
  transition: all 0.35s ease;
  animation: card-enter 0.4s ease;
}

@keyframes card-enter {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}

.card-progress {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 14px;
}

.progress-steps {
  display: flex;
  gap: 6px;
}

.progress-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.15);
  transition: all 0.3s;

  &.active {
    background: #4299e1;
    box-shadow: 0 0 8px rgba(66, 153, 225, 0.5);
    transform: scale(1.2);
  }

  &.done {
    background: #48bb78;
  }
}

.progress-text {
  font-size: 12px;
  color: rgba(255, 255, 255, 0.4);
}

.card-title {
  font-size: 16px;
  font-weight: 600;
  color: #fff;
  margin-bottom: 8px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.step-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%);
  border-radius: 8px;
  font-size: 14px;
  flex-shrink: 0;
}

.card-desc {
  font-size: 14px;
  color: rgba(255, 255, 255, 0.75);
  line-height: 1.6;
  margin-bottom: 12px;
}

.card-tip {
  display: flex;
  align-items: flex-start;
  gap: 6px;
  font-size: 12px;
  color: rgba(255, 200, 50, 0.8);
  background: rgba(255, 200, 50, 0.08);
  border: 1px solid rgba(255, 200, 50, 0.15);
  border-radius: 8px;
  padding: 8px 10px;
  margin-bottom: 14px;

  svg {
    flex-shrink: 0;
    margin-top: 1px;
    color: rgba(255, 200, 50, 0.8);
  }
}

.card-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
}

.btn-skip {
  padding: 8px 16px;
  border: none;
  border-radius: 8px;
  font-size: 13px;
  cursor: pointer;
  background: rgba(255, 255, 255, 0.08);
  color: rgba(255, 255, 255, 0.5);
  transition: all 0.2s;

  &:hover {
    background: rgba(255, 255, 255, 0.15);
    color: rgba(255, 255, 255, 0.8);
  }
}

.btn-next {
  padding: 8px 20px;
  border: none;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%);
  color: #fff;
  transition: all 0.2s;
  box-shadow: 0 4px 12px rgba(66, 153, 225, 0.3);

  &:hover {
    transform: translateY(-1px);
    box-shadow: 0 6px 16px rgba(66, 153, 225, 0.4);
  }
}
</style>

<!-- Unscoped: raise Element Plus popper/dropdown z-index above the overlay -->
<style lang="scss">
.onboarding-type-popper {
  z-index: 100010 !important;

  .el-select-dropdown__item:nth-child(4) {
    background: rgba(66, 153, 225, 0.15) !important;
    color: #4299e1 !important;
    font-weight: 600 !important;
    border-left: 3px solid #4299e1;
    position: relative;

    &::after {
      content: '\2190 Please select this item';
      position: absolute;
      right: 12px;
      font-size: 11px;
      color: #4299e1;
      font-weight: 400;
    }
  }
}
.custom-select-popper2 {
  z-index: 100010 !important;
}
</style>
