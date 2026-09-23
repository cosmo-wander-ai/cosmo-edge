<template>
  <div ref="videoContainer" class="video-container" @mouseover="handleControls('over')" @mouseleave="handleControls('leave')">
    <video v-if="isShowVideo" ref="video" class="video" muted>
      Sorry, your browser doesn't support embedded videos.
    </video>

    <div v-if="isShowLoading" class="loading-spinner"></div>

    <transition name="fade">
      <div class="video-top-control">
        <div class="camera-name">{{ t('event.currentSelected') }}{{ localeColon }}{{ cameraName }}</div>
        <div>
          <span>{{ t('event.algorithmOverlay') }}{{ localeColon }}</span>
          <el-select v-model="algorithmId" popper-class="custom-select-popper2" class="overlay-select" size="small" id="onboarding-overlay-select" @change="overlayAlgorithmChange">
            <el-option v-for="item in algorithmOverlayList" :label="resolveResourceAlgorithmName(item)" :value="item.algorithmId" :key="item.algorithmId"></el-option>
          </el-select>
        </div>
        <div>
          <img v-if="isFullScreen !== null" class="icon" src="@/assets/video_zoom_out.png" @click="handleExitFullScreen">
          <img v-else class="icon" src="@/assets/video_zoom_in.png" @click="handleFullScreen">
          <img class="icon" src="@/assets/video_close.png" @click="stopPreview">
        </div>
      </div>
    </transition>

    <div v-if="isShowStopPreview" class="stop-preview">
      <img src="@/assets/big_screen_no_camera.png" alt />
      <span>{{ t('event.noVideoSignal') }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, shallowRef, nextTick, watch, onBeforeUnmount, onMounted, getCurrentInstance } from 'vue'
import flvjs from 'flv.js'
import { v4 as uuidv4 } from 'uuid'
import { createWhepPlayer } from '@/utils/whepPlayer'
import { normalizeApiError } from '@/utils/apiError'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const props = defineProps({
  channelId: String,
  runAlgorithmId: String,
  cameraName: String,
  videoCodeFormat: String,
  isOverlay: Boolean,
  index: Number,
  isFullScreen: [Number, null],
  taskList: Array
})

const emit = defineEmits(['runAlgorithmIdChange', 'stop', 'fullScreen'])

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const deviceType = localStorage.getItem('deviceType')
const player = shallowRef(null)
const webRtcPlayer = shallowRef(null)
const hideControl = ref(true)
const isShowStopPreview = ref(false)
const isShowVideo = ref(true)
const timers = ref(null)
const isShowLoading = ref(false)
const restartInterval = ref(null)
const algorithmId = ref('')
const algorithmOverlayList = ref([])
const flvRetryCount = ref(0)
const flvRetryTimer = ref(null)
const chaseFrameTimer = ref(null)
const loadingFallbackTimer = ref(null)
const video = ref(null)
const videoContainer = ref(null)
let _onCanPlay = null
let _onScriptData = null
let _onError = null
let activeStreamSession = null
let pendingStreamSession = null
let latestDesiredSession = null
let streamRequestGeneration = 0
let streamSwitchQueue = Promise.resolve()
const activeHeartbeatRequests = new WeakSet()
let isUnmounted = false
let playbackAttempt = null
let playbackRevision = 0
let releaseRetiringStopWait = null
const retiringStopsByKey = new Map()
// Backend expires a viewer after a 10s threshold fails six 10s watchdog checks.
const kViewerWatchdogGraceMs = 80000

const sameStreamKey = (left, right) => {
  return Boolean(left && right) &&
    left.channelId === right.channelId &&
    left.algorithmId === right.algorithmId
}

const clearPendingSession = (session) => {
  if (pendingStreamSession === session) {
    pendingStreamSession = null
  }
}

const createPlaybackAttempt = () => {
  return {
    revision: ++playbackRevision,
    cancelled: false,
    reason: null,
    cancelHandler: null,
    cancel(reason = new Error('Video playback setup cancelled')) {
      if (this.cancelled) return
      this.cancelled = true
      this.reason = reason
      const handler = this.cancelHandler
      this.cancelHandler = null
      if (handler) handler(reason)
    }
  }
}

const createPlaybackError = (error, attempt) => {
  const playbackError = new Error(
    error?.message || `Video playback failed: ${String(error)}`
  )
  playbackError.name = error?.name || playbackError.name
  playbackError.cause = error
  playbackError.playbackSuperseded = attempt.cancelled ||
    playbackRevision !== attempt.revision
  return playbackError
}

const isPlaybackSuperseded = (error) => {
  return error?.playbackSuperseded === true
}

const clearHeartbeat = () => {
  if (timers.value) {
    clearInterval(timers.value)
    timers.value = null
  }
}

