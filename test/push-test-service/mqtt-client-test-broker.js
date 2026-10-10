'use strict';

// Run with the existing push-test-service dependencies, then enable the C++
// [mqtt-broker] cases with COSMO_MQTT_TEST_PORT and optional COSMO_MQTT_TEST_HOST.
const net = require('node:net');
const Aedes = require('aedes');

const port = Number(process.env.COSMO_MQTT_TEST_PORT || 18884);
const host = process.env.COSMO_MQTT_TEST_BIND || '127.0.0.1';
if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error('COSMO_MQTT_TEST_PORT must be an integer from 1 to 65535');
}

const broker = Aedes();
const server = net.createServer(broker.handle);
const clients = new Map();
const reversed = new Map();
const timers = new Set();

function publish(topic, message, done = () => {}) {
  broker.publish({ topic, payload: Buffer.from(JSON.stringify(message)), qos: 1 }, error => {
    if (error) {
      console.error('Test broker publish failed:', error.message);
      process.exitCode = 1;
    }
    done();
  });
}

function reply(packet, message) {
  return {
    head: { ...message.head, msgType: 'response' },
    body: {
      receivedTopic: packet.topic,
      receivedPayload: packet.payload.toString(),
      receivedQos: packet.qos,
    },
  };
}

broker.on('client', client => clients.set(client.id, client));
broker.on('clientDisconnect', client => {
  if (clients.get(client.id) === client) clients.delete(client.id);
});

broker.on('publish', (packet, client) => {
  if (!client) return;
  let message;
  try {
    message = JSON.parse(packet.payload.toString());
  } catch {
    return;
  }
  const head = message.head || {};
  if (packet.topic === '/d2p/aibox' && head.msgType === 'register') {
    publish(`/p2d/aibox/${head.deviceSn}`, { head: { ...head, msgType: 'response' }, body: { resCode: 1 } });
    return;
  }
  if (packet.topic === '/d2p/aibox/heartbeat' && head.msgType === 'heartbeat') {
    publish(`/p2d/aibox/heartbeat/${head.deviceSn}`, {
      head: { ...head, msgType: 'response' }, body: { resCode: 1 },
    });
    return;
  }

  const match = /^(cosmo-mqtt-test\/[^/]+)\/request\/([^/]+)$/.exec(packet.topic);
  if (!match || typeof head.requestId !== 'string') return;
  const [, base, scenario] = match;
  const response = reply(packet, message);
  switch (scenario) {
    case 'ack':
      publish(`${base}/reply`, response);
      break;
    case 'timeout':
      // Aedes sends the QoS 1 PUBACK; only the business ACK is withheld.
      response.head.requestId = `receipt/${head.requestId}`;
      publish(`${base}/observed`, response);
      break;
    case 'late': {
      const timer = setTimeout(() => {
        timers.delete(timer);
        publish(`${base}/reply`, response);
      }, 700);
      timers.add(timer);
      break;
    }
    case 'mismatch':
      response.head.requestId = `unmatched/${head.requestId}`;
      publish(`${base}/reply`, response);
      break;
    case 'reverse': {
      const pending = reversed.get(base) || [];
      response.body.receiveOrder = pending.length;
      pending.push(response);
      reversed.set(base, pending);
      if (pending.length === 4) {
        reversed.delete(base);
        pending.reverse();
        let index = 0;
        const next = () => {
          if (index === pending.length) return;
          const item = pending[index];
          item.body.replyOrder = index++;
          publish(`${base}/reply`, item, next);
        };
        next();
      }
      break;
    }
    case 'disconnect': {
      const targetId = message.body && message.body.clientId;
      const target = typeof targetId === 'string' && targetId.startsWith('cosmo-mqtt-test-')
        ? clients.get(targetId) : undefined;
      response.body.disconnected = Boolean(target);
      if (target) target.conn.destroy();
      publish(`${base}/reply`, response);
      break;
    }
  }
});

function shutdown() {
  for (const timer of timers) clearTimeout(timer);
  for (const client of clients.values()) client.conn.destroy();
  server.close(() => broker.close());
}

process.once('SIGINT', shutdown);
process.once('SIGTERM', shutdown);
server.listen(port, host, () => console.log(`MQTT client test broker ready on ${host}:${port}`));
