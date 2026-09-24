/**
 * 校验 MQTT 端口是否为 1 到 65535 范围内的十进制整数，用于网络配置提交前校验。
 */
export const isValidMqttPort = value => {
  const text = String(value ?? '')
  if (!/^\d+$/.test(text)) return false
  const port = Number(text)
  return Number.isInteger(port) && port >= 1 && port <= 65535
}
