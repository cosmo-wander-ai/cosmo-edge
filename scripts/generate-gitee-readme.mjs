#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sourcePath = path.join(repositoryRoot, 'README.zh-CN.md');
const targetPath = path.join(repositoryRoot, 'Readme.osc.md');
const checkMode = process.argv.includes('--check');
const generatedHeader = '<!-- Generated from README.zh-CN.md by scripts/generate-gitee-readme.mjs. Do not edit directly. -->\n\n';
const githubReleaseBadge = '[![Release](https://img.shields.io/badge/release-v1.2.0-2ea44f?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.2.0)';
const giteeReleaseBadge = '[![Release](https://img.shields.io/badge/release-v1.2.0-2ea44f?style=flat-square)](https://gitee.com/cosmo-wander-ai/cosmo-edge/releases)';
const githubCloneCommand = 'git clone https://github.com/cosmo-wander-ai/cosmo-edge.git';
const giteeCloneCommand = 'git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git';

const replacements = [
  {
    source: '<https://github.com/user-attachments/assets/96eeba7e-5b00-4c54-97b3-3ee4571cd5a0>',
    target: [
      '[![CosmoEdge 产品概览视频封面](https://www.cosmowander.ai/images/landing/zh3.webp)](https://www.cosmowander.ai/zh/demos/#overview)',
      '',
      '[▶ 在官网播放产品概览](https://www.cosmowander.ai/zh/demos/#overview)'
    ].join('\n')
  },
  {
    source: '<https://github.com/user-attachments/assets/c9673081-ad73-4455-9486-1a3021358cdd>',
    target: [
      '[![CosmoEdge 可视化管线视频封面](https://www.cosmowander.ai/images/landing/zh1.webp)](https://www.cosmowander.ai/zh/demos/#pipeline)',
      '',
      '[▶ 在官网播放可视化管线演示](https://www.cosmowander.ai/zh/demos/#pipeline)'
    ].join('\n')
  },
  {
    source: '<https://github.com/user-attachments/assets/f47b541e-0d01-437d-86e1-4183f6e610fd>',
    target: [
      '[![CosmoEdge DINO 与 VLM 视频封面](https://www.cosmowander.ai/images/landing/zh2.webp)](https://www.cosmowander.ai/zh/demos/#vlm-dino)',
      '',
      '[▶ 在官网播放 DINO 与 VLM 演示](https://www.cosmowander.ai/zh/demos/#vlm-dino)'
    ].join('\n')
  },
  {
    source: githubReleaseBadge,
    target: giteeReleaseBadge
  },
  {
    source: githubCloneCommand,
    target: giteeCloneCommand,
    expectedOccurrences: 2
  }
];

const source = fs.readFileSync(sourcePath, 'utf8');
let body = source;

for (const replacement of replacements) {
  const occurrences = countOccurrences(body, replacement.source);
  const expectedOccurrences = replacement.expectedOccurrences ?? 1;
  if (occurrences !== expectedOccurrences) {
    fail('Expected ' + expectedOccurrences + ' source pattern occurrence(s), found ' + occurrences + ': ' + replacement.source);
  }
  body = body.split(replacement.source).join(replacement.target);
}

const generated = generatedHeader + body;
if (generated.includes('github.com/user-attachments')) {
  fail('Generated Gitee README still contains GitHub attachment media');
}

if (checkMode) {
  if (!fs.existsSync(targetPath)) fail('Readme.osc.md is missing; run npm run gitee:readme:generate');
  const actual = fs.readFileSync(targetPath, 'utf8');
  if (actual !== generated) fail('Readme.osc.md is stale; run npm run gitee:readme:generate');
  console.log('Gitee README check passed: generated copy is current, clone commands and the release badge route to Gitee, and all 3 videos route to official playback pages.');
} else {
  fs.writeFileSync(targetPath, generated);
  console.log('Generated Readme.osc.md from README.zh-CN.md with Gitee clone and release entry points plus 3 official video-page replacements.');
}

function countOccurrences(content, value) {
  return content.split(value).length - 1;
}

function fail(message) {
  console.error('Gitee README generation failed: ' + message);
  process.exit(1);
}
