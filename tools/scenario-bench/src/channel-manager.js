// channel-manager.js — Create / reuse video channels on the device.
//
// Two creation paths:
//   - local:           uploadTemp (purpose=video) → Camera/AddVideo (uploadId)
//   - rtsp-*:          Camera/Add       (channelCode + channelName + url)
//
// IMPORTANT identifier convention (verified in source):
//   - `channelCode` is the caller-supplied code.
//   - `videoChannelId` is the system-generated internal id, returned as resData.id
//     from Add/AddVideo, and present in Camera/Page items.
//   - Task binding (ApplyParamsBatch.targetChannelIds) and RunningDetail use
//     `videoChannelId`, NOT channelCode.
// So we must capture and keep videoChannelId for every channel we create.

import path from 'node:path';
import fs from 'node:fs';
import { performance } from 'node:perf_hooks';

const UPLOAD_CHUNK_SIZE = 8 * 1024 * 1024;

export class ChannelCleanupError extends Error {
  constructor(message, result) {
    super(message);
    this.name = 'ChannelCleanupError';
    this.result = result;
  }
}

export class ChannelManager {
  /**
   * @param {import('./cosmo-client.js').CosmoClient} client
   * @param {object} [opts]
   * @param {string} [opts.channelPrefix] prefix for bench channels, default 'bench'
   * @param {boolean} [opts.reuse] reuse existing bench channels by name, default true
   * @param {boolean} [opts.cleanup] delete created channels on finish, default false
   * @param {import('./logger.js').Logger} [opts.logger]
   */
  constructor(client, opts = {}) {
    this.client = client;
    this.prefix = opts.channelPrefix ?? 'bench';
    this.reuse = opts.reuse ?? true;
    this.cleanup = opts.cleanup ?? false;
    this.strictCleanup = opts.strictCleanup ?? false;
    this.cleanupVerifyAttempts = opts.cleanupVerifyAttempts ?? 3;
    this.cleanupVerifyDelayMs = opts.cleanupVerifyDelayMs ?? 250;
    this.sleep = opts.sleep ?? ((ms) => new Promise((resolve) => setTimeout(resolve, ms)));
    this.monotonicNow = opts.monotonicNow ?? (() => performance.now());
    this.recoverAmbiguousCreates = opts.recoverAmbiguousCreates ?? false;
    this.log = opts.logger;
    /** @type {Map<string, {videoChannelId:string, channelCode:string, channelName:string, mode:string}>} */
    this.created = new Map();
    this.timingMs = { videoUpload: 0, cameraCreate: 0 };
    this.pendingUploadIds = new Set();
    this.uploadCancelAttempts = new Set();
    this.cancelledUploadIds = new Set();
  }

  timingSnapshot() {
    return { ...this.timingMs };
  }

  /**
   * Ensure `count` channels exist for the scenario's video mode.
   * Reuses existing bench channels when reuse=true, otherwise always creates new.
   * @param {object} videos normalized scenario channels object
   * @param {number} count max channels needed (largest loadProfile step)
   * @returns {Promise<string[]>} array of videoChannelIds (length === count)
   */
  async ensureChannels(videos, count) {
    const mode = videos.mode;
    let videoChannelIds = [];

    if (this.reuse) {
      videoChannelIds = await this._findExistingBenchChannels(count);
    }

    // Create the shortfall.
    let index = videoChannelIds.length;
    while (videoChannelIds.length < count) {
      const code = `${this.prefix}-${String(index + 1).padStart(2, '0')}`;
      const name = `${this.prefix}-${String(index + 1).padStart(2, '0')}`;
      const created = await this._createOne(mode, code, name, videos, index);
      const id = created.id;
      videoChannelIds.push(id);
      this.created.set(id, {
        videoChannelId: id,
        channelCode: created.channelCode,
        channelName: created.channelName,
        mode,
      });
      index++;
    }
    return videoChannelIds;
  }