const getStreamKey = (session) => {
  return `${session.channelId}\u0000${session.algorithmId || ''}`
}

const stopStreamSession = (session) => {
  if (!session?.channelId) return Promise.resolve()
  if (session.stopPromise) return session.stopPromise

  const key = getStreamKey(session)
  const previousBarrier = retiringStopsByKey.get(key) || Promise.resolve()
  session.stopPromise = previousBarrier.then(async () => {
    try {
      await $API.boxStreamStop({
        channelId: session.channelId,
        algorithmId: session.algorithmId,
        previewSessionId: session.previewSessionId
      })
      return true
    } catch (error) {
      // A session-scoped Stop cannot release a replacement acquisition. A
      // failed request still expires through the backend's session watchdog.
      console.warn('[Video] Failed to stop stream session:', error)
      return false
    }
  })
  let retiringBarrier = session.stopPromise.then((stopped) => {
    if (!stopped && !session.sessionScoped) {
      return new Promise((resolve) => setTimeout(resolve, kViewerWatchdogGraceMs))
    }
  })
  retiringBarrier = retiringBarrier.finally(() => {
    if (retiringStopsByKey.get(key) === retiringBarrier) {
      retiringStopsByKey.delete(key)
    }
  })
  retiringStopsByKey.set(key, retiringBarrier)
  return session.stopPromise
}

const waitForRetiringStop = async (session) => {
  const barrier = retiringStopsByKey.get(getStreamKey(session))
  if (!barrier) return
  let releaseWait = null
  const superseded = new Promise((resolve) => {
    releaseWait = resolve
    releaseRetiringStopWait = resolve
  })
  await Promise.race([barrier, superseded])
  if (releaseRetiringStopWait === releaseWait) releaseRetiringStopWait = null
}

const getstreamkeepalive = async () => {
  const session = activeStreamSession
  if (!session || activeHeartbeatRequests.has(session)) return

  activeHeartbeatRequests.add(session)
  try {
    await $API.boxStreamKeepAlive({
      channelId: session.channelId,
      algorithmId: session.algorithmId,
      previewSessionId: session.previewSessionId
    })
  } catch (error) {
    // A reconnect can create a new viewer with the same channel/algorithm
    // while an earlier heartbeat is still in flight. Object identity keeps a
    // late failure from tearing down that replacement viewer.
    if (activeStreamSession !== session) return

    console.warn('[Video] Stream keepalive failed:', error)
    const code = String(normalizeApiError(error).code)
    const algorithmRetired = Boolean(session.algorithmId) &&
      (code === '12290' || code === '10027') // ActionStop / TaskNotExist
    const viewerMissing = code === '12802' || code === '12303' || code === '12800'
    const channelMissing = code === '10024'
    if (!algorithmRetired && !viewerMissing && !channelMissing) {
      // A transient network/source error must not erase the user's algorithm
      // selection. Keep the current player and retry on the next heartbeat.
      if (code === '10005' || normalizeApiError(error).status === 401) clearHeartbeat()
      return
    }
    clearHeartbeat()
    activeStreamSession = null
    // Retiring the old key must not consume the raw handoff's grace window.
    // Session identity makes a delayed Stop harmless to a newer acquisition.
    void stopStreamSession(session)
    if (isUnmounted) return

    // The Stop request can finish after a replacement stream has committed and
    // cleared pendingStreamSession. Re-check both active identity and the latest
    // desired key before changing the selection.
    if (activeStreamSession || pendingStreamSession ||
        !sameStreamKey(latestDesiredSession, session)) return

    if (algorithmRetired || channelMissing) {
      algorithmId.value = ''
      emit('runAlgorithmIdChange', {
        channelId: channelMissing ? '' : session.channelId,
        runAlgorithmId: '',
        retiredAlgorithmId: algorithmRetired ? session.algorithmId : undefined,
        index: props.index
      })
    } else {
      getrequestLiveStream('heartbeat')
    }
  } finally {
    activeHeartbeatRequests.delete(session)
  }
}

const sendHeartBeat = (time) => {
  clearHeartbeat()
  void getstreamkeepalive()
  const interval = Number(time)
  if (!Number.isFinite(interval) || interval <= 0) return

  timers.value = setInterval(() => {
    getstreamkeepalive()
  }, interval)
}

