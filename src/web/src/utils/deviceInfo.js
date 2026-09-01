const HIDDEN_DEVICE_INFO_KEYS = new Set([
  // Internal capability flag used by the model management page.
  'rkllmAvailable'
])

export const filterDeviceInfoForDisplay = (items) => {
  if (!Array.isArray(items)) return []
  return items.filter(item => !HIDDEN_DEVICE_INFO_KEYS.has(item?.key))
}