  /** Tear down channels created by this manager (only if cleanup=true). */
  async finish({ client = this.client } = {}) {
    const result = {
      attempted: [],
      deleted: false,
      verified: this.cleanup ? null : true,
      remaining: [],
      warnings: [],
      errors: [],
      upload: {
        attempted: [...this.uploadCancelAttempts],
        cancelled: [...this.cancelledUploadIds],
        remaining: [],
        errors: [],
      },
    };
    if (!this.cleanup) return result;
    for (const uploadId of [...this.pendingUploadIds]) {
      if (!result.upload.attempted.includes(uploadId)) result.upload.attempted.push(uploadId);
      try {
        await this._cancelUpload(uploadId, client);
        if (!result.upload.cancelled.includes(uploadId)) result.upload.cancelled.push(uploadId);
      } catch (error) {
        result.upload.errors.push(`${uploadId}:${error.message}`);
      }
    }
    result.upload.remaining = [...this.pendingUploadIds];
    const ids = [...this.created.keys()];
    result.attempted = ids;
    if (ids.length) {
      try {
        await client.cameraBatchDelete(ids);
        result.deleted = true;
      } catch (error) {
        result.warnings.push(`delete:${error.message}`);
      }
    } else {
      result.deleted = true;
    }
    if (this.strictCleanup) {
      if (ids.length) {
        try {
          for (let attempt = 1; attempt <= this.cleanupVerifyAttempts; attempt += 1) {
            result.remaining = await this._findChannelIds(new Set(ids), client);
            if (!result.remaining.length) break;
            if (attempt < this.cleanupVerifyAttempts && this.cleanupVerifyDelayMs > 0) {
              await this.sleep(this.cleanupVerifyDelayMs);
            }
          }
        } catch (error) {
          result.errors.push(`verify:${error.message}`);
        }
      }
      result.verified = result.errors.length === 0
        && result.remaining.length === 0
        && result.upload.errors.length === 0
        && result.upload.remaining.length === 0;
      if (!result.verified) {
        throw new ChannelCleanupError(
          `strict channel cleanup failed (${[
            ...result.errors,
            ...result.upload.errors,
            ...(result.remaining.length ? [`remaining=${result.remaining.join(',')}`] : []),
            ...(result.upload.remaining.length
              ? [`uploads=${result.upload.remaining.join(',')}`]
              : []),
          ].join('; ')})`,
          result,
        );
      }
    } else {
      result.verified = result.upload.remaining.length === 0;
    }
    return result;
  }

  // ── internals ──────────────────────────────────────────────────────────

  async _createOne(mode, code, name, videos, index) {
    if (mode === 'local') {
      const src = videos.local?.[index] ?? videos.local?.[0];
      if (!src) throw new Error(`scenario.yml channels: not enough local sources for channel ${index + 1}`);
      if (!src.file) throw new Error(`scenario.yml channels: local source must define 'file'`);
      // AddVideo consumes the upload session, so every channel needs its own
      // upload even when all channels use the same local source.
      const uploadId = await this._measure('videoUpload', () => this._uploadLocalVideo(src));
      const videoPayload = { uploadId };
      const channelName = this._channelNameForSource(name, src);
      try {
        const res = await this._measure('cameraCreate', () => this.client.cameraAddVideo({
          channelName,
          channelCode: code,
          ...videoPayload,
        }));
        const id = res?.resData?.id;
        if (!id) throw new Error(`AddVideo for ${code} returned no id`);
        this.pendingUploadIds.delete(uploadId);
        return { id, channelCode: code, channelName };
      } catch (error) {
        const recoveryClient = this.client.detachedCleanupClient?.() ?? this.client;
        const recovered = this.recoverAmbiguousCreates
          ? await this._findCreatedChannel(code, channelName, recoveryClient)
          : null;
        if (recovered) {
          this.pendingUploadIds.delete(uploadId);
          return recovered;
        }
        await this._cancelUploadBestEffort(uploadId);
        throw error;
      }
    }
    // rtsp-fidelity / rtsp-deterministic
    const src = videos.rtsp?.[index] ?? videos.rtsp?.[0];
    if (!src) throw new Error(`scenario.yml channels: not enough rtsp sources for channel ${index + 1}`);
    const payload = {
      channelCode: code,
      channelName: src.name ?? name,
      channelType: 0,  // RTSP camera
      url: src.url,
    };
    try {
      const res = await this._measure('cameraCreate', () => this.client.cameraAdd(payload));
      const id = res?.resData?.id;
      if (!id) throw new Error(`Camera/Add for ${code} returned no id`);
      return { id, channelCode: code, channelName: payload.channelName };
    } catch (error) {
      const recoveryClient = this.client.detachedCleanupClient?.() ?? this.client;
      const recovered = this.recoverAmbiguousCreates
        ? await this._findCreatedChannel(code, payload.channelName, recoveryClient)
        : null;
      if (recovered) return recovered;
      throw error;
    }
  }

