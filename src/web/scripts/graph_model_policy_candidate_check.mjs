import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { parse, compileScript, compileTemplate } from '@vue/compiler-sfc'
import { graphModelOptions, allowedAdditionalModels, createLatestRequest } from '../src/views/gam/countManagement/arrangeDetail/flow/graphModelPolicy.js'
const model = atomicCode => ({ atomicCode })
const generic = ['9275710','regular'].map(model)
const person = model('1001003')
const ready = { passthrough: false, allowed: ['1001003'], denied: [{ modelCode:'9275710', reason:'scene mismatch' }] }
assert.deepEqual(allowedAdditionalModels([person,model('other')],ready).map(x=>x.atomicCode), ['1001003'])
let x = graphModelOptions(generic,[person],ready,'ready')
assert.equal(x.find(i=>i.model.atomicCode==='9275710').disabled,true)
assert.equal(x.find(i=>i.model.atomicCode==='9275710').reason,'scene mismatch')
assert.equal(x.find(i=>i.model.atomicCode==='1001003').disabled,false)
assert.equal(x.find(i=>i.model.atomicCode==='regular').disabled,false)
x = graphModelOptions([...generic,person],[],{passthrough:false,allowed:[],denied:[]},'ready')
assert.equal(x.find(i=>i.model.atomicCode==='1001003').disabled,true)
x = graphModelOptions([...generic,person],[],null,'error')
assert.equal(x.find(i=>i.model.atomicCode==='1001003').disabled,true)
assert.equal(x.find(i=>i.model.atomicCode==='9275710').disabled,true)
assert.equal(x.find(i=>i.model.atomicCode==='regular').disabled,false)
x = graphModelOptions([...generic,person],[],{passthrough:true,allowed:[],denied:[{modelCode:'9275710'}]},'ready')
assert.equal(x.find(i=>i.model.atomicCode==='9275710').disabled,false)
assert.equal(x.find(i=>i.model.atomicCode==='1001003').disabled,false)
const requestGate = createLatestRequest()
const earlier = requestGate.next()
const later = requestGate.next()
assert.equal(requestGate.current(earlier), false)
assert.equal(requestGate.current(later), true)
requestGate.invalidate()
assert.equal(requestGate.current(later), false)
for (const name of ['ArrangeFlow.vue','NodeDetailPanel.vue','DynamicForm.vue']) {
  const filename=`src/views/gam/countManagement/arrangeDetail/flow/${name}`,source=readFileSync(filename,'utf8'),parsed=parse(source,{filename})
  assert.deepEqual(parsed.errors,[],`${name} parse`)
  compileScript(parsed.descriptor,{id:name})
  const template=compileTemplate({source:parsed.descriptor.template.content,filename,id:name})
  assert.deepEqual(template.errors,[],`${name} template`)
}
console.log('policy cases and 3 Vue components passed')
