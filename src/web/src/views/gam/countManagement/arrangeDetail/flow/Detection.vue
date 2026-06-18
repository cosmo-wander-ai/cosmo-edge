<template>
  <div class="detection-container">
    <div class="detection-card">
      <div class="card-header">
        <h3 class="card-title">{{ t('glossary.detectionConfig') }}</h3>
      </div>

      <div class="form-section">
        <div class="form-row">
          <label class="form-label">{{ t('glossary.areaType') }}</label>
          <el-select v-model="areaType" :placeholder="t('field.selectAreaType')" size="default" class="form-select">
            <el-option v-for="item in options" :key="item.value" :label="item.label" :value="item.value"></el-option>
          </el-select>
        </div>

        <div class="form-row">
          <label class="form-label">{{ t('glossary.areaLimit') }}</label>
          <el-select v-model="maxAreaCount" size="default" class="form-select">
            <el-option v-for="item in areaCntOptions" :key="item.value" :label="item.label" :value="item.value"></el-option>
          </el-select>
        </div>
      </div>

      <div class="checkbox-section">
        <div class="checkbox-grid">
          <el-checkbox v-model="checked" class="checkbox-item">
            <span class="checkbox-label">{{ t('glossary.supportTimeTemplate') }}</span>
          </el-checkbox>
          <el-checkbox v-model="shieldAreaChecked" class="checkbox-item">
            <span class="checkbox-label">{{ t('glossary.enableShieldArea') }}</span>
          </el-checkbox>
          <el-checkbox v-if="areaType === 'hexagon' || areaType === 'quadrilateral'" v-model="fullScreenChecked" class="checkbox-item">
            <span class="checkbox-label">{{ t('glossary.defaultFullScreen') }}</span>
          </el-checkbox>
          <el-checkbox v-model="associatedAreaChecked" class="checkbox-item">
            <span class="checkbox-label">{{ t('glossary.enableAssociatedArea') }}</span>
          </el-checkbox>
        </div>
      </div>

      <div class="form-section" v-if="associatedAreaChecked">
        <el-form :model="associatedAreaConfig" label-position="top" class="associated-form">
          <div class="form-grid">
            <el-form-item :label="t('glossary.mainDetAreaName')" class="form-item">
              <el-input v-model="associatedAreaConfig.mainName" size="default" :placeholder="t('glossary.mainDetAreaPlaceholder')"></el-input>
            </el-form-item>
            <el-form-item :label="t('glossary.assocAreaName')" class="form-item">
              <el-input v-model="associatedAreaConfig.associatedName" size="default" :placeholder="t('glossary.assocAreaPlaceholder')"></el-input>
            </el-form-item>
            <el-form-item :label="t('glossary.assocAreaType')" class="form-item">
              <el-select v-model="associatedAreaConfig.regionType" :placeholder="t('field.selectAreaType')" size="default" style="width: 100%">
                <el-option v-for="item in associatedOptions" :key="item.value" :label="item.label" :value="item.value"></el-option>
              </el-select>
            </el-form-item>
          </div>
        </el-form>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { t } from '@/i18n'

const props = defineProps({
  algorithmMetadata: {
    type: Object,
    default: () => ({})
  }
})

const options = [
  {
    value: 'hexagon',
    label: t('glossary.hexagon')
  },
  {
    value: 'quadrilateral',
    label: t('glossary.quadrilateral')
  },
  {
    value: 'oneWayCordon',
    label: t('glossary.oneWayLine')
  },
  {
    value: 'cordon',
    label: t('glossary.twoWayLine')
  }
]

const associatedOptions = [
  {
    value: 'hexagon',
    label: t('glossary.hexagon')
  },
  {
    value: 'quadrilateral',
    label: t('glossary.quadrilateral')
  }
]

const areaType = ref('hexagon')
const checked = ref(false)
const shieldAreaChecked = ref(false)
const fullScreenChecked = ref(false)
const associatedAreaChecked = ref(false)
const maxAreaCount = ref(4)
const areaCntOptions = [
  {
    label: '1',
    value: 1
  },
  {
    label: '2',
    value: 2
  },
  {
    label: '3',
    value: 3
  },
  {
    label: t('glossary.nDefault', { n: 4 }),
    value: 4
  }
]
const associatedAreaConfig = ref({
  mainName: '',
  associatedName: '',
  regionType: 'hexagon'
})

watch(
  () => props.algorithmMetadata,
  () => {
    handleData()
  },
  { deep: true }
)

