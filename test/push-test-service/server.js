'use strict';

const express = require('express');
const http = require('http');
const path = require('path');
const { Server } = require('socket.io');
const Aedes = require('aedes');
const net = require('net');
const crypto = require('crypto');
const os = require('os');
const fs = require('fs');

const HTTP_PORT = Number(process.env.HTTP_PORT || 18080);
const MQTT_PORT = Number(process.env.MQTT_PORT || 1883);
const HOST = process.env.HOST || '0.0.0.0';

const TOPIC_REGISTER = '/d2p/aibox';
const TOPIC_COMMAND_PREFIX = '/p2d/aibox/';
const TOPIC_HEARTBEAT = '/d2p/aibox/heartbeat';
const TOPIC_HEARTBEAT_REPLY_PREFIX = '/p2d/aibox/heartbeat/';

const state = {
  startedAt: new Date(),
  httpEvents: [],
  mqttMessages: [],
  devices: new Map(),
  mqttClients: new Map(), // clientId -> client
};

const app = express();
const httpServer = http.createServer(app);
const io = new Server(httpServer);
const aedes = Aedes();
const mqttServer = net.createServer(aedes.handle);

app.use(express.json({ limit: '50mb' }));
app.use(express.static(path.join(__dirname, 'public')));

// Serve video files if they are local paths
// In a real scenario, you might need to map /data/event to a real path
app.use('/video', (req, res) => {
  const videoPath = decodeURIComponent(req.path).substring(1); // remove leading slash
  // Basic security: don't allow traversal
  if (videoPath.includes('..')) {
    return res.status(403).send('Forbidden');
  }
  // Try to find the file.
  if (fs.existsSync(videoPath)) {
    res.sendFile(videoPath);
  } else if (fs.existsSync('/' + videoPath)) {
    res.sendFile('/' + videoPath);
  } else {
    res.status(404).send('Video not found');
  }
});

function now() {
  return new Date().toISOString();
}

function requestId() {
  return crypto.randomUUID ? crypto.randomUUID() : crypto.randomBytes(16).toString('hex');
}

function broadcast(event, data) {
  io.emit(event, data);
}

// --- HTTP Handlers ---

app.get('/health', (req, res) => {
  res.json({
    ok: true,
    startedAt: state.startedAt.toISOString(),
    devices: Array.from(state.devices.values()),
  });
});

app.post('/events', (req, res) => {
  const event = req.body;
  const eventId = requestId();
  const enrichedEvent = {
    ...event,
    _id: eventId,
    _receivedAt: now(),
  };

  state.httpEvents.unshift(enrichedEvent);
  if (state.httpEvents.length > 100) state.httpEvents.pop();

  broadcast('http_event', enrichedEvent);

  res.json({
    resCode: 1,
    resMsg: [],
  });
});

app.use((err, req, res, next) => {
  if (!err) {
    return next();
  }

  const detail = {
    type: err.type || err.name || 'error',
    message: err.message || String(err),
    path: req.originalUrl,
    receivedAt: now(),
  };
  console.error(`[${detail.receivedAt}] HTTP ${req.method} ${req.originalUrl} failed:`, detail);
  res.status(err.status || err.statusCode || 400).json({
    resCode: 0,
    resMsg: [{ msgCode: String(err.status || err.statusCode || 400), msgText: detail.message }],
    error: detail,
  });
});

// --- MQTT Handlers ---

function publishToDevice(deviceSn, action, body) {
  const topic = `${TOPIC_COMMAND_PREFIX}${deviceSn}`;
  const payload = {
    head: {
      requestId: requestId(),
      action: action,
      deviceSn: deviceSn,
      msgType: 'request',
    },
    body: typeof body === 'string' ? body : JSON.stringify(body),
  };

  aedes.publish({
    topic,
    payload: Buffer.from(JSON.stringify(payload)),
    qos: 1,
    retain: false,
  });

  const logEntry = {
    type: 'sent',
    topic,
    payload,
    at: now(),
  };
  state.mqttMessages.unshift(logEntry);
  if (state.mqttMessages.length > 200) state.mqttMessages.pop();
  broadcast('mqtt_log', logEntry);
}

app.post('/api/mqtt/send', (req, res) => {
  const { deviceSn, action, body } = req.body;
  if (!deviceSn || !action) {
    return res.status(400).json({ error: 'deviceSn and action are required' });
  }
  publishToDevice(deviceSn, action, body);
  res.json({ success: true });
});

app.get('/api/state', (req, res) => {
  res.json({
    devices: Array.from(state.devices.values()),
    httpEvents: state.httpEvents,
    mqttMessages: state.mqttMessages,
  });
});

// --- Aedes Logic ---

aedes.on('client', client => {
  state.mqttClients.set(client.id, client);
  broadcast('mqtt_client_connected', { id: client.id });
});

