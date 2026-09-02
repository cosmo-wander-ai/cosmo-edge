// cosmo-client.js — Thin HTTP client for the CosmoEdge /gtw/cwai API.
//
// Authentication convention mirrors the frontend (src/web/src/utils/request.js):
//   - Login with { account, pwd: md5(password).toUpperCase() }
//   - Subsequent requests carry the returned mtk in the `mtk` / `token` headers.
// All routes are POST unless noted. Error handling follows the wire contract:
//   { resCode: 1, resData: {...}, resMsg: [...] }  -> success
//   { resCode: 0|<non-1>, resMsg: [{msgCode, msgText}] } -> failure

import crypto from 'node:crypto';

const API_PREFIX = '/gtw/cwai';
const DEFAULT_TIMEOUT_MS = 20_000;
const LONG_TIMEOUT_MS = 10 * 60_000;  // AddVideo / layout save can be slow
const LONG_TIMEOUT_ROUTES = new Set([
  '/Camera/AddVideo',
  '/algorithm/layout/save',
  '/atomic/model/uploadTemp',
  '/aihost/PTaskCreate',
  '/aihost/PTaskDetectPic',
]);

const DEFAULT_UPLOAD_CONCURRENCY = 2;
const DEFAULT_UPLOAD_ATTEMPTS = 4;
const DEFAULT_UPLOAD_BACKOFF_MS = 250;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function throwIfSignalAborted(signal) {
  if (!signal?.aborted) return;
  if (signal.reason instanceof Error) throw signal.reason;
  throw new Error('request aborted');
}

function requestAbortContext(timeout, externalSignal) {
  const controller = new AbortController();
  let timedOut = false;
  const onExternalAbort = () => controller.abort(externalSignal.reason);
  if (externalSignal?.aborted) {
    onExternalAbort();
  } else {
    externalSignal?.addEventListener('abort', onExternalAbort, { once: true });
  }
  const timer = setTimeout(() => {
    timedOut = true;
    controller.abort();
  }, timeout);
  return {
    signal: controller.signal,
    timedOut: () => timedOut,
    cleanup: () => {
      clearTimeout(timer);
      externalSignal?.removeEventListener('abort', onExternalAbort);
    },
  };
}

/** MD5-hashed + uppercased password, matching the backend's ToUpper(passwdMd5) comparison. */
export function hashPassword(plain) {
  return crypto.createHash('md5').update(String(plain), 'utf8').digest('hex').toUpperCase();
}

export class CosmoClient {
  /**
   * @param {object} opts
   * @param {string} opts.base   Device base URL, e.g. http://192.168.1.10:8080
   * @param {string} [opts.user] Login account.
   * @param {string} [opts.password] Plain-text password (hashed internally).
   * @param {string} [opts.token] Existing short-lived device token.
   * @param {string} [opts.lang] Accept-Language header value, default zh-CN.
   * @param {AbortSignal} [opts.signal] Cancels in-flight benchmark requests.
   */
  constructor({
    base,
    user,
    password,
    token = null,
    lang = 'zh-CN',
    uploadConcurrency = DEFAULT_UPLOAD_CONCURRENCY,
    uploadAttempts = DEFAULT_UPLOAD_ATTEMPTS,
    uploadBackoffMs = DEFAULT_UPLOAD_BACKOFF_MS,
    fetchImpl = globalThis.fetch,
    sleepImpl = sleep,
    signal = null,
  }) {
    if (!Number.isInteger(uploadConcurrency) || uploadConcurrency < 1) {
      throw new Error('uploadConcurrency must be a positive integer');
    }
    if (!Number.isInteger(uploadAttempts) || uploadAttempts < 1) {
      throw new Error('uploadAttempts must be a positive integer');
    }
    if (!Number.isFinite(uploadBackoffMs) || uploadBackoffMs < 0) {
      throw new Error('uploadBackoffMs must be a non-negative number');
    }
    if (typeof fetchImpl !== 'function' || typeof sleepImpl !== 'function') {
      throw new Error('fetchImpl and sleepImpl must be functions');
    }
    this.base = base.replace(/\/+$/, '');
    this.user = user;
    this.password = password;
    this.lang = lang;
    this.mtk = token;
    this.uploadConcurrency = uploadConcurrency;
    this.uploadAttempts = uploadAttempts;
    this.uploadBackoffMs = uploadBackoffMs;
    this.fetchImpl = fetchImpl;
    this.sleepImpl = sleepImpl;
    this.signal = signal;
    this.uploadActive = 0;
    this.uploadWaiters = [];
    this.uploadStats = {
      attempts: 0,
      retries: 0,
      busyResponses: 0,
      maxActive: 0,
      cleanupAttempts: 0,
      cleanupFailures: 0,
    };
  }