const startProvisionalHeartbeat = (session) => {
  let timer = null
  let stopped = false
  let inFlight = false
  let rejectFailure = null
  const failure = new Promise((_, reject) => {
    rejectFailure = reject
  })
  const stop = () => {
    stopped = true
    if (timer) clearInterval(timer)
    timer = null
  }
  const pulse = async () => {
    if (stopped || inFlight) return
    inFlight = true
    try {
      await $API.boxStreamKeepAlive({
        channelId: session.channelId,
        algorithmId: session.algorithmId,
        previewSessionId: session.previewSessionId
      })
    } catch (error) {
      if (!stopped) {
        const code = String(normalizeApiError(error).code)
        if (!['12290', '10027', '10024', '12800', '12802', '12303', '10005'].includes(code) &&
            normalizeApiError(error).status !== 401) return
        stop()
        if (playbackAttempt) playbackAttempt.cancel(error)
        rejectFailure(error)
      }
    } finally {
      inFlight = false
    }
  }

  void pulse()
  const interval = Number(session.keepAliveInterval)
  if (Number.isFinite(interval) && interval > 0) {
    timer = setInterval(pulse, interval)
  }
  return { failure, stop }
}

const buildStreamSession = (session, stream) => {
  const domain = window.location.hostname
  const streamScheme = window.location.protocol === 'https:' ? 'https' : 'http'
  const protocol = stream.protocol || 'httpflv'
  const fallbackUrl = stream.flvUrl
    ? `${streamScheme}://${domain}:${stream.httpPort || 8080}${stream.flvUrl}`
    : `${streamScheme}://${domain}:${stream.port}${stream.url}`
  const primaryUrl = protocol === 'webrtc'
    ? `${streamScheme}://${domain}:${stream.rtcApiPort || stream.port || 1985}${stream.webrtcUrl || stream.url}`
    : `${streamScheme}://${domain}:${stream.httpPort || stream.port}${stream.flvUrl || stream.url}`

  return {
    channelId: session.channelId,
    algorithmId: session.algorithmId,
    previewSessionId: session.previewSessionId,
    sessionScoped: stream.previewSessionId === session.previewSessionId,
    playbackProtocol: protocol,
    sourceUrl: primaryUrl,
    fallbackFlvUrl: fallbackUrl,
    keepAliveInterval: Number(stream.keepAliveInterval) * 1000
  }
}

const restoreParentSelection = (session) => {
  latestDesiredSession = session
  algorithmId.value = session.algorithmId || ''
  if (props.channelId === session.channelId &&
      (props.runAlgorithmId || '') === algorithmId.value) {
    return
  }
  emit('runAlgorithmIdChange', {
    channelId: session.channelId,
    runAlgorithmId: algorithmId.value,
    index: props.index
  })
}