const handleData = () => {
  areaType.value = props.algorithmMetadata?.regionType || 'hexagon'
  checked.value = props.algorithmMetadata.scheduleSupport
  shieldAreaChecked.value =
    props.algorithmMetadata.enableShieldedRegion !== undefined
      ? props.algorithmMetadata.enableShieldedRegion
      : false
  fullScreenChecked.value =
    props.algorithmMetadata.defaultFullScreen !== undefined
      ? props.algorithmMetadata.defaultFullScreen
      : false
  maxAreaCount.value = props.algorithmMetadata.maxAreaCount
    ? props.algorithmMetadata.maxAreaCount
    : 4

  associatedAreaChecked.value = props.algorithmMetadata.region?.areasTitle
    ? true
    : false
  if (associatedAreaChecked.value) {
    associatedAreaConfig.value.mainName =
      props.algorithmMetadata.region.areasTitle[0].name
    associatedAreaConfig.value.associatedName =
      props.algorithmMetadata.region.areasTitle[1].name
    associatedAreaConfig.value.regionType =
      props.algorithmMetadata.region.areasTitle[1].regionType
  }
}

const saveDetection = () => {
  let detection = {
    scheduleSupport: checked.value,
    areaType: areaType.value,
    enableShieldedRegion: shieldAreaChecked.value,
    defaultFullScreen: fullScreenChecked.value,
    maxAreaCount: maxAreaCount.value,
    associatedAreaChecked: associatedAreaChecked.value,
    associatedAreaConfig: associatedAreaConfig.value
  }
  return detection
}

defineExpose({
  saveDetection
})
</script>

<style scoped lang="scss">
.detection-container {
  padding: 20px;
  background: var(--bg-primary);
  min-height: 100%;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.detection-card {
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  box-shadow: var(--shadow-sm);
  overflow: hidden;
  transition: all 0.3s ease;
}

.card-header {
  padding: 16px 20px;
  background: linear-gradient(135deg, var(--bg-secondary) 0%, #e2e8f0 100%);
  border-bottom: 1px solid var(--border-color);
}

.card-title {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--text-primary);
  display: flex;
  align-items: center;

  &::before {
    content: '';
    width: 4px;
    height: 18px;
    background: linear-gradient(
      135deg,
      var(--primary-color) 0%,
      var(--primary-light) 100%
    );
    border-radius: 2px;
    margin-right: 10px;
  }
}

.form-section {
  padding: 20px;
}

.form-row {
  display: flex;
  align-items: center;
  margin-bottom: 16px;
  gap: 16px;

  &:last-child {
    margin-bottom: 0;
  }
}

.form-label {
  min-width: 120px;
  font-size: 0.95rem;
  font-weight: 500;
  color: var(--text-primary);
  text-align: right;
}

.form-select {
  min-width: 200px;

  :deep(.el-input__wrapper) {
    border-radius: var(--radius-sm);
    transition: all 0.2s ease;

    &:hover {
      border-color: var(--primary-color);
    }

    &.is-focus {
      border-color: var(--primary-color);
      box-shadow: 0 0 0 2px rgba(49, 130, 206, 0.1);
    }
  }
}

.checkbox-section {
  padding: 16px 20px;
  background: var(--bg-primary);
  border-top: 1px solid var(--border-light);
}

.checkbox-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
}

.checkbox-item {
  :deep(.el-checkbox__input) {
    .el-checkbox__inner {
      border-radius: var(--radius-sm);
      transition: all 0.2s ease;

      &:hover {
        border-color: var(--primary-color);
      }
    }

    &.is-checked .el-checkbox__inner {
      background: linear-gradient(
        135deg,
        var(--primary-color) 0%,
        var(--primary-light) 100%
      );
      border-color: var(--primary-color);
    }
  }
}

.checkbox-label {
  font-size: 0.9rem;
  color: var(--text-secondary);
  font-weight: 500;
}

.associated-form {
  .form-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 20px;
  }

  .form-item {
    margin-bottom: 0;
    :deep(.el-form-item__content) {
      width: var(--form-item-content-width, 360px);
      max-width: 100%;
    }
    :deep(.el-form-item__label) {
      font-weight: 500;
      color: var(--text-primary);
      margin-bottom: 8px;
    }

    :deep(.el-input__wrapper) {
      border-radius: var(--radius-sm);
      transition: all 0.2s ease;

      &:hover {
        border-color: var(--primary-color);
      }

      &.is-focus {
        border-color: var(--primary-color);
        box-shadow: 0 0 0 2px rgba(49, 130, 206, 0.1);
      }
    }
  }
}

// 响应式设计
@media (max-width: 768px) {
  .detection-container {
    padding: 16px;
    gap: 16px;
  }

  .form-row {
    flex-direction: column;
    align-items: flex-start;
    gap: 8px;
  }

  .form-label {
    min-width: auto;
    text-align: left;
  }

  .form-select {
    width: 100%;
    min-width: auto;
  }

  .associated-form .form-item :deep(.el-form-item__content) {
    width: 100%;
  }

  .checkbox-grid {
    grid-template-columns: 1fr;
  }

  .associated-form .form-grid {
    grid-template-columns: 1fr;
    gap: 16px;
  }
}
</style>
