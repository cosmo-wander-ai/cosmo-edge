import assert from 'node:assert/strict';
import test from 'node:test';

import { CosmoClient } from '../src/cosmo-client.js';

test('accuracy API helpers use the product routes and response contracts', async () => {
  const client = new CosmoClient({ base: 'http://device', token: 'token' });
  const calls = [];
  client._post = async (route, payload) => {
    calls.push({ route, payload });
    if (route === '/Algorithm/Page') return { resData: { rows: [{ algorithmId: '15' }] } };
    if (route === '/schedule/Page') return { resData: { rows: [{ id: 'always' }] } };
    if (route === '/task/page') return { resData: { rows: [{ id: 'task-1' }] } };
    if (route === '/Task/SelectConfigByAlgorithmId') return { resData: { taskConfig: {} } };
    return { resData: {} };
  };

  assert.equal((await client.algorithmPage({ pageNum: 1, pageSize: 10 })).rows[0].algorithmId, '15');
  assert.equal((await client.schedulePage({ pageNum: 1, pageSize: 10 })).rows[0].id, 'always');
  assert.equal((await client.taskPage({ pageNum: 1, pageSize: 10 })).rows[0].id, 'task-1');
  assert.deepEqual(await client.taskSelectConfig({ channelId: 'c', algorithmId: '15' }), { taskConfig: {} });
  await client.taskDelete({ channelId: 'c', algorithmId: '15' });

  assert.deepEqual(calls.map((item) => item.route), [
    '/Algorithm/Page',
    '/schedule/Page',
    '/task/page',
    '/Task/SelectConfigByAlgorithmId',
    '/Task/Delete',
  ]);
});

test('artifact download is authenticated, same-origin, and size bounded', async () => {
  let request = null;
  const client = new CosmoClient({
    base: 'http://device:8080',
    token: 'secret-token',
    fetchImpl: async (url, options) => {
      request = { url, options };
      return new Response(Buffer.from('image'), {
        status: 200,
        headers: { 'Content-Type': 'image/jpeg', 'Content-Length': '5' },
      });
    },
  });

  const result = await client.downloadArtifact('/event/alarm.jpg', { maxBytes: 10 });
  assert.equal(result.buffer.toString(), 'image');
  assert.equal(result.contentType, 'image/jpeg');
  assert.equal(request.url, 'http://device:8080/event/alarm.jpg');
  assert.equal(request.options.headers.token, 'secret-token');
  await assert.rejects(
    client.downloadArtifact('http://other-device/event/alarm.jpg'),
    /same device origin/i,
  );

  const oversized = new CosmoClient({
    base: 'http://device', token: 'token',
    fetchImpl: async () => new Response(Buffer.from('large'), {
      status: 200, headers: { 'Content-Length': '100' },
    }),
  });
  await assert.rejects(oversized.downloadArtifact('/large', { maxBytes: 10 }), /exceeds.*limit/i);
});

test('HTTP metadata distinguishes an unsupported HTML route from a structured API 400', async () => {
  for (const [contentType, routeUnsupported] of [
    ['text/html', true],
    ['application/json', false],
  ]) {
    const client = new CosmoClient({
      base: 'http://device',
      token: 'token',
      fetchImpl: async () => new Response(
        contentType === 'text/html' ? '<h1>BAD REQUEST</h1>' : JSON.stringify({ resCode: 0 }),
        { status: 400, headers: { 'Content-Type': contentType } },
      ),
    });
    await assert.rejects(client.taskPage({ pageNum: 1, pageSize: 10 }), (error) => {
      assert.equal(error.httpStatus, 400);
      assert.equal(error.responseContentType, contentType);
      assert.equal(error.routeUnsupported, routeUnsupported);
      return true;
    });
  }
});