const performLiveStreamRequest = async (session, generation, reason) => {
  if (isUnmounted || generation !== streamRequestGeneration) return
  const previousSession = activeStreamSession

  // Rebuilding the player for the same backend viewer must not call
  // ViewerCreate/ViewerDelete again; those APIs change a reference count.
  if (sameStreamKey(previousSession, session)) {
    const shouldRebuildPlayer = reason === 'timer' || reason === 'flv-retry'
    if (!shouldRebuildPlayer) {
      clearPendingSession(session)
      isShowLoading.value = false
      isShowStopPreview.value = false
      return
    }
    try {
      await playVideo(previousSession)
      if (activeStreamSession === previousSession) {
        clearPendingSession(session)
        isShowStopPreview.value = false
        return
      }
      // The viewer can be invalidated by a heartbeat failure while playback is
      // being rebuilt. Fall through and create a replacement viewer once.
      destroyVideo()
    } catch (error) {
      if (isUnmounted || generation !== streamRequestGeneration) return
      if (isPlaybackSuperseded(error)) {
        clearPendingSession(session)
        return
      }
      console.warn('[Video] Failed to rebuild the current stream player:', error)
      destroyVideo()
      if (activeStreamSession === previousSession) {
        isShowLoading.value = false
        if (reason === 'flv-retry') {
          clearHeartbeat()
          activeStreamSession = null
          await stopStreamSession(previousSession)
          if (isUnmounted || generation !== streamRequestGeneration ||
              !sameStreamKey(latestDesiredSession, session)) return
        } else {
          clearPendingSession(session)
          if (reason === 'timer') {
            scheduleFlvPlayerRetry(previousSession)
          } else {
            isShowStopPreview.value = true
          }
          return
        }
      }
      // A concurrent heartbeat failure retired this viewer. Continue below and
      // create one replacement after that single Stop request has completed.
    }
  }

  if (isUnmounted || generation !== streamRequestGeneration) return

  isShowLoading.value = true
  isShowStopPreview.value = false
  flvRetryCount.value = 0
  let candidateViewerOwned = false
  let playerWasReplaced = false
  let provisionalHeartbeat = null

  const releaseCandidateViewer = async () => {
    if (!candidateViewerOwned) return
    candidateViewerOwned = false
    const retirement = stopStreamSession(session)
    if (!session.sessionScoped) await retirement
  }

  try {
    await waitForRetiringStop(session)
    if (isUnmounted || generation !== streamRequestGeneration) return

    const res = await $API.boxRequestLiveStream({
      channelId: session.channelId,
      algorithmId: session.algorithmId,
      previewSessionId: session.previewSessionId
    })
    candidateViewerOwned = true
    const stream = res?.resData?.stream
    session.sessionScoped = stream?.previewSessionId === session.previewSessionId
    if (isUnmounted || generation !== streamRequestGeneration) {
      await releaseCandidateViewer()
      if (!isUnmounted) isShowLoading.value = false
      return
    }

    if (!stream) {
      throw new Error('Live stream response is missing stream information')
    }

    const candidateSession = buildStreamSession(session, stream)
    session.sessionScoped = candidateSession.sessionScoped
    console.log('视频预览地址==>', candidateSession.sourceUrl, '--------')
    provisionalHeartbeat = startProvisionalHeartbeat(candidateSession)
    playerWasReplaced = true
    await Promise.race([
      playVideo(candidateSession),
      provisionalHeartbeat.failure
    ])
    if (candidateSession.playbackError) throw candidateSession.playbackError

    if (isUnmounted || generation !== streamRequestGeneration) {
      throw new Error('Live stream switch superseded')
    }

    provisionalHeartbeat.stop()
    provisionalHeartbeat = null
    const previousViewerStillActive = activeStreamSession === previousSession
    activeStreamSession = candidateSession
    candidateViewerOwned = false
    clearPendingSession(session)
    algorithmId.value = candidateSession.algorithmId
    sendHeartBeat(candidateSession.keepAliveInterval)
    // Playback has already reached the protocol-specific ready gate. Publish
    // the committed visible state before awaiting old-viewer retirement so a
    // later playback error, retry, or user stop can still override it.
    isShowLoading.value = false
    isShowStopPreview.value = false

    if (previousViewerStillActive && previousSession) {
      // Stop is intentionally attempted once. If it fails, its heartbeat has
      // already been removed and the backend watchdog bounds the stale viewer.
      void stopStreamSession(previousSession)
    }
  } catch (error) {
    provisionalHeartbeat?.stop()
    provisionalHeartbeat = null
    await releaseCandidateViewer()

    let playerRestored = false
    let playerRecoveryNeeded = false
    if (playerWasReplaced && !isUnmounted &&
        activeStreamSession === previousSession && previousSession) {
      try {
        await playVideo(previousSession)
        playerRestored = true
      } catch (restoreError) {
        if (isPlaybackSuperseded(restoreError)) {
          playerRestored = true
        } else {
          console.warn('[Video] Failed to restore the previous player:', restoreError)
          isShowStopPreview.value = true
          playerRecoveryNeeded = true
        }
      }
    }
    if (playerWasReplaced && !playerRestored) {
      destroyVideo()
    }
    if (playerRecoveryNeeded) {
      scheduleFlvPlayerRetry(previousSession)
    }

    if (isUnmounted || generation !== streamRequestGeneration) return

    clearPendingSession(session)
    isShowLoading.value = false
    const msgCode = String(normalizeApiError(error).code)
    const algorithmRetired = session.algorithmId &&
      (msgCode === '12290' || msgCode === '10027')

    if (activeStreamSession === previousSession && previousSession) {
      restoreParentSelection(previousSession)
    } else if (algorithmRetired) {
      // Only a tile without a previous live viewer needs a raw-stream fallback.
      algorithmId.value = ''
      latestDesiredSession = {
        channelId: session.channelId,
        algorithmId: ''
      }
      emit('runAlgorithmIdChange', {
        channelId: session.channelId,
        runAlgorithmId: '',
        retiredAlgorithmId: session.algorithmId,
        index: props.index
      })
    } else if (msgCode === '10024') {
      emit('runAlgorithmIdChange', {
        channelId: '',
        runAlgorithmId: '',
        index: props.index
      })
    } else {
      isShowStopPreview.value = true
    }
  }
}

const getrequestLiveStream = (reason = 'prop-sync') => {
  const session = {
    channelId: props.channelId,
    algorithmId: props.runAlgorithmId || '',
    previewSessionId: uuidv4()
  }
  algorithmId.value = session.algorithmId
  if (sameStreamKey(pendingStreamSession, session)) {
    return streamSwitchQueue
  }

  const currentOperationSession = pendingStreamSession || activeStreamSession || latestDesiredSession
  const supersedesPlayback = currentOperationSession &&
    !sameStreamKey(currentOperationSession, session)
  pendingStreamSession = session
  latestDesiredSession = session
  const generation = ++streamRequestGeneration
  if (supersedesPlayback && flvRetryTimer.value) {
    clearTimeout(flvRetryTimer.value)
    flvRetryTimer.value = null
    flvRetryCount.value = 0
  }
  if (releaseRetiringStopWait && supersedesPlayback) {
    const releaseWait = releaseRetiringStopWait
    releaseRetiringStopWait = null
    releaseWait()
  }
  if (playbackAttempt && supersedesPlayback) {
    playbackAttempt.cancel(new Error('Video playback setup superseded'))
  }

  streamSwitchQueue = streamSwitchQueue
    .catch(() => {})
    .then(() => performLiveStreamRequest(session, generation, reason))
  return streamSwitchQueue
}

