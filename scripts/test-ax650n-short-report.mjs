import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { generateShortCapacity, renderShortCapacity, validateShortCapacity } from './generate-ax650n-short-report.mjs';

const source = new URL('../docs/benchmarks/scenario-bench/ax650n/short-capacity.json', import.meta.url);
const data = JSON.parse(fs.readFileSync(source, 'utf8'));

test('preserves the measured lower bound and incomplete fixed points', () => {
  validateShortCapacity(data);
  const v7 = data.observations.filter((item) => item.series === 'v7');
  assert.equal(v7.find((item) => item.id === 'v7-mixed-25ch').result, 'PASS');
  assert.equal(v7.find((item) => item.id === 'v7-mixed-30ch').result, 'INCOMPLETE');
  assert.equal(v7.find((item) => item.id === 'v7-mixed-25ch-120s').result, 'INCOMPLETE');
  assert.notEqual(data.input.sha256, data.input.referenceV11Sha256);
  assert.equal(v7.find((item) => item.id === 'v7-mixed-25ch').sampleSpanSeconds, 27.013);
  assert.equal(v7.find((item) => item.id === 'v7-mixed-25ch').classifierForwardSuccessDelta, 5323);
  assert.equal(v7.find((item) => item.id === 'v7-mixed-30ch').decodeDiscardDelta, 502 + 900 + 549);
  const integrated = data.observations.find((item) => item.id === 'integrated-4de3-mixed-25ch');
  assert.equal(integrated.result, 'PASS');
  assert.equal(integrated.minimumNodeFps, 4.7391610204);
  assert.equal(integrated.classifierForwardSuccessDelta, 5300);
  assert.equal(integrated.classifierForwardFailureDelta, 0);
  assert.equal(integrated.classifierNonForwardFailureDelta, 0);
  assert.equal(integrated.sampleSpanSeconds, 27.009);
});

test('rejects missing evidence and incomplete points without reasons', () => {
  const altered = structuredClone(data);
  altered.observations[0].sourceSha256 = '';
  assert.throws(() => validateShortCapacity(altered));
  const incomplete = structuredClone(data);
  incomplete.observations.find((item) => item.result === 'INCOMPLETE').reason = null;
  assert.throws(() => validateShortCapacity(incomplete));
});

test('renders both languages and escapes data', () => {
  const modified = structuredClone(data);
  modified.observations.find((item) => item.result === 'INCOMPLETE').reason = '<unsafe>';
  const html = renderShortCapacity(modified, 'zh-CN');
  assert.match(html, /&lt;unsafe&gt;/);
  assert.doesNotMatch(html, /<unsafe>/);
  assert.match(html, /27\.013 s/);
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'ax650n-short-report-'));
  try {
    generateShortCapacity({ destination: directory });
    assert.ok(fs.readFileSync(path.join(directory, 'report.zh-CN.html'), 'utf8').includes('短时容量观测'));
    assert.ok(fs.readFileSync(path.join(directory, 'report.html'), 'utf8').includes('Short-run capacity observation'));
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(directory, 'short-capacity.json'))), data);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('a later measured passing point updates the rendered lower bound', () => {
  const updated = structuredClone(data);
  updated.observations.push({ ...updated.observations.find((item) => item.id === 'v7-mixed-25ch'), id: 'v7-mixed-30ch-retest', channels: 30 });
  assert.match(renderShortCapacity(updated, 'en'), /Highest passing fixed point: 30 channels/);
});

test('separates a different engine series and its boundary', () => {
  const updated = structuredClone(data);
  updated.series.push({ id: 'retest', description: 'Independent build', engineSha256: 'a'.repeat(64) });
  updated.observations.push({ ...updated.observations.find((item) => item.id === 'v7-mixed-25ch'), id: 'retest-mixed-30ch', series: 'retest', channels: 30 });
  const html = renderShortCapacity(updated, 'en');
  assert.match(html, /Highest passing fixed point: 25 channels/);
  assert.match(html, /<h2>retest<\/h2><p>SHA-256: <code>a{64}<\/code><\/p><p>Highest passing fixed point: 30 channels/);
});
