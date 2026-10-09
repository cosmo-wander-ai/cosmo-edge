/**
 * 校验可选 DNS 地址；用于网络设置表单提交前接受空值或合法的单播 IPv4 地址。
 * 地址必须使用无前导零的标准四段十进制格式，首段范围为 1 到 223，其余段范围为 0 到 255。
 */
export function isValidOptionalDnsAddress(value) {
  if (!value) return true
  const parts = value.split('.')
  if (parts.length !== 4 || parts.some(part => !/^(0|[1-9]\d*)$/.test(part))) return false
  const numbers = parts.map(Number)
  return numbers[0] >= 1 && numbers[0] <= 223 &&
    numbers.slice(1).every(number => number >= 0 && number <= 255)
}