  uploadTelemetry() {
    return {
      ...this.uploadStats,
      active: this.uploadActive,
      queued: this.uploadWaiters.length,
      concurrencyLimit: this.uploadConcurrency,
      attemptLimit: this.uploadAttempts,
    };
  }

  /**
   * Detach subsequent bounded cleanup requests from the benchmark shutdown
   * signal. Requests that are already in flight keep their own linked abort
   * controller; task/preview/channel cleanup can then run with normal timeouts.
   */
  beginCleanup() {
    this.signal = null;
  }

  /**
   * Return a token-authenticated client whose bounded requests are detached
   * from the run signal. Concurrent trials keep using this client's original
   * signal while one trial performs mandatory cleanup.
   */
  detachedCleanupClient() {
    return new CosmoClient({
      base: this.base,
      token: this.mtk,
      lang: this.lang,
      uploadConcurrency: this.uploadConcurrency,
      uploadAttempts: this.uploadAttempts,
      uploadBackoffMs: this.uploadBackoffMs,
      fetchImpl: this.fetchImpl,
      sleepImpl: this.sleepImpl,
      signal: null,
    });
  }

  /** Log in and store the mtk token. Returns the login response resData. */
  async login() {
    if (this.mtk) return { mtk: this.mtk };
    if (!this.user || !this.password) throw new Error('login requires user/password or an existing token');
    const res = await this._post('/login/dologin', {
      account: this.user,
      pwd: hashPassword(this.password),
    });
    this.mtk = res.resData?.mtk;
    if (!this.mtk) {
      throw new Error('Login succeeded but no mtk returned');
    }
    this.password = null;
    return res.resData;
  }

  async queryDeviceInfo() {
    return (await this._post('/System/QueryDeviceInfo', {})).resData;
  }

  async queryHardwareResource() {
    return (await this._post('/System/QueryHardwareResource', {})).resData;
  }

  async queryDeviceMemoryPool() {
    const response = await this._post('/v1/cwai/aihost/QueryDeviceMemStatus', {});
    // Legacy /v1 core routes serialize their payload at the response root,
    // while /gtw/cwai routes place it under resData.
    return response.resData ?? response;
  }

  /** Save or update an algorithm orchestration layout. payload = parsed export JSON. */
  async layoutSave(payload) {
    return this._post('/algorithm/layout/save', payload);
  }

  /**
   * Upload one chunk of a file via multipart/form-data to the temp store.
   * Mirrors the frontend uploadVideoByChunk (src/web/src/views/gam/taskManager/index.vue).
   * @param {Buffer} chunkBuf  raw chunk bytes
   * @param {string} fileName  original file name (used for extension + naming)
   * @param {object} meta upload session and chunk metadata
   * @returns {Promise<object>} wire response with the canonical resData.uploadId
   */
  async uploadTempChunk(chunkBuf, fileName, {
    uploadId,
    clientRequestId,
    purpose,
    chunkIndex,
    totalChunks,
    totalSize,
    chunkSize,
  }) {
    const fields = {
      file: { value: chunkBuf, filename: fileName },
      purpose,
      clientRequestId,
      chunkIndex: String(chunkIndex),
      totalChunks: String(totalChunks),
      totalSize: String(totalSize),
      chunkSize: String(chunkSize),
    };
    // The first chunk creates the server-side session. Later chunks must use
    // the opaque upload ID returned by that first response.
    if (uploadId) fields.uploadId = uploadId;
    return this._postMultipart('/atomic/model/uploadTemp', fields);
  }

