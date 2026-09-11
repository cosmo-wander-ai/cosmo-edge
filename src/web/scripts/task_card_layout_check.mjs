import assert from 'node:assert/strict'
import { spawn, spawnSync } from 'node:child_process'
import { readFile, mkdtemp, rm, stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const chrome = process.env.CHROME_BIN || ['google-chrome', 'chromium', 'chromium-browser'].find((name) => spawnSync('which', [name]).status === 0)
if (!chrome) throw Error('BLOCKED: installed Chrome/Chromium is required')
if (typeof WebSocket !== 'function') throw Error('BLOCKED: Node with built-in WebSocket is required (Node 22+)')
const dist = process.env.LAYOUT_FIXTURE_DIST || fileURLToPath(new URL('../node_modules/.cache/f01-layout/dist/', import.meta.url))
await stat(path.join(dist, 'index.html')).catch(() => { throw Error('BLOCKED: build layout fixture in Docker with npm run task-card-layout:build first') })
const server = createServer(async (req, res) => {
  try {
    const pathname = new URL(req.url, 'http://localhost').pathname
    const file = path.resolve(dist, `.${pathname === '/' ? '/index.html' : pathname}`)
    assert.ok(file.startsWith(dist))
    res.setHeader('Content-Type', file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html')
    res.end(await readFile(file))
  } catch { res.statusCode = 404; res.end() }
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const profile = await mkdtemp(path.join(tmpdir(), 'cosmo-layout-'))
const browser = spawn(chrome, ['--headless=new', '--no-sandbox', '--disable-gpu', '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] })
let socket
let closeBrowser
try {
  const endpoint = await new Promise((resolve, reject) => {
    let text = ''
    const timer = setTimeout(() => reject(Error('Chrome startup timed out')), 15000)
    browser.stderr.on('data', (chunk) => { text += chunk; const match = text.match(/DevTools listening on (ws:\/\/[^\s]+)/); if (match) { clearTimeout(timer); resolve(match[1]) } })
    browser.on('exit', (code) => { clearTimeout(timer); reject(Error(`Chrome exited ${code}: ${text}`)) })
  })
  socket = new WebSocket(endpoint)
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject })
  let id = 0
  const pending = new Map()
  socket.onmessage = ({data}) => { const message = JSON.parse(data); const callback = pending.get(message.id); if (callback) { pending.delete(message.id); message.error ? callback.reject(Error(JSON.stringify(message.error))) : callback.resolve(message.result) } }
  const command = (method, params = {}, sessionId) => new Promise((resolve, reject) => { const current = ++id; pending.set(current, {resolve, reject}); socket.send(JSON.stringify({id:current, method, params, sessionId})) })
  closeBrowser = () => command('Browser.close')
  const {targetId} = await command('Target.createTarget', {url:'about:blank'})
  const {sessionId} = await command('Target.attachToTarget', {targetId, flatten:true})
  const call = (method, params) => command(method, params, sessionId)
  const evaluate = async (expression) => { const result = await call('Runtime.evaluate', {expression, awaitPromise:true, returnByValue:true}); if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails)); return result.result.value }
  const ready = async () => evaluate(`new Promise((resolve,reject) => { const until=performance.now()+15000; function check(){if(document.body?.dataset.ready === 'true')return resolve(true); if(performance.now()>until)return reject(Error('fixture not ready')); requestAnimationFrame(check)}check()})`)
  const click = async (selector) => {
    const point = await evaluate(`(() => { const e=document.querySelector(${JSON.stringify(selector)}); if(!e)throw Error('missing control'); const r=e.getBoundingClientRect(); const x=r.x+r.width/2,y=r.y+r.height/2; if(!e.contains(document.elementFromPoint(x,y)))throw Error('control obscured'); return {x,y} })()`)
    await call('Input.dispatchMouseEvent', {type:'mousePressed', button:'left', clickCount:1, ...point})
    await call('Input.dispatchMouseEvent', {type:'mouseReleased', button:'left', clickCount:1, ...point})
    await evaluate('new Promise(requestAnimationFrame)')
  }
  for (const locale of ['zh-CN','en-US']) for (const [width,height] of [[1280,720],[1920,1080]]) {
    await call('Emulation.setDeviceMetricsOverride', {width,height,deviceScaleFactor:1,mobile:false})
    for (const count of [0,1,48]) {
      await call('Page.navigate', {url:`http://127.0.0.1:${server.address().port}/?locale=${locale}&count=${count}`})
      await ready()
      const geometry = await evaluate(`(async () => {
        const visible = e => {const r=e.getBoundingClientRect(); return r.width>0&&r.height>0&&r.left>=-1&&r.top>=-1&&r.right<=innerWidth+1&&r.bottom<=innerHeight+1}
        const toolbar=document.querySelector('.task-toolbar'),pager=document.querySelector('.pagination-container'),grid=document.querySelector('.task-grid');
        grid.scrollTop=grid.scrollHeight; await new Promise(requestAnimationFrame);
        const cards=[...document.querySelectorAll('.task-card')],last=cards.at(-1);
        const buttons=last?[...last.querySelectorAll('.card-actions button')]:[];
        return {toolbar:visible(toolbar),pager:visible(pager),count:cards.length,overflow:document.documentElement.scrollWidth>innerWidth+1||grid.scrollWidth>grid.clientWidth+1,last:!last||visible(last),buttons:buttons.every(e=>{const r=e.getBoundingClientRect();return visible(e)&&e.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2))})}
      })()`)
      assert.deepEqual(geometry, {toolbar:true,pager:true,count,overflow:false,last:true,buttons:true}, `${locale} ${width}x${height} count=${count}`)
      if(count) { await click('.task-card:last-child .card-actions button'); assert.equal(await evaluate('document.body.dataset.clicked'), 'true') }
    }
  }
  await call('Page.navigate', {url:`http://127.0.0.1:${server.address().port}/?mode=parameter`})
  await ready()
  for (const editable of [false,true]) {
    await click('.el-checkbox')
    await click('#save')
    const [saved] = JSON.parse(await evaluate("document.querySelector('#saved').textContent"))
    assert.equal(saved.channelEditable, editable)
    assert.equal(saved.senior, editable ? 0 : 2)
  }
  console.log('Task card browser geometry and checkbox roundtrip checks passed (12 layouts)')
} finally {
  closeBrowser?.().catch(() => {})
  await new Promise((resolve) => {
    if (browser.exitCode !== null) return resolve()
    const timer = setTimeout(() => browser.kill(), 3000)
    browser.once('exit', () => { clearTimeout(timer); resolve() })
  })
  socket?.close()
  await new Promise((resolve) => server.close(resolve)); await rm(profile, {recursive:true,force:true,maxRetries:10,retryDelay:100})
}
