import assert from 'node:assert/strict';
import test from 'node:test';

import { buildRunIdentity } from '../src/accuracy/identity.js';
import { makeAccuracyFixture } from './helpers/accuracy-v4-fixture.js';

test('run identity reuses the caller-frozen tool identity', () => {
  const { suite, execution } = makeAccuracyFixture({ caseCount: 1 });
  const tool = {
    nodeVersion: 'test-node',
    files: { 'src/example.js': 'a'.repeat(64) },
    repository: { commit: 'b'.repeat(40), tree: 'c'.repeat(40), dirty: false },
  };
  const identity = buildRunIdentity({
    suite,
    targetChip: 'bm1688',
    device: { deviceFingerprint: 'device', softwareVersion: 'V1' },
    runId: 'run',
    execution,
    tool,
  });
  assert.deepEqual(identity.tool, tool);
  assert.notEqual(identity.tool, tool);
});