aedes.on('clientDisconnect', client => {
  state.mqttClients.delete(client.id);
  broadcast('mqtt_client_disconnected', { id: client.id });
});

aedes.on('publish', (packet, client) => {
  if (!client || !packet || !packet.topic || packet.topic.startsWith('$SYS/')) {
    return;
  }

  let json = null;
  try {
    json = JSON.parse(packet.payload.toString());
  } catch (e) {
    // Not JSON
  }

  const logEntry = {
    type: 'received',
    topic: packet.topic,
    clientId: client.id,
    payload: json || packet.payload.toString(),
    at: now(),
  };

  state.mqttMessages.unshift(logEntry);
  if (state.mqttMessages.length > 200) state.mqttMessages.pop();
  broadcast('mqtt_log', logEntry);

  if (!json) return;

  if (packet.topic === TOPIC_REGISTER) {
    handleRegister(json);
  } else if (packet.topic === TOPIC_HEARTBEAT) {
    handleHeartbeat(json);
  }
});

function handleRegister(msg) {
  const head = msg.head || {};
  let body;
  try {
    body = typeof msg.body === 'string' ? JSON.parse(msg.body) : (msg.body || {});
  } catch (err) {
    console.error(`[${now()}] MQTT register body parse error:`, err.message);
    return;
  }
  const deviceSn = head.deviceSn || body.devId || 'unknown-device';

  const device = {
    deviceSn,
    devId: body.devId,
    supplier: body.supplier,
    aiHostVersion: body.aiHostVersion,
    engineType: body.engineType,
    deviceModel: body.deviceModel,
    devType: body.devType,
    lastRegisterAt: now(),
    status: 'online',
  };
  state.devices.set(deviceSn, device);
  broadcast('device_updated', device);

  const replyTopic = `${TOPIC_COMMAND_PREFIX}${deviceSn}`;
  const reply = {
    head: {
      requestId: head.requestId || requestId(),
      action: head.action || '',
      deviceSn: deviceSn,
      msgType: 'response',
    },
    body: {
      resCode: 1,
      resMsg: [],
      serverTime: now(),
    },
  };

  aedes.publish({
    topic: replyTopic,
    payload: Buffer.from(JSON.stringify(reply)),
    qos: 1,
  });
}

function handleHeartbeat(msg) {
  const head = msg.head || {};
  let body;
  try {
    body = typeof msg.body === 'string' ? JSON.parse(msg.body) : (msg.body || {});
  } catch (err) {
    console.error(`[${now()}] MQTT heartbeat body parse error:`, err.message);
    return;
  }
  const deviceSn = head.deviceSn || body.devId || 'unknown-device';

  const old = state.devices.get(deviceSn) || { deviceSn };
  const updated = {
    ...old,
    ...body,
    lastHeartbeatAt: now(),
    status: 'online',
  };
  state.devices.set(deviceSn, updated);
  broadcast('device_updated', updated);

  const replyTopic = `${TOPIC_HEARTBEAT_REPLY_PREFIX}${deviceSn}`;
  const reply = {
    head: {
      requestId: head.requestId || requestId(),
      action: head.action || '',
      deviceSn: deviceSn,
      msgType: 'response',
    },
    body: {
      resCode: 1,
      resMsg: [],
      serverTime: now(),
    },
  };

  aedes.publish({
    topic: replyTopic,
    payload: Buffer.from(JSON.stringify(reply)),
    qos: 1,
  });
}

function localAddresses() {
  const addresses = [];
  for (const items of Object.values(os.networkInterfaces())) {
    for (const item of items || []) {
      if (item.family === 'IPv4' && !item.internal) {
        addresses.push(item.address);
      }
    }
  }
  return addresses;
}

httpServer.listen(HTTP_PORT, HOST, () => {
  console.log(`\n[${now()}] HTTP visualization server started at http://${HOST}:${HTTP_PORT}`);
  console.log(`Local access: http://localhost:${HTTP_PORT}`);
  localAddresses().forEach(ip => console.log(`Network access: http://${ip}:${HTTP_PORT}`));
});

mqttServer.listen(MQTT_PORT, HOST, () => {
  console.log(`\n[${now()}] MQTT test broker started at ${HOST}:${MQTT_PORT}`);
});

function shutdown(signal) {
  console.log(`\n[${now()}] Received ${signal}, closing servers...`);
  
  let closed = 0;
  const finish = () => {
    closed++;
    if (closed === 2) {
      console.log('All servers closed, exiting.');
      process.exit(0);
    }
  };

  httpServer.close((err) => {
    if (err) console.error('Error closing HTTP server:', err);
    else console.log('HTTP server closed.');
    finish();
  });

  mqttServer.close(() => {
    console.log('MQTT server closed.');
    finish();
  });

  // Force exit if closing takes too long
  setTimeout(() => {
    console.error('Could not close connections in time, forcefully shutting down');
    process.exit(1);
  }, 5000);
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
