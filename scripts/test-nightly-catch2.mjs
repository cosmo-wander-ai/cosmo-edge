import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync, rmSync, mkdirSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import { spawnSync } from 'node:child_process'
import { readWorkflow } from './check-build-contracts.mjs'

const job = readWorkflow('sophon').jobs['test-sophon']
const step = job.steps.find(s => s.id === 'catch2')
assert.ok(step?.run, 'nightly Catch2 step id required')
const names = ['normal case', 'comma, name', 'quote " name', 'back\\slash name']
// Fixed expectations independent of the tested shell implementation.
const expected = ['"normal case"', '"comma\\, name"', '"quote \\" name"', '"back\\\\slash name"'].sort()
function exercise(script, status, empty = false, options = {}) {
  const dir = mkdtempSync(join(tmpdir(), 'nightly contracts '))
  try {
    mkdirSync(join(dir, 'bin'))
    writeFileSync(join(dir, 'bin/timeout'), '#!/bin/bash\nwhile [[ "$1" != ./cosmo-tests ]]; do shift; done\nexec "$@"\n', { mode: 0o755 })
    writeFileSync(join(dir, 'cosmo-tests'), `#!/usr/bin/env python3
import os, sys
if '--list-tests' in sys.argv:
 print(${JSON.stringify(empty ? '' : names.join('\n'))})
 sys.exit(0)
sys.exit(int(os.environ['FAKE_STATUS']))
`, { mode: 0o755 })
    const scriptPath = join(dir, 'actual-run.sh')
    writeFileSync(scriptPath, script)
    const helper = options.helper || 'fixture_run'
    const wrapper = `${helper}() { bash \"$1\"; }; ${helper} \"$1\"${options.forceSuccess ? '; exit 0' : ''}`
    const result = spawnSync('bash', ['-c', wrapper, 'fixture', scriptPath], { cwd: dir, encoding: 'utf8', timeout: 15000,
      env: { ...process.env, ...Object.fromEntries(Object.entries(job.env).map(([k, v]) => [k, String(v)])),
        COSMO_CANDIDATE_RUNTIME_DIR: dir, COSMO_SOPHON_LD_LIBRARY_PATH: dir,
        RUNNER_TEMP: dir, COSMO_CATCH2_SHARDS: '2', PATH: `${dir}/bin:${process.env.PATH}`, FAKE_STATUS: String(status) } })
    const results = join(dir, 'test-results/catch2')
    if (!empty) {
      if (options.corruptFilter) {
        const paths = readdirSync(join(results, 'shards')).filter(x => x.endsWith('.filter')).map(x => join(results, 'shards', x))
        const target = paths.find(path => readFileSync(path, 'utf8').includes('\\,'))
        assert.ok(target, 'fixture comma filter must exist')
        const before = readFileSync(target, 'utf8')
        const after = before.replace('\\,', ',')
        assert.notEqual(after, before, 'filter mutation must take effect')
        writeFileSync(target, after)
      }
      const filters = readdirSync(join(results, 'shards')).filter(x => x.endsWith('.filter'))
        .flatMap(x => readFileSync(join(results, 'shards', x), 'utf8').trim().split('\n')).sort()
      assert.deepEqual(filters, expected, 'literal Catch2 names must round-trip')
    }
    assert.equal(result.status === 0, status === 0 && !empty, 'Catch2 final status contract: ' + result.stdout + result.stderr)
  } finally { rmSync(dir, { recursive: true, force: true }) }
}
for (const status of [0, 42, 124, 137, 139]) exercise(step.run, status)
exercise(step.run, 0, true)
// Rename only a fixture-owned wrapper; product helper names are unrestricted.
exercise(step.run, 0, false, { helper: 'fixture_equivalent_name' })
assert.throws(() => exercise(step.run, 42, false, { forceSuccess: true }), /Catch2 final status contract/)
assert.throws(() => exercise(step.run, 0, true, { forceSuccess: true }), /Catch2 final status contract/)
// Corrupt a generated filter at the output boundary, after executing real run bytes.
assert.throws(() => exercise(step.run, 0, false, { corruptFilter: true }), /literal Catch2 names/)
console.log('Nightly shell success, escaping, failure, timeout, crash and zero-test contracts passed')
