import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { generateBenchmarkPages } from './generate-v1.1-benchmark-pages.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, '..');
const root = path.join(workspace, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');
const extensionPath = path.join(workspace, 'docs', 'benchmarks', 'scenario-bench', 'rk3588', 'results.json');
const errors = [];
const extension = readJsonIfPresent(path.dirname(extensionPath), 'results.json');
const options = parseArguments(process.argv.slice(2));
const { archivePath } = options;

const requiredSourceFiles = [
  'README.md',
  'README.zh-CN.md',
  'RELEASE-CHECKLIST.md',
  'LICENSES.md',
  'methodology.md',
  'release-manifest.json',
  'SHA256SUMS',
  'results/cases.schema.json',
  'results/dual-cv-72h.json',
  'results/vlm-observations.json',
];

if (!fs.existsSync(root)) {
  fail(`benchmark source root is missing: ${root}`);
} else {
  for (const relativePath of requiredSourceFiles) requireFile(root, relativePath);
}

const manifest = readJsonIfPresent(root, 'release-manifest.json');
const platformDefinitions = validateManifest(manifest);
const canonicalPlatforms = validateCanonicalCases(manifest, platformDefinitions);
const vlm = validateVlmEvidence(manifest, platformDefinitions);
const dualCv72Hour = validateDualCv72HourEvidence(manifest, platformDefinitions, canonicalPlatforms);
if (archivePath) validateArchivedEvidence(archivePath, manifest, canonicalPlatforms);
validateCanonicalSourceLayout(platformDefinitions);
validateSchemaDocument();
if (!extension) fail('RK3588 extension source is required');
else validateRk3588Extension(extension);

const sourceFiles = fs.existsSync(root) ? walk(root) : [];
// VitePress renders these canonical Markdown pages beside the standalone
// reports. Only their source-derived paths belong to the site renderer; an
// arbitrary extra HTML file must still fail the report inventory check.
const sitePagePaths = new Set(options.outputRoot ? sourceFiles
  .filter((file) => file.endsWith('.md'))
  .map((file) => relativeTo(root, file).replace(/\.md$/u, '.html')) : []);
for (const file of sourceFiles.filter((entry) => entry.toLowerCase().endsWith('.json'))) {
  const value = readJsonWithError(file);
  if (value !== null) validateJsonReferences(root, file, value, null);
}

let generatedRoot = options.outputRoot;
let temporaryRoot = null;
let reportCount = 0;
if (!options.sourceOnly) {
  try {
    let generation = null;
    if (!generatedRoot) {
      temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'cosmoedge-v1.1-validate-'));
      generatedRoot = temporaryRoot;
      generation = generateBenchmarkPages({ sourceRoot: root, outputRoot: generatedRoot, requireRk3588Extension: true });
    }
    reportCount = validateGeneratedPack(generatedRoot, manifest, canonicalPlatforms, vlm, dualCv72Hour, generation);
  } catch (error) {
    fail(`benchmark report validation failed: ${error.message}`);
  }
}

validatePublicScrub(sourceFiles, root, 'source');
if (extension) validatePublicScrub([extensionPath], path.dirname(extensionPath), 'RK3588 extension');
validateChecksums(root, 'source SHA256SUMS');
// Markdown may link to generated reports. Resolve those links against the final
// output in the full check, after the source-only prebuild admission succeeds.
if (!options.sourceOnly) validateLinksAndLanguages(root, sourceFiles, generatedRoot);

if (generatedRoot && fs.existsSync(generatedRoot)) {
  const generatedFiles = walk(generatedRoot);
  for (const file of generatedFiles.filter((entry) => entry.toLowerCase().endsWith('.json'))) {
    const value = readJsonWithError(file);
    if (value !== null) validateJsonReferences(generatedRoot, file, value, null);
  }
  validatePublicScrub(generatedFiles, generatedRoot, 'generated pack');
  validateChecksums(generatedRoot, 'generated SHA256SUMS');
  // Site pages use VitePress's base URL and route rules, not portable pack
  // paths. They remain included in scrub and checksum/inventory validation.
  const packFiles = generatedFiles.filter((file) => !sitePagePaths.has(relativeTo(generatedRoot, file)));
  validateLinksAndLanguages(generatedRoot, packFiles, null);
}
// An explicitly supplied output belongs to the caller; validation never
// regenerates, repairs or removes it.
if (temporaryRoot) fs.rmSync(temporaryRoot, { recursive: true, force: true });

if (errors.length) {
  console.error(`Validation failed (${errors.length}):`);
  for (const error of errors) console.error(`- ${error}`);
  process.exit(1);
}

const totalCases = canonicalPlatforms.reduce((count, item) => count + item.cases.length, 0);
if (options.sourceOnly) {
  console.log(`Source validation passed: ${platformDefinitions.length} platforms, ${totalCases} canonical cases, evidence, public scrub and checksums; generated report links remain for the output check.`);
} else {
  console.log(
    `Validation passed: ${platformDefinitions.length} manifest-defined platforms, ${totalCases} canonical cases, ` +
    `${reportCount} generated bilingual reports, validated VLM performance, controlled dual-CV 72-hour evidence, ` +
    'relative links, public scrub, and source/generated checksums verified.',
  );
}

function validateManifest(value) {
  if (!value) return [];
  if (value.schemaVersion !== 4) fail('manifest schemaVersion must be 4');
  if (value.release?.version !== '1.1.0' || value.release?.tag !== 'v1.1.0') {
    fail('manifest release identity must be version 1.1.0 and tag v1.1.0');
  }
  if (value.release?.publicationState !== 'prepared-not-published') {
    fail('manifest release.publicationState must be prepared-not-published');
  }
  if (!sha1(value.sourceBaseline?.commit)) fail('manifest sourceBaseline.commit must be a frozen 40-character Git commit');
  if (!sha1(value.sourceBaseline?.tree)) fail('manifest sourceBaseline.tree must be a frozen 40-character Git tree');
  if (!sha256(value.dataset?.sha256)) fail('manifest dataset.sha256 must be a 64-character SHA-256');
  const targetFpsValues = value.controls?.smallModelTargetFps;
  if (!Array.isArray(targetFpsValues) || !targetFpsValues.length || targetFpsValues.some((fps) => !positiveNumber(fps))) {
    fail('manifest controls.smallModelTargetFps must be a non-empty array of positive numbers');
  } else if (new Set(targetFpsValues).size !== targetFpsValues.length) {
    fail('manifest controls.smallModelTargetFps must not contain duplicates');
  }
  if (!positiveInteger(value.evidence?.smallModelCaseCount)) fail('manifest evidence.smallModelCaseCount must be a positive integer');
  if (value.evidence?.caseSchema !== 'results/cases.schema.json') fail('manifest evidence.caseSchema must point to the canonical schema');
  if (value.evidence?.vlmObservations !== 'results/vlm-observations.json') fail('manifest evidence.vlmObservations must point to the canonical VLM file');
  if (typeof value.benchmarkTool?.role !== 'string') {
    fail('manifest benchmarkTool.role must be a string');
  }

  const benchmarkToolFiles = {
    cliSha256: 'tools/scenario-bench/src/cli.js',
    evaluatorSha256: 'tools/scenario-bench/src/step-evaluator.js',
    metricsSamplerSha256: 'tools/scenario-bench/src/metrics-sampler.js',
    reportWriterSha256: 'tools/scenario-bench/src/report-writer.js',
    vlmReadinessSha256: 'tools/scenario-bench/src/vlm-readiness.js',
    scenarioPackageSha256: 'tools/scenario-bench/src/scenario-package.js',
    taskRunnerSha256: 'tools/scenario-bench/src/task-runner.js',
    lockfileSha256: 'tools/scenario-bench/package-lock.json',
  };
  for (const [field, relative] of Object.entries(benchmarkToolFiles)) {
    const expected = value.benchmarkTool?.[field];
    if (!sha256(expected)) {
      fail(`manifest benchmarkTool.${field} must be a 64-character SHA-256`);
      continue;
    }
    const file = path.join(workspace, ...relative.split('/'));
    if (!fs.existsSync(file)) {
      fail(`manifest benchmark tool file is missing: ${relative}`);
      continue;
    }
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== expected) fail(`manifest benchmarkTool.${field} differs from ${relative}`);
  }

  const archive = value.evidence?.fullEvidenceArchive;
  if (!archive || typeof archive !== 'object') {
    fail('manifest evidence.fullEvidenceArchive is required');
  } else {
    if (!sha256(archive.sha256)) fail('full evidence archive SHA-256 is invalid');
    if (!sha1(archive.sourceCommit) || !sha1(archive.sourceTree)) fail('full evidence archive source commit/tree is invalid');
    if (archive.scope !== '49 canonical small-model cases only') fail('full evidence archive scope must remain limited to the 49 small-model cases');
    if (archive.publicationState !== 'prepared-not-published') fail('full evidence archive must remain prepared-not-published');
    if (archive.includedInRepository !== false) fail('full evidence archive must not be included in the repository');
  }

  if (value.qualification?.canonicalBenchmarkDatasetComplete !== true) {
    fail('manifest qualification.canonicalBenchmarkDatasetComplete must be true');
  }
  if (value.qualification?.generatedReportContractVerified !== true) {
    fail('manifest qualification.generatedReportContractVerified must be true');
  }
  if (value.qualification?.productReleaseQualified !== false) {
    fail('benchmark evidence must not claim product release qualification');
  }

  const definitions = Array.isArray(value.platforms) ? value.platforms : [];
  if (!definitions.length) fail('manifest must declare at least one platform');
  const ids = definitions.map((item) => item?.id);
  if (ids.some((id) => typeof id !== 'string' || !/^[a-z0-9-]+$/.test(id))) fail('manifest contains an invalid platform id');
  if (new Set(ids).size !== ids.length) fail('manifest contains duplicate platform ids');
  if (!definitions.some((item) => item?.scope === 'release-platform')) fail('manifest must declare at least one release platform');
  const rk3576 = definitions.find((item) => item?.id === 'rk3576');
  const rv1126b = definitions.find((item) => item?.id === 'rv1126b');
  if (rk3576?.scope !== 'release-platform' || rv1126b?.scope !== 'release-platform') {
    fail('RK3576 and RV1126B must share the release-platform scope');
  }

  const allowedScopes = new Set(['release-platform', 'additional-experimental-platform']);
  for (const definition of definitions) {
    const label = `manifest platform ${definition?.id ?? '<missing>'}`;
    if (!allowedScopes.has(definition?.scope)) fail(`${label} has an unsupported scope`);
    if (definition?.environment !== `environments/${definition.id}.json`) fail(`${label} environment reference is not canonical`);
    if (definition?.models !== `models/${definition.id}.json`) fail(`${label} model reference is not canonical`);
    if (definition?.results !== `results/${definition.id}/cases.json`) fail(`${label} results reference must point to its canonical case file`);
  }

  const expectedCaseFiles = definitions.map((item) => `results/${item.id}/cases.json`).sort();
  const declaredCaseFiles = Array.isArray(value.evidence?.canonicalSmallModelCases)
    ? [...value.evidence.canonicalSmallModelCases].sort()
    : [];
  if (!sameArray(expectedCaseFiles, declaredCaseFiles)) {
    fail('manifest evidence.canonicalSmallModelCases must exactly match the manifest platform inventory');
  }
  return definitions;
}