  /** Query live transfer, storage, and image capabilities. */
  async uploadCapabilities() {
    return (await this._post('/atomic/model/uploadCapabilities', {})).resData;
  }

  async cancelUpload(uploadId) {
    return this._post('/atomic/model/cancelUpload', { uploadId });
  }

  async cancelUploadBestEffort(uploadId) {
    if (!uploadId) return false;
    this.uploadStats.cleanupAttempts += 1;
    try {
      await this.cancelUpload(uploadId);
      return true;
    } catch {
      // A detect request may already have consumed the one-shot upload. That
      // makes cancel return "missing" even though no staged payload remains.
      this.uploadStats.cleanupFailures += 1;
      return false;
    }
  }

  /** Add an RTSP camera channel. */
  async cameraAdd(payload) {
    return this._post('/Camera/Add', payload);
  }

  /** Add a local video channel. */
  async cameraAddVideo(payload) {
    return this._post('/Camera/AddVideo', payload);
  }

  /** Query existing channels (paginated). */
  async cameraPage(payload) {
    return (await this._post('/Camera/Page', payload)).resData;
  }

  /** Batch delete channels by videoChannelId. */
  async cameraBatchDelete(videoChannelIds) {
    return this._post('/Camera/BatchDelete', { videoChannelIds });
  }

  /** Single-channel save/update task. */
  async taskSaveOrUpdate(payload) {
    return this._post('/Task/SaveOrUpdate', payload);
  }

  /**
   * Apply the same taskConfig/scheduleId/algorithmId to multiple channels at once.
   * NOTE: this auto-enables tasks on each channel (see design doc §任务绑定).
   *
   * The backend DTO (MsgApplyParamsBatchRecv → MsgChannelTask::from_json, VideoTaskDto.cc:20)
   * requires a top-level `channelId` and `algorithmId` via j.at(). For a batch call the
   * `channelId` is a template value; the engine iterates `targetChannelIds` instead. We pass
   * the first target channel as channelId to satisfy deserialization.
   * @returns {import('./types').ApplyResult}
   */
  async taskApplyParamsBatch({ algorithmId, scheduleId, taskConfig, targetChannelIds }) {
    const res = await this._post('/Task/ApplyParamsBatch', {
      channelId: targetChannelIds[0],
      algorithmId,
      scheduleId,
      taskConfig,
      targetChannelIds,
    });
    return { failedList: res.resData?.failedList ?? [] };
  }

  /** Batch switch tasks on/off. tasks = [{ id, channelId, algorithmId, enable }]. */
  async taskBatchSwitch(tasks) {
    const wireTasks = tasks.map(({ enable, ...task }) => ({
      ...task,
      switch: enable,
    }));
    const res = await this._post('/Task/BatchSwitchTask', { tasks: wireTasks });
    return { failedList: res.resData?.failedList ?? [] };
  }

  /** Query running detail for the given taskIds (NOT channelIds). */
  async taskRunningDetail(taskIds) {
    return (await this._post('/Task/RunningDetail', { tasks: taskIds })).resData;
  }

  async eventPage(payload, { signal } = {}) {
    return (await this._post('/event/page', payload, { signal: signal ?? this.signal })).resData;
  }

  async algorithmPage(payload) {
    return (await this._post('/Algorithm/Page', payload)).resData;
  }

  async schedulePage(payload) {
    return (await this._post('/schedule/Page', payload)).resData;
  }

  async taskPage(payload) {
    return (await this._post('/task/page', payload)).resData;
  }

