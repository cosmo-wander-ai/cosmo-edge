const DEFAULT_ICE_SERVERS = [
  { urls: 'stun:stun.l.google.com:19302' }
]

const appendCandidatesToSdp = (sdp, candidates) => {
  if (!candidates.length) {
    return sdp
  }

  const candidateLines = candidates
    .filter((candidate) => candidate?.candidate && !sdp.includes(candidate.candidate))
    .map((candidate) => `a=${candidate.candidate}`)

  if (!candidateLines.length) {
    return sdp
  }

  const endOfCandidates = sdp.includes('a=end-of-candidates')
    ? ''
    : '\r\na=end-of-candidates'

  return `${sdp.trim()}\r\n${candidateLines.join('\r\n')}${endOfCandidates}\r\n`
}

const getCandidateLines = (sdp = '') => {
  return sdp.split(/\r?\n/).filter((line) => line.startsWith('a=candidate:'))
}

const waitForFirstCandidate = (peer, candidates, timeoutMs) => {
  if (candidates.length || peer.iceGatheringState === 'complete') {
    return Promise.resolve()
  }

  return new Promise((resolve) => {
    let timeout = null
    const cleanup = () => {
      if (timeout) {
        clearTimeout(timeout)
      }
      peer.removeEventListener('icecandidate', onCandidate)
      peer.removeEventListener('icegatheringstatechange', onStateChange)
    }
    const finish = () => {
      cleanup()
      resolve()
    }
    const onCandidate = (event) => {
      if (event.candidate || peer.iceGatheringState === 'complete') {
        finish()
      }
    }
    const onStateChange = () => {
      if (peer.iceGatheringState === 'complete') {
        finish()
      }
    }

    peer.addEventListener('icecandidate', onCandidate)
    peer.addEventListener('icegatheringstatechange', onStateChange)
    timeout = setTimeout(finish, timeoutMs)
  })
}

const logPeerState = (peer, label) => {
  console.log(
    `[WHEP] ${label}:`,
    `signaling=${peer.signalingState}`,
    `iceGathering=${peer.iceGatheringState}`,
    `iceConnection=${peer.iceConnectionState}`,
    `connection=${peer.connectionState}`
  )
}