const getRestartTime = () => {
  return Math.floor((Math.random() * (6 - 3) + 3) * 60 * 60 * 1000)
}

const scheduleFlvPlayerRetry = (session) => {
  if (activeStreamSession !== session || isUnmounted || flvRetryTimer.value) return

  if (flvRetryCount.value < 3) {
    const delay = flvRetryCount.value === 0
      ? 300
      : Math.pow(2, flvRetryCount.value) * 1000
    flvRetryCount.value++
    console.log(`FLV retry ${flvRetryCount.value}/3 in ${delay}ms`)
    flvRetryTimer.value = setTimeout(() => {
      flvRetryTimer.value = null
      if (activeStreamSession !== session || isUnmounted) return
      void playVideo(session).then(() => {
        if (activeStreamSession === session &&
            sameStreamKey(latestDesiredSession, session)) {
          isShowStopPreview.value = false
        }
      }).catch((retryError) => {
        if (isPlaybackSuperseded(retryError) || isUnmounted ||
            activeStreamSession !== session ||
            !sameStreamKey(latestDesiredSession, session)) return
        console.warn('[Video] HTTP-FLV player retry failed:', retryError)
        scheduleFlvPlayerRetry(session)
      })
    }, delay)
    return
  }

  flvRetryCount.value = 0
  flvRetryTimer.value = setTimeout(() => {
    flvRetryTimer.value = null
    if (activeStreamSession !== session || isUnmounted) return
    getrequestLiveStream('flv-retry')
  }, 5000)
}

const playVideo = async (session) => {
  if (!session) {
    throw new Error('Cannot play a stream without an active session')
  }

  destroyVideo()
  const attempt = createPlaybackAttempt()
  playbackAttempt = attempt
  isShowLoading.value = true
  isShowVideo.value = true

  try {
    await nextTick()
    if (attempt.cancelled) throw attempt.reason
    if (!video.value) {
      throw new Error('Video element was not mounted')
    }

    if (session.playbackProtocol === 'webrtc') {
      await playWebRtc(session, attempt)
    } else {
      await playFlv(session.sourceUrl, session, attempt)
    }
  } catch (error) {
    const playbackError = createPlaybackError(error, attempt)
    // Only the attempt that still owns the player may tear it down. A stale
    // retry can reject after a newer stream has already installed its player.
    if (playbackAttempt === attempt) {
      destroyVideo({ cancelPlaybackAttempt: false })
      isShowLoading.value = false
    }
    throw playbackError
  } finally {
    if (playbackAttempt === attempt) {
      playbackAttempt = null
    }
  }
}

const playFallbackFlv = async (reason, session, attempt) => {
  if (!session.fallbackFlvUrl) {
    isShowLoading.value = false
    proxy.$message.error(reason?.message || t('event.webRtcConnectionFailed'))
    throw reason || new Error('HTTP-FLV fallback URL is unavailable')
  }

  console.warn('[Video] WebRTC failed, fallback to HTTP-FLV:', reason, session.fallbackFlvUrl)
  session.playbackProtocol = 'httpflv'
  session.sourceUrl = session.fallbackFlvUrl
  flvRetryCount.value = 0
  await playFlv(session.sourceUrl, session, attempt)
}