  async taskSelectConfig(payload) {
    return (await this._post('/Task/SelectConfigByAlgorithmId', payload)).resData;
  }

  async taskDelete(payload) {
    return this._post('/Task/Delete', payload);
  }

  /**
   * Download one device-owned report artifact without allowing cross-origin
   * requests or unbounded response buffering.
   */
  async downloadArtifact(resource, { maxBytes = 20 * 1024 * 1024 } = {}) {
    if (!Number.isSafeInteger(maxBytes) || maxBytes <= 0) {
      throw new TypeError('maxBytes must be a positive safe integer');
    }
    const base = new URL(this.base);
    const url = new URL(String(resource ?? ''), base);
    if (url.origin !== base.origin) {
      throw new Error('artifact URL must use the same device origin');
    }
    throwIfSignalAborted(this.signal);
    const abortContext = requestAbortContext(DEFAULT_TIMEOUT_MS, this.signal);
    const headers = { 'Accept-Language': this.lang };
    if (this.mtk) {
      headers.mtk = this.mtk;
      headers.token = this.mtk;
    }
    try {
      const response = await this.fetchImpl(url.href, {
        method: 'GET',
        headers,
        signal: abortContext.signal,
      });
      if (!response.ok) throw new Error(`HTTP ${response.status} while downloading artifact`);
      const advertised = Number(response.headers.get('content-length'));
      if (Number.isFinite(advertised) && advertised > maxBytes) {
        throw new Error(`artifact exceeds ${maxBytes}-byte limit`);
      }
      const chunks = [];
      let size = 0;
      if (response.body?.getReader) {
        const reader = response.body.getReader();
        while (true) {
          const { done, value } = await reader.read();
          if (done) break;
          size += value.byteLength;
          if (size > maxBytes) {
            await reader.cancel('artifact size limit exceeded').catch(() => {});
            throw new Error(`artifact exceeds ${maxBytes}-byte limit`);
          }
          chunks.push(Buffer.from(value));
        }
      } else {
        const fallback = Buffer.from(await response.arrayBuffer());
        size = fallback.length;
        chunks.push(fallback);
      }
      const buffer = Buffer.concat(chunks, size);
      if (buffer.length > maxBytes) throw new Error(`artifact exceeds ${maxBytes}-byte limit`);
      return {
        buffer,
        contentType: response.headers.get('content-type')?.split(';', 1)[0] ?? 'application/octet-stream',
      };
    } catch (error) {
      throwIfSignalAborted(this.signal);
      if (abortContext.timedOut()) {
        throw new Error(`Artifact download timed out after ${DEFAULT_TIMEOUT_MS}ms`);
      }
      throw new Error(`Artifact download failed: ${error.message}`);
    } finally {
      abortContext.cleanup();
    }
  }

  /** Create or reuse one picture-analysis task. */
  async pictureTaskCreate(payload) {
    return this._post('/aihost/PTaskCreate', payload);
  }

  /** Run one authenticated, staged-image picture inference request. */
  async pictureDetect(payload) {
    return this._post('/aihost/PTaskDetectPic', payload);
  }

  /** Cancel one picture-analysis task and release its model/action instances. */
  async pictureTaskCancel(payload) {
    return this._post('/aihost/PTaskCancle', payload);
  }

  /** Start or join a live preview and wait until its first frame reaches SRS. */
  async requestLiveStream({ channelId, algorithmId = '' }) {
    return (await this._post('/LiveStream/RequestLiveStream', { channelId, algorithmId })).resData?.stream;
  }

  async streamKeepAlive({ channelId, algorithmId = '' }) {
    return this._post('/LiveStream/StreamKeepAlive', { channelId, algorithmId });
  }

  async streamStop({ channelId, algorithmId = '' }) {
    return this._post('/LiveStream/StreamStop', { channelId, algorithmId });
  }