export const createWhepPlayer = ({
  video,
  url,
  onConnected,
  onError,
  timeoutMs = 5000,
  mediaTimeoutMs = 15000,
  firstCandidateTimeoutMs = 3000,
  iceServers = DEFAULT_ICE_SERVERS
}) => {
  let peer = null
  let abortController = null
  let resourceUrl = ''
  let stopped = false
  let timeout = null
  let mediaTimeout = null
  let failed = false
  let mediaReady = false
  let connectedNotified = false
  const diagnosticsTimers = []

  const clearMediaTimeout = () => {
    if (mediaTimeout) {
      clearTimeout(mediaTimeout)
      mediaTimeout = null
    }
  }

  const cleanup = async ({ markStopped = true } = {}) => {
    if (markStopped) {
      stopped = true
    }
    if (timeout) {
      clearTimeout(timeout)
      timeout = null
    }
    clearMediaTimeout()
    diagnosticsTimers.forEach((timer) => clearTimeout(timer))
    diagnosticsTimers.length = 0
    if (abortController) {
      abortController.abort()
      abortController = null
    }
    if (resourceUrl) {
      resourceUrl = resourceUrl.replace('/rtc/v1/whip/', '/rtc/v1/whep/')
      fetch(resourceUrl, { method: 'DELETE' }).catch(() => {})
      resourceUrl = ''
    }
    if (peer) {
      peer.getReceivers().forEach((receiver) => {
        receiver.track && receiver.track.stop()
      })
      peer.close()
      peer = null
    }
    if (video) {
      video.srcObject = null
    }
  }

  const stop = () => cleanup({ markStopped: true })

  const fail = (error) => {
    if (!stopped && !failed && onError) {
      failed = true
      onError(error)
    }
  }

  const markMediaReady = () => {
    mediaReady = true
    clearMediaTimeout()
    if (!connectedNotified) {
      connectedNotified = true
      onConnected && onConnected()
    }
  }

  const start = async () => {
    try {
      abortController = new AbortController()
      console.log('[WHEP] secure context:', window.isSecureContext, 'url:', url)
      peer = new RTCPeerConnection({
        iceServers,
        iceCandidatePoolSize: 4
      })
      const candidates = []
      const onCandidate = (event) => {
        if (event.candidate) {
          candidates.push(event.candidate)
          console.log('[WHEP] ICE candidate:', event.candidate.candidate)
        } else {
          console.log('[WHEP] ICE candidate gathering complete')
        }
      }
      peer.addEventListener('icecandidate', onCandidate)
      peer.addEventListener('icecandidateerror', (event) => {
        console.warn('[WHEP] ICE candidate error:', {
          address: event.address,
          port: event.port,
          url: event.url,
          errorCode: event.errorCode,
          errorText: event.errorText
        })
      })
      peer.addEventListener('icegatheringstatechange', () => {
        logPeerState(peer, 'state changed')
      })
      peer.addEventListener('iceconnectionstatechange', () => {
        logPeerState(peer, 'state changed')
        if (['connected', 'completed'].includes(peer.iceConnectionState)) {
          markMediaReady()
        }
      })
      peer.addEventListener('connectionstatechange', () => {
        logPeerState(peer, 'state changed')
        if (peer.connectionState === 'connected') {
          markMediaReady()
        }
      })
      peer.addEventListener('signalingstatechange', () => {
        logPeerState(peer, 'state changed')
      })
      peer.addTransceiver('video', { direction: 'recvonly' })
      peer.ontrack = (event) => {
        if (video && video.srcObject !== event.streams[0]) {
          video.srcObject = event.streams[0]
          video.play().catch(() => {})
        }
        markMediaReady()
      }
      peer.onconnectionstatechange = () => {
        if (peer.connectionState === 'connected') {
          markMediaReady()
        }
        if (['failed', 'disconnected', 'closed'].includes(peer.connectionState)) {
          fail(new Error(`WebRTC ${peer.connectionState}`))
        }
      }
      peer.oniceconnectionstatechange = () => {
        if (['connected', 'completed'].includes(peer.iceConnectionState)) {
          markMediaReady()
        }
        if (['failed', 'disconnected', 'closed'].includes(peer.iceConnectionState)) {
          fail(new Error(`WebRTC ICE ${peer.iceConnectionState}`))
        }
      }

      const offer = await peer.createOffer()
      await peer.setLocalDescription(offer)
      await waitForFirstCandidate(peer, candidates, firstCandidateTimeoutMs)
      console.log('[WHEP] ICE gathering state:', peer.iceGatheringState, 'candidate count:', candidates.length)
      const offerSdp = appendCandidatesToSdp(peer.localDescription.sdp, candidates)
      const localCandidateLines = getCandidateLines(offerSdp)
      console.log('[WHEP] local SDP candidate count:', localCandidateLines.length, localCandidateLines)
      if (!localCandidateLines.length) {
        console.warn('[WHEP] no local ICE candidates in initial offer. Continue with WHEP answer and watch live ICE candidates.')
        if (!window.isSecureContext) {
          throw new Error('WebRTC requires HTTPS or a trusted browser origin to gather local ICE candidates')
        }
      }

      timeout = setTimeout(() => {
        abortController && abortController.abort()
        fail(new Error('WebRTC connect timeout'))
      }, timeoutMs)

      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sdp'
        },
        body: offerSdp,
        signal: abortController.signal
      })

      if (!response.ok) {
        throw new Error(`WHEP request failed: ${response.status}`)
      }

      if (timeout) {
        clearTimeout(timeout)
        timeout = null
      }

      resourceUrl = response.headers.get('Location') || ''
      if (resourceUrl && resourceUrl.startsWith('/')) {
        resourceUrl = new URL(resourceUrl, url).toString()
      }

      const answer = await response.text()
      const remoteCandidateLines = getCandidateLines(answer)
      console.log('[WHEP] remote SDP candidate count:', remoteCandidateLines.length, remoteCandidateLines)
      await peer.setRemoteDescription({ type: 'answer', sdp: answer })
      logPeerState(peer, 'after remote answer')
      diagnosticsTimers.push(setTimeout(() => {
        if (peer) {
          console.log('[WHEP] candidates after 2s:', candidates.length, candidates.map((candidate) => candidate.candidate))
          logPeerState(peer, 'after 2s')
        }
      }, 2000))
      diagnosticsTimers.push(setTimeout(() => {
        if (peer) {
          console.log('[WHEP] candidates after 10s:', candidates.length, candidates.map((candidate) => candidate.candidate))
          logPeerState(peer, 'after 10s')
          if (!candidates.length && peer.iceConnectionState === 'new') {
            fail(new Error('Browser did not gather local ICE candidates; check Chrome WebRTC UDP/IP policy'))
          }
        }
      }, 10000))

      if (!mediaReady) {
        mediaTimeout = setTimeout(() => {
          fail(new Error('WebRTC media timeout'))
        }, mediaTimeoutMs)
      }
    } catch (error) {
      await cleanup({ markStopped: false })
      fail(error)
    }
  }

  return { start, stop }
}