function validateCanonicalCases(manifestValue, definitions) {
  const result = [];
  const globalCaseKeys = new Set();
  const sourceSummaryHashes = new Set();
  for (const definition of definitions) {
    const relativePath = `results/${definition.id}/cases.json`;
    requireFile(root, relativePath);
    const value = readJsonIfPresent(root, relativePath);
    if (!value) continue;
    result.push(value);
    const label = `${definition.id} canonical cases`;
    expectObjectKeys(value, [
      '$schema', 'schemaVersion', 'benchmark', 'platformId', 'platform', 'scope', 'evidenceDate', 'caseCount', 'gates', 'cases',
    ], label);
    if (value.$schema !== '../cases.schema.json') fail(`${label} has an incorrect schema reference`);
    if (value.schemaVersion !== 3) fail(`${label} schemaVersion must be 3`);
    if (value.platformId !== definition.id) fail(`${label} platformId does not match the manifest`);
    if (value.scope !== definition.scope) fail(`${label} scope does not match the manifest`);
    if (!/^\d{4}-\d{2}-\d{2}$/.test(value.evidenceDate ?? '')) fail(`${label} evidenceDate is invalid`);
    if (!deepEqual(value.gates, manifestValue?.controls?.cvGates)) fail(`${label} gates differ from the manifest controls`);

    const cases = Array.isArray(value.cases) ? value.cases : [];
    if (!Array.isArray(value.cases)) fail(`${label} cases must be an array`);
    if (value.caseCount !== cases.length) fail(`${label} caseCount does not match its case array`);
    const localIds = new Set();
    for (const benchmarkCase of cases) {
      validateCase(definition, value.gates ?? {}, benchmarkCase);
      const caseKey = `${definition.id}/${benchmarkCase?.caseId}`;
      if (localIds.has(benchmarkCase?.caseId)) fail(`${label} contains duplicate case id ${benchmarkCase?.caseId}`);
      if (globalCaseKeys.has(caseKey)) fail(`duplicate global case key ${caseKey}`);
      localIds.add(benchmarkCase?.caseId);
      globalCaseKeys.add(caseKey);
      if (sha256(benchmarkCase?.sourceSummarySha256)) {
        if (sourceSummaryHashes.has(benchmarkCase.sourceSummarySha256)) fail(`duplicate source summary hash in ${caseKey}`);
        sourceSummaryHashes.add(benchmarkCase.sourceSummarySha256);
      }
    }
    for (const workload of ['person-detector', 'no-safety-helmet-analysis']) {
      for (const fps of manifestValue?.controls?.smallModelTargetFps ?? []) {
        if (!cases.some((item) => item.workload === workload && item.targetFps === fps)) {
          fail(`${label} is missing ${workload} at ${fps} FPS`);
        }
      }
    }
    if (!cases.some((item) => item.workload === 'concurrent-mixed')) fail(`${label} is missing a concurrent-mixed case`);
  }

  const actualCount = result.reduce((count, item) => count + (item.cases?.length ?? 0), 0);
  if (manifestValue && actualCount !== manifestValue.evidence?.smallModelCaseCount) {
    fail(`canonical case total ${actualCount} does not match manifest evidence.smallModelCaseCount ${manifestValue.evidence?.smallModelCaseCount}`);
  }
  return result;
}

function validateCase(definition, gates, value) {
  const label = `${definition.id}/${value?.caseId ?? '<missing-case-id>'}`;
  expectObjectKeys(value, [
    'caseId', 'sourceCaseId', 'workload', 'targetFps', 'configuredChannels', 'outcome', 'boundaryKind', 'lastPassingChannels',
    'firstBlockedChannels', 'blockedReason', 'sourceSummarySha256', 'steps',
  ], label);
  const identity = parseCaseId(value?.caseId);
  const sourceIdentity = parseSourceCaseId(value?.sourceCaseId);
  if (!identity) fail(`${label} caseId does not encode workload, target FPS, and configured channels`);
  if (!sourceIdentity) fail(`${label} sourceCaseId does not encode the archived workload, target FPS, and configured channels`);
  if (identity && value.workload !== identity.workload) fail(`${label} workload differs from its caseId`);
  if (identity && value.targetFps !== identity.targetFps) fail(`${label} targetFps differs from its caseId`);
  if (identity && value.configuredChannels !== identity.configuredChannels) fail(`${label} configuredChannels differs from its caseId`);
  if (sourceIdentity && value.workload !== sourceIdentity.workload) fail(`${label} workload differs from its archived sourceCaseId`);
  if (sourceIdentity && value.targetFps !== sourceIdentity.targetFps) fail(`${label} targetFps differs from its archived sourceCaseId`);
  if (sourceIdentity && value.configuredChannels !== sourceIdentity.configuredChannels) fail(`${label} configuredChannels differs from its archived sourceCaseId`);
  if (!sha256(value?.sourceSummarySha256)) fail(`${label} sourceSummarySha256 is invalid`);
  if (!positiveInteger(value?.configuredChannels) || !positiveInteger(value?.lastPassingChannels)) fail(`${label} channel boundaries must be positive integers`);
  if (value?.lastPassingChannels > value?.configuredChannels) fail(`${label} lastPassingChannels exceeds configuredChannels`);

  const allowedOutcomes = new Set(['completed', 'stopped', 'setup-blocked']);
  const allowedBoundaries = new Set(['lower-bound', 'performance-stop', 'binding-blocked', 'storage-blocked']);
  if (!allowedOutcomes.has(value?.outcome)) fail(`${label} outcome is invalid`);
  if (!allowedBoundaries.has(value?.boundaryKind)) fail(`${label} boundaryKind is invalid`);

  const steps = Array.isArray(value?.steps) ? value.steps : [];
  if (!steps.length) fail(`${label} must contain at least one measured step`);
  let previousChannel = 0;
  for (const [index, step] of steps.entries()) {
    validateStep(label, value, gates, step, index);
    if (positiveInteger(step?.channels) && step.channels <= previousChannel) fail(`${label} step channels must be strictly increasing`);
    previousChannel = step?.channels ?? previousChannel;
  }

  const passingChannels = steps.filter((step) => step?.result === 'PASS').map((step) => step.channels);
  const nonPassingChannels = steps.filter((step) => step?.result !== 'PASS').map((step) => step.channels);
  const measuredLastPass = passingChannels.length ? Math.max(...passingChannels) : null;
  if (measuredLastPass !== value?.lastPassingChannels) fail(`${label} lastPassingChannels does not match measured PASS steps`);

  if (value?.boundaryKind === 'lower-bound') {
    if (value.outcome !== 'completed' || value.firstBlockedChannels !== null || value.blockedReason !== null) {
      fail(`${label} lower-bound metadata is inconsistent`);
    }
    if (value.lastPassingChannels !== value.configuredChannels || nonPassingChannels.length) {
      fail(`${label} lower-bound must pass every configured step`);
    }
  } else if (value?.boundaryKind === 'performance-stop') {
    const firstNonPass = nonPassingChannels.length ? Math.min(...nonPassingChannels) : null;
    if (value.outcome !== 'stopped' || value.firstBlockedChannels !== firstNonPass || typeof value.blockedReason !== 'string') {
      fail(`${label} performance-stop metadata is inconsistent`);
    }
  } else if (value) {
    if (value.outcome !== 'setup-blocked' || value.firstBlockedChannels !== value.lastPassingChannels + 1) {
      fail(`${label} setup-blocked boundary metadata is inconsistent`);
    }
    if (nonPassingChannels.length || typeof value.blockedReason !== 'string') {
      fail(`${label} setup-blocked case must contain only completed PASS steps and a reason`);
    }
  }
}

function validateStep(caseLabel, benchmarkCase, gates, step, index) {
  const label = `${caseLabel} step ${index + 1}`;
  expectObjectKeys(step, [
    'channels', 'holdSeconds', 'result', 'tasks', 'minimumProcessingFps', 'maximumDetectorLatencyMs',
    'maximumCriticalPathLatencyMs', 'averageDiscardRate', 'maximumChannelDiscardRate', 'acceleratorPeakPercent',
    'acceleratorMemoryPeakPercent', 'cpuPeakPercent', 'memoryPeakPercent', 'failureReason',
  ], label);
  if (!positiveInteger(step?.channels) || step.channels > benchmarkCase.configuredChannels) fail(`${label} channels are invalid`);
  if (!positiveNumber(step?.holdSeconds)) fail(`${label} holdSeconds must be positive`);
  if (!new Set(['PASS', 'FAIL', 'STOP']).has(step?.result)) fail(`${label} result is invalid`);
  if (step?.result === 'PASS' && step.failureReason !== null) fail(`${label} PASS must not contain a failure reason`);
  if (step?.result !== 'PASS' && typeof step?.failureReason !== 'string') fail(`${label} non-PASS result must contain a failure reason`);

  const tasks = Array.isArray(step?.tasks) ? step.tasks : [];
  const expectedTaskNames = benchmarkCase.workload === 'concurrent-mixed'
    ? ['person-detector', 'no-safety-helmet-analysis']
    : [benchmarkCase.workload];
  if (!sameArray(tasks.map((task) => task?.name).sort(), [...expectedTaskNames].sort())) fail(`${label} task inventory differs from its workload`);
  for (const task of tasks) {
    expectObjectKeys(task, [
      'name', 'targetFps', 'minimumProcessingFps', 'minimumFpsRatio', 'missingRate', 'averageDiscardRate',
      'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs',
    ], `${label} task ${task?.name ?? '<missing>'}`);
    if (task?.targetFps !== benchmarkCase.targetFps) fail(`${label} task ${task?.name} targetFps differs from its case`);
    for (const key of ['minimumProcessingFps', 'minimumFpsRatio', 'missingRate', 'averageDiscardRate', 'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs']) {
      if (!nullableNonNegativeNumber(task?.[key])) fail(`${label} task ${task?.name} ${key} must be a non-negative number or null`);
    }
  }
  for (const key of [
    'minimumProcessingFps', 'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs', 'averageDiscardRate',
    'maximumChannelDiscardRate', 'acceleratorPeakPercent', 'acceleratorMemoryPeakPercent', 'cpuPeakPercent', 'memoryPeakPercent',
  ]) {
    if (!nullableNonNegativeNumber(step?.[key])) fail(`${label} ${key} must be a non-negative number or null`);
  }

  if (step?.result === 'PASS') {
    for (const task of tasks) {
      if (!(task.minimumFpsRatio >= gates.minFpsRatio)) fail(`${label} PASS violates the task FPS-ratio gate`);
      if (!(task.missingRate <= gates.maxMissingRate)) fail(`${label} PASS violates the task missing-rate gate`);
      if (!(task.averageDiscardRate <= gates.maxAverageDiscardRate)) fail(`${label} PASS violates the task discard-rate gate`);
      if (!(task.maximumDetectorLatencyMs <= gates.maxDetectorLatencyMs)) fail(`${label} PASS violates the detector-latency gate`);
      if (!(task.maximumCriticalPathLatencyMs <= gates.maxCriticalPathLatencyMs)) fail(`${label} PASS violates the critical-path gate`);
    }
    if (!(step.averageDiscardRate <= gates.maxAverageDiscardRate)) fail(`${label} PASS violates the aggregate discard-rate gate`);
    if (!(step.maximumDetectorLatencyMs <= gates.maxDetectorLatencyMs)) fail(`${label} PASS violates the aggregate detector-latency gate`);
    if (!(step.maximumCriticalPathLatencyMs <= gates.maxCriticalPathLatencyMs)) fail(`${label} PASS violates the aggregate critical-path gate`);
  }
}