const playWebRtc = (session, attempt) => {
  if (!window.RTCPeerConnection) {
    return playFallbackFlv(new Error('Browser does not support WebRTC'), session, attempt)
  }

  return new Promise((resolve, reject) => {
    let settled = false
    let whepPlayer = null

    const finish = (callback, value) => {
      if (settled) return
      settled = true
      if (attempt.cancelHandler === onCancelled) attempt.cancelHandler = null
      callback(value)
    }
    const stopWhep = () => {
      if (webRtcPlayer.value === whepPlayer) webRtcPlayer.value = null
      if (whepPlayer) void whepPlayer.stop()
    }
    const onCancelled = (reason) => {
      stopWhep()
      finish(reject, reason)
    }
    const handleRuntimeError = (error) => {
      if (isUnmounted) return
      if (activeStreamSession !== session) {
        session.playbackError = error
        return
      }
      if (!session.fallbackFlvUrl) {
        isShowLoading.value = false
        isShowStopPreview.value = true
        proxy.$message.error(error?.message || t('event.webRtcConnectionFailed'))
        return
      }
      session.playbackProtocol = 'httpflv'
      session.sourceUrl = session.fallbackFlvUrl
      void playVideo(session).catch((fallbackError) => {
        if (isPlaybackSuperseded(fallbackError) || isUnmounted ||
            activeStreamSession !== session ||
            !sameStreamKey(latestDesiredSession, session)) return
        console.warn('[Video] Runtime HTTP-FLV fallback failed:', fallbackError)
        scheduleFlvPlayerRetry(session)
      })
    }
    const handleError = (error) => {
      console.log('==========WebRTC ERROR ============', error)
      if (settled) {
        if (webRtcPlayer.value !== whepPlayer) return
        handleRuntimeError(error)
        return
      }
      if (playbackAttempt !== attempt ||
          (whepPlayer && webRtcPlayer.value !== whepPlayer)) return
      stopWhep()
      playFallbackFlv(error, session, attempt).then(
        () => finish(resolve),
        (fallbackError) => finish(reject, fallbackError)
      )
    }

    if (attempt.cancelled) return onCancelled(attempt.reason)
    attempt.cancelHandler = onCancelled

    try {
      whepPlayer = createWhepPlayer({
        video: video.value,
        url: session.sourceUrl,
        onConnected: () => {
          if (settled || playbackAttempt !== attempt ||
              webRtcPlayer.value !== whepPlayer) return
          isShowLoading.value = false
          flvRetryCount.value = 0
          finish(resolve)
        },
        onError: handleError
      })
      webRtcPlayer.value = whepPlayer
      Promise.resolve(whepPlayer.start()).catch(handleError)
    } catch (error) {
      handleError(error)
    }
  })
}

const playFlv = (url, session, attempt) => {
  if (!flvjs.isSupported()) {
    proxy.$message.error(t('event.videoPreviewUnsupported'))
    throw new Error('Browser does not support HTTP-FLV playback')
  }

  if (attempt.cancelled) throw attempt.reason

  const videoEl = video.value
  if (!videoEl) {
    throw new Error('Video element not found after nextTick')
  }

  const flvPlayer = flvjs.createPlayer(
    {
      type: 'flv',
      isLive: true,
      url,
      hasAudio: false,
      hasVideo: true,
      loadStartTimeout: 10000
    },
    {
      enableStashBuffer: false,
      autoCleanupSourceBuffer: true
    }
  )
  player.value = flvPlayer
  flvPlayer.attachMediaElement(videoEl)

  return new Promise((resolve, reject) => {
    let settled = false
    let playPending = false
    let setupTimeout = null
    let onCanPlay = null
    let onScriptData = null
    let onError = null

    const finish = (error) => {
      if (settled) return
      settled = true
      if (setupTimeout) {
        clearTimeout(setupTimeout)
        if (loadingFallbackTimer.value === setupTimeout) {
          loadingFallbackTimer.value = null
        }
        setupTimeout = null
      }
      if (onCanPlay) {
        videoEl.removeEventListener('canplay', onCanPlay)
      }
      if (_onCanPlay === onCanPlay) {
        _onCanPlay = null
      }
      if (attempt.cancelHandler === onCancelled) attempt.cancelHandler = null
      if (!error) {
        resolve()
        return
      }
      reject(error instanceof Error
        ? error
        : new Error(`HTTP-FLV playback failed: ${String(error)}`))
    }
    const onCancelled = (reason) => finish(reason)

    // Resolve setup only after the media element is actually playable.
    onCanPlay = () => {
      if (playPending || settled) return
      playPending = true
      Promise.resolve(flvPlayer.play()).then(() => {
        if (settled || playbackAttempt !== attempt ||
            player.value !== flvPlayer) return
        isShowLoading.value = false
        flvRetryCount.value = 0
        finish()
      }).catch((error) => {
        if (settled || playbackAttempt !== attempt ||
            player.value !== flvPlayer) return
        finish(error)
      })
    }
    _onCanPlay = onCanPlay
    videoEl.addEventListener('canplay', onCanPlay)

    // 追帧：延迟超过 3s 时跳到最新帧
    chaseFrameTimer.value = setInterval(() => {
      if (!videoEl.buffered.length) return
      const end = videoEl.buffered.end(0)
      const diff = end - videoEl.currentTime
      if (diff >= 3) {
        videoEl.currentTime = end
      }
    }, 3000)

    // SCRIPTDATA_ARRIVED can hide loading, but canplay remains the setup gate.
    onScriptData = () => {
      flvPlayer.off(flvjs.Events.SCRIPTDATA_ARRIVED, onScriptData)
      if (player.value !== flvPlayer) return
      isShowLoading.value = false
      if (_onScriptData === onScriptData) _onScriptData = null
    }
    _onScriptData = onScriptData
    flvPlayer.on(flvjs.Events.SCRIPTDATA_ARRIVED, onScriptData)

    onError = (error) => {
      if (!settled) {
        console.log('==========FLV ERROR ============', error)
        finish(error)
        return
      }
      if (activeStreamSession !== session || player.value !== flvPlayer) return

      console.log('==========FLV ERROR ============', error)
      scheduleFlvPlayerRetry(session)
    }
    _onError = onError
    flvPlayer.on(flvjs.Events.ERROR, onError)

    setupTimeout = setTimeout(() => {
      finish(new Error('HTTP-FLV media setup timeout'))
    }, 10000)
    loadingFallbackTimer.value = setupTimeout

    if (attempt.cancelled) return onCancelled(attempt.reason)
    attempt.cancelHandler = onCancelled
    try {
      flvPlayer.load()
    } catch (error) {
      finish(error)
    }
  })
}

