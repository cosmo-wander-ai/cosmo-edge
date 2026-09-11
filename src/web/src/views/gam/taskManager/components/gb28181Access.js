import { t } from '@/i18n'

export function isServerAddress(value) {
  if (typeof value !== 'string' || !/^(0|[1-9]\d{0,2})(\.(0|[1-9]\d{0,2})){3}$/.test(value)) return false
  const parts = value.split('.').map(Number)
  return parts.every(part => part <= 255) && parts[0] > 0 && parts[0] < 224 && parts[0] !== 127
}
export function serverAddresses(cards) {
  const addresses = new Map()
  for (const card of cards) {
    const address = card?.ipAddr?.trim()
    if (!isServerAddress(address)) continue
    const names = addresses.get(address) || new Set()
    if (card.ethName) names.add(card.ethName)
    addresses.set(address, names)
  }
  return [...addresses].map(([address, names]) => ({ address, name: [...names].join(' / ') }))
}

export function errorText(code = '') {
  const labels = {
    invalid_parameter: t('gbAccess.invalidParameter'), password_required: t('gbAccess.passwordRequired'),
    unauthorized: t('gbAccess.unauthorized'), device_offline: t('gbAccess.deviceOffline'),
    channel_not_found: t('gbAccess.channelNotFound'), duplicate_channel_id: t('gbAccess.duplicateChannel'),
    duplicate_source: t('gbAccess.alreadyAdded'), channel_save_failed: t('gbAccess.saveFailed'),
    busy: t('gbAccess.busy'), device_limit: t('gbAccess.limit'), storage_error: t('gbAccess.storageError'),
    listen_failed: t('gbAccess.listenFailed'), realm_in_use: t('gbAccess.realmInUse'),
    catalog_timeout: t('gbAccess.catalogTimeout'), catalog_invalid: t('gbAccess.catalogInvalid'),
    media_unavailable: t('gbAccess.mediaUnavailable'), media_timeout: t('gbAccess.mediaTimeout'),
    invite_timeout: t('gbAccess.inviteTimeout'), unsupported_transport: t('gbAccess.transportError'),
    connection_closed: t('gbAccess.connectionClosed'), registration_or_heartbeat_timeout: t('gbAccess.heartbeatTimeout'),
    invalid_sip: t('gbAccess.protocolError')
  }
  if (/^invite_rejected_\d{3}$/.test(code)) return t('gbAccess.inviteRejected', { code: code.slice(-3) })
  return labels[code] || t('gbAccess.unavailable')
}
export function stateText(code) {
  const labels = {
    unregistered: t('gbAccess.unregistered'), registered: t('gbAccess.registered'), auth_failed: t('gbAccess.authFailed'),
    offline: t('status.offline'), catalog_query: t('gbAccess.catalogQuery'), catalog_failed: t('gbAccess.catalogFailed'),
    idle: t('gbAccess.idle'), inviting: t('gbAccess.inviting'), waiting_media: t('gbAccess.waitingMedia'),
    receiving: t('gbAccess.receiving'), stream_failed: t('gbAccess.streamFailed'), invite_failed: t('gbAccess.streamFailed'),
    device_offline: t('gbAccess.deviceOffline'), channel_offline: t('status.offline'), channel_not_found: t('gbAccess.channelNotFound'),
    unsupported_transport: t('gbAccess.transportError'), device_stopped: t('gbAccess.deviceStopped')
  }
  return labels[code] || t('gbAccess.unavailable')
}
export function mergeDevices(current, incoming) {
  return incoming.map(device => ({ ...device, channels: device.channels.map(channel => {
    const prior = current.find(row => row.id === device.id)?.channels.find(row => row.id === channel.id)
    return { ...channel, channelName: prior?.channelName ?? (channel.name || channel.id) }
  }) }))
}
export function deviceRequest(form) {
  const id = form.id.trim()
  const username = form.username.trim() || id
  if (!/^\d{20}$/.test(id) || !/^[\w.@-]{1,256}$/.test(username)) throw new Error('invalid_parameter')
  if (!form.allowUnauthenticated && !form.password && !form.hasPassword) throw new Error('password_required')
  const result = { action: 'saveDevice', id, username, allowUnauthenticated: !!form.allowUnauthenticated }
  if (form.password) result.password = form.password
  return result
}