function validateVlmEvidence(manifestValue, definitions) {
  const value = readJsonIfPresent(root, 'results/vlm-observations.json');
  if (!value) return { observations: [] };
  const label = 'canonical VLM evidence';
  if (value.schemaVersion !== 4 || typeof value.title !== 'string'
      || value.validationDate !== '2026-08-24' || value.crossPlatformComparable !== true) {
    fail(`${label} identity is inconsistent`);
  }

  const refresh = manifestValue?.evidence?.vlmRefresh;
  if (!sha1(value.source?.commit) || !sha1(value.source?.tree)
      || value.source.commit !== refresh?.sourceCommit
      || value.source.tree !== refresh?.sourceTree
      || value.source.commit !== 'f4ec2fc51f1753231165f143d14522effbb77125'
      || value.source.tree !== '2ed191ac67df2fc23c36ffa9933a387683f51732'
      || value.source.candidateVersion !== 'V1.1.0'
      || value.source.releaseStateAtFreeze !== 'draft-release-without-tag-or-assets') {
    fail(`${label} source commit/tree differs from the frozen candidate`);
  }
  if (refresh?.crossPlatformComparable !== true || refresh?.protocolId !== value.protocol?.id) {
    fail('manifest VLM refresh differs from the canonical protocol');
  }

  const protocol = value.protocol;
  if (protocol?.targetFpsPerChannel !== 0.1
      || protocol?.holdSecondsPerStep !== 60
      || protocol?.sampleIntervalSeconds !== 3
      || !sameArray(protocol?.channelSequence ?? [], [1, 2, 3, 4, 5, 6, 7, 8])
      || protocol?.rampChannelsPerStep !== 1
      || protocol?.readiness?.scope !== 'newly-added-route'
      || protocol?.readiness?.completionCounter !== 'task-local'
      || protocol?.readiness?.requiresCompletionAdvance !== true
      || protocol?.readiness?.requiresQwenLatency !== true
      || protocol?.readiness?.occursBeforeFormalSampling !== true
      || protocol?.gates?.minimumFpsRatio !== 0.8
      || protocol?.gates?.maximumMissingRate !== 0
      || protocol?.gates?.maximumAverageDiscardRate !== 0.05
      || protocol?.gates?.maximumChannelDiscardRate !== 0.01
      || protocol?.gates?.maximumDiskUsedPercent !== 99
      || protocol?.gates?.fpsGateExecuted !== true
      || manifestValue?.controls?.vlmFpsGateEnabled !== true) {
    fail(`${label} executed protocol is inconsistent`);
  }
  const manifestValidation = manifestValue?.evidence?.vlmValidation;
  if (manifestValidation?.classification !== value.claim?.class
      || manifestValidation?.targetFpsPerChannel !== protocol.targetFpsPerChannel
      || manifestValidation?.minimumActiveRouteFpsRatio !== protocol.gates.minimumFpsRatio
      || manifestValidation?.fpsGateExecuted !== true
      || manifestValidation?.taskLocalReadinessRequired !== true
      || manifestValidation?.taskLocalCompletionCounterRequired !== true
      || manifestValidation?.sameInputPromptAndWindow !== true
      || manifestValidation?.threePlatformEndToEndAccepted !== value.claim?.threePlatformEndToEndAccepted) {
    fail('manifest VLM validation policy differs from the canonical evidence');
  }
  if (value.claim?.class !== 'validated-vlm-short-run-capacity'
      || value.claim?.capacityMeasured !== true
      || value.claim?.maximumCapacityCertified !== false
      || value.claim?.productionConfigurationCertified !== false) {
    fail(`${label} claim class or scope is inconsistent`);
  }
  if (value.input?.sha256 !== manifestValue?.dataset?.sha256
      || value.input?.sizeBytes !== manifestValue?.dataset?.sizeBytes
      || value.prompt?.text !== '判断有没有人') {
    fail(`${label} controlled input or prompt differs from the frozen protocol`);
  }

  const expectedVlmIds = ['bm1688', 'cv186x', 'rk3576'];
  const expectedVlmResults = {
    bm1688: {
      sourceMetricsSha256: '337da0e746343cfa02025dc939842a68f661731d77a7bc83070159efc8300c74',
      sourceSummarySha256: 'cc591974ae6416632bf0a70e7f394c4f54639f63b2411b6360552762f6748c61',
      sourceReportSha256: '9caad6ad2a0a25a65be1ab831fe7e47e11cb2d691b3043c76406feac2bddc771',
      packageSha256: 'd9ef69d53eb3c7a55c281dcdf3eabeb97766d653af67806257654c3ad66b50a4',
      packageSizeBytes: 62936226,
      engineSha256: '33dc3f74c7dd656e6980c97d83181cc660ee72ce73719ff0750e5ef509503bdb',
      libraries: [
        { name: 'libbmlib.so', sha256: '9cb9d523440026f764a3274cf4fb4ece53a104c7ef938188019129641c0baef3' },
        { name: 'libbmrt.so', sha256: '6ccf533e5d95e418a4ee7a20f2739758218c36aac22ac4fd187bec226b80dbff' },
        { name: 'libbmrt.so.1.0', sha256: '51d8f2c9255fb167d8d73e235268ac4fb302593c241e12c5bffb29b20320fce1' },
      ],
      modelSha256: '31a03f4845d612b3c9e946468c2032254c42be54594a0848eb5938fc0f4bd767',
      tokenizerSha256: 'cb16258a9df7062f4c4d291a1501b07fb0750c9a8b247c5639dba0369e42f0cb',
      configSha256: 'ca7583cd9fb359ea842461464a1b4f08ad0604f9daf54b04e759d4a2233659e6',
      passingChannels: 6,
      firstFailureChannels: 7,
      firstFailureRatio: 0.695,
      endToEndSha256: '9750d12e35f54470bb0a349dc4ef7ffb523e9dd35bb0acef28b77871afeae833',
      endToEndSizeBytes: 5551,
    },
    cv186x: {
      sourceMetricsSha256: '0a2509dea89d9f74ef5c6bdeeed373affd413ac2c06c8b51a38e18db0c5974f6',
      sourceSummarySha256: 'eb72a0f2fba8aaf501189ba819b930ae900dc37f0acdb1aafd949e75b2197219',
      sourceReportSha256: '6e699611390db033d4e16cd072330a3612c230d1c45c20483d2657b48ec7af9e',
      packageSha256: 'b1ddea61b8fa8b7c5985fb8802bc29ffc69ecf8ee5ffae5629e6cbcd6356a742',
      packageSizeBytes: 62936226,
      engineSha256: '33dc3f74c7dd656e6980c97d83181cc660ee72ce73719ff0750e5ef509503bdb',
      libraries: [
        { name: 'libbmlib.so', sha256: '9cb9d523440026f764a3274cf4fb4ece53a104c7ef938188019129641c0baef3' },
        { name: 'libbmrt.so', sha256: '6ccf533e5d95e418a4ee7a20f2739758218c36aac22ac4fd187bec226b80dbff' },
        { name: 'libbmrt.so.1.0', sha256: '51d8f2c9255fb167d8d73e235268ac4fb302593c241e12c5bffb29b20320fce1' },
      ],
      modelSha256: '8d258ab0d16bb8836fe8de4e66b241890d71e64f016961e7d63588b6f7f76f1b',
      tokenizerSha256: 'cb16258a9df7062f4c4d291a1501b07fb0750c9a8b247c5639dba0369e42f0cb',
      configSha256: 'b2cdea93d82808d62e47b3abff616a9c13838f7c10af094f15138fda3f5d226b',
      passingChannels: 6,
      firstFailureChannels: 7,
      firstFailureRatio: 0.692,
      endToEndSha256: '7614232c403abbb410657f3ad5caa39a2ad2d23849ae28d100fb2d2fc9226653',
      endToEndSizeBytes: 5547,
    },
    rk3576: {
      sourceMetricsSha256: '1905968e8b9cde3785d1bc779d0bf2413eb5db4743bbe43d12065886d5ff9f76',
      sourceSummarySha256: '0fca37d7c6338b84802833ed77cc563caf09d60f388c48776b4db5481c0c1e3e',
      sourceReportSha256: 'c3f4f23e0a9a059b9332e93e9e887d28cbeaf926962636f12f553f7c9be999c2',
      packageSha256: 'c63b8618fbfe497dd9d49fc7e2b80d7a4d184eb3d34286dec7813d7566886f71',
      packageSizeBytes: 52422736,
      engineSha256: 'b8d8a4f93d851ecd9ccd48454f9e57b1bc08dbf10e2fe96b4d8da64730220931',
      libraries: [
        { name: 'librkllmrt.so', sha256: '6a9e4fc5324c68921c3a900340361e107af7599fe34dc8fa7759b2c5ae22a6e6' },
        { name: 'librknnrt.so', sha256: 'd31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8' },
      ],
      modelSha256: 'ef6c1b234620f51749093b0a5210e5ea40c474c85cc91b3187260be8c7cfdb02',
      visionSha256: 'a64fa9ebd58314b80f5e002c37c13ac98df2445b3f7de1ba9a685e921782778f',
      tokenizerSha256: '5f9e4d4901a92b997e463c1f46055088b6cca5ca61a6522d1b9f64c4bb81cb42',
      configSha256: '996f59bda0494d9d56519a6ae026c0c64369c935cc3f8bbf7127e094cbcb267a',
      passingChannels: 4,
      firstFailureChannels: 5,
      firstFailureRatio: 0.697,
      endToEndSha256: 'bf5204b3204b959c84933c2842263662d2aa0b01a76c5a2d627158e23a0bdbca',
      endToEndSizeBytes: 5546,
    },
  };
  const releaseIds = new Set(definitions.filter((item) => item.scope === 'release-platform').map((item) => item.id));
  const observations = Array.isArray(value.observations) ? value.observations : [];
  if (!sameArray(observations.map((item) => item?.platformId).sort(), [...expectedVlmIds].sort())) {
    fail(`${label} platform inventory must be exactly BM1688, CV186X, and RK3576`);
  }

  const acceptance = value.endToEndAcceptance;
  const acceptancePlatforms = Array.isArray(acceptance?.platforms) ? acceptance.platforms : [];
  if (acceptance?.candidatePackageSharedWithCapacityRun !== true
      || !sameArray(acceptance?.requiredStages ?? [], [
        'model-load', 'task-creation', 'valid-inference-result', 'event-or-alarm-output',
        'task-recovery-after-service-restart',
      ])
      || !sameArray(acceptancePlatforms.map((item) => item?.platformId).sort(), [...expectedVlmIds].sort())) {
    fail(`${label} end-to-end acceptance inventory is inconsistent`);
  }
  const acceptanceFields = [
    'modelLoad', 'taskCreation', 'validInferenceResult', 'eventOrAlarmOutput',
    'taskRecoveryAfterServiceRestart',
  ];
  for (const item of acceptancePlatforms) {
    const expected = expectedVlmResults[item?.platformId];
    if (!['PASS', 'PENDING'].includes(item?.status)) fail(`${item?.platformId} VLM end-to-end status is invalid`);
    for (const field of acceptanceFields) {
      if (!['PASS', 'PENDING'].includes(item?.[field])) fail(`${item?.platformId} VLM end-to-end ${field} is invalid`);
    }
    const allStagesPass = acceptanceFields.every((field) => item[field] === 'PASS');
    if ((item.status === 'PASS') !== allStagesPass) fail(`${item.platformId} VLM end-to-end status does not match its stages`);
    if (item.status === 'PASS' && (!sha256(item?.evidenceSha256)
        || !positiveInteger(item?.evidenceSizeBytes))) {
      fail(`${item.platformId} VLM end-to-end source evidence identity is incomplete`);
    }
    if (item.status !== 'PASS'
        || item.evidenceSha256 !== expected?.endToEndSha256
        || item.evidenceSizeBytes !== expected?.endToEndSizeBytes) {
      fail(`${item.platformId} VLM end-to-end result differs from the frozen evidence identity`);
    }
  }
  const allEndToEndPass = acceptancePlatforms.every((item) => item.status === 'PASS');
  if (value.claim?.threePlatformEndToEndAccepted !== allEndToEndPass) {
    fail(`${label} three-platform end-to-end claim differs from the recorded stages`);
  }
  const expectedEvidenceStatus = allEndToEndPass ? 'validated-vlm-performance' : 'validated-capacity-e2e-pending';
  if (value.evidenceStatus !== expectedEvidenceStatus) fail(`${label} evidenceStatus is inconsistent`);

  for (const observation of observations) {
    const observationLabel = `${observation?.platformId ?? '<missing>'} VLM result`;
    const expected = expectedVlmResults[observation?.platformId];
    if (!releaseIds.has(observation?.platformId)) fail(`${observationLabel} is not attached to a release platform`);
    for (const field of ['sourceSummarySha256', 'sourceMetricsSha256', 'sourceReportSha256']) {
      if (!sha256(observation?.[field])) fail(`${observationLabel} ${field} is invalid`);
    }
    if (observation?.evidenceDate !== value.validationDate) fail(`${observationLabel} evidence date is inconsistent`);
    if (observation?.sourceMetricsSha256 !== expected?.sourceMetricsSha256
        || observation?.sourceSummarySha256 !== expected?.sourceSummarySha256
        || observation?.sourceReportSha256 !== expected?.sourceReportSha256) {
      fail(`${observationLabel} source evidence identity differs from the frozen result`);
    }
    if (observation?.package?.version !== 'V1.1.0'
        || observation?.package?.targetChip !== observation.platformId
        || !sha256(observation?.package?.sha256)
        || !positiveInteger(observation?.package?.sizeBytes)) {
      fail(`${observationLabel} package identity is incomplete`);
    }
    if (observation.package.sha256 !== expected?.packageSha256
        || observation.package.sizeBytes !== expected?.packageSizeBytes) {
      fail(`${observationLabel} package identity differs from the frozen candidate package`);
    }
    if (!sha256(observation?.runtimeIdentity?.engineSha256)
        || !Array.isArray(observation?.runtimeIdentity?.libraries)
        || observation.runtimeIdentity.libraries.length < 2
        || observation.runtimeIdentity.libraries.some((item) => typeof item?.name !== 'string' || !sha256(item?.sha256))) {
      fail(`${observationLabel} runtime identity is incomplete`);
    }
    if (observation.runtimeIdentity.engineSha256 !== expected?.engineSha256
        || !deepEqual(observation.runtimeIdentity.libraries, expected?.libraries)) {
      fail(`${observationLabel} runtime identity differs from the frozen candidate`);
    }
    if (!sha256(observation?.modelIdentity?.model?.sha256)
        || !sha256(observation?.modelIdentity?.tokenizer?.sha256)
        || !sha256(observation?.modelIdentity?.config?.sha256)) {
      fail(`${observationLabel} model/tokenizer/config identity is incomplete`);
    }
    if (observation.modelIdentity.model.sha256 !== expected?.modelSha256
        || observation.modelIdentity.tokenizer.sha256 !== expected?.tokenizerSha256
        || observation.modelIdentity.config.sha256 !== expected?.configSha256
        || (observation.platformId === 'rk3576' && observation.modelIdentity.vision.sha256 !== expected?.visionSha256)) {
      fail(`${observationLabel} model identity differs from the frozen candidate`);
    }
    if (observation.platformId === 'rk3576' && !sha256(observation?.modelIdentity?.vision?.sha256)) {
      fail('RK3576 VLM vision-model identity is incomplete');
    }
    const modelDocument = readJsonIfPresent(root, `models/${observation.platformId}.json`);
    const publicModel = modelDocument?.models?.find((item) => String(item?.publicId ?? '').startsWith('vlm-'));
    if (publicModel?.sha256 !== observation.modelIdentity.model.sha256
        || publicModel?.tokenizerSha256 !== observation.modelIdentity.tokenizer.sha256
        || publicModel?.configSha256 !== observation.modelIdentity.config.sha256) {
      fail(`${observationLabel} model identity differs from the platform model document`);
    }
    if (observation.platformId === 'rk3576' && publicModel?.visionSha256 !== observation.modelIdentity.vision.sha256) {
      fail('RK3576 VLM vision-model hash differs from the platform model document');
    }
    if (observation?.workload?.targetFpsPerChannel !== protocol.targetFpsPerChannel
        || observation?.workload?.counterSemantics !== 'task-local-completion-counter'
        || observation?.gates?.minimumFpsRatio !== protocol.gates.minimumFpsRatio
        || observation?.gates?.maximumMissingRate !== protocol.gates.maximumMissingRate
        || observation?.gates?.maximumAverageDiscardRate !== protocol.gates.maximumAverageDiscardRate) {
      fail(`${observationLabel} workload or gate identity differs from the common protocol`);
    }

    const steps = Array.isArray(observation?.steps) ? observation.steps : [];
    let expectedChannel = 1;
    for (const step of steps) {
      const stepLabel = `${observationLabel} step ${expectedChannel}`;
      if (step?.channels !== expectedChannel) fail(`${observationLabel} channels must be the contiguous prefix starting at one`);
      expectedChannel += 1;
      for (const field of [
        'holdSeconds', 'targetFpsPerChannel', 'currentRouteFps', 'minimumActiveRouteFps',
        'minimumActiveRouteFpsRatioObserved', 'telemetryMissingRate', 'averageDiscardRate',
        'maximumChannelDiscardRate', 'cpuPeakPercent', 'memoryPeakPercent', 'diskPeakPercent',
      ]) {
        if (!nonNegativeNumber(step?.[field])) fail(`${stepLabel} ${field} is invalid`);
      }
      if (!nullableNonNegativeNumber(step?.acceleratorPeakPercent)) fail(`${stepLabel} acceleratorPeakPercent is invalid`);
      if (step.holdSeconds !== protocol.holdSecondsPerStep
          || step.targetFpsPerChannel !== protocol.targetFpsPerChannel) {
        fail(`${stepLabel} timing or target FPS differs from the common protocol`);
      }
      if (step?.readiness?.status !== 'PASS'
          || !positiveInteger(step?.readiness?.probes)
          || !nonNegativeNumber(step?.readiness?.elapsedMs)
          || step?.readiness?.taskLocalCompletionCounterAdvanced !== true
          || step?.readiness?.qwenLatencyObserved !== true) {
        fail(`${stepLabel} did not complete task-local readiness before formal sampling`);
      }
      const gatePass = step.minimumActiveRouteFpsRatioObserved >= protocol.gates.minimumFpsRatio
        && step.telemetryMissingRate <= protocol.gates.maximumMissingRate
        && step.averageDiscardRate <= protocol.gates.maximumAverageDiscardRate
        && step.maximumChannelDiscardRate <= protocol.gates.maximumChannelDiscardRate
        && step.diskPeakPercent <= protocol.gates.maximumDiskUsedPercent;
      if (step.result !== (gatePass ? 'PASS' : 'FAIL')) fail(`${stepLabel} result differs from the executed gates`);
      if ((step.result === 'PASS') !== (step.failureReason === null)) fail(`${stepLabel} failure reason is inconsistent`);
    }

    const passing = steps.filter((step) => step.result === 'PASS').map((step) => step.channels);
    const failures = steps.filter((step) => step.result === 'FAIL').map((step) => step.channels);
    const lastPassing = passing.length ? Math.max(...passing) : null;
    const firstFailure = failures.length ? Math.min(...failures) : null;
    const boundary = observation?.capacityBoundary;
    if (boundary?.classification !== 'gate-first-failure'
        || boundary?.verifiedPassingChannels !== lastPassing
        || boundary?.firstFailureChannels !== firstFailure
        || boundary?.firstFailureChannels !== boundary?.verifiedPassingChannels + 1
        || boundary?.firstFailureCategory !== 'fps-ratio'
        || boundary?.capacityExact !== true
        || typeof boundary?.reason !== 'string'
        || steps.at(-1)?.channels !== firstFailure
        || steps.at(-1)?.minimumActiveRouteFpsRatioObserved >= protocol.gates.minimumFpsRatio) {
      fail(`${observationLabel} exact first-gate-failure boundary is inconsistent`);
    }
    if (boundary.verifiedPassingChannels !== expected?.passingChannels
        || boundary.firstFailureChannels !== expected?.firstFailureChannels
        || steps.at(-1)?.minimumActiveRouteFpsRatioObserved !== expected?.firstFailureRatio) {
      fail(`${observationLabel} capacity boundary differs from the accepted short-run result`);
    }
  }
  if (manifestValue?.evidence?.vlmObservations !== 'results/vlm-observations.json') fail('manifest VLM reference changed');
  return value;
}

