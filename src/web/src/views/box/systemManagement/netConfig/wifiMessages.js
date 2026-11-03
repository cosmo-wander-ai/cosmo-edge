const reasonCodes = new Set([
  'invalidSsid', 'invalidPassword', 'passwordRequired', 'unsupportedSecurity', 'invalidIpv4',
  'invalidNetMask', 'invalidGateway', 'invalidDns', 'invalidInterface', 'invalidWifiConfig',
  'authenticationFailed', 'authenticationConfigFailed', 'connectionTimeout', 'connectionFailed',
  'confirmationTimeout', 'rollbackFailed', 'rollbackInterfaceUnavailable', 'restored', 'failed',
  'networkBusy', 'serviceUnavailable', 'servicePreparationFailed', 'serviceStopping', 'wifiUnavailable',
  'radioUnavailable', 'addressConfigFailed', 'addressConflict', 'addressConflictCheckUnavailable',
  'addressConflictCheckFailed', 'routeConfigFailed', 'dnsConfigFailed', 'saveFailed', 'loadFailed',
  'scanFailed', 'scanTimeout', 'controlFailed', 'controlTimeout', 'operationFailed',
  'operationNotFound', 'operationNotConfirmable', 'disconnectFailed', 'interrupted'
])

export function wifiReasonText(reason, translate) {
  if (!reason) return ''
  return reasonCodes.has(reason)
    ? translate(`wifi.reason.${reason}`)
    : translate('wifi.reason.unknown', { reason: String(reason) })
}

export function wifiRequestErrorText(error, translate, fallback = 'wifi.requestFailed') {
  const payload = error?.response?.data || error?.data || error
  const reason = payload?.resMsg?.[0]?.msgText
  return reason ? wifiReasonText(reason, translate) : translate(fallback)
}