  /**
   * Multipart POST. Builds a multipart/form-data body (manual, to avoid FormData/Blob
   * quirks), sends it, and applies the same wire-contract normalization as _post.
   * @param {string} path route path after /gtw/cwai
   * @param {object} fields  scalars are sent as text; {value:Buffer,filename:string} as file
   * @returns {Promise<object>} full wire response
   */
  async _postMultipart(path, fields) {
    const url = `${this.base}${API_PREFIX}${path}`;
    const boundary = '----bench' + crypto.randomBytes(8).toString('hex');
    const enc = (s) => s;
    const textParts = [];
    for (const [k, v] of Object.entries(fields)) {
      if (v && typeof v === 'object' && 'value' in v) continue;  // file field handled below
      textParts.push(`--${boundary}\r\nContent-Disposition: form-data; name="${k}"\r\n\r\n${v}\r\n`);
    }
    const headBuf = Buffer.from(textParts.join(''), 'utf8');
    const bufs = [headBuf];
    for (const [k, v] of Object.entries(fields)) {
      if (v && typeof v === 'object' && 'value' in v) {
        const fileHead = Buffer.from(
          `--${boundary}\r\nContent-Disposition: form-data; name="${k}"; filename="${v.filename}"\r\nContent-Type: application/octet-stream\r\n\r\n`,
          'utf8',
        );
        bufs.push(fileHead, Buffer.from(v.value), Buffer.from('\r\n', 'utf8'));
      }
    }
    bufs.push(Buffer.from(`--${boundary}--\r\n`, 'utf8'));
    const body = Buffer.concat(bufs);

    const headers = {
      'Content-Type': `multipart/form-data; boundary=${boundary}`,
      'Accept-Language': this.lang,
    };
    if (this.mtk) {
      headers.mtk = this.mtk;
      headers.token = this.mtk;
    }
    return this._withUploadSlot(() => this._sendMultipartWithRetry(path, url, headers, body));
  }

  async _withUploadSlot(operation) {
    if (this.uploadActive >= this.uploadConcurrency) {
      await new Promise((resolve) => this.uploadWaiters.push(resolve));
    }
    this.uploadActive += 1;
    this.uploadStats.maxActive = Math.max(this.uploadStats.maxActive, this.uploadActive);
    try {
      return await operation();
    } finally {
      this.uploadActive -= 1;
      this.uploadWaiters.shift()?.();
    }
  }

  async _sendMultipartWithRetry(path, url, headers, body) {
    const timeout = LONG_TIMEOUT_ROUTES.has(path) ? LONG_TIMEOUT_MS : DEFAULT_TIMEOUT_MS;
    for (let attempt = 1; attempt <= this.uploadAttempts; attempt += 1) {
      throwIfSignalAborted(this.signal);
      this.uploadStats.attempts += 1;
      const abortContext = requestAbortContext(timeout, this.signal);
      let resp;
      try {
        resp = await this.fetchImpl(url, {
          method: 'POST', headers, body, signal: abortContext.signal, duplex: 'half',
        });
      } catch (err) {
        throwIfSignalAborted(this.signal);
        if (abortContext.timedOut()) {
          throw new Error(`Request timed out after ${timeout}ms: POST ${path}`);
        }
        throw new Error(`Network error on POST ${path}: ${err.message}`);
      } finally {
        abortContext.cleanup();
      }

      let responseText;
      let data;
      try {
        responseText = await resp.text();
        data = JSON.parse(responseText);
      } catch (error) {
        throwIfSignalAborted(this.signal);
        data = null;
      }

      if (resp.status === 503) {
        this.uploadStats.busyResponses += 1;
        const firstMsg = Array.isArray(data?.resMsg) ? data.resMsg[0] : null;
        if (attempt < this.uploadAttempts) {
          this.uploadStats.retries += 1;
          const serverDelayMs = Number(firstMsg?.retryAfterSeconds ?? 0) * 1000;
          const exponentialMs = this.uploadBackoffMs * (2 ** (attempt - 1));
          await this.sleepImpl(Math.max(serverDelayMs, exponentialMs));
          continue;
        }
        const error = new Error(`HTTP 503 on POST ${path} after ${attempt} attempts`);
        error.httpStatus = 503;
        error.retryable = firstMsg?.retryable ?? true;
        error.msgCode = firstMsg?.msgCode ?? 'HTTP_SERVICE_BUSY';
        throw error;
      }
      if (!resp.ok) {
        const error = new Error(`HTTP ${resp.status} on POST ${path}`);
        error.httpStatus = resp.status;
        throw error;
      }
      if (!data) {
        throw new Error(`Non-JSON response on POST ${path}`);
      }
      if (data.resCode !== 1) {
        const firstMsg = Array.isArray(data.resMsg) ? data.resMsg[0] : null;
        const text = firstMsg?.msgText || firstMsg?.msgKey || data.msg || 'unknown error';
        const code = firstMsg?.msgCode || '';
        const error = new Error(`API error on POST ${path}: ${text}${code ? ` (code ${code})` : ''}`);
        error.resCode = data.resCode;
        error.msgCode = code;
        throw error;
      }
      return data;
    }
    throw new Error(`Multipart retry loop exhausted on POST ${path}`);
  }

