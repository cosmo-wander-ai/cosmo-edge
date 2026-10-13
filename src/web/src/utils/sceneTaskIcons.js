// Semantic identities from the platform algorithm_template resources. Codes and
// explicit resource keys survive localization and user-renamed preset tasks.
const PRESET_ICONS = Object.freeze({
  2: 'face', 3: 'intrusion', 5: 'crowd', 6: 'fall', 7: 'parking',
  9: 'smoke', 10: 'fire', 11: 'smoking', 12: 'phone-call', 13: 'people-flow',
  15: 'helmet', 16: 'off-post', 22: 'phone-use', 29: 'plate', 32: 'vest',
  51: 'region-count', 57: 'tripwire', 58: 'uniform', 65: 'sleep',
  2935: 'foundation-detection', 8533: 'foundation-detection',
  15760: 'foundation-segmentation', 21283: 'foundation-segmentation',
  34707: 'vision-language', 89336: 'vision-language', 7602: 'face',
  38873: 'detection', 38874: 'oriented-detection',
  77380: 'detect-classify', 82819: 'detect-segment'
})

const normalizeName = (value) => typeof value === 'string'
  ? value.normalize('NFKC').trim().toLowerCase().replace(/\s+/g, ' ')
  : ''

// Exact aliases only: arbitrary names containing "phone", "face", "检测", etc.
// do not establish what a custom pipeline does.
const NAME_ICONS = new Map([
  ['face', ['人脸识别', '人脸比对', '人脸识别算法', 'Face Recognition', 'Face Comparison', 'Face Recognition Algorithm']],
  ['intrusion', ['区域入侵', 'Regional Intrusion', 'Region Intrusion']],
  ['crowd', ['人员聚集', 'Crowd Gathering', 'People Gathering']],
  ['fall', ['人员跌倒', 'Person Falling', 'Person Fall', 'Fall Detection']],
  ['parking', ['车辆违停', 'Illegal Vehicle Parking', 'Illegal Parking']],
  ['vehicle-detection', ['图片车辆', '车辆检测', 'Image Vehicle', 'Image Vehicle Detection', 'Vehicle Detection']],
  ['head-shoulder', ['头肩', '头肩检测', 'Head and Shoulder', 'Head and Shoulder Detection', 'Head-Shoulder Detection']],
  ['person-detection', ['人体侦测', '人体检测', 'Person Detection', 'Human Detection']],
  ['smoke', ['烟雾检测', 'Smoke Detection']],
  ['fire', ['火焰检测', 'Fire Detection', 'Flame Detection']],
  ['smoking', ['吸烟检测', 'Smoking Detection']],
  ['phone-call', ['打电话检测', 'Phone Call Detection']],
  ['people-flow', ['人流量统计', 'Passenger Flow Statistics', 'People Flow Counting']],
  ['helmet', ['未戴安全帽', 'No Safety Helmet']],
  ['off-post', ['离岗检测', 'Off-post Detection']],
  ['phone-use', ['玩手机检测', 'Phone Usage Detection']],
  ['plate', ['车牌识别', 'License Plate Recognition']],
  ['vest', ['未穿反光衣', 'No Reflective Vest']],
  ['region-count', ['区域人数统计', 'Area People Counting', 'Regional People Counting']],
  ['tripwire', ['绊线检测', 'Tripwire Detection']],
  ['uniform', ['未穿工服', 'No Work Uniform', 'No Uniform']],
  ['sleep', ['睡岗检测', 'Sleeping on Duty', 'Sleeping on Duty Detection']],
  ['foundation-detection', ['检测大模型', '视觉检测大模型', '视觉检测大模型分析', '图片检测大模型', 'Detection Foundation Model', 'Vision Detection Foundation Model', 'Vision-Detection Foundation Model', 'Vision Detection Foundation Model Analysis', 'Vision-Detection Foundation Model Analysis', 'Image-Detection Foundation Model']],
  ['foundation-segmentation', ['分割大模型', '视觉分割大模型', '视觉分割大模型分析', '图片分割大模型', 'Segmentation Foundation Model', 'Vision Segmentation Foundation Model', 'Vision-Segmentation Foundation Model', 'Vision Segmentation Foundation Model Analysis', 'Vision-Segmentation Foundation Model Analysis', 'Image-Segmentation Foundation Model']],
  ['vision-language', ['视觉语言大模型', '视觉语言大模型分析', '图片语言视觉大模型', 'Vision-Language Model', 'Vision-Language Model Analysis', 'Vision-Language Foundation Model', 'Vision-Language Foundation Model Analysis', 'Image-Language-Vision Foundation Model']],
  ['detection', ['检测类算法', '图片检测任务', 'Detection-class Algorithm', 'Detection Algorithm', 'Image Detection Task']],
  ['oriented-detection', ['旋转框检测算法', 'Oriented Box Detection', 'Oriented Object Detection Algorithm']],
  ['detect-classify', ['检测后分类算法', '图片检测分类任务', 'Post-detection Classification Algorithm', 'Detection-then-Classification Algorithm', 'Image Detection & Classification Task']],
  ['detect-segment', ['检测后分割算法', 'Post-detection Segmentation Algorithm', 'Detection-then-Segmentation Algorithm']]
].flatMap(([icon, names]) => names.map((name) => [normalizeName(name), icon])))

