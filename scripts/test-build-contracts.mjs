import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { root, yaml, compose, checkMac, checkPackage, readWorkflow, checkWorkflow } from './check-build-contracts.mjs'

const temporary = mkdtempSync(join(tmpdir(), 'cosmo contracts '))
try {
  // JSON is valid YAML: serialization changes all key/quote/layout choices.
  for (const kind of ['sophon', 'rockchip']) {
    const original = readWorkflow(kind)
    checkWorkflow(original, kind)
    const equivalent = structuredClone(original)
    equivalent.on.schedule[0].cron = '31 3 * * 2'
    for (const job of Object.values(equivalent.jobs)) for (const step of job.steps) step.name = 'A valid display title'
    checkWorkflow(yaml(JSON.stringify(equivalent)), kind)
    const missingUpload = structuredClone(original)
    const key = kind === 'sophon' ? 'build-sophon' : 'package'
    missingUpload.jobs[key].steps = missingUpload.jobs[key].steps.filter(s => !s.uses?.startsWith('actions/upload-artifact@'))
    assert.throws(() => checkWorkflow(missingUpload, kind), /artifact upload/)
    const missing = structuredClone(original)
    missing.jobs[key].steps.find(s => s.uses?.startsWith('actions/upload-artifact@')).with['if-no-files-found'] = 'warn'
    assert.throws(() => checkWorkflow(missing, kind), /missing artifact/)
    for (const disabled of ['pull_request', 'push', 'workflow_call']) {
      const changed = structuredClone(original)
      changed.on[disabled] = {}
      assert.throws(() => checkWorkflow(changed, kind), /disabled trigger/)
    }
    if (kind === 'sophon') {
      const noBuild = structuredClone(original)
      noBuild.jobs[key].steps = noBuild.jobs[key].steps.filter(s => s.id !== 'build_package')
      assert.throws(() => checkWorkflow(noBuild, kind), /Sophon build/)
      for (const path of ['build/install/', 'build/cosmo-tests', 'build/install/lib/libcosmo_model_guard.so.2.0.0']) {
        const wrongPath = structuredClone(original)
        const upload = wrongPath.jobs[key].steps.find(s => s.with?.path === path)
        assert.ok(upload, 'upload mutation target')
        upload.with.path = 'wrong-output/'
        assert.throws(() => checkWorkflow(wrongPath, kind), /required upload path/)
      }
    }
    if (kind === 'rockchip') {
      const bad = structuredClone(original)
      bad.jobs.package.strategy.matrix.include.pop()
      assert.throws(() => checkWorkflow(bad, kind))
      const noAudit = structuredClone(original)
      noAudit.jobs.package.steps = noAudit.jobs.package.steps.filter(s => s.id !== 'verify_artifacts')
      assert.throws(() => checkWorkflow(noAudit, kind), /artifact verification/)
      const wrongTarget = structuredClone(original)
      wrongTarget.jobs.package.steps.find(s => s.id === 'build_package').env.COSMO_TARGET_CHIP = 'rk3576'
      assert.throws(() => checkWorkflow(wrongTarget, kind))
    }
  }
  for (const kind of ['sophon', 'rockchip']) {
    const original = compose(join(root, `docker-compose.${kind}.yml`))
    // Compose's normalized long syntax is an equivalent input to Compose itself.
    const equivalent = structuredClone(original)
    const file = join(temporary, `${kind}.json`)
    writeFileSync(file, JSON.stringify(equivalent))
    checkPackage(compose(file), kind)
    equivalent.services[`cosmo-${kind}-package`].volumes.find(v => v.target === '/workspace').source = '/wrong-workspace'
    assert.notDeepEqual(equivalent, original)
    writeFileSync(file, JSON.stringify(equivalent))
    assert.throws(() => checkPackage(compose(file), kind), /workspace bind source/)
  }
  const parsed = compose(join(root, 'docker-compose.x86.macos.yml'))
  const file = join(temporary, 'compose.json')
  // Reverse normalized lists and object keys: none of their positions are contracts.
  parsed.services['cosmo-x86-macos'].volumes.reverse()
  parsed.services['cosmo-x86-macos'].ports.reverse()
  parsed.services = Object.fromEntries(Object.entries(parsed.services).reverse())
  writeFileSync(file, JSON.stringify(parsed))
  checkMac(compose(file))
  for (const [change, reason] of [
    [s => { s.platform = 'linux/arm64' }, /architecture/],
    [s => { s.ports.find(p => p.target === 80).host_ip = '0.0.0.0' }, /loopback/],
    [s => { s.volumes = s.volumes.filter(v => v.target !== '/build_output') }, /bind/]
  ]) {
    const mutant = structuredClone(parsed)
    change(mutant.services['cosmo-x86-macos'])
    assert.notDeepEqual(mutant, parsed, 'mutation must take effect')
    writeFileSync(file, JSON.stringify(mutant))
    assert.throws(() => checkMac(compose(file)), reason)
  }
  console.log('Configuration equivalent and rejecting variants passed')
} finally { rmSync(temporary, { recursive: true, force: true }) }
