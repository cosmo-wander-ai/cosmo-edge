// Presentation metadata only: model identifiers, stored names, and labels stay intact.
// Match both the preset source and its original name so imported/customized models
// never inherit a built-in alias merely because they reuse an identifier.
const entries = [
  ['1000001', 'FaceRGBDetection', '人脸检测', '定位图像中的人脸区域。', 'Locates face regions in images.'],
  ['1000005', 'FaceRGBRecognition', '人脸特征', '提取人脸特征，供后续比对使用。', 'Extracts face features for subsequent comparison.'],
  ['1000010', 'FaceMask', '口罩分类', '对人脸图像中的口罩佩戴状态进行分类。', 'Classifies mask-wearing state in face images.'],
  ['1000012', 'FaceQuality', '人脸质量', '对人脸图像的朝向、完整程度和模糊情况进行分类。', 'Classifies face orientation, completeness, and image blur.'],
  ['1000016', 'FaceLandmark', '人脸关键点', '定位人脸图像中的关键点。', 'Locates landmarks in face images.'],
  ['1001001', 'HsDetection', '头肩检测', '定位图像中的人体头肩区域。', 'Locates human head-and-shoulder regions in images.'],
  ['1001003', 'PedestrianDetection', '人员检测', '定位图像中的人员区域。', 'Locates people in images.'],
  ['1001005', 'PedestrianAttribute', '人员属性', '对人员衣着、携带物和帽类等外观属性进行分类。', 'Classifies clothing, carried items, headwear, and other visible attributes.'],
  ['1001007', 'PedestrianRecognition', '人员特征', '提取人员外观特征，供后续比对使用。', 'Extracts appearance features of people for subsequent comparison.'],
  ['1001008', 'PedestrianQuality', '人员图像质量', '对人员图像质量进行分类，类别含义以模型配置为准。', 'Classifies image quality for people; category meanings depend on model configuration.'],
  ['1001010', 'PedSleep', '趴卧分类', '对人员图像中的趴卧姿态进行分类。', 'Classifies prone and lying postures in images of people.'],
  ['2000001', 'Vehicle', '人车检测', '定位图像中的人员、机动车和非机动车。', 'Locates people, motor vehicles, and non-motor vehicles in images.'],
  ['2000011', 'VehicleAttribute', '车辆属性', '对车辆类型、颜色和朝向进行分类。', 'Classifies vehicle type, color, and orientation.'],
  ['2000020', 'ElectricElevatorClassify', '两轮车分类', '对自行车和电动车图像进行分类，包含不确定类别。', 'Classifies bicycle and electric-vehicle images, including an uncertain category.'],
  ['2001002', 'HsHelemet', '头肩帽类', '根据头肩图像区分安全帽、头盔、普通帽和未戴帽状态。', 'Classifies safety helmets, helmets, ordinary hats, and no-headwear states from head-and-shoulder images.'],
  ['2001003', 'FireDetection', '火焰烟雾', '定位图像中的火焰和烟雾区域。', 'Locates fire and smoke regions in images.'],
  ['2001008', 'RubbishDumpDetection', '垃圾堆检测', '定位图像中的垃圾堆区域。', 'Locates rubbish piles in images.'],
  ['2001036', 'PedHelemet', '人员帽类', '根据人员图像区分头盔、安全帽、普通帽和未戴帽状态。', 'Classifies helmets, safety helmets, ordinary hats, and no-headwear states from images of people.'],
  ['2002008', 'PedFalldown', '倒地分类', '对人员图像中的疑似倒地姿态进行分类。', 'Classifies possible fallen postures in images of people.'],
  ['2002011', 'PhoneSmoking', '吸烟与手机', '对人员图像中的吸烟、接打电话和玩手机状态进行分类。', 'Classifies smoking, phone-call, and phone-use states in images of people.'],
  ['2180145', 'Yolov8Detect', '目标检测', '定位图像中的目标，具体类别以模型标签配置为准。', 'Locates objects in images; supported categories depend on the model labels.'],
  ['2248766', 'Yolov12', '目标检测', '定位图像中的目标，具体类别以模型标签配置为准。', 'Locates objects in images; supported categories depend on the model labels.'],
  ['3000003', 'MouseDetection', '鼠类检测', '定位图像中的鼠类区域。', 'Locates rodent regions in images.'],
  ['3612265', 'CosmoEdge-VL-Judge-0.8B', '视觉语言', '视觉语言类模型，具体用途以模型配置为准。', 'A vision-language model; its specific purpose depends on model configuration.'],
  ['6047042', 'YOLOV8n', '人员检测', '用于人员检测的内置示例模型。', 'A built-in example model for detecting people.'],
  ['7486163', 'helmet', '安全帽分类', '对安全帽佩戴状态进行分类。', 'Classifies safety-helmet wearing state.'],
  ['7792861', 'Yolo26', '目标检测', '定位图像中的目标，具体类别以模型标签配置为准。', 'Locates objects in images; supported categories depend on the model labels.'],
  ['9183416', 'GrounddingDIno', '文本提示检测', '根据文本提示定位图像中的目标。', 'Locates objects in images using text prompts.'],
  ['9233591', 'SAM2', '提示分割', '根据提示分割图像中的目标区域。', 'Segments target regions in images using prompts.'],
  ['9909281', 'VehicleClasses', '人员两轮车', '定位图像中的人员、自行车和电动自行车。', 'Locates people, bicycles, and electric bicycles in images.']
]

export const PRESET_MODEL_DISPLAY_CATALOG = Object.freeze(Object.fromEntries(
  entries.map(([code, originalName, title, description, descriptionEn]) => [
    code,
    Object.freeze({ originalName, title, description, descriptionEn })
  ])
))

/**
 * Resolve display text without changing the model DTO or its persisted name.
 * Both repository DTOs (modelCode/modelName) and selector DTOs
 * (atomicCode/atomicName) are accepted. Source must be explicitly preset.
 */
export const resolvePresetModelDisplay = (model, locale = 'zh-CN') => {
  const name = model?.modelName ?? model?.atomicName
  const rawName = typeof name === 'string' ? name : ''
  const rawCode = model?.modelCode ?? model?.atomicCode
  const code = rawCode == null ? '' : String(rawCode).trim()
  const entry = Object.prototype.hasOwnProperty.call(PRESET_MODEL_DISPLAY_CATALOG, code)
    ? PRESET_MODEL_DISPLAY_CATALOG[code]
    : undefined
  const isPreset = model?.isExportable === false && entry?.originalName === rawName.trim()

  if (!isPreset) {
    return { title: rawName, subtitle: '', description: '', searchText: [rawName, code].filter(Boolean).join(' ') }
  }

  const isChinese = locale === 'zh-CN'
  return {
    title: isChinese ? entry.title : rawName,
    subtitle: isChinese ? rawName : '',
    description: isChinese ? entry.description : entry.descriptionEn,
    // Keep both names searchable when the UI locale changes.
    searchText: [entry.title, rawName, code, entry.description, entry.descriptionEn].join(' ')
  }
}
