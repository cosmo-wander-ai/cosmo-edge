import assert from 'node:assert/strict'
import { test } from 'node:test'
import { isValidOptionalDnsAddress } from '../src/utils/networkValidation.js'

test('DNS validation rejects non-numeric and out-of-range IPv4 segments', () => {
  for (const address of [
    '8a.8.8.8',
    '008.8.8.8',
    '8.08.8.8',
    '8.8.8.8x',
    '8.8.8.256',
    '224.1.1.1',
    '2001:4860:4860::8888',
    '8.8.8.8:53',
    'dns.google',
    'https://dns.google/dns-query'
  ]) {
    assert.equal(isValidOptionalDnsAddress(address), false, address)
  }
})

test('DNS validation accepts an empty secondary address and valid unicast IPv4 addresses', () => {
  for (const address of ['', '1.1.1.1', '223.255.255.255']) {
    assert.equal(isValidOptionalDnsAddress(address), true, address)
  }
})
