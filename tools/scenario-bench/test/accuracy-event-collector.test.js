import assert from 'node:assert/strict';
import test from 'node:test';

import { collectPersistedEvents } from '../src/accuracy/event-collector.js';

test('event collection paginates, filters, deduplicates, and settles on two identical snapshots', async () => {
  let poll = 0;
  const calls = [];
  const snapshots = [
    [
      { id: 'a', videoChannelId: 'channel-1', channelName: 'acc-case', algorithmCode: '15', timestamp: 150 },
      { id: 'wrong', videoChannelId: 'other', channelName: 'acc-case', algorithmCode: '15', timestamp: 150 },
    ],
    [
      { id: 'a', videoChannelId: 'channel-1', channelName: 'acc-case', algorithmCode: '15', timestamp: 150 },
      { id: 'b', videoChannelId: 'channel-1', channelName: 'acc-case', algorithmCode: '15', timestamp: 160 },
    ],
    [
      { id: 'a', videoChannelId: 'channel-1', channelName: 'acc-case', algorithmCode: '15', timestamp: 150 },
      { id: 'b', videoChannelId: 'channel-1', channelName: 'acc-case', algorithmCode: '15', timestamp: 160 },
    ],
  ];
  const client = {
    async eventPage(payload) {
      calls.push(payload);
      const rows = snapshots[Math.min(poll, snapshots.length - 1)];
      if (payload.pageNum === 1) poll += 1;
      return { total: rows.length, rows };
    },
  };
  let now = 0;
  const result = await collectPersistedEvents({
    client,
    channelName: 'acc-case',
    channelId: 'channel-1',
    algorithmCode: '15',
    timeBegin: 100,
    timeEnd: 200,
    flushTimeoutSec: 10,
    pollIntervalSec: 1,
    pageSize: 100,
    now: () => now,
    sleep: async (ms) => { now += ms; },
  });
  assert.equal(result.settled, true);
  assert.deepEqual(result.events.map((item) => item.id), ['a', 'b']);
  assert.equal(calls[0].videoChannelName, 'acc-case');
  assert.deepEqual(calls[0].algorithmCodes, ['15']);
});

test('event collection fails closed when the event set never settles', async () => {
  let index = 0;
  let now = 0;
  const client = {
    async eventPage() {
      index += 1;
      return { total: 1, rows: [{
        id: `event-${index}`, videoChannelId: 'channel', channelName: 'name',
        algorithmCode: '15', timestamp: 150,
      }] };
    },
  };
  await assert.rejects(
    collectPersistedEvents({
      client, channelName: 'name', channelId: 'channel', algorithmCode: '15',
      timeBegin: 100, timeEnd: 200, flushTimeoutSec: 2, pollIntervalSec: 1,
      now: () => now,
      sleep: async (ms) => { now += ms; },
    }),
    /did not settle/i,
  );
});