const destroyVideo = ({ cancelPlaybackAttempt = true } = {}) => {
  if (playbackAttempt) {
    const attempt = playbackAttempt
    playbackAttempt = null
    if (cancelPlaybackAttempt) attempt.cancel()
  }
  if (webRtcPlayer.value) {
    const currentWebRtcPlayer = webRtcPlayer.value
    webRtcPlayer.value = null
    void currentWebRtcPlayer.stop()
  }
  if (flvRetryTimer.value) {
    clearTimeout(flvRetryTimer.value)
    flvRetryTimer.value = null
  }
  if (chaseFrameTimer.value) {
    clearInterval(chaseFrameTimer.value)
    chaseFrameTimer.value = null
  }
  if (loadingFallbackTimer.value) {
    clearTimeout(loadingFallbackTimer.value)
    loadingFallbackTimer.value = null
  }
  if (_onCanPlay) {
    const videoEl = video.value
    videoEl && videoEl.removeEventListener('canplay', _onCanPlay)
    _onCanPlay = null
  }
  if (player.value) {
    const currentPlayer = player.value
    player.value = null
    try {
      if (_onScriptData) {
        currentPlayer.off(flvjs.Events.SCRIPTDATA_ARRIVED, _onScriptData)
      }
      if (_onError) {
        currentPlayer.off(flvjs.Events.ERROR, _onError)
      }
      currentPlayer.pause()
      currentPlayer.unload()
      currentPlayer.detachMediaElement()
      currentPlayer.destroy()
    } catch (error) {
      console.warn('[Video] Failed to destroy HTTP-FLV player cleanly:', error)
    }
  }
  _onScriptData = null
  _onError = null
  const videoEl = video.value
  if (videoEl?.srcObject) {
    videoEl.srcObject = null
  }
  isShowVideo.value = false
}

const stop = async () => {
  ++streamRequestGeneration
  if (releaseRetiringStopWait) {
    const releaseWait = releaseRetiringStopWait
    releaseRetiringStopWait = null
    releaseWait()
  }
  pendingStreamSession = null
  latestDesiredSession = null
  isShowLoading.value = false
  isShowStopPreview.value = true
  clearHeartbeat()
  destroyVideo()
  const session = activeStreamSession
  activeStreamSession = null
  await stopStreamSession(session)
}

const stopPreview = () => {
  void stop()
  emit('stop', props.index)
}

const overlayAlgorithmChange = (val) => {
  latestDesiredSession = {
    channelId: props.channelId,
    algorithmId: val || ''
  }
  emit('runAlgorithmIdChange', {
    channelId: props.channelId,
    runAlgorithmId: val,
    index: props.index
  })
}

const handleControls = (flag) => {
  hideControl.value = flag === 'leave'
}

const handleFullScreen = () => {
  emit('fullScreen', {
    index: props.index,
    isFullScreen: true
  })
}

const handleExitFullScreen = () => {
  emit('fullScreen', {
    index: props.index,
    isFullScreen: false
  })
}

watch(() => props.taskList, (taskList) => {
  const filterList = (taskList || []).filter(
    (item) => item.enableStatus == 1
  )
  algorithmOverlayList.value = [
    {
      algorithmId: '',
      algorithmName: t('common.none')
    },
    ...filterList
  ]
}, { immediate: true, deep: true })

watch(() => props.channelId, () => {
  isShowStopPreview.value = false
  getrequestLiveStream()
}, { immediate: true })

watch(() => props.runAlgorithmId, (newVal) => {
  algorithmId.value = newVal || ''
  getrequestLiveStream()
  console.log('====runAlgorithmId====', algorithmId.value)
})

onMounted(() => {
  restartInterval.value = setInterval(() => {
    getrequestLiveStream('timer')
  }, getRestartTime())
})

onBeforeUnmount(() => {
  isUnmounted = true
  if (restartInterval.value) {
    clearInterval(restartInterval.value)
    restartInterval.value = null
  }
  void stop()
})
</script>

