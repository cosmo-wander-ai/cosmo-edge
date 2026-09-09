import { sleepWithSignal, throwIfAborted } from '../shutdown-signal.js';

export async function collectPersistedEvents({
  client,
  channelName,
  channelId,
  algorithmCode,
  timeBegin,
  timeEnd,
  flushTimeoutSec = 15,
  pollIntervalSec = 2,
  settleMinSec = Math.min(5, flushTimeoutSec),
  pageSize = 200,
  signal,
  now = () => Date.now(),
  sleep = sleepWithSignal,
} = {}) {
  if (!client?.eventPage) throw new Error('event collector requires client.eventPage');
  if (!channelName || !channelId || !algorithmCode) {
    throw new Error('channelName, channelId, and algorithmCode are required');
  }
  const startedAt = now();
  const deadline = startedAt + positiveSeconds(flushTimeoutSec, 'flushTimeoutSec') * 1000;
  const pollMs = positiveSeconds(pollIntervalSec, 'pollIntervalSec') * 1000;
  const settleMinMs = positiveSeconds(settleMinSec, 'settleMinSec') * 1000;
  let previousKey = null;
  let stableSnapshots = 0;
  let queryCount = 0;
  let latest = [];

  while (true) {
    throwIfAborted(signal);
    latest = await queryPersistedEventSnapshot({
      client,
      channelName,
      channelId,
      algorithmCode,
      timeBegin,
      timeEnd,
      pageSize,
      signal,
    });
    queryCount += 1;
    const key = latest.map((event) => event.id).join('\n');
    if (key === previousKey) stableSnapshots += 1;
    else stableSnapshots = 1;
    previousKey = key;
    if (stableSnapshots >= 2 && now() - startedAt >= settleMinMs) {
      return { settled: true, queryCount, events: latest };
    }
    if (now() >= deadline) {
      throw new Error(`Event/Page did not settle within ${flushTimeoutSec}s`);
    }
    await sleep(Math.min(pollMs, Math.max(0, deadline - now())), signal);
  }
}

export async function queryPersistedEventSnapshot({
  client,
  channelName,
  channelId,
  algorithmCode,
  timeBegin,
  timeEnd,
  pageSize,
  signal,
}) {
  const rows = [];
  for (let pageNum = 1; ; pageNum += 1) {
    throwIfAborted(signal);
    const response = await client.eventPage({
      timeBegin,
      timeEnd,
      pageNum,
      pageSize,
      algorithmCodes: [String(algorithmCode)],
      videoChannelName: channelName,
      reportStatus: -1,
    }, { signal });
    const page = Array.isArray(response?.rows) ? response.rows : [];
    rows.push(...page);
    const total = Number(response?.total ?? rows.length);
    if (page.length === 0 || page.length < pageSize || rows.length >= total) break;
  }
  const byId = new Map();
  for (const event of rows
    .filter((item) => String(item.videoChannelId ?? '') === String(channelId))
    .filter((item) => String(item.channelName ?? '') === String(channelName))
    .filter((item) => String(item.algorithmCode ?? '') === String(algorithmCode))
    .filter((item) => Number(item.timestamp) >= Number(timeBegin)
      && Number(item.timestamp) <= Number(timeEnd))) {
    const id = String(event.id ?? '').trim();
    if (!id) throw new Error('Event/Page returned an event without id');
    byId.set(id, event);
  }
  return [...byId.values()].sort((a, b) =>
    Number(a.timestamp ?? 0) - Number(b.timestamp ?? 0)
    || String(a.id).localeCompare(String(b.id)));
}

function positiveSeconds(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) throw new Error(`${label} must be positive`);
  return number;
}
