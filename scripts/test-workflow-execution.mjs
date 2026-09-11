import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import { createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { readWorkflow } from './check-build-contracts.mjs'

function execute(script, cwd, env, forceSuccess = false) {
  const file = join(cwd, 'actual-workflow-run.sh')
  writeFileSync(file, script)
  const args = forceSuccess
    ? ['-c', 'bash -e -o pipefail "$1"; exit 0', 'fixture', file]
    : ['-e', '-o', 'pipefail', file]
  return spawnSync('bash', args, { cwd, env, encoding: 'utf8' })
}

const job = readWorkflow('rockchip').jobs.package
const build = job.steps.find(s => s.id === 'build_package')
const audit = job.steps.find(s => s.id === 'verify_artifacts')
assert.ok(build?.run && audit?.run, 'build and artifact verification steps required')
for (const chip of ['rk3576', 'rv1126b']) {
  const dir = mkdtempSync(join(tmpdir(), 'workflow execution '))
  try {
    const bin = join(dir, 'bin'); mkdirSync(bin)
    writeFileSync(join(bin, 'docker'), `#!/usr/bin/env python3
import json, os, sys
with open(os.environ['LOG'], 'w') as f: json.dump({'argv': sys.argv[1:], 'chip': os.getenv('COSMO_TARGET_CHIP'), 'models': os.getenv('COSMO_PACKAGE_MODELS')}, f)
sys.exit(int(os.getenv('BUILD_STATUS','0')))
`, { mode: 0o755 })
    writeFileSync(join(bin, 'file'), '#!/bin/sh\necho "ELF 64-bit LSB pie executable, ARM aarch64"\n', { mode: 0o755 })
    // Resolve only this declared matrix contract, never arbitrary expressions.
    const substitutions = { '${{ matrix.chip }}': chip, '${{ matrix.models }}': 'include' }
    const env = { ...process.env, ...Object.fromEntries(Object.entries(build.env).map(([k,v]) => [k, substitutions[v] || v])),
      CHIP: chip, PATH: `${bin}:${process.env.PATH}`, LOG: join(dir, 'calls.json'), GITHUB_STEP_SUMMARY: join(dir, 'summary.md') }
    const run = (script, extra = {}, forceSuccess = false) => execute(script, dir, { ...env, ...extra }, forceSuccess)
    const verifyBuild = (script, forceSuccess = false) => {
      const result = run(script)
      assert.equal(result.status, 0, result.stderr)
      assert.deepEqual(JSON.parse(readFileSync(env.LOG, 'utf8')), { argv: ['compose', '-f', 'docker-compose.rockchip.yml', 'run', '--rm', 'cosmo-rockchip-package'], chip, models: 'include' })
      assert.notEqual(run(script, { BUILD_STATUS: '17' }, forceSuccess).status, 0, 'build failure must propagate')
    }
    verifyBuild(build.run)
    verifyBuild(`command docker compose \\\n-f docker-compose.rockchip.yml run --rm cosmo-rockchip-package\n`)
    assert.throws(() => verifyBuild(build.run, true), /build failure/)
    const output = join(dir, 'build_output', chip); mkdirSync(output, { recursive: true })
    const filename = 'cosmo-fixture.tar.gz'
    writeFileSync(join(output, filename), 'candidate')
    const digest = createHash('sha256').update('candidate').digest('hex')
    writeFileSync(join(output, 'SHA256SUMS'), `${digest}  ${filename}\n`)
    writeFileSync(join(output, 'TARGET_CHIP'), `${chip}\n`)
    assert.equal(run(audit.run).status, 0, 'valid candidate artifacts')
    writeFileSync(join(output, 'TARGET_CHIP'), 'wrong-chip\n')
    assert.notEqual(run(audit.run).status, 0, 'wrong target marker must fail')
    writeFileSync(join(output, 'TARGET_CHIP'), `${chip}\n`)
    writeFileSync(join(output, filename), 'tampered')
    assert.notEqual(run(audit.run).status, 0, 'wrong checksum must fail')
    rmSync(join(output, filename))
    assert.notEqual(run(audit.run).status, 0, 'missing archive must fail')
  } finally { rmSync(dir, { recursive: true, force: true }) }
}
console.log('Real workflow build argv, failure propagation and artifact checks passed')

const sophon = readWorkflow('sophon').jobs['build-sophon']
const sophonBuild = sophon.steps.find(s => s.id === 'build_package')
assert.ok(sophonBuild?.run, 'Sophon build step required')
const directory = mkdtempSync(join(tmpdir(), 'sophon workflow '))
try {
  mkdirSync(join(directory, 'scripts'))
  writeFileSync(join(directory, 'scripts/build.sh'), `#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
if os.getenv('CORRUPT_ARGV') == '1': args = [value for value in args if value != '-T']
with open(os.environ['LOG'], 'w') as f: json.dump(args, f)
sys.exit(int(os.getenv('BUILD_STATUS','0')))
`, { mode: 0o644 })
  const log = join(directory, 'calls.json')
  function verify(script, options = {}) {
    const env = { ...process.env, ...sophon.env, LOG: log, CORRUPT_ARGV: options.corruptArgv ? '1' : '0' }
    const run = extra => execute(script, directory, { ...env, ...extra }, options.forceSuccess)
    const result = run({})
    assert.equal(result.status, 0, result.stderr)
    assert.deepEqual(JSON.parse(readFileSync(log, 'utf8')), ['-T', '-c', 'bm1688'], 'Sophon build argv contract')
    assert.notEqual(run({ BUILD_STATUS: '19' }).status, 0, 'Sophon build failure must propagate')
  }
  verify(sophonBuild.run)
  verify('chmod +x scripts/build.sh\nentry=scripts/build.sh\n"$entry" -T -c bm1688\n')
  assert.throws(() => verify(sophonBuild.run, { forceSuccess: true }), /failure must propagate/)
  assert.throws(() => verify(sophonBuild.run, { corruptArgv: true }), /Sophon build argv contract/)

} finally { rmSync(directory, { recursive: true, force: true }) }
console.log('Sophon workflow build presence, argv and failure contracts passed')
