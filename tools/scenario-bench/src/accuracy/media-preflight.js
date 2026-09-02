import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);

export async function inspectLocalMedia(cases, {
  executable = 'ffprobe',
  concurrency = 8,
  execute = execFileAsync,
} = {}) {
  if (!Array.isArray(cases) || !cases.length) {
    throw new Error('media preflight requires at least one case');
  }
  await execute(executable, ['-version'], {
    timeout: 5_000,
    maxBuffer: 256 * 1024,
  });

  const representativeByHash = new Map();
  for (const item of cases) {
    if (!representativeByHash.has(item.sha256)) representativeByHash.set(item.sha256, item);
  }
  const uniqueCases = [...representativeByHash.values()];
  const uniqueResults = new Array(uniqueCases.length);
  let nextIndex = 0;
  const workerCount = Math.min(normalizeConcurrency(concurrency), uniqueCases.length);
  await Promise.all(Array.from({ length: workerCount }, async () => {
    while (nextIndex < uniqueCases.length) {
      const index = nextIndex;
      nextIndex += 1;
      uniqueResults[index] = await inspectCase(uniqueCases[index], executable, execute);
    }
  }));
  const resultByHash = new Map(uniqueCases.map((item, index) => [item.sha256, uniqueResults[index]]));
  const results = cases.map((item) => ({ ...resultByHash.get(item.sha256), id: item.id }));

  const valid = results.filter((item) => item.ok);
  const invalid = results.filter((item) => !item.ok).map(({ id, reason }) => ({ id, reason }));
  const durations = valid.map((item) => item.durationSec).sort((a, b) => a - b);
  const duplicateGroups = duplicateContentGroups(cases);
  return {
    checked: results.length,
    uniqueContent: uniqueCases.length,
    valid: valid.length,
    invalid,
    durationSec: durations.length ? {
      min: round(durations[0]),
      median: round(quantile(durations, 0.5)),
      p95: round(quantile(durations, 0.95)),
      max: round(durations.at(-1)),
    } : null,
    duplicateContent: {
      groups: duplicateGroups.length,
      extraCases: duplicateGroups.reduce((sum, group) => sum + group.ids.length - 1, 0),
      groupsByTask: duplicateGroups,
    },
    cases: results.filter((item) => item.ok).map((item) => ({
      id: item.id,
      durationSec: item.durationSec,
      codec: item.codec,
      width: item.width,
      height: item.height,
    })),
  };
}

async function inspectCase(item, executable, execute) {
  try {
    const { stdout } = await execute(executable, [
      '-v', 'error',
      '-select_streams', 'v:0',
      '-show_entries', 'stream=codec_name,codec_type,width,height:format=duration',
      '-of', 'json',
      item.absoluteFile,
    ], {
      timeout: 15_000,
      maxBuffer: 1024 * 1024,
    });
    const parsed = JSON.parse(stdout);
    const stream = parsed?.streams?.find((candidate) => candidate.codec_type === 'video')
      ?? parsed?.streams?.[0];
    const durationSec = Number(parsed?.format?.duration);
    if (!stream || !Number.isFinite(durationSec) || durationSec <= 0) {
      throw new Error('no video stream or finite duration');
    }
    return {
      id: item.id,
      ok: true,
      durationSec,
      codec: String(stream.codec_name ?? 'unknown'),
      width: finiteInteger(stream.width),
      height: finiteInteger(stream.height),
    };
  } catch (error) {
    return { id: item.id, ok: false, reason: sanitizeProbeError(error, item.absoluteFile) };
  }
}

function duplicateContentGroups(cases) {
  const byTaskAndHash = new Map();
  for (const item of cases) {
    const key = `${item.task}\0${item.sha256}`;
    const group = byTaskAndHash.get(key) ?? [];
    group.push(item.id);
    byTaskAndHash.set(key, group);
  }
  return [...byTaskAndHash.entries()]
    .filter(([, ids]) => ids.length > 1)
    .map(([key, ids]) => ({ task: key.split('\0', 1)[0], ids }))
    .sort((a, b) => a.task.localeCompare(b.task) || a.ids[0].localeCompare(b.ids[0]));
}

function sanitizeProbeError(error, absoluteFile) {
  const stderr = String(error?.stderr ?? '').trim();
  const raw = stderr || error?.message || 'ffprobe failed';
  const withoutPath = absoluteFile ? raw.replaceAll(absoluteFile, '[sample]') : raw;
  return withoutPath.replace(/\s+/gu, ' ').slice(0, 240);
}

function normalizeConcurrency(value) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1 || parsed > 32) {
    throw new Error('media preflight concurrency must be an integer between 1 and 32');
  }
  return parsed;
}

function quantile(sorted, value) {
  return sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * value))];
}

function finiteInteger(value) {
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed >= 0 ? parsed : null;
}

function round(value) {
  return Math.round(value * 1000) / 1000;
}
