import { spawn } from 'node:child_process';

import { sleepWithSignal } from '../shutdown-signal.js';

export function buildAccuracyFfmpegCommand({ ffmpeg = 'ffmpeg', sourceUrl, targetUrl }) {
  if (!sourceUrl || !targetUrl) throw new Error('sourceUrl and targetUrl are required');
  return {
    file: ffmpeg,
    args: [
      '-hide_banner', '-loglevel', 'warning',
      '-re', '-stream_loop', '-1', '-i', sourceUrl,
      '-an', '-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'zerolatency',
      '-g', '25', '-keyint_min', '25', '-sc_threshold', '0',
      '-x264-params', 'repeat-headers=1', '-pix_fmt', 'yuv420p',
      '-rtsp_transport', 'tcp', '-f', 'rtsp', targetUrl,
    ],
  };
}

export class ManagedRtspPusher {
  constructor({ spawnImpl = spawn, sleep = sleepWithSignal, logger = null } = {}) {
    this.spawnImpl = spawnImpl;
    this.sleep = sleep;
    this.logger = logger;
    this.process = null;
    this.stderrTail = '';
    this.spawnError = null;
  }

  async start({ ffmpeg = 'ffmpeg', sourceUrl, targetUrl, warmupMs = 4_000, signal } = {}) {
    if (this.process) throw new Error('RTSP pusher is already started');
    const command = buildAccuracyFfmpegCommand({ ffmpeg, sourceUrl, targetUrl });
    this.process = this.spawnImpl(command.file, command.args, {
      stdio: ['ignore', 'ignore', 'pipe'],
      signal,
    });
    this.process.stderr?.on?.('data', (chunk) => {
      this.stderrTail = `${this.stderrTail}${chunk}`.slice(-2_000);
    });
    this.process.on?.('error', (error) => { this.spawnError = error; });
    await this.sleep(warmupMs, signal);
    if (this.spawnError) throw new Error(`ffmpeg failed to start: ${this.spawnError.message}`);
    if (this.process.exitCode != null) {
      const fallback = this.process.stderr?.read?.();
      const detail = String(this.stderrTail || fallback || '').trim().slice(-500);
      throw new Error(`ffmpeg exited during RTSP warmup${detail ? `: ${detail}` : ''}`);
    }
    this.logger?.info?.('deterministic RTSP pusher is ready');
    return { targetUrl };
  }

  async stop() {
    const child = this.process;
    if (!child) return { stopped: true, signal: null };
    this.process = null;
    if (child.exitCode != null) return { stopped: true, signal: null };
    child.kill('SIGTERM');
    await this.sleep(250).catch(() => {});
    if (child.exitCode == null) child.kill('SIGKILL');
    return { stopped: true, signal: child.exitCode == null ? 'SIGKILL' : 'SIGTERM' };
  }
}