  _channelNameForSource(baseName, src) {
    const sourceLabel = src?.name
      ?? (src?.file ? path.parse(src.file).name : null);
    return sourceLabel ? `${baseName}-${sourceLabel}` : baseName;
  }

  /**
   * Upload a local video file to the device temp store in chunks, returning the
   * canonical upload ID to pass to AddVideo. Mirrors the frontend
   * uploadFileInChunks. The preferred chunk is 8 MB, but the device's live
   * capability response is authoritative. Chunk zero creates a server-side
   * session; later chunks use the returned opaque upload ID. The completed
   * session is consumed by Camera/AddVideo.
   */
  async _uploadLocalVideo(src) {
    const localPath = src.file;
    if (!localPath) throw new Error(`scenario.yml channels: local source missing 'file' path`);
    const stat = fs.statSync(localPath);
    const totalSize = stat.size;
    if (!stat.isFile() || !Number.isSafeInteger(totalSize) || totalSize <= 0) {
      throw new Error(`local video must be a non-empty regular file: ${localPath}`);
    }
    const capabilities = await this.client.uploadCapabilities();
    const asBigInt = (key) => {
      const raw = capabilities?.[key];
      if (raw == null || raw === '') return null;
      try {
        const value = BigInt(raw);
        return value >= 0n ? value : null;
      } catch {
        return null;
      }
    };
    const maxTotalSize = asBigInt('maxTotalSize');
    const available = asBigInt('availableForNewUploadsBytes');
    if (maxTotalSize && maxTotalSize > 0n && BigInt(totalSize) > maxTotalSize) {
      throw new Error(`local video exceeds deployment upload policy (${totalSize} > ${maxTotalSize})`);
    }
    if (available != null && BigInt(totalSize) > available) {
      throw new Error(`insufficient safe device storage (${totalSize} required, ${available} available)`);
    }
    const advertisedChunkSize = asBigInt('maxChunkSize');
    const chunkSize = advertisedChunkSize && advertisedChunkSize > 0n
      ? Math.min(UPLOAD_CHUNK_SIZE, Number(advertisedChunkSize))
      : UPLOAD_CHUNK_SIZE;
    if (!Number.isSafeInteger(chunkSize) || chunkSize <= 0) {
      throw new Error('device returned an invalid maxChunkSize');
    }
    const totalChunks = Math.ceil(totalSize / chunkSize);
    const maxChunks = asBigInt('maxChunks');
    if (maxChunks && maxChunks > 0n && BigInt(totalChunks) > maxChunks) {
      throw new Error(`upload needs ${totalChunks} chunks but device policy allows ${maxChunks}`);
    }
    const clientRequestId = `bench_${Date.now()}_${Math.random().toString(16).slice(2)}`;
    const fileName = path.basename(localPath);
    const fd = fs.openSync(localPath, 'r');
    let uploadId = '';
    try {
      let chunkIndex = 0;
      while (chunkIndex < totalChunks) {
        const start = chunkIndex * chunkSize;
        const currentChunkSize = Math.min(chunkSize, totalSize - start);
        const chunkBuf = Buffer.alloc(currentChunkSize);
        fs.readSync(fd, chunkBuf, 0, currentChunkSize, start);
        const res = await this.client.uploadTempChunk(chunkBuf, fileName, {
          uploadId,
          clientRequestId,
          purpose: 'video',
          chunkIndex,
          totalChunks,
          totalSize,
          chunkSize: currentChunkSize,
        });
        const responseUploadId = res?.resData?.uploadId ?? '';
        if (!responseUploadId) {
          throw new Error(`uploadTemp returned no uploadId for ${fileName} chunk ${chunkIndex}`);
        }
        if (uploadId && responseUploadId !== uploadId) {
          throw new Error(`uploadTemp changed uploadId for ${fileName}`);
        }
        uploadId = responseUploadId;
        this.pendingUploadIds.add(uploadId);
        const nextChunkIndex = Number(res?.resData?.nextChunkIndex);
        if (!Number.isInteger(nextChunkIndex)
          || nextChunkIndex <= chunkIndex
          || nextChunkIndex > totalChunks) {
          throw new Error(`uploadTemp returned invalid nextChunkIndex for ${fileName}`);
        }
        const complete = nextChunkIndex === totalChunks;
        if (res?.resData?.complete !== complete) {
          throw new Error(`uploadTemp returned an invalid completion state for ${fileName}`);
        }
        chunkIndex = nextChunkIndex;
      }
    } catch (error) {
      if (uploadId) {
        await this._cancelUploadBestEffort(uploadId);
      }
      throw error;
    } finally {
      fs.closeSync(fd);
    }
    this.log?.debug?.(`uploaded ${fileName} → ${uploadId}`);
    return uploadId;
  }