  /**
   * Core POST. Prepends /gtw/cwai unless the caller supplies an absolute API
   * path such as /v1/cwai/aihost/QueryDeviceMemStatus.
   * @param {string} path route path after /gtw/cwai, or an absolute /v1 path
   * @param {object} body JSON body
   * @returns {Promise<object>} full wire response (with resCode/resData/resMsg)
   */
  async _post(path, body, { signal = this.signal } = {}) {
    const url = path.startsWith('/v1/')
      ? `${this.base}${path}`
      : `${this.base}${API_PREFIX}${path}`;
    const headers = {
      'Content-Type': 'application/json',
      'Accept-Language': this.lang,
    };
    if (this.mtk || path.toLowerCase() === '/login/dologin') {
      if (this.mtk) {
        headers.mtk = this.mtk;
        headers.token = this.mtk;
      }
    }
    const timeout = LONG_TIMEOUT_ROUTES.has(path) ? LONG_TIMEOUT_MS : DEFAULT_TIMEOUT_MS;
    throwIfSignalAborted(signal);
    const abortContext = requestAbortContext(timeout, signal);
    let resp;
    try {
      resp = await this.fetchImpl(url, {
        method: 'POST',
        headers,
        body: JSON.stringify(body ?? {}),
        signal: abortContext.signal,
      });
    } catch (err) {
      throwIfSignalAborted(signal);
      if (abortContext.timedOut()) {
        throw new Error(`Request timed out after ${timeout}ms: POST ${path}`);
      }
      throw new Error(`Network error on POST ${path}: ${err.message}`);
    } finally {
      abortContext.cleanup();
    }

    if (!resp.ok) {
      const error = new Error(`HTTP ${resp.status} on POST ${path}`);
      const contentType = resp.headers.get('content-type')?.split(';', 1)[0].toLowerCase() ?? null;
      error.httpStatus = resp.status;
      error.responseContentType = contentType;
      error.routeUnsupported = [404, 405].includes(resp.status)
        || (resp.status === 400 && contentType === 'text/html');
      throw error;
    }
    let data;
    try {
      data = await resp.json();
    } catch (error) {
      throwIfSignalAborted(signal);
      throw new Error(`Non-JSON response on POST ${path}`);
    }
    // Wire contract: resCode === 1 means success.
    if (data.resCode !== 1) {
      const firstMsg = Array.isArray(data.resMsg) ? data.resMsg[0] : null;
      const text = firstMsg?.msgText || firstMsg?.msgKey || data.msg || 'unknown error';
      const code = firstMsg?.msgCode || '';
      const err = new Error(`API error on POST ${path}: ${text}${code ? ` (code ${code})` : ''}`);
      err.resCode = data.resCode;
      err.msgCode = code;
      throw err;
    }
    return data;
  }
}
