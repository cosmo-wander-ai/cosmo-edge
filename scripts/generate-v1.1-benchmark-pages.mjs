import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(here, '..');
const defaultSourceRoot = path.join(repositoryRoot, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');
const defaultOutputRoot = path.join(repositoryRoot, 'docs', '.vitepress', 'dist', 'benchmarks', 'scenario-bench', 'v1.1');

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const options = parseArgs(process.argv.slice(2));
  if (options.writeSourceChecksums) {
    const sourceRoot = path.resolve(options.sourceRoot ?? defaultSourceRoot);
    writeChecksums(sourceRoot);
    console.log(`Updated canonical source checksums in ${path.relative(repositoryRoot, sourceRoot)}.`);
    process.exit(0);
  }
  const result = generateBenchmarkPages(options);
  console.log(
    `Generated ${result.reportCount} benchmark reports for ${result.platformCount} platforms and ` +
    `${result.caseCount} canonical cases in ${path.relative(repositoryRoot, result.outputRoot) || '.'}.`,
  );
}

export function generateBenchmarkPages({ sourceRoot = defaultSourceRoot, outputRoot = defaultOutputRoot } = {}) {
  sourceRoot = path.resolve(sourceRoot);
  outputRoot = path.resolve(outputRoot);
  if (sourceRoot === outputRoot) throw new Error('benchmark pages must be generated outside the canonical source directory');
  if (!fs.existsSync(sourceRoot)) throw new Error(`canonical benchmark source is missing: ${sourceRoot}`);

  const manifest = readJson(path.join(sourceRoot, 'release-manifest.json'));
  const platformDefs = Array.isArray(manifest.platforms) ? manifest.platforms : [];
  const platformIds = platformDefs.map((definition) => definition.id);
  fs.mkdirSync(outputRoot, { recursive: true });
  copyCanonicalAssets(sourceRoot, outputRoot, platformIds);

  const platforms = platformDefs.map((definition) => loadPlatform(sourceRoot, definition));
  const vlm = readJson(path.join(sourceRoot, 'results', 'vlm-observations.json'));
  const vlmByPlatform = new Map(vlm.observations.map((item) => [item.platformId, item]));
  const longRun = readJson(path.join(sourceRoot, 'results', 'dual-cv-72h.json'));
  const longRunByPlatform = new Map(longRun.observations.map((item) => [item.platformId, item]));
  const caseCount = platforms.reduce((count, item) => count + item.cases.length, 0);
  let reportCount = 0;

  removeGeneratedPages(outputRoot, platforms.map((item) => item.id));
  writeDerivedIndexes(outputRoot, manifest, platforms, vlm, longRun);

  for (const locale of ['en', 'zh-CN']) {
    const suffix = locale === 'zh-CN' ? '.zh-CN.html' : '.html';
    writeReport(outputRoot, `report${suffix}`, renderRootReport(locale, manifest, platforms, vlmByPlatform, longRun));
    writeReport(outputRoot, `results/dual-cv-72h/report${suffix}`, renderLongRunReport(locale, longRun, platforms));
    reportCount += 2;

    for (const platform of platforms) {
      const longRunObservation = longRunByPlatform.get(platform.id);
      writeReport(
        outputRoot,
        `results/${platform.id}/report${suffix}`,
        renderPlatformReport(locale, manifest, platform, vlmByPlatform.get(platform.id), longRunObservation),
      );
      writeReport(outputRoot, `results/${platform.id}/cases/report${suffix}`, renderCaseIndex(locale, platform));
      writeReport(outputRoot, `results/${platform.id}/single-workload/report${suffix}`, renderSingleWorkloadReport(locale, manifest, platform));
      writeReport(outputRoot, `results/${platform.id}/concurrent-mixed/report${suffix}`, renderConcurrentMixedReport(locale, platform));
      reportCount += 4;

      if (longRunObservation) {
        writeReport(
          outputRoot,
          `results/${platform.id}/dual-cv-72h/report${suffix}`,
          renderPlatformLongRunReport(locale, longRun, platform, longRunObservation),
        );
        reportCount += 1;
      }

      const observation = vlmByPlatform.get(platform.id);
      if (observation) {
        writeReport(
          outputRoot,
          `results/${platform.id}/vlm-observation/report${suffix}`,
          renderVlmReport(locale, platform, observation, vlm),
        );
        reportCount += 1;
      }

      for (const benchmarkCase of platform.cases) {
        writeReport(
          outputRoot,
          `results/${platform.id}/cases/${benchmarkCase.caseId}/report${suffix}`,
          renderCaseReport(locale, platform, benchmarkCase),
        );
        reportCount += 1;
      }
    }
  }

  writeChecksums(outputRoot);
  return { outputRoot, reportCount, platformCount: platforms.length, caseCount };
}

function loadPlatform(sourceRoot, definition) {
  const id = definition.id;
  const canonical = readJson(path.join(sourceRoot, 'results', id, 'cases.json'));
  const environment = readJson(path.join(sourceRoot, 'environments', `${id}.json`));
  return {
    id,
    name: canonical.platform,
    scope: canonical.scope,
    cases: canonical.cases,
    gates: canonical.gates,
    environment,
  };
}

