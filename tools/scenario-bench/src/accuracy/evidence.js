import fs from 'node:fs';
import path from 'node:path';

import { renderAccuracyHtml, renderThresholdDiagnosticHtml } from './report.js';
import { assertAccuracySummary } from './summary.js';
import { assertThresholdDiagnosticDocument } from './threshold.js';
import { sha256Buffer, sha256File, stableStringify } from './utils.js';
import {
  PRIVATE_FILE_MODE,
  chmodBestEffort,
  ensurePrivateDir,
  writePrivateFile,
  writePrivateJson,
} from './private-files.js';
const SENSITIVE_KEY = /(?:password|token|mtk|deviceSn|serialNumber|deviceAddress|deviceUrl|absoluteFile|videoChannelId|channelId|secret)/iu;
const ABSOLUTE_WINDOWS_PATH = /^[A-Za-z]:[\\/]/u;
const IPV4 = /\b(?:\d{1,3}\.){3}\d{1,3}\b/gu;
const URL = /\b(?:https?|rtsp):\/\/[^\s"']+/giu;
const EMBEDDED_POSIX_PATH = /(^|\s)\/(?!\/)[^\s"']+/gu;
const EMBEDDED_DEVICE_ID = /\b(?:deviceSn|serialNumber|sn)\s*[:=]\s*[^\s,;]+/giu;

export class AccuracyEvidenceWriter {
  constructor(outputDir, {
    resume = false,
    partialWriteIntervalMs = 60_000,
    now = () => Date.now(),
  } = {}) {
    if (!Number.isFinite(partialWriteIntervalMs) || partialWriteIntervalMs < 0) {
      throw new Error('partialWriteIntervalMs must be a non-negative number');
    }
    this.outputDir = path.resolve(outputDir);
    this.resume = resume;
    this.partialWriteIntervalMs = partialWriteIntervalMs;
    this.now = now;
    this.lastPartialWriteAt = null;
    this.identity = null;
  }

  initialize(identity) {
    if (!identity || typeof identity !== 'object') throw new Error('run identity is required');
    if (fs.existsSync(this.outputDir)) {
      const entries = fs.readdirSync(this.outputDir);
      if (entries.length && !this.resume) {
        throw new Error(`accuracy output directory must be new and empty: ${this.outputDir}`);
      }
    }
    ensurePrivateDir(this.outputDir);
    this.identity = structuredClone(identity);
    return this.outputDir;
  }

  async writePartial(value, { force = false } = {}) {
    this._requireInitialized();
    const now = this.now();
    if (!force && this.lastPartialWriteAt != null
        && now - this.lastPartialWriteAt < this.partialWriteIntervalMs) {
      return { path: null, skipped: true };
    }
    const payload = { ...value, identity: structuredClone(this.identity) };
    const partialPath = path.join(this.outputDir, 'run.partial.json');
    writePrivateJson(partialPath, payload);
    this.lastPartialWriteAt = now;
    return { path: partialPath, skipped: false };
  }

  loadPartial(expectedIdentity) {
    this._requireInitialized();
    const file = path.join(this.outputDir, 'run.partial.json');
    if (!fs.existsSync(file)) throw new Error('resume partial evidence not found');
    const value = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (value.status === 'completed') throw new Error('completed accuracy runs cannot be resumed');
    if (stableStringify(value.identity) !== stableStringify(expectedIdentity)) {
      throw new Error('resume identity mismatch');
    }
    return value;
  }

  async archiveAlertImages(client, events) {
    this._requireInitialized();
    const archived = [];
    for (const event of events ?? []) {
      const copy = { ...event };
      for (const field of ['detectedPicture', 'fullPicture']) {
        const resource = copy[field];
        delete copy[field];
        if (!resource) continue;
        try {
          const artifact = await client.downloadArtifact(resource);
          const sha256 = sha256Buffer(artifact.buffer);
          const extension = extensionForContentType(artifact.contentType);
          const relative = path.posix.join('artifacts', 'alerts', `${sha256}${extension}`);
          const absolute = path.join(this.outputDir, ...relative.split('/'));
          if (!fs.existsSync(absolute)) {
            ensurePrivateDir(path.dirname(absolute));
            fs.writeFileSync(absolute, artifact.buffer, { mode: PRIVATE_FILE_MODE });
            chmodBestEffort(absolute, PRIVATE_FILE_MODE);
          }
          copy[`${field}Artifact`] = {
            path: relative,
            sha256,
            sizeBytes: artifact.buffer.length,
            contentType: artifact.contentType,
          };
        } catch (error) {
          copy[`${field}ArtifactError`] = sanitizeError(error.message);
        }
      }
      archived.push(copy);
    }
    return archived;
  }

  async finalize({ privateRun, summary }) {
    this._requireInitialized();
    const sanitized = sanitizeSummary(summary);
    const paths = await this._finalizeBundle({
      privateRun,
      publicName: 'summary.json',
      publicValue: sanitized,
      validate: () => assertAccuracySummary(sanitized),
      render: () => renderAccuracyHtml(sanitized),
    });
    return { ...paths, summaryPath: paths.publicPath };
  }

  async finalizeDiagnostic({ privateRun, diagnostic }) {
    this._requireInitialized();
    const sanitized = sanitizeSummary(diagnostic);
    const paths = await this._finalizeBundle({
      privateRun,
      publicName: 'threshold-diagnostic.json',
      publicValue: sanitized,
      validate: () => assertThresholdDiagnosticDocument(sanitized),
      render: () => renderThresholdDiagnosticHtml(sanitized),
    });
    return { ...paths, diagnosticPath: paths.publicPath };
  }

  async _finalizeBundle({ privateRun, publicName, publicValue, validate, render }) {
    const privatePath = path.join(this.outputDir, 'run.private.json');
    const publicPath = path.join(this.outputDir, publicName);
    const htmlPath = path.join(this.outputDir, 'report.html');
    const integrityPath = path.join(this.outputDir, 'integrity.json');
    const warnings = [];
    writePrivateJson(privatePath, { ...privateRun, identity: structuredClone(this.identity) });
    writePrivateJson(publicPath, publicValue);
    try { validate(); }
    catch (error) { warnings.push(`result validation failed: ${error.message}`); }
    try { writePrivateFile(htmlPath, render()); }
    catch (error) { warnings.push(`report generation failed: ${error.message}`); }
    const partialPath = path.join(this.outputDir, 'run.partial.json');
    try {
      const artifacts = inventoryFiles(this.outputDir, new Set([
        'integrity.json',
        'run.partial.json',
      ]));
      writePrivateJson(integrityPath, {
        schemaVersion: 1,
        evidenceKind: 'cosmo-accuracy-integrity',
        inputs: {
          protocolVersion: this.identity.protocolVersion ?? null,
          suiteSha256: this.identity.suiteSha256 ?? null,
          caseManifestSha256: this.identity.caseManifestSha256 ?? null,
          caseSetSha256: this.identity.caseSetSha256 ?? null,
          taskConfigSha256: Array.isArray(publicValue.tasks)
            ? Object.fromEntries(publicValue.tasks.map((task) => [
                task.id,
                [...(task.taskConfigHashes ?? [])],
              ]))
            : structuredClone(this.identity.taskConfigSha256 ?? {}),
          toolIdentitySha256: this.identity.tool == null
            ? null
            : sha256Buffer(Buffer.from(stableStringify(this.identity.tool))),
          samples: (privateRun?.cases ?? []).map((item) => ({
            id: item.id,
            sha256: item.sha256 ?? null,
          })),
        },
        artifacts,
      });
    } catch (error) {
      warnings.push(`integrity generation failed: ${error.message}`);
    }
    if (fs.existsSync(partialPath)) {
      try {
        writePrivateJson(partialPath, {
          status: 'completed',
          protocolVersion: this.identity.protocolVersion ?? null,
          runId: this.identity.runId ?? null,
          completedAt: privateRun?.endedAt ?? publicValue?.endedAt ?? new Date().toISOString(),
          identitySha256: sha256Buffer(Buffer.from(stableStringify(this.identity))),
          privateRunSha256: sha256File(privatePath),
          resultPath: publicName,
          resultSha256: sha256File(publicPath),
          integritySha256: fs.existsSync(integrityPath) ? sha256File(integrityPath) : null,
        });
      } catch (error) {
        warnings.push(`checkpoint finalization failed: ${error.message}`);
      }
    }
    return { privatePath, publicPath, htmlPath, integrityPath, warnings };
  }

  _requireInitialized() {
    if (!this.identity) throw new Error('evidence writer is not initialized');
  }
}

export function sanitizeSummary(value) {
  return sanitizeValue(value);
}

function sanitizeValue(value) {
  if (Array.isArray(value)) return value.map(sanitizeValue);
  if (value && typeof value === 'object') {
    return Object.fromEntries(Object.entries(value)
      .filter(([key]) => !SENSITIVE_KEY.test(key))
      .map(([key, item]) => [key, sanitizeValue(item)]));
  }
  if (typeof value !== 'string') return value;
  if (value.startsWith('/') || ABSOLUTE_WINDOWS_PATH.test(value)) return '[REDACTED_PATH]';
  return value
    .replace(URL, '[REDACTED_URL]')
    .replace(IPV4, '[REDACTED_IP]')
    .replace(EMBEDDED_POSIX_PATH, '$1[REDACTED_PATH]')
    .replace(EMBEDDED_DEVICE_ID, '[REDACTED_ID]');
}

function inventoryFiles(root, excluded) {
  const files = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const absolute = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(absolute);
      else if (entry.isFile()) {
        const relative = path.relative(root, absolute).split(path.sep).join('/');
        if (excluded.has(relative) || relative.endsWith('.tmp')) continue;
        const stat = fs.statSync(absolute);
        files.push({ path: relative, sha256: sha256File(absolute), sizeBytes: stat.size });
      }
    }
  };
  walk(root);
  return files.sort((a, b) => a.path.localeCompare(b.path));
}

function extensionForContentType(contentType) {
  const normalized = String(contentType ?? '').toLowerCase();
  if (normalized === 'image/png') return '.png';
  if (normalized === 'image/webp') return '.webp';
  if (normalized === 'image/gif') return '.gif';
  return '.jpg';
}

function sanitizeError(message) {
  return String(message ?? 'artifact download failed')
    .replace(URL, '[REDACTED_URL]')
    .replace(IPV4, '[REDACTED_IP]');
}
