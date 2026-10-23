import assert from 'node:assert/strict'
import { getShieldedRegionHeads, isShieldedRegionEnabled } from '../src/utils/shieldedRegion.js'
import zh from '../src/i18n/locales/zh-CN.js'
import en from '../src/i18n/locales/en-US.js'
for (const locale of [zh, en]) {
 const t = key => {
  const value = key.split('.').reduce((v,k) => v?.[k],locale)
  assert.equal(typeof value, 'string', key)
  return value
 }
 for (const shieldedRegion of [undefined, {}, { heads: [] }]) {
  const metadata = { enableShieldedRegion: true, shieldedRegion }
  const before = structuredClone(metadata)
  const heads = getShieldedRegionHeads(metadata,t)
  assert.equal(heads.length,1)
  assert.equal(heads[0].key,'name')
  assert.equal(heads[0].name,t('glossary.regionName'))
  const rule = new RegExp(heads[0].regexpr.slice(1,-1))
  assert.ok(rule.test('mask-1'))
  for (const invalid of ['', 'two words', 'x'.repeat(33)]) assert.equal(rule.test(invalid),false)
  const saved = JSON.parse(JSON.stringify({...metadata,shieldedRegion:{heads}}))
  assert.deepEqual(getShieldedRegionHeads(saved,t),heads)
  assert.deepEqual(metadata,before)
 }
 const heads = [{key:'name',type:'text',name:'Custom'}]
 const legacy = {shieldedRegion:{heads}}
 assert.equal(isShieldedRegionEnabled(legacy),true)
 assert.deepEqual(getShieldedRegionHeads(legacy,t),heads)
 assert.equal(getShieldedRegionHeads({...legacy,enableShieldedRegion:false},t),undefined)
 assert.deepEqual(getShieldedRegionHeads({...legacy,enableShieldedRegion:true},t),heads)
 for (const metadata of [{},{shieldedRegion:{}},{shieldedRegion:{heads:[]}}]) {
  assert.equal(isShieldedRegionEnabled(metadata),false)
  assert.equal(getShieldedRegionHeads(metadata,t),undefined)
 }
}
console.log('Shield region regression checks passed')
