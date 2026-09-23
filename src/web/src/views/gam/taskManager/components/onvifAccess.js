// ONVIF handles discovery and authentication; media stays on the existing RTSP path.
export function deviceAddress(endpoint) {
  try { return new URL(endpoint.includes('://') ? endpoint : `http://${endpoint}`).host }
  catch { return endpoint }
}

export function deviceName(device) {
  const scope = (device.scopes || '').split(/\s+/).find(value => value.startsWith('onvif://www.onvif.org/name/'))
  if (scope) {
    try {
      const name = decodeURIComponent(scope.slice('onvif://www.onvif.org/name/'.length)).trim()
      if (name && new TextEncoder().encode(name).length <= 256) return name
    } catch { /* Malformed advertised names fall back to the device address. */ }
  }
  return `ONVIF ${deviceAddress(device.endpoint)}`
}

export function chooseProfile(profiles, previous = '') {
  // Match media::IsValidVideoResolution in src/util/VideoInfo.h. A larger but
  // invalid Profile must not hide another stream that the pipeline can accept.
  const supported = profiles.filter(p => p.token && ['H264', 'H265', 'MJPEG'].includes(p.codec?.toUpperCase()) &&
    Number(p.width) > 0 && Number(p.height) > 0 && p.width * p.height >= 1280 * 720 && p.width * p.height <= 7680 * 4320)
  if (!supported.length) throw new Error(profiles.length ? 'unsupported_stream' : 'no_profiles')
  // Preserve an existing channel's selection. For new channels prefer resolution,
  // then frame rate; prefer H264 only when both are equal. Do not infer from names.
  const existing = supported.find(p => p.token === previous)
  if (existing) return existing
  const rank = p => ({ H264: 3, H265: 2, MJPEG: 1 })[p.codec.toUpperCase()]
  return [...supported].sort((a, b) => (b.width * b.height - a.width * a.height) ||
    ((Number(b.fps) || 0) - (Number(a.fps) || 0)) || (rank(b) - rank(a)) || String(a.token).localeCompare(String(b.token)))[0]
}

export function requestConfig(row, credentials) {
  const config = { ...credentials, endpoint: row.endpoint, source: row.source || '', uuid: row.uuid || '' }
  if (row.source && !config.password) delete config.password
  if (row.source && !config.rtspPassword) delete config.rtspPassword
  return config
}

export async function probeDevice(api, row, credentials, active = () => true) {
  row.profile = null
  const endpoints = [...new Set([row.endpoint, ...(row.endpoints || [])].filter(Boolean))].slice(0, 4)
  for (let index = 0; index < endpoints.length && active(); index++) {
    try {
      const result = await api('OnvifProbe', { ...requestConfig(row, credentials), endpoint: endpoints[index] })
      if (!active()) return
      const profile = chooseProfile(result.profiles || [], row.source ? row.profileToken : '')
      row.endpoint = endpoints[index]
      row.profile = profile
      row.profileToken = profile.token
      return
    } catch (error) {
      // A different advertised address may be reachable; wrong credentials must
      // not be retried across addresses to avoid camera account lockout.
      if (!active()) return
      if (!['timeout', 'network_error', 'invalid_endpoint'].includes(error.message) || index === endpoints.length - 1) throw error
    }
  }
  if (active()) throw new Error('invalid_endpoint')
}

export async function saveDevice(api, row, credentials, active = () => true) {
  if (row.saved || !active()) return
  await probeDevice(api, row, credentials, active)
  if (!active()) return
  await api('OnvifSave', { ...requestConfig(row, credentials), channelName: row.channelName.trim(),
    profileToken: row.profile.token, videoSourceToken: row.profile.videoSourceToken || '', videoChannelId: row.videoChannelId || '' })
  // A dispatched save may finish after cancellation; remember it to avoid duplicates.
  row.saved = true
}

export async function runDeviceBatch(rows, action, active = () => true) {
  let index = 0
  const worker = async () => {
    while (index < rows.length && active()) {
      const row = rows[index++]
      if (row.saved) continue
      row.pending = true; row.error = ''
      try { await action(row) }
      catch (error) { if (active()) row.error = error.message || 'failed' }
      finally { row.pending = false }
    }
  }
  await Promise.all([worker(), worker()])
}