function writeDerivedIndexes(outputRoot, manifest, platforms, vlm, longRun) {
  const resultsRoot = path.join(outputRoot, 'results');
  const vlmPlatformIds = new Set(vlm.observations.map((item) => item.platformId));
  const longRunByPlatform = new Map(longRun.observations.map((item) => [item.platformId, item]));
  writeJson(path.join(resultsRoot, 'index.json'), {
    schemaVersion: 3,
    benchmark: 'CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark',
    publicationStatus: manifest.release.publicationState,
    manifest: '../release-manifest.json',
    generatedAt: manifest.release.frozenAt,
    primaryClaim: 'observed short-run local-loop capacity boundary',
    caseCount: platforms.reduce((count, platform) => count + platform.cases.length, 0),
    longRun: {
      canonical: 'dual-cv-72h.json',
      report: 'dual-cv-72h/report.html',
      reportZhCn: 'dual-cv-72h/report.zh-CN.html',
      claimClass: longRun.claim.class,
    },
    platforms: platforms.map((platform) => ({
      platformId: platform.id,
      platform: platform.name,
      scope: platform.scope,
      environment: `../environments/${platform.id}.json`,
      models: `../models/${platform.id}.json`,
      cases: `${platform.id}/cases.json`,
      report: `${platform.id}/report.html`,
      reportZhCn: `${platform.id}/report.zh-CN.html`,
      vlmObservation: vlmPlatformIds.has(platform.id) ? 'vlm-observations.json' : null,
      longRunObservation: longRunByPlatform.has(platform.id) ? 'dual-cv-72h.json' : null,
      longRunReport: longRunByPlatform.has(platform.id) ? `${platform.id}/dual-cv-72h/report.html` : null,
      longRunReportZhCn: longRunByPlatform.has(platform.id) ? `${platform.id}/dual-cv-72h/report.zh-CN.html` : null,
    })),
  });

  const globalCases = [];
  for (const platform of platforms) {
    const cases = platform.cases.map((item) => ({
      platformId: platform.id,
      platform: platform.name,
      caseId: item.caseId,
      sourceCaseId: item.sourceCaseId,
      workload: item.workload,
      targetFps: item.targetFps,
      configuredChannels: item.configuredChannels,
      outcome: item.outcome,
      boundaryKind: item.boundaryKind,
      lastPassingChannels: item.lastPassingChannels,
      canonical: `${platform.id}/cases.json`,
      report: `${platform.id}/cases/${item.caseId}/report.html`,
      reportZhCn: `${platform.id}/cases/${item.caseId}/report.zh-CN.html`,
    }));
    globalCases.push(...cases);
    writeJson(path.join(resultsRoot, platform.id, 'cases', 'index.json'), {
      schemaVersion: 3,
      platformId: platform.id,
      caseCount: cases.length,
      canonical: '../cases.json',
      cases: cases.map(({
        platformId: unusedId,
        platform: unusedName,
        canonical: unusedCanonical,
        report: unusedReport,
        reportZhCn: unusedReportZhCn,
        ...item
      }) => ({
        ...item,
        report: `${item.caseId}/report.html`,
        reportZhCn: `${item.caseId}/report.zh-CN.html`,
      })),
    });
  }
  writeJson(path.join(resultsRoot, 'cases.json'), {
    schemaVersion: 3,
    benchmark: 'CosmoEdge 1.1 small-model capacity benchmark',
    caseCount: globalCases.length,
    cases: globalCases,
  });

  writeJson(path.join(resultsRoot, 'workload-matrix.json'), {
    schemaVersion: 3,
    publicationStatus: manifest.release.publicationState,
    interpretation: {
      singleWorkload: 'last passing short-run channel count under enabled CV gates',
      concurrentMixed: 'highest configured short-run point passed; observed lower bound only',
      vlm: 'exact short-run boundary from the executed 80% FPS gate after task-local readiness at each added route',
      longRun: 'configured dual-CV workload completed a controlled 72-hour local-loop observation; this is not a capacity, RTSP-resilience, production-profile, or product-release claim',
      bindingBlocked: 'the next configured channel was blocked during task binding before measurement',
      storageBlocked: 'the expansion run was blocked before measurement by its storage precondition',
    },
    platforms: platforms.map((platform) => ({
      platformId: platform.id,
      platform: platform.name,
      scope: platform.scope,
      singleWorkload: platform.cases.filter((item) => item.workload !== 'concurrent-mixed').map(matrixEntry),
      concurrentMixed: platform.cases.filter((item) => item.workload === 'concurrent-mixed').map(matrixEntry),
    })),
    vlmEvidenceStatus: vlm.evidenceStatus,
    vlmValidation: {
      claim: vlm.claim,
      protocol: vlm.protocol,
      endToEndAcceptance: vlm.endToEndAcceptance,
      platforms: platforms.map((platform) => {
        const observation = vlm.observations.find((item) => item.platformId === platform.id);
        return {
          platformId: platform.id,
          capacityBoundary: observation?.capacityBoundary ?? null,
        };
      }),
    },
    longRunObservation: {
      canonical: 'dual-cv-72h.json',
      evidenceStatus: longRun.evidenceStatus,
      claim: longRun.claim,
      window: longRun.window,
      workload: longRun.workload,
      executionPolicy: longRun.executionPolicy,
      platforms: platforms.map((platform) => {
        const observation = longRunByPlatform.get(platform.id);
        return {
          platformId: platform.id,
          configuredChannels: observation?.configuredChannels ?? null,
          businessTaskBindings: observation?.businessTaskBindings ?? null,
          observedSamples: observation?.samples?.observed ?? null,
          minimumFps: observation?.fps?.minimum ?? null,
          averageFps: observation?.fps?.average ?? null,
          diskTrendPercent: observation?.diskTrendPercent ?? null,
          monitorIntegrityPass: observation?.integrity?.monitorPass ?? null,
          publicationPass: observation?.integrity?.publicationPass ?? null,
          status: observation?.status ?? null,
          report: observation ? `${platform.id}/dual-cv-72h/report.html` : null,
          reportZhCn: observation ? `${platform.id}/dual-cv-72h/report.zh-CN.html` : null,
        };
      }),
    },
  });
}

function renderBenchmarkScope(locale) {
  return locale === 'zh-CN'
    ? '测试范围：小模型容量结果来自30秒本地循环阶梯，VLM 性能来自60秒正式门禁阶梯；独立的72小时测试覆盖表中固定路数与受控本地循环输入。完整条件见测试方法。'
    : 'Benchmark scope: small-model capacity results come from 30-second local-loop steps, VLM performance from formal 60-second gated steps, and the separate 72-hour test covers the listed fixed-channel profiles under controlled local-loop input. See the methodology for complete conditions.';
}

function renderLongRunScope(locale, linkedEvidence = false) {
  if (linkedEvidence) {
    return locale === 'zh-CN'
      ? '范围：结果对应表中固定路数与受控本地循环输入；本轮未测量最大容量、RTSP 韧性或重启恢复。完整执行条件见多平台证据说明与测试方法。'
      : 'Scope: Results apply to the listed channel counts and controlled local-loop input; maximum capacity, RTSP resilience, and restart recovery were not measured in this run. See the multi-platform evidence notes and methodology for complete execution conditions.';
  }
  return locale === 'zh-CN'
    ? '范围：结果对应表中固定路数与受控本地循环输入；本轮未测量最大容量、RTSP 韧性或重启恢复。完整执行条件见下方证据说明与测试方法。'
    : 'Scope: Results apply to the listed channel counts and controlled local-loop input; maximum capacity, RTSP resilience, and restart recovery were not measured in this run. See the evidence notes and methodology for complete execution conditions.';
}

function renderVlmScope(locale) {
  return locale === 'zh-CN'
    ? '范围：已验证 VLM 性能边界来自60秒短时阶梯；每新增一路先通过 task-local readiness，再执行每路0.1 FPS、80%达标率门禁。该结果不是最大容量、长稳或生产推荐配置认证。'
    : 'Scope: the validated VLM performance boundary comes from 60-second short-run steps. Each added route passes task-local readiness before the executed 0.1 FPS-per-channel, 80% achievement gate. This is not maximum-capacity, long-run, or production-profile certification.';
}