  /**
   * Find existing channels whose channelName starts with our prefix,
   * up to `count`. Used for reuse across reruns.
   */
  async _findExistingBenchChannels(count) {
    const found = [];
    let pageNum = 1;
    const pageSize = 200;
    try {
      while (found.length < count) {
        const res = await this.client.cameraPage({ pageNum, pageSize });
        const list = res?.rows ?? res?.list ?? res?.data ?? [];
        if (!list.length) break;
        for (const ch of list) {
          // Local AddVideo may not persist channelCode, so channelName also carries the bench prefix.
          const code = ch.channelCode ?? '';
          const name = ch.channelName ?? ch.name ?? '';
          const id = ch.videoChannelId ?? ch.id;
          if ((code.startsWith(this.prefix) || name.startsWith(this.prefix)) && id) {
            found.push(id);
            if (found.length >= count) break;
          }
        }
        if (list.length < pageSize) break;
        pageNum++;
      }
    } catch {
      // Reuse is best-effort; fall through to create new.
    }
    return found;
  }

  async _findChannelIds(wanted, client = this.client) {
    const found = [];
    let pageNum = 1;
    const pageSize = 200;
    while (true) {
      const res = await client.cameraPage({ pageNum, pageSize });
      const list = res?.rows ?? res?.list ?? res?.data ?? [];
      for (const channel of list) {
        const id = channel.videoChannelId ?? channel.id;
        if (id && wanted.has(id)) found.push(id);
      }
      if (list.length < pageSize) break;
      pageNum += 1;
    }
    return found;
  }

  async _findCreatedChannel(channelCode, channelName, client = this.client) {
    if (typeof client.cameraPage !== 'function') return null;
    let pageNum = 1;
    const pageSize = 200;
    try {
      while (true) {
        const response = await client.cameraPage({ pageNum, pageSize });
        const rows = response?.rows ?? response?.list ?? response?.data ?? [];
        const row = rows.find((item) =>
          String(item.channelCode ?? '') === String(channelCode)
          || String(item.channelName ?? item.name ?? '') === String(channelName));
        const id = row?.videoChannelId ?? row?.id;
        if (id) {
          return {
            id,
            channelCode: row.channelCode ?? channelCode,
            channelName: row.channelName ?? row.name ?? channelName,
          };
        }
        if (rows.length < pageSize) break;
        pageNum += 1;
      }
    } catch { /* Preserve the original create failure. */ }
    return null;
  }

  async _measure(name, action) {
    const startedAt = this.monotonicNow();
    try { return await action(); }
    finally { this.timingMs[name] += this.monotonicNow() - startedAt; }
  }

  async _cancelUploadBestEffort(uploadId) {
    const client = this.client.detachedCleanupClient?.() ?? this.client;
    try { await this._cancelUpload(uploadId, client); }
    catch { /* finish() retries and reports any remaining upload session. */ }
  }

  async _cancelUpload(uploadId, client) {
    if (!uploadId || !this.pendingUploadIds.has(uploadId)) return;
    this.uploadCancelAttempts.add(uploadId);
    if (typeof client.cancelUpload === 'function') {
      await client.cancelUpload(uploadId);
    } else if (typeof client.cancelUploadBestEffort === 'function') {
      const cancelled = await client.cancelUploadBestEffort(uploadId);
      if (cancelled === false) throw new Error('upload cancellation was not confirmed');
    } else {
      throw new Error('client does not support upload cancellation');
    }
    this.pendingUploadIds.delete(uploadId);
    this.cancelledUploadIds.add(uploadId);
  }
}