function validateDualCv72HourEvidence(manifestValue, definitions, canonicalPlatforms) {
  const value = readJsonIfPresent(root, 'results/dual-cv-72h.json');
  if (!value) return { observations: [] };

  const label = 'canonical dual-CV 72-hour evidence';
  expectObjectKeys(value, [
    'schemaVersion', 'evidenceStatus', 'title', 'claim', 'window', 'source', 'input', 'workload',
    'executionPolicy', 'integrityPolicy', 'observations', 'privateEvidence',
  ], label);
  if (value.schemaVersion !== 2) fail(`${label} schemaVersion must be 2`);
  if (value.evidenceStatus !== 'completed-private-source') fail(`${label} evidenceStatus is inconsistent`);
  if (typeof value.title !== 'string') {
    fail(`${label} title must be a string`);
  }

  expectObjectKeys(value.claim, [
    'class', 'configuredWorkloadPass', 'capacityClaimAllowed', 'rtspResilienceClaimAllowed',
    'productReleaseQualified', 'interpretation',
  ], `${label} claim`);
  if (value.claim?.class !== 'controlled-72h-local-loop-stability-observation'
      || value.claim?.configuredWorkloadPass !== true
      || value.claim?.capacityClaimAllowed !== false
      || value.claim?.rtspResilienceClaimAllowed !== false
      || value.claim?.productReleaseQualified !== false
      || typeof value.claim?.interpretation !== 'string') {
    fail(`${label} must remain a configured-workload stability observation without capacity, RTSP, or production claims`);
  }

  const controls = manifestValue?.controls?.dualCv72Hour;
  expectObjectKeys(controls, [
    'durationHours', 'sampleIntervalSeconds', 'expectedSamples', 'minimumCoverageRatio',
    'boundaryGraceSeconds', 'maximumGapSeconds', 'targetFpsPerTask', 'minimumFpsRatio',
    'maximumDiscardRate', 'businessTasksPerChannel', 'modelStagesPerChannel', 'diskPolicy',
    'scheduledRestartPolicy',
  ], 'manifest controls.dualCv72Hour');
  const expectedControls = {
    durationHours: 72,
    sampleIntervalSeconds: 60,
    expectedSamples: 4320,
    minimumCoverageRatio: 0.95,
    boundaryGraceSeconds: 180,
    maximumGapSeconds: 180,
    targetFpsPerTask: 5,
    minimumFpsRatio: 0.8,
    maximumDiscardRate: 0,
    businessTasksPerChannel: 2,
    modelStagesPerChannel: 3,
    diskPolicy: {
      executedIntegrityGateEnabled: false,
      projectionReportThresholdPercent: 99,
      futureSafeguardThresholdPercent: 90,
      futureSafeguardEffectiveAfterObservation: true,
      futureSafeguardAppliedRetroactively: false,
    },
    scheduledRestartPolicy: {
      initialStateDisabledOnAllPlatforms: true,
      controlledState: 'disabled',
      restorationRequired: false,
      restartResilienceClaimAllowed: false,
    },
  };
  if (!deepEqual(controls, expectedControls)) fail('manifest controls.dualCv72Hour differ from the frozen observation contract');

  expectObjectKeys(value.window, [
    'startedAt', 'endedAt', 'durationHours', 'sampleIntervalSeconds', 'expectedSamples',
    'minimumCoverageRatio', 'boundaryGraceSeconds', 'maximumGapSeconds',
  ], `${label} window`);
  for (const key of [
    'durationHours', 'sampleIntervalSeconds', 'expectedSamples', 'minimumCoverageRatio',
    'boundaryGraceSeconds', 'maximumGapSeconds',
  ]) {
    if (!deepEqual(value.window?.[key], controls?.[key])) fail(`${label} window.${key} differs from the manifest controls`);
  }
  const startedAt = Date.parse(value.window?.startedAt);
  const endedAt = Date.parse(value.window?.endedAt);
  if (!Number.isFinite(startedAt) || !Number.isFinite(endedAt)) {
    fail(`${label} window timestamps must be valid ISO-8601 values`);
  } else if (endedAt - startedAt !== expectedControls.durationHours * 60 * 60 * 1000) {
    fail(`${label} window timestamps do not span exactly 72 hours`);
  }
  if (value.window?.expectedSamples !== value.window?.durationHours * 3600 / value.window?.sampleIntervalSeconds) {
    fail(`${label} expectedSamples is inconsistent with its duration and sample interval`);
  }

  const refresh = manifestValue?.evidence?.dualCv72HourRefresh;
  expectObjectKeys(refresh, [
    'status', 'sourceCommit', 'sourceTree', 'runManifestSha256', 'suiteStateSha256',
    'suiteSummarySha256', 'projectionToolSha256', 'projectionKind', 'monitorLaunchBytesFrozen',
    'rawEvidence', 'claimClass', 'capacityClaimAllowed', 'rtspResilienceClaimAllowed',
    'productReleaseQualified',
  ], 'manifest evidence.dualCv72HourRefresh');
  if (manifestValue?.evidence?.dualCv72HourObservation !== 'results/dual-cv-72h.json') {
    fail('manifest evidence.dualCv72HourObservation must point to the canonical 72-hour file');
  }
  if (refresh?.status !== '2026-08-24 completed controlled observation'
      || refresh?.claimClass !== value.claim?.class
      || refresh?.capacityClaimAllowed !== false
      || refresh?.rtspResilienceClaimAllowed !== false
      || refresh?.productReleaseQualified !== false
      || refresh?.projectionKind !== 'post-run deterministic projection'
      || refresh?.monitorLaunchBytesFrozen !== false
      || typeof refresh?.rawEvidence !== 'string') {
    fail('manifest dual-CV 72-hour evidence classification is inconsistent');
  }
  for (const key of ['sourceCommit', 'sourceTree']) {
    if (!sha1(refresh?.[key])) fail(`manifest dualCv72HourRefresh.${key} is invalid`);
  }
  for (const key of ['runManifestSha256', 'suiteStateSha256', 'suiteSummarySha256', 'projectionToolSha256']) {
    if (!sha256(refresh?.[key])) fail(`manifest dualCv72HourRefresh.${key} is invalid`);
  }
  if (manifestValue?.qualification?.controlledLongRunConfiguredWorkloadVerified !== true) {
    fail('manifest qualification must record the controlled long-run configured workload as verified');
  }

  expectObjectKeys(value.source, ['repository', 'branch', 'commit', 'tree'], `${label} source`);
  if (value.source?.repository !== manifestValue?.sourceBaseline?.repository
      || value.source?.branch !== manifestValue?.sourceBaseline?.branch
      || value.source?.commit !== refresh?.sourceCommit
      || value.source?.tree !== refresh?.sourceTree
      || !sha1(value.source?.commit)
      || !sha1(value.source?.tree)) {
    fail(`${label} source provenance differs from the manifest`);
  }

  expectObjectKeys(value.input, [
    'datasetId', 'sha256', 'codec', 'width', 'height', 'sourceFps', 'mode', 'previewLoad',
  ], `${label} input`);
  if (value.input?.datasetId !== manifestValue?.dataset?.id
      || value.input?.sha256 !== manifestValue?.dataset?.sha256
      || value.input?.codec !== manifestValue?.dataset?.video?.codec
      || value.input?.width !== manifestValue?.dataset?.video?.width
      || value.input?.height !== manifestValue?.dataset?.video?.height
      || value.input?.sourceFps !== manifestValue?.dataset?.video?.sourceFps
      || value.input?.mode !== 'local infinite loop'
      || value.input?.previewLoad !== false
      || !sha256(value.input?.sha256)) {
    fail(`${label} controlled input identity is inconsistent`);
  }

  expectObjectKeys(value.workload, [
    'businessTasksPerChannel', 'modelStagesPerChannel', 'targetFpsPerTask', 'businessTasks',
  ], `${label} workload`);
  if (value.workload?.businessTasksPerChannel !== controls?.businessTasksPerChannel
      || value.workload?.modelStagesPerChannel !== controls?.modelStagesPerChannel
      || value.workload?.targetFpsPerTask !== controls?.targetFpsPerTask
      || !sameArray(value.workload?.businessTasks ?? [], ['person detection', 'no-safety-helmet analysis'])) {
    fail(`${label} workload differs from the frozen dual-CV contract`);
  }

  const { identityLimitation, ...monitor } = value.executionPolicy?.monitor ?? {};
  const { interpretation, ...disk } = value.executionPolicy?.disk ?? {};
  const expectedExecutionPolicy = {
    monitor: {
      kind: 'private multi-platform controller',
      standardScenarioBenchCliUsed: false,
      launchBytesFrozen: false,
    },
    disk: {
      ...controls.diskPolicy,
    },
    scheduledRestart: controls.scheduledRestartPolicy,
    projection: {
      kind: 'post-run deterministic projection',
      source: 'complete private metrics.jsonl and final private state',
      publicArtifactHashesReferToProjection: true,
    },
  };
  if (typeof identityLimitation !== 'string' || typeof interpretation !== 'string'
      || !deepEqual({ ...value.executionPolicy, monitor, disk }, expectedExecutionPolicy)) {
    fail(`${label} execution and post-observation policy boundary is inconsistent`);
  }

  expectObjectKeys(value.integrityPolicy, [
    'minimumFpsRatio', 'maximumDiscardRate', 'minimumSampleCoverageRatio',
    'maximumBoundaryLagSeconds', 'maximumSamplingGapSeconds',
    'maximumCollectorErrorSamples', 'maximumIncompleteBindingSamples', 'maximumMissingBindingSamples',
    'maximumOpenCriticalIncidents',
  ], `${label} integrity policy`);
  const expectedIntegrityPolicy = {
    minimumFpsRatio: controls?.minimumFpsRatio,
    maximumDiscardRate: controls?.maximumDiscardRate,
    minimumSampleCoverageRatio: controls?.minimumCoverageRatio,
    maximumBoundaryLagSeconds: controls?.boundaryGraceSeconds,
    maximumSamplingGapSeconds: controls?.maximumGapSeconds,
    maximumCollectorErrorSamples: 0,
    maximumIncompleteBindingSamples: 0,
    maximumMissingBindingSamples: 0,
    maximumOpenCriticalIncidents: 0,
  };
  if (!deepEqual(value.integrityPolicy, expectedIntegrityPolicy)) fail(`${label} integrity policy is inconsistent`);

  const expectedPlatformIds = definitions.map((definition) => definition.id);
  const observations = Array.isArray(value.observations) ? value.observations : [];
  const observationIds = observations.map((observation) => observation?.platformId);
  if (!sameArray(observationIds, expectedPlatformIds)) fail(`${label} platform inventory/order differs from the manifest`);
  if (new Set(observationIds).size !== observationIds.length) fail(`${label} contains duplicate platforms`);
  const canonicalNames = new Map(canonicalPlatforms.map((platform) => [platform.platformId, platform.platform]));
  const artifactHashes = new Set();
  for (const observation of observations) {
    const observationLabel = `${observation?.platformId ?? '<missing>'} dual-CV 72-hour observation`;
    expectObjectKeys(observation, [
      'platformId', 'platform', 'status', 'configuredChannels', 'businessTaskBindings', 'samples', 'fps',
      'observedResourcePeaksPercent', 'diskTrendPercent', 'telemetry', 'integrity', 'timedRestart',
      'cleanupRecord', 'sourceArtifacts',
    ], observationLabel);
    if (!expectedPlatformIds.includes(observation?.platformId)) {
      fail(`${observationLabel} is not a declared release platform`);
      continue;
    }
    if (observation.platform !== canonicalNames.get(observation.platformId)) {
      fail(`${observationLabel} platform label differs from its canonical small-model dataset`);
    }
    if (observation.status !== 'PASS'
        || !positiveInteger(observation.configuredChannels)
        || observation.businessTaskBindings !== observation.configuredChannels * value.workload?.businessTasksPerChannel) {
      fail(`${observationLabel} configured workload identity is inconsistent`);
    }

    expectObjectKeys(observation.samples, [
      'expected', 'observed', 'coverageRatio', 'firstSampleLagSeconds', 'finalSampleLagSeconds', 'maximumGapSeconds',
    ], `${observationLabel} samples`);
    const calculatedCoverage = observation.samples?.observed / observation.samples?.expected;
    if (!positiveInteger(observation.samples?.expected)
        || !positiveInteger(observation.samples?.observed)
        || ['coverageRatio', 'firstSampleLagSeconds', 'finalSampleLagSeconds', 'maximumGapSeconds']
          .some((key) => !nonNegativeNumber(observation.samples?.[key]))
        || !Number.isFinite(calculatedCoverage)
        || Math.abs(observation.samples?.coverageRatio - calculatedCoverage) > 0.000001
        || observation.samples?.expected !== value.window?.expectedSamples
        || !(observation.samples?.observed >= value.window?.expectedSamples * value.window?.minimumCoverageRatio)
        || observation.samples?.firstSampleLagSeconds > value.integrityPolicy?.maximumBoundaryLagSeconds
        || observation.samples?.finalSampleLagSeconds > value.integrityPolicy?.maximumBoundaryLagSeconds
        || observation.samples?.maximumGapSeconds > value.integrityPolicy?.maximumSamplingGapSeconds) {
      fail(`${observationLabel} violates the sampling coverage, boundary, or gap contract`);
    }

    expectObjectKeys(observation.fps, ['minimum', 'average', 'maximum'], `${observationLabel} FPS`);
    if (['minimum', 'average', 'maximum'].some((key) => !positiveNumber(observation.fps?.[key]))
        || !(observation.fps.minimum <= observation.fps.average && observation.fps.average <= observation.fps.maximum)
        || observation.fps.minimum / value.workload?.targetFpsPerTask < controls?.minimumFpsRatio) {
      fail(`${observationLabel} FPS values or PASS ratio are inconsistent`);
    }

    expectObjectKeys(observation.observedResourcePeaksPercent, ['cpu', 'memory', 'disk'], `${observationLabel} resource peaks`);
    if (Object.values(observation.observedResourcePeaksPercent ?? {}).some((peak) => !Number.isFinite(peak) || peak < 0 || peak > 100)) {
      fail(`${observationLabel} resource peaks must be finite percentages in [0, 100]`);
    }

    expectObjectKeys(observation.diskTrendPercent, [
      'first', 'last', 'minimum', 'maximum', 'changes',
    ], `${observationLabel} disk trend`);
    if (['first', 'last', 'minimum', 'maximum']
          .some((key) => !Number.isFinite(observation.diskTrendPercent?.[key])
            || observation.diskTrendPercent[key] < 0
            || observation.diskTrendPercent[key] > 100)
        || !Number.isInteger(observation.diskTrendPercent?.changes)
        || observation.diskTrendPercent.changes < 0
        || observation.diskTrendPercent.minimum > observation.diskTrendPercent.first
        || observation.diskTrendPercent.minimum > observation.diskTrendPercent.last
        || observation.diskTrendPercent.maximum < observation.diskTrendPercent.first
        || observation.diskTrendPercent.maximum < observation.diskTrendPercent.last
        || observation.diskTrendPercent.maximum !== observation.observedResourcePeaksPercent.disk) {
      fail(`${observationLabel} disk trend is inconsistent with the observed peak`);
    }

    expectObjectKeys(observation.telemetry, [
      'maximumDiscardRate', 'collectorErrorSamples', 'incompleteBindingSamples', 'missingBindingSamples',
      'openCriticalIncidents',
    ], `${observationLabel} telemetry`);
    if (observation.telemetry?.maximumDiscardRate !== controls?.maximumDiscardRate
        || observation.telemetry?.collectorErrorSamples !== value.integrityPolicy?.maximumCollectorErrorSamples
        || observation.telemetry?.incompleteBindingSamples !== value.integrityPolicy?.maximumIncompleteBindingSamples
        || observation.telemetry?.missingBindingSamples !== value.integrityPolicy?.maximumMissingBindingSamples
        || observation.telemetry?.openCriticalIncidents !== value.integrityPolicy?.maximumOpenCriticalIncidents) {
      fail(`${observationLabel} PASS must retain zero discard, collector, binding, and incident errors`);
    }

    expectObjectKeys(observation.integrity, [
      'monitorPass', 'monitorChecksPassed', 'monitorChecksTotal', 'publicationPass',
    ], `${observationLabel} integrity`);
    if (observation.integrity?.monitorPass !== true
        || observation.integrity?.monitorChecksPassed !== 8
        || observation.integrity?.monitorChecksTotal !== 8
        || observation.integrity?.publicationPass !== true) {
      fail(`${observationLabel} must pass all eight executed monitor checks and the publication projection`);
    }

    expectObjectKeys(observation.timedRestart, [
      'initialState', 'checks', 'failures', 'corrections', 'finalState', 'restorationRequired',
      'restartResilienceClaimAllowed',
    ], `${observationLabel} timed restart`);
    if (observation.timedRestart?.initialState !== 'disabled'
        || observation.timedRestart?.checks !== 80
        || observation.timedRestart?.failures !== 0
        || observation.timedRestart?.corrections !== 0
        || observation.timedRestart?.finalState !== 'disabled'
        || observation.timedRestart?.restorationRequired !== false
        || observation.timedRestart?.restartResilienceClaimAllowed !== false) {
      fail(`${observationLabel} timed-restart policy evidence is inconsistent`);
    }

    expectObjectKeys(observation.cleanupRecord, [
      'status', 'errors', 'disabledTaskBindings', 'requestedChannelDeletions', 'remainingOwnedChannels',
      'layoutsRestored', 'independentFinalStateArtifactEmitted',
    ], `${observationLabel} cleanup record`);
    if (observation.cleanupRecord?.status !== 'completed'
        || observation.cleanupRecord?.errors !== 0
        || observation.cleanupRecord?.disabledTaskBindings !== observation.businessTaskBindings
        || observation.cleanupRecord?.requestedChannelDeletions !== observation.configuredChannels
        || observation.cleanupRecord?.remainingOwnedChannels !== 0
        || observation.cleanupRecord?.layoutsRestored !== true
        || observation.cleanupRecord?.independentFinalStateArtifactEmitted !== false) {
      fail(`${observationLabel} cleanup result or independent-final-state limitation changed`);
    }

    expectObjectKeys(observation.sourceArtifacts, [
      'metricsJsonlSha256', 'summarySha256', 'reportSha256', 'restartGuardSha256', 'cleanupSha256',
    ], `${observationLabel} source artifacts`);
    if (Object.values(observation.sourceArtifacts ?? {}).some((hash) => !sha256(hash))) {
      fail(`${observationLabel} private source-artifact hashes are invalid`);
    }
    for (const hash of Object.values(observation.sourceArtifacts ?? {})) {
      if (artifactHashes.has(hash)) fail(`${label} reuses a private source-artifact hash across platforms`);
      artifactHashes.add(hash);
    }
  }

  expectObjectKeys(value.privateEvidence, [
    'publicationState', 'rawArtifactsIncludedInRepository', 'runManifestSha256', 'suiteStateSha256',
    'suiteSummarySha256', 'projectionToolSha256', 'verification', 'monitorLaunchBytesFrozen',
    'cleanupEvidence', 'cleanupLimitation',
  ], `${label} private evidence`);
  if (value.privateEvidence?.publicationState !== 'verified-private-not-published'
      || value.privateEvidence?.rawArtifactsIncludedInRepository !== false
      || value.privateEvidence?.runManifestSha256 !== refresh?.runManifestSha256
      || value.privateEvidence?.suiteStateSha256 !== refresh?.suiteStateSha256
      || value.privateEvidence?.suiteSummarySha256 !== refresh?.suiteSummarySha256
      || value.privateEvidence?.projectionToolSha256 !== refresh?.projectionToolSha256
      || typeof value.privateEvidence?.verification !== 'string'
      || value.privateEvidence?.monitorLaunchBytesFrozen !== false
      || value.privateEvidence?.cleanupEvidence !== 'private monitor record'
      || typeof value.privateEvidence?.cleanupLimitation !== 'string') {
    fail(`${label} private-evidence publication state, hashes, or cleanup limitation is inconsistent`);
  }
  for (const key of ['runManifestSha256', 'suiteStateSha256', 'suiteSummarySha256', 'projectionToolSha256']) {
    if (!sha256(value.privateEvidence?.[key])) fail(`${label} privateEvidence.${key} is invalid`);
  }
  return value;
}