function renderRootReport(locale, manifest, platforms, vlmByPlatform, longRun) {
  const zh = locale === 'zh-CN';
  const targetFpsValues = manifest.controls.smallModelTargetFps;
  const longRunByPlatform = new Map(longRun.observations.map((item) => [item.platformId, item]));
  const mixedRows = platforms.map((platform) => {
    const item = findWorkloadCase(platform, 'concurrent-mixed');
    const passingTaskBindings = item.lastPassingChannels * 2;
    return [
      platformLabel(platform, locale),
      zh ? '人员检测 + 未佩戴安全帽分析' : 'Person detection + no-safety-helmet analysis',
      3,
      item.targetFps,
      displayBoundary(item),
      `${passingTaskBindings}/${passingTaskBindings}`,
    ];
  });
  const singleRows = platforms.flatMap((platform) => ['person-detector', 'no-safety-helmet-analysis'].map((workload) => [
    platform.name,
    workloadLabel(workload, locale),
    ...targetFpsValues.map((fps) => displayBoundary(findCase(platform, workload, fps))),
  ]));
  const validationProtocol = manifest.evidence?.vlmValidation;
  const vlmRows = platforms.map((platform) => {
    const item = vlmByPlatform.get(platform.id);
    if (!item) {
      return [platform.name, '—', '—', '—', zh ? '不在本次 VLM 验证范围内' : 'Outside this VLM validation'];
    }
    return [
      platform.name,
      value(validationProtocol?.targetFpsPerChannel),
      percent(validationProtocol?.minimumActiveRouteFpsRatio),
      value(item.capacityBoundary?.verifiedPassingChannels),
      capacityBoundaryReason(item, locale),
    ];
  });
  const environmentRows = platforms.map((platform) => [
    platform.name,
    platform.environment.deviceDescription,
    platform.environment.os,
    `${platform.environment.runtime.inference}; ${platform.environment.runtime.media}`,
    platform.environment.cosmoEdgeInstalledVersion,
  ]);
  const linksRows = platforms.map((platform) => [
    platformLabel(platform, locale),
    link(`results/${platform.id}/report${zh ? '.zh-CN' : ''}.html`, zh ? '平台汇总' : 'Platform overview'),
    link(`results/${platform.id}/cases/report${zh ? '.zh-CN' : ''}.html`, `${platform.cases.length} ${zh ? '个用例' : 'cases'}`),
    link(`results/${platform.id}/single-workload/report${zh ? '.zh-CN' : ''}.html`, zh ? '单任务' : 'Single-task'),
    link(`results/${platform.id}/concurrent-mixed/report${zh ? '.zh-CN' : ''}.html`, zh ? '混合任务' : 'Mixed workload'),
    longRunByPlatform.has(platform.id)
      ? link(`results/${platform.id}/dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, zh ? '72 小时长稳' : '72-hour long run')
      : '—',
    vlmByPlatform.has(platform.id) ? link(`results/${platform.id}/vlm-observation/report${zh ? '.zh-CN' : ''}.html`, 'VLM') : '—',
  ]);
  const canonicalCaseCount = platforms.reduce((count, platform) => count + platform.cases.length, 0);

  const body = [
    `<h1>${zh ? 'CosmoEdge 1.1 多平台视频分析容量基准' : 'CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark'}</h1>`,
    `<p class="lead">${platforms.map((platform) => platformLabel(platform, locale)).join(' · ')}</p>`,
    `<p>${zh
      ? `本报告发布 ${canonicalCaseCount} 个小模型受控用例、4 个完成的72小时固定配置结果，以及 ${vlmByPlatform.size} 个平台的 VLM 正式验证结果。`
      : `This report publishes ${canonicalCaseCount} controlled small-model cases, four completed 72-hour fixed-profile results, and formal VLM validation results for ${vlmByPlatform.size} platforms.`}</p>`,
    notice(renderBenchmarkScope(locale), 'scope-note'),
    `<h2>${zh ? '并发混合任务矩阵' : 'Concurrent mixed-workload matrix'}</h2>`,
    `<p>${zh ? '每路包含两个业务任务和三个模型阶段：人员检测为单检测阶段，未佩戴安全帽分析为检测加分类两阶段。' : 'Each channel contains two business tasks across three model stages: one person-detector stage plus detector and classifier stages for no-safety-helmet analysis.'}</p>`,
    table(
      zh
        ? ['平台', '每路任务组成', '模型阶段/路', '目标 FPS/任务', '通过路数', '业务任务绑定']
        : ['Platform', 'Workload per channel', 'Model stages/ch', 'Target FPS/task', 'Passing channels', 'Business-task bindings'],
      mixedRows,
    ),
    `<h2>${zh ? '单任务容量矩阵' : 'Single-task capacity matrix'}</h2>`,
    `<p>${boundaryLegend(locale)}</p>`,
    table(
      [zh ? '平台' : 'Platform', zh ? '任务' : 'Workload', ...targetFpsValues.map((fps) => `${fps} FPS`)],
      singleRows,
    ),
    `<h2>${zh ? '72 小时受控长稳观测' : '72-hour controlled long-run observation'}</h2>`,
    `<p>${zh ? '四个平台均在设定路数下完成72小时、5 FPS/任务的双 CV 本地循环观测。' : 'All four platforms completed the configured dual-CV local-loop workload for 72 hours at 5 FPS per task.'}</p>`,
    table(
      longRunMatrixHeaders(locale),
      longRunMatrixRows(locale, platforms, longRunByPlatform, 'results/', longRun.workload.targetFpsPerTask),
    ),
    `<p>${anchor(`results/dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, zh ? '打开 72 小时多平台报告' : 'Open the 72-hour multi-platform report')} · ${anchor('results/dual-cv-72h.json', zh ? 'canonical 数据' : 'canonical data')}</p>`,
  ];

  body.push(
    `<h2>${zh ? '已验证 VLM 性能' : 'Validated VLM performance'}</h2>`,
    `<p>${zh ? '三平台使用同一协议，80%吞吐阈值在测试执行时直接参与 PASS/FAIL。' : 'All three platforms use one protocol, with the 80% throughput threshold participating directly in runtime PASS/FAIL.'}</p>`,
    table(zh ? ['平台', '目标 FPS/路', '执行门禁', '最后通过路数', '首个失败原因'] : ['Platform', 'Target FPS/ch', 'Executed gate', 'Last passing channels', 'First failure'], vlmRows),
    `<p>${zh ? 'BM1688、CV186X 与 RK3576 还各自在与其容量测试相同的固定候选包上，通过模型加载、任务创建、有效推理、事件/告警输出和服务重启后任务恢复。CosmoEdge 1.1 在记录的安装包、模型与协议范围内支持三平台 VLM。' : 'BM1688, CV186X, and RK3576 also pass model load, task creation, valid inference, event/alarm output, and task recovery after service restart, each on the same fixed candidate package as its capacity run. CosmoEdge 1.1 supports VLM on all three platforms within the recorded package, model, and protocol scope.'}</p>`,
    `<h2>${zh ? '证据入口' : 'Evidence entry points'}</h2>`,
    table(zh ? ['平台', '平台报告', '用例', '单任务', '混合任务', '72 小时', 'VLM'] : ['Platform', 'Overview', 'Cases', 'Single-task', 'Mixed workload', '72-hour', 'VLM'], linksRows),
    `<h2>${zh ? '测试环境' : 'Test environment'}</h2>`,
    table(zh ? ['平台', '设备', '操作系统', '运行时 / 媒体', 'CosmoEdge'] : ['Platform', 'Device', 'OS', 'Runtime / media', 'CosmoEdge'], environmentRows),
    `<h2>${zh ? '方法与复现' : 'Method and reproduction'}</h2>`,
    `<ul><li>${zh ? '小模型测试源码' : 'Small-model source'}: <code>${escapeHtml(manifest.sourceBaseline.commit)}</code></li>` +
      `<li>${zh ? 'VLM 测试源码' : 'VLM source'}: <code>${escapeHtml(manifest.evidence?.vlmRefresh?.sourceCommit ?? '—')}</code> · ${anchor('results/vlm-observations.json', zh ? 'VLM canonical 数据' : 'VLM canonical data')}</li>` +
      `<li>${zh ? '72 小时测试源码' : '72-hour source'}: <code>${escapeHtml(longRun.source.commit)}</code> · ${anchor('results/dual-cv-72h.json', zh ? '长稳 canonical 数据' : 'long-run canonical data')}</li>` +
      `<li>${zh ? '受控输入 SHA-256' : 'Controlled input SHA-256'}: <code>${escapeHtml(manifest.dataset.sha256)}</code></li>` +
      `<li>${zh ? '四份小模型 canonical case 数据、一份 VLM canonical 数据和一份72小时长稳 canonical 数据是机器可读事实源；HTML、索引和矩阵由构建生成。' : 'Four small-model canonical case datasets, one VLM canonical dataset, and one 72-hour long-run canonical dataset are the machine-readable sources of truth; HTML, indexes, and matrices are generated at build time.'}</li></ul>`,
  );

  return page(locale, zh ? 'CosmoEdge 1.1 多平台容量基准' : 'CosmoEdge 1.1 Multi-Platform Benchmark', rootNav(locale), body.join(''));
}

function renderPlatformReport(locale, manifest, platform, observation, longRunObservation) {
  const zh = locale === 'zh-CN';
  const targetFpsValues = manifest.controls.smallModelTargetFps;
  const singleRows = ['person-detector', 'no-safety-helmet-analysis'].map((workload) => [
    workloadLabel(workload, locale),
    ...targetFpsValues.map((fps) => displayBoundary(findCase(platform, workload, fps))),
  ]);
  const mixed = findWorkloadCase(platform, 'concurrent-mixed');
  const body = [
    `<h1>${escapeHtml(platform.name)} · ${zh ? '短时容量概览' : 'Short-run capacity overview'}</h1>`,
    notice(zh
      ? '所有数值均绑定本报告的受控本地循环输入、30秒单级窗口和禁用预览条件。'
      : 'All values are bound to the controlled local-loop input, 30-second step window, and preview-disabled conditions in this report.'),
    `<h2>${zh ? '并发混合任务' : 'Concurrent mixed workload'}</h2>`,
    table(
      zh ? ['工作负载', '模型阶段/路', '目标 FPS/任务', '通过边界', '设定上限'] : ['Workload', 'Model stages/ch', 'Target FPS/task', 'Observed boundary', 'Configured maximum'],
      [[workloadLabel(mixed.workload, locale), 3, mixed.targetFps, displayBoundary(mixed), mixed.configuredChannels]],
    ),
    `<h2>${zh ? '单任务' : 'Single-task workloads'}</h2>`,
    `<p>${boundaryLegend(locale)}</p>`,
    table([zh ? '任务' : 'Workload', ...targetFpsValues.map((fps) => `${fps} FPS`)], singleRows),
    `<h2>${zh ? '环境' : 'Environment'}</h2>`,
    table(zh ? ['设备', '架构', '操作系统', '推理运行时', '媒体链路', 'CosmoEdge'] : ['Device', 'Architecture', 'OS', 'Inference runtime', 'Media path', 'CosmoEdge'], [[
      platform.environment.deviceDescription,
      platform.environment.architecture,
      platform.environment.os,
      platform.environment.runtime.inference,
      platform.environment.runtime.media,
      platform.environment.cosmoEdgeInstalledVersion,
    ]]),
    `<h2>${zh ? '详细结果' : 'Detailed results'}</h2>`,
    `<ul><li>${anchor(`cases/report${zh ? '.zh-CN' : ''}.html`, `${platform.cases.length} ${zh ? '个用例' : 'cases'}`)}</li>` +
      `<li>${anchor(`single-workload/report${zh ? '.zh-CN' : ''}.html`, zh ? '单任务汇总' : 'Single-task summary')}</li>` +
      `<li>${anchor(`concurrent-mixed/report${zh ? '.zh-CN' : ''}.html`, zh ? '混合任务汇总' : 'Mixed-workload summary')}</li>` +
      (longRunObservation ? `<li>${anchor(`dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, zh ? '72 小时双 CV 长稳观测' : '72-hour dual-CV long-run observation')}</li>` : '') +
      (observation ? `<li>${anchor(`vlm-observation/report${zh ? '.zh-CN' : ''}.html`, zh ? '已验证 VLM 性能' : 'Validated VLM performance')}</li>` : '') +
      `</ul>`,
    `<p>${zh ? '小模型测试源码' : 'Small-model test source'}: <code>${escapeHtml(manifest.sourceBaseline.commit)}</code></p>`,
  ];
  return page(locale, `${platform.name} ${zh ? '容量概览' : 'capacity overview'}`, platformNav(locale), body.join(''));
}

function renderCaseIndex(locale, platform) {
  const zh = locale === 'zh-CN';
  const rows = platform.cases.map((item) => [
    link(`${item.caseId}/report${zh ? '.zh-CN' : ''}.html`, caseLabel(item, locale)),
    workloadLabel(item.workload, locale),
    item.targetFps,
    item.configuredChannels,
    displayBoundary(item),
    boundaryKindLabel(item.boundaryKind, locale),
  ]);
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '受控用例' : 'Controlled cases'}</h1>` +
    notice(zh
      ? '这些页面由单一 canonical JSON 确定性生成，不是额外的数据副本。'
      : 'These pages are generated deterministically from one canonical JSON file; they are not additional data copies.') +
    table(zh ? ['用例', '任务', '目标 FPS', '设定路数', '观测边界', '边界类型'] : ['Case', 'Workload', 'Target FPS', 'Configured channels', 'Observed boundary', 'Boundary type'], rows);
  return page(locale, `${platform.name} ${zh ? '用例' : 'cases'}`, caseIndexNav(locale), body);
}

function renderSingleWorkloadReport(locale, manifest, platform) {
  const zh = locale === 'zh-CN';
  const sections = [];
  for (const workload of ['person-detector', 'no-safety-helmet-analysis']) {
    sections.push(`<h2>${escapeHtml(workloadLabel(workload, locale))}</h2>`);
    for (const fps of manifest.controls.smallModelTargetFps) {
      const item = findCase(platform, workload, fps);
      if (!item) continue;
      sections.push(`<h3>${fps} FPS · ${escapeHtml(displayBoundary(item))}</h3>`, renderStepTable(locale, item));
    }
  }
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '单任务短时容量' : 'Single-task short-run capacity'}</h1>` +
    notice(zh ? '结果是短时容量边界，不是生产推荐路数。' : 'Results are short-run capacity boundaries, not recommended production channel counts.') +
    sections.join('');
  return page(locale, `${platform.name} ${zh ? '单任务容量' : 'single-task capacity'}`, workloadNav(locale), body);
}

function renderConcurrentMixedReport(locale, platform) {
  const zh = locale === 'zh-CN';
  const item = findWorkloadCase(platform, 'concurrent-mixed');
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '并发混合任务观测' : 'Concurrent mixed-workload observation'}</h1>` +
    notice(zh
      ? `每路同时运行人员检测与未佩戴安全帽分析，共两个业务任务、三个模型阶段；每个业务任务 ${item.targetFps} FPS。`
      : `Each channel runs person detection and two-stage no-safety-helmet analysis concurrently: two business tasks across three model stages, at ${item.targetFps} FPS per business task.`) +
    renderStepTable(locale, item);
  return page(locale, `${platform.name} ${zh ? '并发混合任务' : 'concurrent mixed workload'}`, workloadNav(locale), body);
}

function renderLongRunReport(locale, longRun, platforms) {
  const zh = locale === 'zh-CN';
  const suffix = zh ? '.zh-CN' : '';
  const observations = new Map(longRun.observations.map((item) => [item.platformId, item]));
  const policy = longRun.integrityPolicy;
  const execution = longRun.executionPolicy;
  const firstObservation = longRun.observations[0];
  const maximumGap = Math.max(...longRun.observations.map((item) => item.samples.maximumGapSeconds));
  const body = `<h1>${zh ? 'CosmoEdge 1.1 · 72 小时受控长稳观测' : 'CosmoEdge 1.1 · 72-hour controlled long-run observation'}</h1>` +
    `<p class="lead">${zh ? '人员检测 + 未佩戴安全帽分析 · 本地循环输入 · 5 FPS/任务' : 'Person detection + no-safety-helmet analysis · local-loop input · 5 FPS/task'}</p>` +
    `<h2>${zh ? '多平台结果' : 'Multi-platform results'}</h2>` +
    table(
      longRunMatrixHeaders(locale),
      longRunMatrixRows(locale, platforms, observations, '../', longRun.workload.targetFpsPerTask),
    ) +
    `<p>${zh
      ? `四个平台固定配置均完成 PASS，各保留 ${firstObservation.samples.observed}/${firstObservation.samples.expected} 个分钟样本，覆盖率 ${percent(firstObservation.samples.coverageRatio)}；最大采样间隔 ${maximumGap} 秒，观测丢弃、采集错误、任务绑定缺失和未关闭严重事件均为 0。`
      : `All four fixed profiles completed with PASS, each retaining ${firstObservation.samples.observed}/${firstObservation.samples.expected} minute samples (${percent(firstObservation.samples.coverageRatio)} coverage). The maximum sampling gap was ${maximumGap} seconds, with zero observed discard, collection errors, missing task bindings, or open critical incidents.`}</p>` +
    notice(renderLongRunScope(locale), 'scope-note') +
    `<h2>${zh ? '固定协议' : 'Fixed protocol'}</h2>` +
    table(
      zh ? ['项目', '设定'] : ['Item', 'Setting'],
      [
        [zh ? '观测窗口' : 'Observation window', `${longRun.window.startedAt} — ${longRun.window.endedAt} (${longRun.window.durationHours} h)`],
        [zh ? '证据终点' : 'Evidence endpoint', zh ? '一个连续的72小时窗口；不另列24/48小时中间过程' : 'one continuous 72-hour window; 24/48-hour intermediate milestones are not separately reported'],
        [zh ? '采样' : 'Sampling', `${longRun.window.sampleIntervalSeconds} s · ${longRun.window.expectedSamples} ${zh ? '个理论样本' : 'expected samples'}`],
        [zh ? '完整性门禁' : 'Integrity gates', `${zh ? '覆盖率' : 'coverage'} ≥ ${percent(policy.minimumSampleCoverageRatio)} · ${zh ? '首尾延迟和最大间隔' : 'boundary lag and maximum gap'} ≤ ${policy.maximumSamplingGapSeconds} s`],
        [zh ? '工作负载' : 'Workload', `${longRun.workload.businessTasksPerChannel} ${zh ? '个业务任务/路' : 'business tasks/ch'} · ${longRun.workload.modelStagesPerChannel} ${zh ? '个模型阶段/路' : 'model stages/ch'} · ${longRun.workload.targetFpsPerTask} FPS/${zh ? '任务' : 'task'}`],
        [zh ? '输入' : 'Input', `${longRun.input.codec} ${longRun.input.width}×${longRun.input.height} @ ${longRun.input.sourceFps} FPS · SHA-256 ${longRun.input.sha256}`],
        [zh ? '预览负载' : 'Preview load', longRun.input.previewLoad ? (zh ? '开启' : 'enabled') : (zh ? '关闭' : 'disabled')],
      ],
    ) +
    `<h2>${zh ? '平台详情' : 'Platform details'}</h2><ul>` +
    platforms.map((platform) => `<li>${anchor(`../${platform.id}/dual-cv-72h/report${suffix}.html`, `${platform.name} · ${zh ? '72 小时报告' : '72-hour report'}`)}</li>`).join('') +
    `</ul>` +
    `<details class="evidence-notes"><summary>${zh ? '执行与证据说明' : 'Execution and evidence notes'}</summary><ul>` +
    `<li>${zh
      ? `磁盘在执行时只做观测，公开投影阈值为 ${execution.disk.projectionReportThresholdPercent}%；后来增加的 ${execution.disk.futureSafeguardThresholdPercent}% 防护只适用于未来运行。BM1688 与 CV186X 全程为 96%，RK3576 为 14%→15%，RV1126B 为 46%→47%。`
      : `Disk was observational during execution. The public projection threshold is ${execution.disk.projectionReportThresholdPercent}%, and the later ${execution.disk.futureSafeguardThresholdPercent}% safeguard applies only to future runs. BM1688 and CV186X stayed at 96%, RK3576 moved from 14% to 15%, and RV1126B moved from 46% to 47%.`}</li>` +
    `<li>${zh
      ? '四个平台在开始前定时重启均已关闭，观测期间各完成80次检查，无失败、无纠正写入且无需恢复；该记录只说明控制变量连续。'
      : 'Scheduled restart was disabled before launch on all four platforms. Each recorded 80 checks with no failure or corrective write and required no restoration; this establishes only control-variable continuity.'}</li>` +
    `<li>${zh
      ? 'ScenarioBench 源码快照已冻结，但长稳进程启动后私有控制器文件发生过更新，且启动时没有产出控制器摘要；因此不声称已冻结启动时控制器字节。'
      : 'The ScenarioBench source snapshot is frozen, but the private controller files changed after the process started and no launch-time controller digest was emitted; frozen launch-time controller bytes are therefore not claimed.'}</li>` +
    `<li>${zh
      ? '清理记录显示剩余本次通道为0、布局已恢复且无清理错误，但没有生成独立 final-state 侧车；清理结论仅绑定该监控记录。'
      : 'The cleanup record reports zero remaining run-owned channels, restored layouts, and no cleanup errors, but no independent final-state sidecar was emitted; the cleanup conclusion is bound to that monitor record.'}</li>` +
    `<li>${zh
      ? 'Canonical 数据按 SHA-256 记录私有 run manifest、suite state、suite summary、投影工具及逐平台 metrics、summary、report、restart-guard 和 cleanup 产物。'
      : 'The canonical data records exact SHA-256 identities for the private run manifest, suite state, suite summary, projection tool, and each platform metrics, summary, report, restart-guard, and cleanup artifact.'}</li>` +
    `</ul></details>` +
    `<p>${anchor(`../dual-cv-72h.json`, zh ? '打开 canonical 数据' : 'Open canonical data')} · ${anchor(`../../methodology.md`, zh ? '方法说明' : 'methodology')} · ${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</p>`;
  return page(locale, zh ? 'CosmoEdge 1.1 72 小时长稳观测' : 'CosmoEdge 1.1 72-hour long-run observation', longRunNav(locale), body);
}

function renderPlatformLongRunReport(locale, longRun, platform, observation) {
  const zh = locale === 'zh-CN';
  const samples = observation.samples;
  const telemetry = observation.telemetry;
  const resources = observation.observedResourcePeaksPercent;
  const disk = observation.diskTrendPercent;
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '72 小时双 CV 长稳观测' : '72-hour dual-CV long-run observation'}</h1>` +
    `<p class="lead">${observation.configuredChannels} ${zh ? '路' : 'channels'} · ${observation.businessTaskBindings} ${zh ? '个业务任务绑定' : 'business-task bindings'} · ${longRun.workload.targetFpsPerTask} FPS/${zh ? '任务' : 'task'}</p>` +
    `<h2>${zh ? '观测结果' : 'Observation result'}</h2>` +
    table(
      zh ? ['设定路数', '任务绑定', '时长', '目标 FPS/任务', '最低 / 平均 / 最高 FPS', '结果'] : ['Configured channels', 'Task bindings', 'Duration', 'Target FPS/task', 'Minimum / average / maximum FPS', 'Result'],
      [[observation.configuredChannels, observation.businessTaskBindings, `${longRun.window.durationHours} h`, longRun.workload.targetFpsPerTask, `${observation.fps.minimum} / ${observation.fps.average} / ${observation.fps.maximum}`, observation.status]],
    ) +
    notice(renderLongRunScope(locale, true), 'scope-note') +
    `<h2>${zh ? '采样连续性与完整性' : 'Sampling continuity and integrity'}</h2>` +
    table(
      zh ? ['观测 / 理论样本', '覆盖率', '首样本延迟', '尾样本延迟', '最大间隔', '采集错误', '绑定不完整', '绑定遥测缺失', '未关闭严重事故', '完整性'] : ['Observed / expected samples', 'Coverage', 'First-sample lag', 'Final-sample lag', 'Maximum gap', 'Collector errors', 'Incomplete bindings', 'Missing binding telemetry', 'Open critical incidents', 'Integrity'],
      [[
        `${samples.observed} / ${samples.expected}`,
        percent(samples.coverageRatio),
        `${samples.firstSampleLagSeconds} s`,
        `${samples.finalSampleLagSeconds} s`,
        `${samples.maximumGapSeconds} s`,
        telemetry.collectorErrorSamples,
        telemetry.incompleteBindingSamples,
        telemetry.missingBindingSamples,
        telemetry.openCriticalIncidents,
        observation.integrity.monitorPass
          ? `PASS (${observation.integrity.monitorChecksPassed}/${observation.integrity.monitorChecksTotal})`
          : 'FAIL',
      ]],
    ) +
    `<h2>${zh ? '负载与资源' : 'Workload and resources'}</h2>` +
    table(
      zh ? ['最大丢弃率', 'CPU 峰值', '内存峰值', '磁盘首值 → 尾值', '磁盘最小 / 峰值', '磁盘变化次数'] : ['Maximum discard rate', 'Peak CPU', 'Peak memory', 'Disk first → last', 'Disk minimum / peak', 'Disk changes'],
      [[percent(telemetry.maximumDiscardRate), percentWhole(resources.cpu), percentWhole(resources.memory), `${percentWhole(disk.first)} → ${percentWhole(disk.last)}`, `${percentWhole(disk.minimum)} / ${percentWhole(disk.maximum)}`, disk.changes]],
    ) +
    `<h2>${zh ? '来源' : 'Provenance'}</h2>` +
    `<p>${zh ? '源码' : 'Source'}: <code>${escapeHtml(longRun.source.commit)}</code> · tree <code>${escapeHtml(longRun.source.tree)}</code></p>` +
    `<p>${anchor(`../../dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告与证据说明' : 'Multi-platform report and evidence notes')} · ${anchor(`../../dual-cv-72h.json`, zh ? 'canonical 数据' : 'canonical data')} · ${anchor(`../../../methodology.md`, zh ? '方法说明' : 'methodology')}</p>`;
  return page(locale, `${platform.name} ${zh ? '72 小时长稳观测' : '72-hour long-run observation'}`, platformLongRunNav(locale), body);
}

function longRunMatrixHeaders(locale) {
  return locale === 'zh-CN'
    ? ['平台', '设定路数', '任务绑定', '目标 FPS/任务', '时长', '观测 / 理论样本', '覆盖率', '最低 / 平均 / 最高 FPS', '最大采样间隔', '磁盘首值 → 尾值', '结果']
    : ['Platform', 'Configured channels', 'Task bindings', 'Target FPS/task', 'Duration', 'Observed / expected samples', 'Coverage', 'Minimum / average / maximum FPS', 'Maximum sampling gap', 'Disk first → last', 'Result'];
}

function longRunMatrixRows(locale, platforms, observations, reportPrefix = null, targetFpsPerTask = 5) {
  const zh = locale === 'zh-CN';
  return platforms.map((platform) => {
    const observation = observations.get(platform.id);
    if (!observation) return [platform.name, '—', '—', '—', '—', '—', '—', '—', '—', '—', '—'];
    const label = reportPrefix
      ? link(`${reportPrefix}${platform.id}/dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, platform.name)
      : platform.name;
    return [
      label,
      observation.configuredChannels,
      observation.businessTaskBindings,
      targetFpsPerTask,
      '72 h',
      `${observation.samples.observed} / ${observation.samples.expected}`,
      percent(observation.samples.coverageRatio),
      `${observation.fps.minimum} / ${observation.fps.average} / ${observation.fps.maximum}`,
      `${observation.samples.maximumGapSeconds} s`,
      `${percentWhole(observation.diskTrendPercent.first)} → ${percentWhole(observation.diskTrendPercent.last)}`,
      observation.status,
    ];
  });
}

function renderCaseReport(locale, platform, item) {
  const zh = locale === 'zh-CN';
  const body = `<h1>${escapeHtml(platform.name)} · ${escapeHtml(caseLabel(item, locale))}</h1>` +
    `<p class="lead">${item.configuredChannels} ${zh ? '路设定' : 'configured channels'} · ${item.targetFps} FPS · 30 s/${zh ? '级' : 'step'}</p>` +
    notice(caseNotice(item, locale), item.boundaryKind === 'lower-bound' ? '' : 'experimental') +
    renderStepTable(locale, item) +
    `<p>${anchor('../../cases.json', zh ? '查看 canonical JSON' : 'Open canonical JSON')}</p>`;
  return page(locale, `${platform.name} ${caseLabel(item, locale)}`, individualCaseNav(locale), body);
}

function renderVlmReport(locale, platform, observation, vlm) {
  const zh = locale === 'zh-CN';
  const acceptance = vlm.endToEndAcceptance?.platforms?.find((item) => item.platformId === observation.platformId);
  const rows = observation.steps.map((step) => [
    step.channels,
    `${step.holdSeconds} s`,
    step.targetFpsPerChannel,
    step.currentRouteFps,
    `${value(step.minimumActiveRouteFps)} / ${percent(step.minimumActiveRouteFpsRatioObserved)}`,
    percent(step.averageDiscardRate),
    percentWhole(step.acceleratorPeakPercent),
    percentWhole(step.cpuPeakPercent),
    percentWhole(step.memoryPeakPercent),
    status(step.readiness?.status),
    status(step.result),
    value(step.failureReason),
  ]);
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '已验证 VLM 性能' : 'Validated VLM performance'}</h1>` +
    `<p class="lead">${zh ? '最后通过' : 'Last passing'}: ${observation.capacityBoundary?.verifiedPassingChannels ?? '—'} ${zh ? '路' : 'channels'}</p>` +
    notice(renderVlmScope(locale), 'scope-note') +
    table(zh ? ['路数', '时长', '目标 FPS/路', '当前新增路 FPS', '全路最低 FPS / 目标比例', '平均丢弃', '加速器', 'CPU', '内存', '新增路 readiness', '执行门禁', '失败原因'] : ['Channels', 'Hold', 'Target FPS/ch', 'Current new-route FPS', 'Minimum active-route FPS / target ratio', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'New-route readiness', 'Executed gate', 'Failure reason'], rows) +
    `<h2>${zh ? '最小端到端验收' : 'Minimum end-to-end acceptance'}</h2>` +
    table(
      zh ? ['模型加载', '任务创建', '有效推理', '事件/告警', '重启后任务恢复', '结果'] : ['Model load', 'Task creation', 'Valid inference', 'Event/alarm', 'Task recovery after restart', 'Result'],
      [[status(acceptance?.modelLoad), status(acceptance?.taskCreation), status(acceptance?.validInferenceResult), status(acceptance?.eventOrAlarmOutput), status(acceptance?.taskRecoveryAfterServiceRestart), status(acceptance?.status)]],
    ) +
    `<h2>${zh ? '冻结身份' : 'Frozen identities'}</h2>` +
    table(
      zh ? ['候选包 SHA-256', 'VLM 模型 SHA-256', 'Tokenizer SHA-256', '引擎 SHA-256', '端到端证据 SHA-256', '证据字节数'] : ['Candidate package SHA-256', 'VLM model SHA-256', 'Tokenizer SHA-256', 'Engine SHA-256', 'End-to-end evidence SHA-256', 'Evidence bytes'],
      [[observation.package?.sha256, observation.modelIdentity?.model?.sha256, observation.modelIdentity?.tokenizer?.sha256, observation.runtimeIdentity?.engineSha256, acceptance?.evidenceSha256, acceptance?.evidenceSizeBytes]],
    ) +
    `<p>${zh ? '测试源码' : 'Test source'}: <code>${escapeHtml(vlm.source?.commit ?? '—')}</code> · tree <code>${escapeHtml(vlm.source?.tree ?? '—')}</code> · ` +
    `${anchor('../../vlm-observations.json', zh ? 'canonical 数据' : 'canonical data')} · ` +
    `${anchor('../../../methodology.md', zh ? '方法说明' : 'methodology')}</p>`;
  return page(locale, `${platform.name} VLM`, workloadNav(locale), body);
}

function renderStepTable(locale, item) {
  const zh = locale === 'zh-CN';
  const mixed = item.workload === 'concurrent-mixed';
  const rows = item.steps.map((step) => {
    const base = [step.channels, `${step.holdSeconds} s`];
    if (mixed) {
      base.push(value(step.tasks.find((task) => task.name === 'person-detector')?.minimumProcessingFps));
      base.push(value(step.tasks.find((task) => task.name === 'no-safety-helmet-analysis')?.minimumProcessingFps));
    } else {
      base.push(value(step.minimumProcessingFps));
    }
    base.push(
      value(step.maximumCriticalPathLatencyMs),
      percent(step.averageDiscardRate),
      percentWhole(step.acceleratorPeakPercent),
      percentWhole(step.cpuPeakPercent),
      percentWhole(step.memoryPeakPercent),
      status(step.result),
      value(step.failureReason),
    );
    return base;
  });
  const headers = mixed
    ? (zh ? ['路数', '时长', '人员检测最低 FPS', '安全帽分析最低 FPS', '关键路径 ms', '平均丢弃', '加速器', 'CPU', '内存', '结果', '原因'] : ['Channels', 'Hold', 'Person min FPS', 'Helmet-analysis min FPS', 'Critical path ms', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'Result', 'Reason'])
    : (zh ? ['路数', '时长', '最低 FPS', '关键路径 ms', '平均丢弃', '加速器', 'CPU', '内存', '结果', '原因'] : ['Channels', 'Hold', 'Minimum FPS', 'Critical path ms', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'Result', 'Reason']);
  return table(headers, rows);
}

function page(locale, title, nav, body) {
  return `<!doctype html>
<html lang="${locale}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>${escapeHtml(title)}</title>
  <style>${styles()}</style>
</head>
<body><main>${nav}${body}</main></body>
</html>
`;
}

function rootNav(locale) {
  return `<nav class="report-nav" aria-label="Report navigation"><span>CosmoEdge 1.1</span>${anchor(locale === 'zh-CN' ? 'report.html' : 'report.zh-CN.html', locale === 'zh-CN' ? 'English' : '中文')}</nav>`;
}

function platformNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function longRunNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function platformLongRunNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`../../dual-cv-72h/report${zh ? '.zh-CN' : ''}.html`, zh ? '72 小时多平台报告' : '72-hour multi-platform report')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function caseIndexNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function workloadNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function individualCaseNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '用例索引' : 'Case index')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function table(headers, rows) {
  return `<div class="table" tabindex="0" role="region" aria-label="Scrollable data table"><table><thead><tr>${headers.map((item) => `<th>${escapeHtml(item)}</th>`).join('')}</tr></thead><tbody>${rows.map((row) => `<tr>${row.map((item) => `<td${statusAttribute(item)}>${renderCell(item)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
}

function statusAttribute(item) {
  return item === 'PASS' || item === '通过' ? ' data-status="PASS"' : item === 'STOP' || item === 'FAIL' || item === '停止' || item === '失败' ? ' data-status="FAIL"' : '';
}

function renderCell(item) {
  if (item && typeof item === 'object' && item.__html) return item.__html;
  return escapeHtml(value(item));
}

function link(href, text) {
  return { __html: anchor(href, text) };
}

function anchor(href, text) {
  return `<a href="${escapeHtml(href)}">${escapeHtml(text)}</a>`;
}

function notice(text, extraClass = '') {
  return `<p class="notice${extraClass ? ` ${extraClass}` : ''}">${escapeHtml(text)}</p>`;
}

function findCase(platform, workload, fps) {
  return selectPreferredCase(platform.cases.filter((item) => item.workload === workload && item.targetFps === fps));
}

function findWorkloadCase(platform, workload) {
  return selectPreferredCase(platform.cases.filter((item) => item.workload === workload));
}

function selectPreferredCase(cases) {
  return cases.reduce((selected, item) => (
      !selected || item.configuredChannels > selected.configuredChannels ? item : selected
  ), null);
}

function matrixEntry(value) {
  return {
    caseId: value.caseId,
    workload: value.workload,
    targetFps: value.targetFps,
    configuredChannels: value.configuredChannels,
    outcome: value.outcome,
    boundaryKind: value.boundaryKind,
    lastPassingChannels: value.lastPassingChannels,
    firstBlockedChannels: value.firstBlockedChannels,
    display: displayBoundary(value),
    blockedReason: value.blockedReason,
  };
}

function displayBoundary(item) {
  if (!item) return '—';
  if (item.boundaryKind === 'lower-bound') return `≥${item.lastPassingChannels}`;
  if (item.boundaryKind === 'binding-blocked') return `≥${item.lastPassingChannels}*`;
  if (item.boundaryKind === 'storage-blocked') return `≥${item.lastPassingChannels}†`;
  return String(item.lastPassingChannels);
}

function boundaryLegend(locale) {
  return locale === 'zh-CN'
    ? '数字是最后通过路数；≥ 表示设定上限仍通过，* 表示下一路绑定阻断，† 表示扩容在测量前被存储前置条件阻断。'
    : 'Values are last-passing channels; ≥ means the configured maximum still passed, * means the next binding was blocked, and † means expansion was blocked before measurement by storage preconditions.';
}

function platformLabel(platform, locale) {
  return platform.scope === 'release-platform' ? platform.name : `${platform.name} (${locale === 'zh-CN' ? '实验' : 'experimental'})`;
}

function workloadLabel(workload, locale) {
  const zh = locale === 'zh-CN';
  if (workload === 'person-detector') return zh ? '人员检测' : 'Person detector';
  if (workload === 'no-safety-helmet-analysis') return zh ? '未佩戴安全帽分析' : 'No-safety-helmet analysis';
  if (workload === 'concurrent-mixed') return zh ? '并发混合任务' : 'Concurrent mixed workload';
  return workload;
}

function capacityBoundaryReason(observation, locale) {
  const zh = locale === 'zh-CN';
  const boundary = observation?.capacityBoundary;
  const step = observation?.steps?.find((item) => item.channels === boundary?.firstFailureChannels);
  if (boundary?.firstFailureCategory === 'fps-ratio') return zh
    ? `${step?.channels ?? '下一'} 路全路最低 FPS 达标率 ${percent(step?.minimumActiveRouteFpsRatioObserved)}，低于 80% 门禁`
    : `${step?.channels ?? 'Next'} channels: minimum active-route FPS ratio ${percent(step?.minimumActiveRouteFpsRatioObserved)}, below the 80% gate`;
  return value(step?.failureReason ?? boundary?.firstFailureCategory);
}

function caseLabel(item, locale) {
  return `${workloadLabel(item.workload, locale)} ${item.targetFps} FPS × ${item.configuredChannels}`;
}

function boundaryKindLabel(kind, locale) {
  const zh = locale === 'zh-CN';
  const labels = {
    'lower-bound': zh ? '下界' : 'lower bound',
    'binding-blocked': zh ? '绑定阻断' : 'binding blocked',
    'storage-blocked': zh ? '存储前置阻断' : 'storage blocked',
    'performance-stop': zh ? '性能停止' : 'performance stop',
  };
  return labels[kind] ?? kind;
}

function caseNotice(item, locale) {
  const zh = locale === 'zh-CN';
  if (item.boundaryKind === 'lower-bound') return zh ? `设定的 ${item.configuredChannels} 路全部通过；这是实测下界，不是极限或推荐值。` : `All ${item.configuredChannels} configured channels passed; this is an observed lower bound, not a maximum or recommendation.`;
  if (item.boundaryKind === 'binding-blocked') return zh ? `${item.lastPassingChannels} 路完成测量；第 ${item.firstBlockedChannels} 路在绑定时被阻断。` : `${item.lastPassingChannels} channels completed measurement; channel ${item.firstBlockedChannels} was blocked during binding.`;
  if (item.boundaryKind === 'storage-blocked') return zh ? `${item.lastPassingChannels} 路完成测量；后续扩容在测量前被存储前置条件阻断。` : `${item.lastPassingChannels} channels completed measurement; further expansion was blocked before measurement by storage preconditions.`;
  return zh ? `最后通过 ${item.lastPassingChannels} 路；${item.blockedReason ?? '后续步骤触发性能停止'}。` : `Last passing point: ${item.lastPassingChannels} channels; ${item.blockedReason ?? 'the following step triggered a performance stop'}.`;
}

function value(input) {
  return input === null || input === undefined || input === '' ? '—' : String(input);
}

function percent(input) {
  return input === null || input === undefined ? '—' : `${Number((input * 100).toFixed(2))}%`;
}

function percentWhole(input) {
  return input === null || input === undefined ? '—' : `${input}%`;
}

function status(input) {
  if (input === null || input === undefined) return '—';
  return String(input);
}

function escapeHtml(input) {
  return value(input).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
}

function styles() {
  return ':root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#f7f9fc;color:#172033;font:15px/1.65,Inter,"Segoe UI",Arial,sans-serif}main{max-width:1120px;margin:auto;padding:42px 24px 70px;min-width:0}h1{font-size:32px;line-height:1.25;margin:0 0 8px;letter-spacing:-.02em}h2{margin-top:38px;line-height:1.35}h3{margin-top:28px}.lead{color:#526071}.notice{background:#eef4ff;border-left:4px solid #2563eb;padding:14px 16px;border-radius:0 8px 8px 0}.experimental{background:#fff7e8;border-left-color:#d97706}.evidence-notes{margin:28px 0;padding:12px 16px;border:1px solid #dce3ed;border-radius:8px;background:#fff}.evidence-notes summary{cursor:pointer;font-weight:700}.report-nav{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:0 0 22px}.report-nav a,.report-nav span{display:inline-flex;align-items:center;min-height:34px;padding:5px 11px;border:1px solid #cbd5e1;border-radius:999px;background:#fff;color:#1d4ed8;text-decoration:none}.report-nav span{color:#475569;background:#f8fafc}.table{max-width:100%;overflow-x:auto;overscroll-behavior-inline:contain;-webkit-overflow-scrolling:touch;border:1px solid #dce3ed;border-radius:8px;margin:12px 0 24px;background:#fff}.table:focus{outline:2px solid #93c5fd;outline-offset:2px}table{border-collapse:collapse;width:100%;background:#fff}th,td{padding:10px 12px;border-bottom:1px solid #dce3ed;text-align:left;vertical-align:top}th{background:#f1f5f9;white-space:nowrap}tr:last-child td{border-bottom:0}td[data-status="PASS"]{color:#047857;font-weight:700}td[data-status="FAIL"]{color:#b91c1c;font-weight:700}img{display:block;max-width:100%;height:auto;margin:24px auto;background:#fff;border:1px solid #e2e8f0;border-radius:10px}code{background:#eef2f7;padding:2px 5px;border-radius:4px;overflow-wrap:anywhere;word-break:break-word}a{color:#1d4ed8}@media(max-width:600px){main{padding:26px 15px 52px}h1{font-size:27px}h2{font-size:22px;margin-top:32px}.table{margin-right:0}.table table{width:max-content;min-width:100%;max-width:none}th,td{padding:9px 11px;min-width:76px}img{margin:18px auto}.report-nav{gap:7px}.report-nav a,.report-nav span{font-size:13px;min-height:32px;padding:4px 9px}}';
}

function copyCanonicalAssets(sourceRoot, outputRoot, platformIds) {
  const canonicalCaseFiles = new Set(platformIds.map((id) => `results/${id}/cases.json`));
  for (const file of walk(sourceRoot)) {
    const relative = path.relative(sourceRoot, file).replaceAll('\\', '/');
    if (!canonicalStaticAsset(relative, canonicalCaseFiles)) continue;
    const target = path.join(outputRoot, ...relative.split('/'));
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(file, target);
  }
}

function canonicalStaticAsset(relative, canonicalCaseFiles) {
  if (relative === 'SHA256SUMS' || /^report(?:\.zh-CN)?\.html$/.test(relative)) return false;
  if (!relative.startsWith('results/')) return true;
  return canonicalCaseFiles.has(relative)
    || relative === 'results/cases.schema.json'
    || relative === 'results/vlm-observations.json'
    || relative === 'results/dual-cv-72h.json';
}

function removeGeneratedPages(outputRoot, platformIds) {
  for (const relative of [
    'report.html',
    'report.zh-CN.html',
    'results/cases.json',
    'results/index.json',
    'results/workload-matrix.json',
    'results/dual-cv-72h',
  ]) {
    removePath(path.join(outputRoot, ...relative.split('/')));
  }
  for (const platform of platformIds) {
    for (const relative of [
      `results/${platform}/report.html`,
      `results/${platform}/report.zh-CN.html`,
      `results/${platform}/command.txt`,
      `results/${platform}/environment.json`,
      `results/${platform}/metrics.json`,
      `results/${platform}/summary.json`,
      `results/${platform}/test.log`,
      `results/${platform}/cases`,
      `results/${platform}/single-detector`,
      `results/${platform}/dual-detector`,
      `results/${platform}/single-workload`,
      `results/${platform}/concurrent-mixed`,
      `results/${platform}/dual-cv-72h`,
      `results/${platform}/vlm-observation`,
    ]) removePath(path.join(outputRoot, ...relative.split('/')));
  }
  removePath(path.join(outputRoot, 'SHA256SUMS'));
}

function removePath(target) {
  if (!fs.existsSync(target)) return;
  fs.rmSync(target, { recursive: true, force: false });
}

function writeReport(outputRoot, relative, html) {
  const file = path.join(outputRoot, ...relative.split('/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, html, 'utf8');
}

export function writeChecksums(outputRoot) {
  const checksumPath = path.join(outputRoot, 'SHA256SUMS');
  const lines = walk(outputRoot)
    .filter((file) => path.resolve(file) !== path.resolve(checksumPath))
    .map((file) => {
      const relative = path.relative(outputRoot, file).replaceAll('\\', '/');
      const digest = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
      return `${digest}  ${relative}`;
    })
    .sort();
  fs.writeFileSync(checksumPath, `${lines.join('\n')}\n`, 'utf8');
}

function writeJson(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function walk(directory) {
  if (!fs.existsSync(directory)) return [];
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--source') options.sourceRoot = requireValue(args, ++index, argument);
    else if (argument === '--output') options.outputRoot = requireValue(args, ++index, argument);
    else if (argument === '--write-source-checksums') options.writeSourceChecksums = true;
    else throw new Error(`unknown argument: ${argument}`);
  }
  return options;
}

function requireValue(args, index, option) {
  if (!args[index]) throw new Error(`${option} requires a value`);
  return args[index];
}
