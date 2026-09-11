import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve, join } from 'node:path'
import { spawnSync } from 'node:child_process'
import { pathToFileURL } from 'node:url'
import { createMarkdownRenderer } from 'vitepress'

export const root = resolve(import.meta.dirname, '..')
const markdown = await createMarkdownRenderer(root)
export function yaml(source) {
  const env = {}
  markdown.render(`---\n${source}\n---\n`, env)
  assert.ok(env.frontmatter && typeof env.frontmatter === 'object', 'YAML frontmatter must parse')
  return env.frontmatter
}
export function compose(file, env = {}) {
  const result = spawnSync('docker', ['compose', '-f', file, 'config', '--format', 'json'], {
    cwd: root, env: { ...process.env, ...env }, encoding: 'utf8'
  })
  assert.equal(result.status, 0, `BLOCKED: Compose V2 config required: ${result.stderr || result.error}`)
  return JSON.parse(result.stdout)
}
const pinned = value => assert.match(value, /^[^\s@]+@sha256:[0-9a-f]{64}$/, 'image must use a valid digest')
export function checkMac(config, jobs = '1', port = '8080') {
  const service = config.services['cosmo-x86-macos']
  assert.equal(service.platform, 'linux/amd64', 'Mac architecture')
  pinned(service.build.args.BUILD_ENV_IMAGE)
  pinned(service.build.args.RUNTIME_BASE_IMAGE)
  assert.equal(String(service.build.args.COSMO_BUILD_JOBS), jobs, 'build jobs')
  assert.equal(service.build.args.RESOURCE_DIR, 'data/resource/aiboxresource_x86')
  assert.equal(service.environment.COSMO_STREAM_PLAY_MODE, 'httpflv-srs')
  assert.equal(String(service.environment.COSMO_STREAM_HTTP_PORT), '18088')
  assert.deepEqual(service.healthcheck.test, ['CMD', '/usr/local/bin/cosmo-x86-healthcheck'])
  assert.ok(!service.devices?.length && !service.cap_add?.length && !Object.keys(service.sysctls || {}).length, 'no devices or network privileges')
  assert.ok(!service.privileged && service.network_mode !== 'host', 'no privileged or host network')
  const ports = service.ports
  for (const binding of ports) {
    assert.ok(['127.0.0.1', '::1'].includes(binding.host_ip), 'loopback binding')
    assert.equal(binding.protocol || 'tcp', 'tcp', 'TCP only')
  }
  for (const [target, published] of [[80, port], [1936, '1936'], [1985, '1985'], [18088, '18088']]) {
    assert.ok(ports.some(p => p.target === target && String(p.published) === published), `missing port ${target}`)
  }
  const data = service.volumes.find(v => v.target === '/data/cwaiuserdata')
  const resource = service.volumes.find(v => v.target === '/appfs/cosmo_wander/cwai_data/resource')
  assert.equal(data?.type, 'volume'); assert.equal(resource?.type, 'volume')
  assert.notEqual(data.source, resource.source, 'independent persistent volumes')
  for (const volume of [data, resource]) assert.match(config.volumes[volume.source].name, /^cosmo-x86-macos-preview-/)
  const output = service.volumes.find(v => v.target === '/build_output')
  assert.equal(output?.type, 'bind'); assert.ok(output.source.endsWith('/build_output/macos-x86'))
}
export function checkPackage(config, kind, env = {}) {
  const service = config.services[`cosmo-${kind}-package`]
  assert.equal(service.working_dir, '/workspace')
  if (kind === 'rockchip' && !env.COSMO_ROCKCHIP_BUILDER_IMAGE) pinned(service.image)
  assert.deepEqual(service.entrypoint, kind === 'sophon' ? ['/bin/bash', '/workspace/scripts/build_sophon_package.sh'] : ['/workspace/scripts/build_rockchip_package.sh'])
  assert.deepEqual(service.command, kind === 'sophon' ? [] : ['--chip', env.COSMO_TARGET_CHIP || 'rk3576', '--models', env.COSMO_PACKAGE_MODELS || 'include'])
  for (const [target, type] of [['/workspace', 'bind'], ['/build_output', 'bind'], ['/root/.npm', 'volume']])
    assert.equal(service.volumes.find(v => v.target === target)?.type, type, `mount ${target}`)
  assert.equal(service.volumes.find(v => v.target === '/workspace').source, root, 'workspace bind source')
  assert.equal(service.volumes.find(v => v.target === '/build_output').source, join(root, 'build_output'), 'output bind source')
  assert.equal(service.volumes.find(v => v.target === '/root/.npm').source, 'cosmo-npm-cache', 'npm cache source')
  const expected = { COSMO_MODEL_GUARD_BUILD_PROFILE: 'public-runtime', COSMO_MODEL_GUARD_SDK_ROOT: '', NPM_CONFIG_MAXSOCKETS: '1', NPM_CONFIG_PREFER_OFFLINE: 'true', NPM_CONFIG_FETCH_RETRIES: '3', NPM_CONFIG_FETCH_TIMEOUT: '120000' }
  if (kind === 'sophon') expected.COSMO_PACKAGE_MODELS = 'include'
  else Object.assign(expected, { COSMO_BUILD_JOBS: '4', COSMO_MODEL_GUARD_SDK_ROOT: '', COSMO_RKNN_ARTIFACT_MANIFEST: '', COSMO_RKNN_MODELS_DIR: '', COSMO_RKNN_RESOURCE_OVERLAY_DIR: '' })
  for (const [key, fallback] of Object.entries(expected)) assert.equal(String(service.environment[key]), env[key] ?? fallback, key)
}
export function checkWorkflow(workflow, kind) {
  assert.ok(Object.hasOwn(workflow.on, 'workflow_dispatch'), 'manual trigger')
  assert.ok(workflow.on.schedule?.length && workflow.on.schedule.every(x => typeof x.cron === 'string' && x.cron.trim().split(/\s+/).length === 5), 'scheduled trigger')
  assert.equal(workflow.permissions.contents, 'read')
  for (const disabled of ['pull_request', 'push', 'workflow_call']) assert.ok(!Object.hasOwn(workflow.on, disabled), `disabled trigger ${disabled}`)
  const job = workflow.jobs[kind === 'rockchip' ? 'package' : 'build-sophon']
  const uploads = job.steps.filter(step => step.uses?.startsWith('actions/upload-artifact@'))
  assert.ok(uploads.length, 'artifact upload required')
  for (const step of uploads) {
    assert.ok(step.with.path.trim(), 'upload path')
    assert.equal(step.with['if-no-files-found'], 'error', 'missing artifact must fail')
  }
  if (kind === 'rockchip') {
    assert.deepEqual(job.strategy.matrix.include.map(x => x.chip).sort(), ['rk3576', 'rv1126b'])
    assert.ok(uploads.some(s => s.with.path.includes('build_output/${{ matrix.chip }}/*.tar.gz') && s.with.path.includes('SHA256SUMS')))
    const build = job.steps.find(s => s.id === 'build_package' && s.run)
    assert.ok(job.steps.some(s => s.id === 'verify_artifacts' && s.run), 'artifact verification required')
    assert.equal(build?.env.COSMO_TARGET_CHIP, '${{ matrix.chip }}')
    assert.equal(build?.env.COSMO_PACKAGE_MODELS, '${{ matrix.models }}')
    const publish = workflow.jobs['publish-builder']
    assert.equal(publish.if, "github.event_name == 'workflow_dispatch'")
    assert.ok([publish.needs].flat().includes('package'))
    assert.equal(publish.permissions.packages, 'write')
  } else {
    assert.ok(job.steps.some(s => s.id === 'build_package' && s.run), 'Sophon build step required')
    for (const requiredPath of ['build/install/', 'build/cosmo-tests', 'build/install/lib/libcosmo_model_guard.so.2.0.0'])
      assert.ok(uploads.some(s => s.with.path.trim().split(/\r?\n/).includes(requiredPath)), `required upload path ${requiredPath}`)
    const shell = job.defaults?.run?.shell || workflow.defaults?.run?.shell
    for (const step of job.steps.filter(s => s.run)) assert.equal(step.shell || shell, 'bash', 'effective build shell')
    assert.equal(job.env.COSMO_MODEL_GUARD_BUILD_PROFILE, 'public-runtime')
    assert.ok(Number(job.env.CARGO_HTTP_TIMEOUT) > 0 && Number(job.env.CARGO_NET_RETRY) > 0)
    assert.ok(workflow.jobs['test-sophon'].steps.some(s => s.id === 'catch2' && s.run))
    assert.ok([workflow.jobs['test-sophon'].needs].flat().includes('build-sophon'))
  }
}
export function readWorkflow(kind) {
  return yaml(readFileSync(join(root, '.github/workflows', kind === 'rockchip' ? 'ci-build-rockchip.yml' : 'nightly-build-test-sophon.yml'), 'utf8'))
}
export function checkComposeContracts() {
  const mac = join(root, 'docker-compose.x86.macos.yml')
  checkMac(compose(mac))
  checkMac(compose(mac, { COSMO_X86_WEB_PORT: '8090', COSMO_X86_BUILD_JOBS: '3' }), '3', '8090')
  const compatibility = compose(join(root, 'docker-compose.rk3576.yml'))
  compatibility.services['cosmo-rockchip-package'] = compatibility.services['cosmo-rk3576-package']
  checkPackage(compatibility, 'rockchip')
  for (const kind of ['sophon', 'rockchip']) {
    const file = join(root, `docker-compose.${kind}.yml`)
    checkPackage(compose(file), kind)
    const env = { COSMO_MODEL_GUARD_BUILD_PROFILE: 'production-release', COSMO_PACKAGE_MODELS: 'preserve', COSMO_TARGET_CHIP: 'rk3576', COSMO_BUILD_JOBS: '7', COSMO_MODEL_GUARD_SDK_ROOT: '/sdk with space', COSMO_RKNN_ARTIFACT_MANIFEST: '/model manifest.json', COSMO_RKNN_MODELS_DIR: '/models', COSMO_RKNN_RESOURCE_OVERLAY_DIR: '/resources', NPM_CONFIG_MAXSOCKETS: '2', NPM_CONFIG_PREFER_OFFLINE: 'false' }
    checkPackage(compose(file, env), kind, env)
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  checkComposeContracts()
  for (const kind of ['sophon', 'rockchip']) checkWorkflow(readWorkflow(kind), kind)
  console.log('Effective Compose and workflow contracts passed')
}
