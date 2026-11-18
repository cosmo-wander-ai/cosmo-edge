import { isValidOptionalDnsAddress } from '@/utils/networkValidation'

export function isWifiIpv4(value) {
  return typeof value === 'string' && value !== '' && isValidOptionalDnsAddress(value) &&
    !value.startsWith('127.') && !value.startsWith('169.254.')
}

function validMask(value) {
  if (typeof value !== 'string') return false
  const parts = value.split('.')
  if (parts.length !== 4 || parts.some(part => !/^(0|[1-9]\d*)$/.test(part) || Number(part) > 255)) return false
  const bits = parts.map(part => Number(part).toString(2).padStart(8, '0')).join('')
  return /^1{1,30}0{2,31}$/.test(bits)
}

export function wifiValidationError(form, canReusePassword) {
  const ssidBytes = new TextEncoder().encode(form.ssid).length
  if (ssidBytes < 1 || ssidBytes > 32 || form.ssid.includes('\0')) return 'wifi.invalidSsid'
  if (!(canReusePassword && !form.password) &&
      !(/^[\da-fA-F]{64}$/.test(form.password) ||
        /^[\x20-\x7e]{8,63}$/.test(form.password))) return 'wifi.invalidPassword'
  if (!form.dhcp) {
    if (form.autoDns) return 'wifi.staticDnsRequired'
    if (!isWifiIpv4(form.ipAddr)) return 'systemManage.ipFormatError'
    if (!validMask(form.netMask)) return 'systemManage.subnetMaskFormatError'
    if (form.gateway && !isWifiIpv4(form.gateway)) return 'systemManage.gatewayFormatError'
  }
  if (!form.autoDns && (!isWifiIpv4(form.dns1) || (form.dns2 && !isWifiIpv4(form.dns2)))) return 'systemManage.dnsFormatError'
  return ''
}

export function wifiConnectionRequest(form, canReusePassword) {
  const data = {
    ssid: form.ssid, hidden: form.hidden, security: 'wpa2', dhcp: form.dhcp,
    ipAddr: form.dhcp ? '' : form.ipAddr, netMask: form.dhcp ? '' : form.netMask,
    gateway: form.dhcp ? '' : form.gateway, autoDns: form.autoDns,
    dns: form.autoDns ? [] : [form.dns1, form.dns2].filter(Boolean)
  }
  if (form.password || !canReusePassword) data.password = form.password
  return data
}