<style lang="scss" scoped>
.video-container {
  width: 100%;
  height: 100%;
  position: relative;
  background: #212839;
  border: 1px solid rgba(95, 200, 223, 0.2);
  box-sizing: border-box;

  .video {
    width: 100%;
    height: 100%;
    object-fit: fill;
    background-color: #000;
  }

  .video-top-control {
    position: absolute;
    top: 0;
    width: 100%;
    height: 36px;
    left: 0;
    background: linear-gradient(180deg, rgba(19, 31, 58, 0.9) 0%, rgba(21, 35, 69, 0.7) 100%);
    backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 10px;
    box-sizing: border-box;
    color: #94d0ff;
    font-size: 13px;
    border-bottom: 1px solid rgba(95, 200, 223, 0.15);
    z-index: 10;

    .icon {
      margin-left: 8px;
      width: 20px;
      height: 20px;
      cursor: pointer;
      transition: all 0.2s;
      opacity: 0.85;

      &:hover {
        opacity: 1;
        transform: scale(1.1);
      }
    }
  }
}

.fade-enter-active {
  transition: all 0.2s;
}
.fade-leave-active {
  transition: all 0.8s;
}
.fade-enter-from,
.fade-leave-to {
  visibility: hidden;
  opacity: 0;
}

.stop-preview {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  display: flex;
  flex-flow: column;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  color: #94d0ff;
  background: linear-gradient(180deg, #1a2540 0%, #212839 100%);

  img {
    margin-bottom: 20px;
    width: 80px;
    opacity: 0.6;
  }

  span {
    font-size: 14px;
    color: #6b7a99;
  }
}

$primary-background-color: #131f3a !default;
$primary-background-transparency: 0.85 !default;
$secondary-background-color: #5fc8df !default;
$secondary-background-transparency: 0.6 !default;

.loading-spinner {
  position: absolute;
  top: 50%;
  left: 50%;
  margin: -30px 0 0 -30px;
  opacity: 0.9;
  text-align: left;
  border: 4px solid rgba($primary-background-color, $primary-background-transparency);
  box-sizing: border-box;
  background-clip: padding-box;
  width: 60px;
  height: 60px;
  border-radius: 30px;
  visibility: hidden;
  animation: spinner-show 0s linear 0.2s forwards;
}

.loading-spinner:before,
.loading-spinner:after {
  content: '';
  position: absolute;
  margin: -4px;
  box-sizing: inherit;
  width: inherit;
  height: inherit;
  border-radius: inherit;
  opacity: 1;
  border: inherit;
  border-color: transparent;
  border-top-color: $secondary-background-color;
  animation: spinner-spin 1s cubic-bezier(0.6, 0.2, 0, 0.8) infinite, spinner-fade 1s linear infinite;
}

@keyframes spinner-show {
  to {
    visibility: visible;
  }
}

@keyframes spinner-spin {
  100% {
    transform: rotate(360deg);
  }
}

@keyframes spinner-fade {
  0% {
    border-top-color: rgba($secondary-background-color, 0.3);
  }
  20% {
    border-top-color: rgba($secondary-background-color, 0.5);
  }
  35% {
    border-top-color: $secondary-background-color;
  }
  60% {
    border-top-color: rgba($secondary-background-color, 0.5);
  }
  100% {
    border-top-color: rgba($secondary-background-color, 0.3);
  }
}

.overlay-select {
  width: 130px;
  :deep(.el-input__inner) {
    background: rgba(19, 31, 58, 0.6);
    color: #94d0ff;
    border: 1px solid rgba(95, 200, 223, 0.2);
    font-size: 12px;
    height: 28px;
    line-height: 28px;
    
    &:hover {
      border-color: rgba(95, 200, 223, 0.4);
    }
    
    &:focus {
      border-color: #5fc8df;
    }
  }

  :deep(.el-select__caret) {
    color: #94d0ff !important;
  }
}

.camera-name {
  max-width: 40%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-weight: 500;
  color: #fff;
}
</style>

<style lang="scss">
.custom-select-popper2 {
  border: 1px solid rgba(95, 200, 223, 0.4);
  background: linear-gradient(180deg, rgba(19, 31, 58, 0.98) 0%, rgba(21, 35, 69, 0.98) 100%);
  backdrop-filter: blur(8px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.6);

  .popper__arrow {
    display: none;
  }

  .el-select-dropdown__item {
    color: #94d0ff;
    font-size: 13px;
    padding: 0 16px;
    height: 36px;
    line-height: 36px;
    background: transparent;
    transition: all 0.2s;
    
    &.selected {
      color: #5fc8df;
      font-weight: 500;
      background: rgba(95, 200, 223, 0.2);
    }

    &.hover,
    &:hover {
      background: rgba(95, 200, 223, 0.15) !important;
      color: #5fc8df;
    }
  }

  .el-scrollbar {
    background: transparent;
  }

  .el-scrollbar__wrap {
    margin-right: -17px !important;
  }

  .el-select-dropdown__list {
    padding: 4px 0;
  }
}
</style>