function validateArchivedEvidence(file, manifestValue, platforms) {
  const resolved = path.resolve(file);
  if (!fs.existsSync(resolved) || !fs.statSync(resolved).isFile()) {
    fail(`full evidence archive is missing: ${resolved}`);
    return;
  }
  const archiveBytes = fs.readFileSync(resolved);
  const archiveDigest = crypto.createHash('sha256').update(archiveBytes).digest('hex');
  if (archiveDigest !== manifestValue?.evidence?.fullEvidenceArchive?.sha256) {
    fail('full evidence archive SHA-256 differs from the manifest');
    return;
  }

  const archiveRoot = 'docs/benchmarks/scenario-bench/v1.1/results';
  for (const platform of platforms) {
    for (const benchmarkCase of platform.cases) {
      const entry = `${archiveRoot}/${platform.platformId}/cases/${benchmarkCase.sourceCaseId}/summary.json`;
      const bytes = readArchiveEntry(resolved, entry);
      if (!bytes) continue;
      const digest = crypto.createHash('sha256').update(bytes).digest('hex');
      if (digest !== benchmarkCase.sourceSummarySha256) fail(`archive summary hash differs for ${platform.platformId}/${benchmarkCase.caseId}`);
      let raw;
      try {
        raw = JSON.parse(bytes.toString('utf8'));
      } catch (error) {
        fail(`archive summary is invalid JSON for ${platform.platformId}/${benchmarkCase.caseId}: ${error.message}`);
        continue;
      }
      const normalizedRaw = normalizeArchivedCase(raw);
      if (raw.caseId !== benchmarkCase.sourceCaseId) {
        fail(`canonical sourceCaseId differs from archived caseId for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      for (const key of [
        'workload', 'targetFps', 'configuredChannels', 'outcome', 'boundaryKind',
        'lastPassingChannels', 'firstBlockedChannels', 'blockedReason', 'steps',
      ]) {
        if (!deepEqual(normalizedRaw[key], benchmarkCase[key])) fail(`canonical ${key} differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      const archivedScope = normalizeArchivedPlatformScope(platform.platformId, raw.platformScope);
      if (raw.platformId !== platform.platformId || raw.platform !== platform.platform || archivedScope !== platform.scope) {
        fail(`canonical platform identity differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      if (raw.evidenceDate !== platform.evidenceDate) fail(`canonical evidenceDate differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      const normalizedGates = {
        minFpsRatio: raw.gates?.minimumFpsRatio,
        maxMissingRate: raw.gates?.maximumMissingRate,
        maxAverageDiscardRate: raw.gates?.maximumAverageDiscardRate,
        maxCriticalPathLatencyMs: raw.gates?.maximumCriticalPathLatencyMs,
        maxDetectorLatencyMs: raw.gates?.maximumDetectorLatencyMs,
      };
      if (!deepEqual(normalizedGates, platform.gates)) fail(`canonical gates differ from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
    }
  }

}

function readArchiveEntry(archive, entry) {
  const result = spawnSync('tar', ['-xOf', archive, entry], { encoding: null, maxBuffer: 16 * 1024 * 1024 });
  if (result.status !== 0) {
    fail(`full evidence archive is missing ${entry}`);
    return null;
  }
  return result.stdout;
}

function validateCanonicalSourceLayout(definitions) {
  for (const relativePath of ['report.html', 'report.zh-CN.html']) {
    if (fs.existsSync(path.join(root, relativePath))) fail(`generated report must not be tracked in canonical source: ${relativePath}`);
  }
  const resultsRoot = path.join(root, 'results');
  if (!fs.existsSync(resultsRoot)) return;
  const allowedRootEntries = new Set([
    'cases.schema.json', 'dual-cv-72h.json', 'vlm-observations.json', ...definitions.map((item) => item.id),
  ]);
  for (const entry of fs.readdirSync(resultsRoot, { withFileTypes: true })) {
    if (!allowedRootEntries.has(entry.name)) fail(`derived or undeclared result is tracked in canonical source: results/${entry.name}`);
  }
  for (const definition of definitions) {
    const platformRoot = path.join(resultsRoot, definition.id);
    if (!fs.existsSync(platformRoot)) continue;
    for (const entry of fs.readdirSync(platformRoot, { withFileTypes: true })) {
      if (entry.name !== 'cases.json' || !entry.isFile()) {
        fail(`derived platform artifact is tracked in canonical source: results/${definition.id}/${entry.name}`);
      }
    }
  }
}

function validateRk3588Extension(data) {
  const nonempty = (value) => typeof value === 'string' && value.trim().length > 0;
  if (data.schemaVersion !== 1 || data.platform !== 'RK3588') fail('RK3588 extension identity is invalid');
  for (const field of ['device', 'architecture', 'os', 'runtime', 'build']) {
    if (!nonempty(data.environment?.[field])) fail(`RK3588 environment.${field} is missing`);
  }
  if (!Array.isArray(data.conditions?.fpsTargets) || !sameArray(data.conditions.fpsTargets, [24, 10, 7, 5])
    || !nonempty(data.conditions?.summaryZh) || !nonempty(data.conditions?.summaryEn)) fail('RK3588 test conditions are incomplete');
  for (const kind of ['singleWorkloads', 'mixedWorkloads']) {
    if (!Array.isArray(data[kind]) || !data[kind].length) { fail(`RK3588 ${kind} must not be empty`); continue; }
    for (const row of data[kind]) {
      if (!['workloadZh', 'workloadEn', 'version', 'detailsZh', 'detailsEn'].every((key) => nonempty(row[key]))) fail(`RK3588 ${kind} row is incomplete`);
      if (!/^[0-9a-f]{8,64}$/u.test(row.version ?? '')) fail(`RK3588 ${kind} engine identity must be a SHA-256 prefix`);
      if (kind === 'singleWorkloads' && (!row.results || ![24, 10, 7, 5].every((fps) => nonempty(row.results[String(fps)])))) fail('RK3588 single-workload FPS matrix is incomplete');
      if (kind === 'mixedWorkloads' && (!Number.isFinite(row.modelStages) || !Number.isFinite(row.targetFps) || !nonempty(row.passingChannels) || !nonempty(row.bindings))) fail('RK3588 mixed-workload row is incomplete');
    }
  }
  if (!nonempty(data.functionalSource?.summaryZh) || !nonempty(data.functionalSource?.summaryEn)) fail('RK3588 function-test source description is incomplete');
  if (!Array.isArray(data.functionalValidation) || !data.functionalValidation.length) fail('RK3588 function-test results are missing');
  else for (const row of data.functionalValidation) {
    if (!['areaZh', 'areaEn', 'engine', 'resultZh', 'resultEn'].every((key) => nonempty(row[key]))) fail('RK3588 function-test row is incomplete');
  }
  if (!['pending', 'observed-not-accepted'].includes(data.longRun?.status)) fail('RK3588 long-run status is invalid');
  if (data.longRun?.status === 'observed-not-accepted') {
    const run = data.longRun;
    if (!/^[0-9a-f]{64}$/u.test(run.engine ?? '')) fail('RK3588 long-run engine must be a full SHA-256');
    if (!['summaryZh', 'summaryEn', 'conditionsZh', 'conditionsEn'].every((key) => nonempty(run[key]))) fail('RK3588 long-run description is incomplete');
    const fields = ['durationHours', 'channels', 'tasks', 'nodes', 'completeWindows', 'passingWindows', 'samples', 'gateGroups', 'queues', 'queueDiscardDelta'];
    if (!fields.every((key) => Number.isSafeInteger(run[key]) && run[key] >= 0) || typeof run.observedBypassZero !== 'boolean') fail('RK3588 long-run observations are invalid');
    if (run.durationHours !== 48 || run.channels < 1 || run.tasks < 1 || run.nodes < 1 || run.passingWindows > run.completeWindows || run.completeWindows > run.durationHours * 120) fail('RK3588 long-run observation counts are inconsistent');
  }
}

function validateSchemaDocument() {
  const schema = readJsonIfPresent(root, 'results/cases.schema.json');
  if (!schema) return;
  if (schema.$schema !== 'https://json-schema.org/draft/2020-12/schema') fail('case schema must use JSON Schema draft 2020-12');
  if (schema.type !== 'object' || schema.additionalProperties !== false) fail('case schema root must be a closed object');
  for (const definition of ['gates', 'case', 'step', 'task']) {
    if (!schema.$defs?.[definition]) fail(`case schema is missing $defs.${definition}`);
  }
  if (!schema.$defs?.case?.required?.includes('sourceCaseId')) fail('case schema must require sourceCaseId provenance');
}

function validateGeneratedPack(outputRoot, manifestValue, platforms, vlmValue, dualCv72HourValue, generationResult) {
  const caseCount = platforms.reduce((count, item) => count + item.cases.length, 0);
  const vlmIds = new Set((vlmValue?.observations ?? []).map((item) => item.platformId));
  const dualCv72HourByPlatform = new Map(
    (dualCv72HourValue?.observations ?? []).map((item) => [item.platformId, item]),
  );
  const expectedReports = [
    'report.html',
    'report.zh-CN.html',
    'results/dual-cv-72h/report.html',
    'results/dual-cv-72h/report.zh-CN.html',
  ];
  if (extension) {
    expectedReports.push('results/rk3588/report.html', 'results/rk3588/report.zh-CN.html');
    const copied = path.join(outputRoot, 'results', 'rk3588', 'results.json');
    if (!fs.existsSync(copied) || !fs.readFileSync(copied).equals(fs.readFileSync(extensionPath))) fail('RK3588 canonical copy differs from its source');
  }
  for (const platform of platforms) {
    for (const localeSuffix of ['', '.zh-CN']) {
      expectedReports.push(
        `results/${platform.platformId}/report${localeSuffix}.html`,
        `results/${platform.platformId}/cases/report${localeSuffix}.html`,
        `results/${platform.platformId}/single-workload/report${localeSuffix}.html`,
        `results/${platform.platformId}/concurrent-mixed/report${localeSuffix}.html`,
      );
      if (dualCv72HourByPlatform.has(platform.platformId)) {
        expectedReports.push(`results/${platform.platformId}/dual-cv-72h/report${localeSuffix}.html`);
      }
      if (vlmIds.has(platform.platformId)) {
        expectedReports.push(`results/${platform.platformId}/vlm-observation/report${localeSuffix}.html`);
      }
      for (const benchmarkCase of platform.cases) {
        expectedReports.push(`results/${platform.platformId}/cases/${benchmarkCase.caseId}/report${localeSuffix}.html`);
      }
    }
  }
  const actualReports = walk(outputRoot)
    .filter((file) => file.toLowerCase().endsWith('.html'))
    .map((file) => path.relative(outputRoot, file).replaceAll('\\', '/'))
    .filter((relative) => !sitePagePaths.has(relative))
    .sort();
  const sortedExpected = [...expectedReports].sort();
  if (!sameArray(actualReports, sortedExpected)) compareSets('generated report inventory', new Set(actualReports), new Set(sortedExpected));
  if (generationResult) {
    if (generationResult.reportCount !== expectedReports.length) fail('generator reportCount does not match the manifest-derived inventory');
    if (generationResult.platformCount !== platforms.length || generationResult.caseCount !== caseCount) fail('generator platform/case counts are inconsistent');
  }

  for (const report of expectedReports) {
    const file = path.join(outputRoot, ...report.split('/'));
    if (!fs.existsSync(file)) continue;
    const html = fs.readFileSync(file, 'utf8');
    const expectedLang = report.endsWith('.zh-CN.html') ? 'zh-CN' : 'en';
    const actualLang = html.match(/<html\b[^>]*\blang=["']([^"']+)["']/i)?.[1];
    if (actualLang !== expectedLang) fail(`generated report has incorrect lang: ${report}`);
    if (!/<html\b[^>]*>/iu.test(html) || !/<title>[^<]+<\/title>/iu.test(html) || !/<\/html>\s*$/iu.test(html)) {
      fail(`generated report is incomplete: ${report}`);
    }
    if (/\b(?:undefined|NaN)\b|\[object Object\]/.test(html)) fail(`generated report contains an unresolved value: ${report}`);
  }

  for (const rootReport of ['report.html', 'report.zh-CN.html']) {
    const html = fs.readFileSync(path.join(outputRoot, rootReport), 'utf8');
    const extensionLink = rootReport === 'report.zh-CN.html' ? 'results/rk3588/report.zh-CN.html' : 'results/rk3588/report.html';
    if (!reportLinks(html).has(extensionLink)) fail(`${rootReport} is missing its RK3588 extension link`);
    for (const platform of platforms) {
      if (!html.includes(platform.platform)) fail(`${rootReport} is missing release platform ${platform.platform}`);
    }
  }

  for (const relative of ['results/dual-cv-72h/report.html', 'results/dual-cv-72h/report.zh-CN.html']) {
    const html = fs.readFileSync(path.join(outputRoot, ...relative.split('/')), 'utf8');
    for (const platform of platforms) {
      if (!html.includes(platform.platform)) fail(`${relative} is missing long-run platform ${platform.platform}`);
    }
  }

  for (const platform of platforms) {
    const observation = dualCv72HourByPlatform.get(platform.platformId);
    if (!observation) continue;
    for (const localeSuffix of ['', '.zh-CN']) {
      const relative = `results/${platform.platformId}/dual-cv-72h/report${localeSuffix}.html`;
      const html = fs.readFileSync(path.join(outputRoot, ...relative.split('/')), 'utf8');
      if (!html.includes(platform.platform)
          || !html.includes(String(observation.configuredChannels))
          || !html.includes(String(observation.samples.observed))
          || !html.includes(String(observation.fps.minimum))
          || !html.includes(observation.status)) {
        fail(`${relative} does not project the canonical platform observation`);
      }
    }
  }

  for (const observation of vlmValue?.observations ?? []) {
    const acceptance = vlmValue?.endToEndAcceptance?.platforms?.find((item) => item.platformId === observation.platformId);
    const platform = platforms.find((item) => item.platformId === observation.platformId);
    for (const localeSuffix of ['', '.zh-CN']) {
      const relative = `results/${observation.platformId}/vlm-observation/report${localeSuffix}.html`;
      const html = fs.readFileSync(path.join(outputRoot, ...relative.split('/')), 'utf8');
      if (!html.includes(platform?.platform)
          || !html.includes(String(observation.capacityBoundary?.verifiedPassingChannels))
          || !html.includes(String(observation.capacityBoundary?.firstFailureChannels))
          || !html.includes(observation.package?.sha256)
          || !html.includes(observation.modelIdentity?.model?.sha256)
          || !html.includes(acceptance?.evidenceSha256)
          || !html.includes(String(acceptance?.evidenceSizeBytes))) {
        fail(`${relative} does not project the frozen VLM result and end-to-end evidence identity`);
      }
    }
  }

  for (const reportName of ['report.html', 'report.zh-CN.html']) {
    const file = path.join(outputRoot, reportName);
    if (!fs.existsSync(file)) continue;
    const html = fs.readFileSync(file, 'utf8');
    for (const platform of platforms) {
      for (const target of [
        `results/${platform.platformId}/${reportName}`,
        `results/${platform.platformId}/single-workload/${reportName}`,
        `results/${platform.platformId}/concurrent-mixed/${reportName}`,
        `results/${platform.platformId}/cases/${reportName}`,
        ...(dualCv72HourByPlatform.has(platform.platformId) ? [`results/${platform.platformId}/dual-cv-72h/${reportName}`] : []),
        ...(vlmIds.has(platform.platformId) ? [`results/${platform.platformId}/vlm-observation/${reportName}`] : []),
      ]) {
        if (!reportLinks(html).has(target)) fail(`${reportName}: missing report link ${target}`);
      }
    }
    if (!reportLinks(html).has(`results/dual-cv-72h/${reportName}`)) {
      fail(`${reportName}: missing multi-platform 72-hour report link`);
    }
  }

  for (const suffix of ['', '.zh-CN']) {
    const reportName = `report${suffix}.html`;
    const longRunReport = path.join(outputRoot, 'results', 'dual-cv-72h', reportName);
    if (fs.existsSync(longRunReport)) {
      const html = fs.readFileSync(longRunReport, 'utf8');
      for (const platform of platforms.filter((item) => dualCv72HourByPlatform.has(item.platformId))) {
        const target = `../${platform.platformId}/dual-cv-72h/${reportName}`;
        if (!reportLinks(html).has(target)) fail(`results/dual-cv-72h/${reportName}: missing platform link ${target}`);
      }
      if (!reportLinks(html).has('../dual-cv-72h.json')) {
        fail(`results/dual-cv-72h/${reportName}: missing canonical long-run link`);
      }
    }

    for (const platform of platforms.filter((item) => dualCv72HourByPlatform.has(item.platformId))) {
      const platformOverview = path.join(outputRoot, 'results', platform.platformId, reportName);
      if (!fs.existsSync(platformOverview)) continue;
      const html = fs.readFileSync(platformOverview, 'utf8');
      if (!reportLinks(html).has(`dual-cv-72h/${reportName}`)) {
        fail(`results/${platform.platformId}/${reportName}: missing 72-hour detail link`);
      }
    }
  }

  const index = readJsonIfPresent(outputRoot, 'results/index.json');
  const casesIndex = readJsonIfPresent(outputRoot, 'results/cases.json');
  const matrix = readJsonIfPresent(outputRoot, 'results/workload-matrix.json');
  if (index?.caseCount !== caseCount || casesIndex?.caseCount !== caseCount) fail('generated indexes do not contain the canonical case total');
  if (index?.publicationStatus !== manifestValue?.release?.publicationState || matrix?.publicationStatus !== manifestValue?.release?.publicationState) {
    fail('generated index publication status differs from the manifest');
  }
  if (!deepEqual(matrix?.vlmValidation?.claim, vlmValue?.claim)
      || !deepEqual(matrix?.vlmValidation?.protocol, vlmValue?.protocol)
      || !deepEqual(matrix?.vlmValidation?.endToEndAcceptance, vlmValue?.endToEndAcceptance)) {
    fail('generated VLM validation policy differs from the canonical evidence');
  }
  const expectedCapacityBoundaries = platforms.map((platform) => ({
    platformId: platform.platformId,
    capacityBoundary: vlmValue?.observations?.find((item) => item.platformId === platform.platformId)?.capacityBoundary ?? null,
  }));
  if (!deepEqual(matrix?.vlmValidation?.platforms, expectedCapacityBoundaries)) {
    fail('generated VLM capacity boundaries differ from the canonical evidence');
  }

  const generatedVlm = readJsonIfPresent(outputRoot, 'results/vlm-observations.json');
  if (!deepEqual(generatedVlm, vlmValue)) fail('generated VLM canonical copy differs from the source evidence');

  const generatedDualCv72Hour = readJsonIfPresent(outputRoot, 'results/dual-cv-72h.json');
  if (!deepEqual(generatedDualCv72Hour, dualCv72HourValue)) {
    fail('generated dual-CV 72-hour canonical copy differs from the source evidence');
  }
  const expectedLongRunIndex = {
    canonical: 'dual-cv-72h.json',
    report: 'dual-cv-72h/report.html',
    reportZhCn: 'dual-cv-72h/report.zh-CN.html',
    claimClass: dualCv72HourValue?.claim?.class,
  };
  if (!deepEqual(index?.longRun, expectedLongRunIndex)) {
    fail('generated index dual-CV 72-hour entry differs from the canonical evidence');
  }
  const indexPlatforms = Array.isArray(index?.platforms) ? index.platforms : [];
  if (!sameArray(indexPlatforms.map((item) => item?.platformId), platforms.map((item) => item.platformId))) {
    fail('generated index platform inventory differs from the canonical platform order');
  }
  for (const platform of platforms) {
    const item = indexPlatforms.find((entry) => entry?.platformId === platform.platformId);
    const hasObservation = dualCv72HourByPlatform.has(platform.platformId);
    if (item?.longRunObservation !== (hasObservation ? 'dual-cv-72h.json' : null)
        || item?.longRunReport !== (hasObservation ? `${platform.platformId}/dual-cv-72h/report.html` : null)
        || item?.longRunReportZhCn !== (hasObservation ? `${platform.platformId}/dual-cv-72h/report.zh-CN.html` : null)) {
      fail(`generated index long-run links differ for ${platform.platformId}`);
    }
  }

  const expectedLongRunMatrix = {
    canonical: 'dual-cv-72h.json',
    evidenceStatus: dualCv72HourValue?.evidenceStatus,
    claim: dualCv72HourValue?.claim,
    window: dualCv72HourValue?.window,
    workload: dualCv72HourValue?.workload,
    executionPolicy: dualCv72HourValue?.executionPolicy,
    platforms: platforms.map((platform) => {
      const observation = dualCv72HourByPlatform.get(platform.platformId);
      return {
        platformId: platform.platformId,
        configuredChannels: observation?.configuredChannels ?? null,
        businessTaskBindings: observation?.businessTaskBindings ?? null,
        observedSamples: observation?.samples?.observed ?? null,
        minimumFps: observation?.fps?.minimum ?? null,
        averageFps: observation?.fps?.average ?? null,
        diskTrendPercent: observation?.diskTrendPercent ?? null,
        monitorIntegrityPass: observation?.integrity?.monitorPass ?? null,
        publicationPass: observation?.integrity?.publicationPass ?? null,
        status: observation?.status ?? null,
        report: observation ? `${platform.platformId}/dual-cv-72h/report.html` : null,
        reportZhCn: observation ? `${platform.platformId}/dual-cv-72h/report.zh-CN.html` : null,
      };
    }),
  };
  if (!deepEqual(matrix?.longRunObservation, expectedLongRunMatrix)) {
    fail('generated workload matrix dual-CV 72-hour projection differs from the canonical evidence');
  }
  return expectedReports.length;
}

function validatePublicScrub(files, packRoot, label) {
  const forbidden = [
    [/\bhost\d+\b/gi, 'internal host alias'],
    [/\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b/g, 'private IP address'],
    [/(?<![\d.])(?:4773|7463|11099|16064|41773|67093|78510|91985|43921|63606|6047042|7486163|7463001)(?![\d.])/g, 'internal algorithm/model ID'],
    [/["']?(?:algorithm|schedule|task|scene|model)[_-]?(?:id|ID)["']?\s*[:=]\s*["']?\d{3,}/gi, 'internal algorithm/task/model ID field'],
    [/\b(?:AA|BA)_\d+\b/gi, 'internal action-node ID'],
    [/\b(?:LX|DA)\d{5,}\b/gi, 'internal channel ID'],
    [/["']?(?:channel|videoChannel)[_-]?(?:id|ID)["']?\s*:/g, 'internal channel ID field'],
    [/\b[A-Za-z]:[\\/](?![\\/])[^\s<>"']*/g, 'Windows absolute path'],
    [/(?:^|[\s"'(=])\/(?:home|root|Users|data|var|tmp|opt|mnt|srv|etc)\/[^\s<>"')]+/gm, 'Linux absolute path'],
    [/\b(?:linaro|moons|admin123|songyuhang|123456)\b/gi, 'known credential'],
    [/\b(?:username|user|account|账号|用户名)\b\s*[:=：]\s*["']?(?:admin|root|linaro|moons|songyuhang)\b/gi, 'credential/account value'],
    [/\b(?:password|passwd|credential|secret|access[_-]?token)\b\s*[:=：]\s*["']?(?!<|\$\{|redacted\b|not-set\b)[A-Za-z0-9@#$%^&*._-]{4,}/gi, 'credential-like value'],
    [/:\/\/[^/\s:@]+:[^@\s/]+@/g, 'credential embedded in URL'],
    [/\b20230808003\b/g, 'device serial number'],
    [/\b(?:5\.10\.4(?:-[a-z0-9-]+)?|6\.1\.118)\b/gi, 'non-public kernel identity'],
    [/"kernel(?:Build)?"\s*:/gi, 'public kernel field'],
    [/<th>Kernel<\/th>|OS\s*\/\s*Kernel/gi, 'public kernel column'],
  ];
  for (const file of files) {
    if (path.basename(file) === 'SHA256SUMS' || !textFile(file)) continue;
    const text = fs.readFileSync(file, 'utf8');
    for (const [pattern, description] of forbidden) {
      pattern.lastIndex = 0;
      if (pattern.test(text)) fail(`${description} found in ${label} ${relativeTo(packRoot, file)}`);
    }
  }
}

function validateChecksums(packRoot, label) {
  const checksumPath = path.join(packRoot, 'SHA256SUMS');
  if (!fs.existsSync(checksumPath)) {
    fail(`${label} is missing`);
    return;
  }
  const files = walk(packRoot);
  const expected = new Set(files
    .filter((file) => path.resolve(file) !== path.resolve(checksumPath))
    .map((file) => relativeTo(packRoot, file)));
  const declared = new Map();
  for (const line of fs.readFileSync(checksumPath, 'utf8').split(/\r?\n/).filter(Boolean)) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/);
    if (!match) {
      fail(`${label} contains an invalid line: ${line}`);
      continue;
    }
    const [, digest, target] = match;
    const normalized = path.posix.normalize(target);
    if (target !== normalized || path.posix.isAbsolute(target) || normalized === '..' || normalized.startsWith('../') || target.includes('\\')) {
      fail(`${label} contains an unsafe or non-canonical target: ${target}`);
      continue;
    }
    if (target === 'SHA256SUMS') fail(`${label} must not hash itself`);
    if (declared.has(target)) fail(`${label} contains a duplicate target: ${target}`);
    declared.set(target, digest);
  }
  compareSets(`${label} inventory`, new Set(declared.keys()), expected);
  for (const [target, digest] of declared) {
    const file = path.join(packRoot, ...target.split('/'));
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) continue;
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== digest) fail(`${label} mismatch: ${target}`);
  }
}

function validateLinksAndLanguages(packRoot, files, generatedFallbackRoot) {
  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.html'))) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(/(?:href|src)\s*=\s*["']([^"']+)["']/gi)) {
      validateRelativeLink(packRoot, file, decodeHtmlEntities(match[1]), 'HTML', generatedFallbackRoot);
    }
  }
  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.md'))) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(/!?\[[^\]]*\]\(([^)\s]+)(?:\s+["'][^)]*)?\)/g)) {
      validateRelativeLink(packRoot, file, match[1].replace(/^<|>$/g, ''), 'Markdown', generatedFallbackRoot);
    }
    for (const match of text.matchAll(/<(?:a|img)\b[^>]*(?:href|src)=["']([^"']+)["']/gi)) {
      validateRelativeLink(packRoot, file, decodeHtmlEntities(match[1]), 'Markdown HTML', generatedFallbackRoot);
    }
  }
}

function validateJsonReferences(packRoot, file, value, generatedFallbackRoot) {
  const referenceKeys = new Set([
    '$schema', 'manifest', 'releaseManifest', 'environment', 'models', 'dataset', 'card', 'overview',
    'concurrentMixed', 'singleWorkload', 'vlmObservation', 'vlmObservations', 'caseSchema', 'canonicalSmallModelCases',
    'cases', 'canonical', 'summary', 'report', 'reportZhCn', 'metrics', 'command', 'testLog', 'results',
  ]);
  visit(value, null);
  function visit(node, key) {
    if (typeof node === 'string' && referenceKeys.has(key) && looksLikeArtifactReference(node)) {
      validateRelativeLink(packRoot, file, node, 'JSON', generatedFallbackRoot);
      return;
    }
    if (Array.isArray(node)) {
      for (const item of node) visit(item, key);
    } else if (node && typeof node === 'object') {
      for (const [childKey, child] of Object.entries(node)) visit(child, childKey);
    }
  }
}

function validateRelativeLink(packRoot, sourceFile, rawTarget, kind, generatedFallbackRoot) {
  if (!rawTarget || rawTarget.startsWith('#') || /^(?:https?:|data:|mailto:|tel:)/i.test(rawTarget)) return;
  if (/^(?:javascript:|file:|\\\\|\/\/)/i.test(rawTarget)) {
    fail(`${kind} link is not an allowed relative link in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  const withoutFragment = rawTarget.split('#')[0].split('?')[0];
  if (!withoutFragment) return;
  if (withoutFragment.includes('\\') || path.posix.isAbsolute(withoutFragment)) {
    fail(`${kind} link is not relative/POSIX in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  let decoded;
  try {
    decoded = decodeURIComponent(withoutFragment);
  } catch {
    fail(`${kind} link has invalid URL encoding in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  const resolved = path.resolve(path.dirname(sourceFile), ...decoded.split('/'));
  if (!inside(packRoot, resolved)) {
    fail(`${kind} link escapes the benchmark pack in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  if (fs.existsSync(resolved)) return;
  if (generatedFallbackRoot) {
    const sourceDirectoryRelative = path.relative(packRoot, path.dirname(sourceFile));
    const generatedTarget = path.resolve(generatedFallbackRoot, sourceDirectoryRelative, ...decoded.split('/'));
    if (inside(generatedFallbackRoot, generatedTarget) && fs.existsSync(generatedTarget)) return;
  }
  fail(`broken ${kind} link in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
}

function parseCaseId(caseId) {
  const match = /^(person|nohelmet|mixed-cv)-(\d+(?:\.\d+)?)fps-(\d+)ch$/.exec(caseId ?? '');
  if (!match) return null;
  return {
    workload: match[1] === 'person' ? 'person-detector' : match[1] === 'nohelmet' ? 'no-safety-helmet-analysis' : 'concurrent-mixed',
    targetFps: Number(match[2]),
    configuredChannels: Number(match[3]),
  };
}

function parseSourceCaseId(caseId) {
  const match = /^(person|nohelmet|dual-cv)-(\d+(?:\.\d+)?)fps-(\d+)ch$/.exec(caseId ?? '');
  if (!match) return null;
  return {
    workload: match[1] === 'person' ? 'person-detector' : match[1] === 'nohelmet' ? 'no-safety-helmet-analysis' : 'concurrent-mixed',
    targetFps: Number(match[2]),
    configuredChannels: Number(match[3]),
  };
}

function normalizeArchivedCase(value) {
  const normalized = JSON.parse(JSON.stringify(value));
  if (normalized.workload === 'safety-helmet-detector') normalized.workload = 'no-safety-helmet-analysis';
  if (normalized.workload === 'dual-detector') normalized.workload = 'concurrent-mixed';
  for (const step of normalized.steps ?? []) {
    for (const task of step.tasks ?? []) {
      if (task.name === 'safety-helmet-detector') task.name = 'no-safety-helmet-analysis';
    }
  }
  return normalized;
}

function normalizeArchivedPlatformScope(platformId, scope) {
  return platformId === 'rv1126b' && scope === 'additional-experimental-platform' ? 'release-platform' : scope;
}

function parseArguments(args) {
  const result = { archivePath: null, outputRoot: null, sourceOnly: false };
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--source-only') {
      result.sourceOnly = true;
    } else if (['--archive', '--output-root'].includes(argument) && args[index + 1] && !args[index + 1].startsWith('--')) {
      const value = args[++index];
      if (argument === '--archive') result.archivePath = value;
      else result.outputRoot = path.resolve(value);
    } else {
      throw new Error('usage: validate-public-v1.1-multistream-benchmark.mjs [--archive <path>] [--source-only | --output-root <path>]');
    }
  }
  if (result.sourceOnly && result.outputRoot) throw new Error('--source-only and --output-root are mutually exclusive');
  return result;
}

function reportLinks(html) {
  return new Set([...html.matchAll(/\bhref\s*=\s*["']([^"']+)["']/giu)]
    .map((match) => decodeHtmlEntities(match[1])));
}

function expectObjectKeys(value, requiredKeys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail(`${label} must be an object`);
    return;
  }
  const allowed = new Set(requiredKeys);
  for (const key of requiredKeys) if (!(key in value)) fail(`${label} is missing ${key}`);
  for (const key of Object.keys(value)) if (!allowed.has(key)) fail(`${label} contains unexpected property ${key}`);
}

function requireFile(packRoot, relativePath) {
  const file = path.join(packRoot, ...relativePath.split('/'));
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(`missing required file: ${relativePath}`);
}

function readJsonIfPresent(packRoot, relativePath) {
  const file = path.join(packRoot, ...relativePath.split('/'));
  if (!fs.existsSync(file)) return null;
  return readJsonWithError(file);
}

function readJsonWithError(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    fail(`invalid JSON: ${relativeTo(root, file)} (${error.message})`);
    return null;
  }
}

function compareSets(label, actual, expected) {
  for (const item of expected) if (!actual.has(item)) fail(`${label} is missing: ${item}`);
  for (const item of actual) if (!expected.has(item)) fail(`${label} contains unexpected entry: ${item}`);
}

function sameArray(actual, expected) {
  return actual.length === expected.length && actual.every((value, index) => Object.is(value, expected[index]));
}

function deepEqual(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
}

function positiveInteger(value) {
  return Number.isInteger(value) && value > 0;
}

function positiveNumber(value) {
  return Number.isFinite(value) && value > 0;
}

function nonNegativeNumber(value) {
  return Number.isFinite(value) && value >= 0;
}

function nullableNonNegativeNumber(value) {
  return value === null || (Number.isFinite(value) && value >= 0);
}

function sha1(value) {
  return typeof value === 'string' && /^[0-9a-f]{40}$/.test(value);
}

function sha256(value) {
  return typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
}

function inside(directory, file) {
  const relativePath = path.relative(directory, file);
  return relativePath === '' || (!relativePath.startsWith(`..${path.sep}`) && relativePath !== '..' && !path.isAbsolute(relativePath));
}

function relativeTo(packRoot, file) {
  return path.relative(packRoot, file).replaceAll('\\', '/');
}

function walk(directory) {
  if (!fs.existsSync(directory)) return [];
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function textFile(file) {
  return /\.(?:md|html|json|txt|log|csv|tsv|xml|css|js|cjs|mjs|yml|yaml|toml|ini|cfg|svg|sh)$/i.test(file);
}

function looksLikeArtifactReference(value) {
  return value.endsWith('/') || /\.(?:html?|json|md|txt|log|yml|yaml|svg|csv)$/i.test(value.split('#')[0].split('?')[0]);
}

function decodeHtmlEntities(value) {
  return value.replaceAll('&amp;', '&').replaceAll('&#38;', '&');
}

function fail(message) {
  errors.push(message);
}
