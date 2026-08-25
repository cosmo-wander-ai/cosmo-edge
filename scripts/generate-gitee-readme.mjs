#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sourcePath = path.join(repositoryRoot, 'README.zh-CN.md');
const targetPath = path.join(repositoryRoot, 'Readme.osc.md');
const checkMode = process.argv.includes('--check');
const generatedHeader = '<!-- Generated from README.zh-CN.md by scripts/generate-gitee-readme.mjs. Do not edit directly. -->\n\n';
const githubReleaseBadge = '[![Release](https://img.shields.io/badge/release-v1.1.0-2ea44f?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0)';
const giteeReleaseBadge = '[![Release](https://img.shields.io/badge/release-v1.1.0-2ea44f?style=flat-square)](https://gitee.com/cosmo-wander-ai/cosmo-edge/releases)';

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
  }
];

const source = fs.readFileSync(sourcePath, 'utf8');
let body = source;

for (const replacement of replacements) {
  const occurrences = countOccurrences(body, replacement.source);
  if (occurrences !== 1) {
    fail('Expected exactly one source pattern, found ' + occurrences + ': ' + replacement.source);
  }
  body = body.replace(replacement.source, replacement.target);
}

const generated = generatedHeader + body;
validateGenerated(generated);

if (checkMode) {
  if (!fs.existsSync(targetPath)) fail('Readme.osc.md is missing; run npm run gitee:readme:generate');
  const actual = fs.readFileSync(targetPath, 'utf8');
  if (actual !== generated) fail('Readme.osc.md is stale; run npm run gitee:readme:generate');
  console.log('Gitee README check passed: generated copy is current, all 3 videos route to official playback pages, and the release badge routes to Gitee.');
} else {
  fs.writeFileSync(targetPath, generated);
  console.log('Generated Readme.osc.md from README.zh-CN.md with 3 official video-page replacements and the Gitee release link.');
}

function validateGenerated(content) {
  if (content.includes('github.com/user-attachments')) {
    fail('Generated Gitee README still contains GitHub attachment media');
  }
  if (content.includes(githubReleaseBadge)) {
    fail('Generated Gitee README still routes the release badge to GitHub');
  }
  if (countOccurrences(content, giteeReleaseBadge) !== 1) {
    fail('Expected exactly one Gitee release badge');
  }
  for (const anchor of ['#overview', '#pipeline', '#vlm-dino']) {
    const expected = 'https://www.cosmowander.ai/zh/demos/' + anchor;
    if (countOccurrences(content, expected) !== 2) {
      fail('Expected one poster and one text link for ' + expected);
    }
  }
  for (const poster of ['zh3.webp', 'zh1.webp', 'zh2.webp']) {
    if (!content.includes('https://www.cosmowander.ai/images/landing/' + poster)) {
      fail('Missing official poster image ' + poster);
    }
  }
}

function countOccurrences(content, value) {
  return content.split(value).length - 1;
}

function fail(message) {
  console.error('Gitee README generation failed: ' + message);
  process.exit(1);
}