const presetIcon = (value) => {
  const code = String(value ?? '').trim()
  return /^\d+$/.test(code) ? PRESET_ICONS[Number(code)] : undefined
}

export function sceneTaskIcon(task) {
  if (!task || typeof task !== 'object') return 'workflow'
  const resourceCode = /^resource\.algorithm\.(\d+)\.algorithmname$/i.exec(task.algorithmNameI18nKey || '')?.[1]
  const preset = presetIcon(resourceCode) || presetIcon(task.algorithmCode) || presetIcon(task.algorithmId)
  if (preset) return preset
  return NAME_ICONS.get(normalizeName(task.algorithmName)) || NAME_ICONS.get(normalizeName(task.name)) || 'workflow'
}

// Hand-drawn on a shared 24px grid. Each path uses the same currentColor stroke;
// the task name supplies the accessible label beside the decorative SVG.
export const SCENE_TASK_ICON_PATHS = Object.freeze({
  face: [
    'M7 3H3v4M17 3h4v4M21 17v4h-4M7 21H3v-4',
    'M7 9a5 5 0 0 1 10 0v4a5 5 0 0 1-10 0ZM9 10h.01M15 10h.01M10 15q2 1.5 4 0'
  ],
  intrusion: [
    'M8 3H3v18h18v-6M16 3h5v5',
    'M14 6a2 2 0 1 0 4 0 2 2 0 1 0-4 0M9 11l4-2 3 3h4M13 10l-2 5 4 2v4M11 15l-4 5'
  ],
  crowd: [
    'M9.5 6a2.5 2.5 0 1 0 5 0 2.5 2.5 0 1 0-5 0M7 19v-3a5 5 0 0 1 10 0v3',
    'M3 8a2 2 0 1 0 4 0 2 2 0 1 0-4 0M2 17v-3a3 3 0 0 1 4-2.8M17 8a2 2 0 1 0 4 0 2 2 0 1 0-4 0M22 17v-3a3 3 0 0 0-4-2.8'
  ],
  fall: [
    'M3 19h18M4 9a2 2 0 1 0 4 0 2 2 0 1 0-4 0M9 12l5 3 5-2 2 3M9 12l-3 3H3M14 15l2 3',
    'M14 3v5m-2-2 2 2 2-2'
  ],
  parking: [
    'M4 12l1.5-5H14l2 5M3 12h14v7H3ZM5 19v2M15 19v2M3 15h3M14 15h3',
    'M16 5a3 3 0 1 0 6 0 3 3 0 1 0-6 0M17 7l4-4'
  ],
  'vehicle-detection': [
    'M5 10l2-6h10l2 6M3 11l2-1h14l2 1v8H3ZM5 19v2M19 19v2M3 14h4M17 14h4M9 17h6'
  ],
  'head-shoulder': [
    'M7 3H3v4M17 3h4v4M3 17v4h4M21 17v4h-4',
    'M9 8a3 3 0 1 0 6 0 3 3 0 1 0-6 0M6 19v-2a6 5 0 0 1 12 0v2'
  ],
  'person-detection': [
    'M6 3H3v5M18 3h3v5M3 16v5h3M21 16v5h-3',
    'M10 5a2 2 0 1 0 4 0 2 2 0 1 0-4 0M8 14v-3a4 2 0 0 1 8 0v3M10 11v9M14 11v9M10 15h4'
  ],
  smoke: [
    'M5 21h14M7 17c-4-3 5-4 1-8M12 17c-4-4 5-6 1-12M17 17c-4-3 5-5 1-10'
  ],
  fire: [
    'M12 2c1 5-5 6-5 10-2-1-2-3-2-3-5 8 0 13 7 13 7 0 12-8 5-14 0 3-2 4-2 4 0-5-1-8-3-10Z',
    'M12 14c0 3-3 3-3 5a3 3 0 0 0 6 0c0-2-2-3-3-5Z'
  ],
  smoking: [
    'M3 15h15v4H3ZM14 15v4M21 15v4',
    'M16 11c3-3-3-4 0-7M20 11c3-3-3-4 0-7'
  ],
  'phone-call': [
    'M5 3 3 5c-1 6 9 16 15 16l3-3-5-4-3 3c-3-1-5-3-6-6l3-3Z',
    'M15 3a7 7 0 0 1 6 6M15 7a3 3 0 0 1 2 2'
  ],
  'people-flow': [
    'M9.5 6a2.5 2.5 0 1 0 5 0 2.5 2.5 0 1 0-5 0M8 17v-4a4 4 0 0 1 8 0v4',
    'M2 19h7m-3-3 3 3-3 3M22 19h-7m3-3-3 3 3 3'
  ],
  helmet: [
    'M4 16v-3a8 8 0 0 1 16 0v3M10 5V3h4v2M10 5v6M14 5v6M3 16h18v4H3Z'
  ],
  'off-post': [
    'M3 21V3h10v5M3 21h10v-5M7 7h2',
    'M12 12h10m-4-4 4 4-4 4'
  ],
  'phone-use': [
    'M10 3h9v17h-9ZM13 6h3M14.5 17h.01',
    'M10 14 7 11a1.5 1.5 0 0 0-2 2l2 3v5M3 9l2-2'
  ],
  plate: [
    'M3 7h18v11H3ZM6 10v5M10 10h3v5h-3ZM17 10h1M17 13h1M17 15h1M5 4h14M5 21h14'
  ],
  vest: [
    'M8 3h2l2 5 2-5h2l1 6 4 3-2 9H5l-2-9 4-3ZM12 8v13',
    'M7 9 9 21M17 9l-2 12M5 15h14'
  ],
  'region-count': [
    'M7 3H3v5M17 3h4v5M21 16v5h-4M7 21H3v-5',
    'M6 8a2 2 0 1 0 4 0 2 2 0 1 0-4 0M5 17v-3a3 3 0 0 1 6 0v3M15 8v9M19 8v9M13 11h8M13 14h8'
  ],
  tripwire: [
    'M3 4v3M3 10v4M3 17v4',
    'M12 4a2 2 0 1 0 4 0 2 2 0 1 0-4 0M8 11l4-3 3 4h5M12 8l-2 7 4 2v4M10 15l-4 6M1 12h5'
  ],
  uniform: [
    'M8 3 3 6 1 11l4 2 2-3v11h10V10l2 3 4-2-2-5-5-3M8 3l4 4 4-4M12 7v14M14 11h2v3h-2'
  ],
  sleep: [
    'M3 18h18M4 18v3M20 18v3M5 13a2 2 0 1 0 4 0 2 2 0 1 0-4 0M10 16l2-4h5l3 4',
    'M13 4h5l-5 5h5'
  ],
  'foundation-detection': [
    'M8 4H3v5M21 14v7h-7M3 15v6h5M7 9h9v8H7Z',
    'm18 2 1.3 3.7L23 7l-3.7 1.3L18 12l-1.3-3.7L13 7l3.7-1.3Z'
  ],
  'foundation-segmentation': [
    'M10 3H3v18h18v-8M7 9l5-2 5 5-3 6-7-1-2-5ZM7 13l5-5M9 17l6-7',
    'm18 1 1 3 3 1-3 1-1 3-1-3-3-1 3-1Z'
  ],
  'vision-language': [
    'M3 3h13v11H3ZM3 12l4-4 4 4 3-3M6 6h.01',
    'M16 8h5v11h-7l-4 3v-3H7v-2M17 12h1M13 16h5'
  ],
  detection: [
    'M8 3H3v5M16 3h5v5M21 16v5h-5M8 21H3v-5M7 7h6v6H7ZM13 13h4v4h-4'
  ],
  'oriented-detection': [
    'M7 3H3v4M17 3h4v4M21 17v4h-4M7 21H3v-4M6 11l7-5 5 7-7 5Z'
  ],
  'detect-classify': [
    'M3 4h8v8H3ZM7 8h.01M7 12v5h5M15 4h6v6h-6ZM15 15h6v6h-6M18 10v5M12 17h3'
  ],
  'detect-segment': [
    'M3 3h10v10H3ZM6 6h4v4H6M13 8h8v13H8v-8',
    'm14 12 4 2-1 4-4 1-2-3ZM13 16l3-3'
  ],
  workflow: [
    'M9 3h6v5H9ZM3 16h6v5H3ZM15 16h6v5h-6M12 8v4M6 16v-4h12v4'
  ]
})
