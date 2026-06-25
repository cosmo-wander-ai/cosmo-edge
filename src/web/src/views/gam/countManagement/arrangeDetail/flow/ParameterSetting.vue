<template>
  <div class="param-setting-body">
    <!-- 添加模式切换按钮 -->
    <div class="mode-switch">
      <el-radio-group v-model="viewMode" size="small">
        <el-radio-button value="simple">{{ t('glossary.simpleMode') }}</el-radio-button>
        <el-radio-button value="detail">{{ t('glossary.detailMode') }}</el-radio-button>
      </el-radio-group>
    </div>

    <div style="display: flex;flex-wrap: wrap;">
      <div v-if="viewMode === 'simple'" class="simple-mode">
        <div class="param-name param-header">{{ t('glossary.paramName') }}</div>
        <div class="param-value param-header">{{ t('glossary.defaultValue') }}</div>
        <div class="show-level param-header">{{ t('glossary.displayLevel') }}</div>
      </div>
      <div class="box" :class="{'simple-box': viewMode === 'simple'}" v-for="(item,index) in formData" :key="index" :draggable="!isEditing && !isSelecting" @dragstart="dragstart($event,index)" @dragenter="dragenter($event, index)" @dragover="dragover($event, index)" @mousedown="handleMouseDown" @mouseup="handleMouseUp">
        <!-- 精简模式下只显示基础信息 -->
        <template v-if="viewMode === 'simple'">
          <div class="simple-mode">
            <div class="simple-item">
              <div class="param-name">
                {{ resolveParameterName(item) }}
                <el-tooltip v-if="item.describe" :content="resolveParameterDescription(item)" placement="top" effect="dark">
                  <i class="el-icon-question" style="margin-right: 5px;color: #909399;cursor: pointer;"></i>
                </el-tooltip>{{ localeColon }}
              </div>
              <div class="param-value">
                <template v-if="item.moduleType === 'slider'">
                  <el-slider v-model.number="item.defaultValue" :min="Number(item.scopeMinValue)" :max="Number(item.scopeMaxValue)"></el-slider>
                </template>
                <template v-else-if="item.moduleType === 'switch'">
                  <el-switch v-model="item.defaultValue" :active-value="'1'" :inactive-value="'0'"></el-switch>
                </template>
                <template v-else-if="item.moduleType === 'select'">
                  <el-select v-model="item.defaultValue" size="small" style="width: 100%">
                    <el-option v-for="option in getEnumOptions(item)" :key="option.value" :label="option.name" :value="option.value">
                    </el-option>
                  </el-select>
                </template>
                <template v-else-if="item.moduleType === 'radio'">
                  <el-radio-group v-model="item.defaultValue">
                    <el-radio v-for="option in getEnumOptions(item)" :key="option.value" :value="option.value">
                      {{option.name}}
                    </el-radio>
                  </el-radio-group>
                </template>
                <template v-else-if="item.moduleType === 'check'">
                  <el-checkbox-group v-model="item.defaultValue">
                    <el-checkbox v-for="option in getEnumOptions(item)" :key="option.value" :value="option.value">
                      {{option.name}}
                    </el-checkbox>
                  </el-checkbox-group>
                </template>
                <template v-else>
                  <el-input v-model="item.defaultValue" size="small"></el-input>
                </template>
              </div>
            </div>
            <div class="show-level">
              <el-radio-group v-model="item.checkedClient">
                <el-radio :value="0">{{ t('glossary.notHidden') }}</el-radio>
                <!-- <el-radio :value="1">{{ t('glossary.clientHidden') }}</el-radio> -->
                <el-radio :value="2">{{ t('glossary.allHidden') }}</el-radio>
              </el-radio-group>
              <!-- <div v-if="item.level != '2'" class="delete-icon" @click="forkClick(item,index)">
                <span>x</span>
              </div> -->
            </div>
          </div>
        </template>

        <!-- 详细模式显示原有内容 -->
        <template v-else>
          <el-radio-group v-model="item.checkedClient" class="hidden-top">
            <el-radio :value="0">{{ t('glossary.notHidden') }}</el-radio>
            <el-radio :value="1">{{ t('glossary.clientHidden') }}</el-radio>
            <el-radio :value="2">{{ t('glossary.allHidden') }}</el-radio>
          </el-radio-group>
          <div v-if="item.level != '2'" class="delete-icon" @click="forkClick(item,index)">
            <span>x</span>
          </div>
          <!-- <el-button class="el-icon-close fork" @click="forkClick(item,index)" :disabled="item.level == '2'"></el-button> -->

          <!-- 父组件 -->
          <div v-if="item.showFather" style="margin-top:10px;">
            <div>
              {{ t('glossary.childShowCondition') }}
            </div>
            <div class="formDiv">
              <div class="formtext"> {{ t('glossary.parentKey') }}{{ localeColon }}</div>
              <el-select class="formR" v-model="item.dependOn" size="small" @change="selectChange">
                <el-option v-for="obj in Object.keys(dependFatherOptions)" :key="obj" :label="obj" :value="obj"></el-option>
              </el-select>
              <!-- <el-input v-model="item.dependOn" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR"></el-input> -->
            </div>
            <div class="formDiv">
              <div class="formtext"> {{ t('glossary.matchValue') }}{{ localeColon }}</div>
              <el-select class="formR" v-model="item.matchingValue" size="small" :placeholder="t('validate.pleaseSelect', { name: '' })">
                <el-option v-for="obj in dependFatherOptions[item.dependOn]" :key="obj.value" :label="obj.name" :value="obj.value">
                </el-option>
              </el-select>
            </div>
            <!-- <div class="formDiv" v-if="item.dependOnType=='switch'">
            <div class="formtext">{{ t('glossary.showValue') }}{{ localeColon }}</div>
            <el-input v-model="item.showValue" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR"></el-input>
          </div> -->
            <el-divider></el-divider>
          </div>

          <!-- 子组件 -->
          <div v-if="item.dependOn && !item.showFather" style="margin-top:10px;">
            <div>
              {{ t('glossary.childShowCondition') }}
            </div>
            <div class="formDiv">
              <div class="formtext">{{ t('glossary.parentKey') }}{{ localeColon }}</div>
              <el-input v-model="item.dependOn" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>
            <div class="formDiv" v-if="item.enumerationOptions && item.enumerationOptions.length > 0">
              <div class="formtext">{{ t('glossary.matchValue') }}{{ localeColon }}</div>
              <el-select class="formR" v-model="item.matchingValue" size="small" :placeholder="t('validate.pleaseSelect', { name: '' })">
                <el-option v-for="item in item.enumerationOptions" :key="item.value" :label="item.label" :value="item.value">
                </el-option>
              </el-select>
            </div>
            <div class="formDiv" v-if="item.dependOnType=='switch'">
              <div class="formtext">{{ t('glossary.showValue') }}{{ localeColon }}</div>
              <el-input v-model="item.showValue" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>
            <el-divider></el-divider>
          </div>

          <div class="formDiv">
            <div class="formtext"> {{ t('glossary.componentType') }}{{ localeColon }}</div>
            <el-select class="formR" v-model="item.moduleType" size="small" :disabled="disabledTypes.includes(item.moduleType)" :placeholder="t('validate.pleaseSelect', { name: '' })">
              <el-option v-for="item in options" :key="item.value" :label="item.label" :value="item.value">
              </el-option>
            </el-select>
          </div>

          <div class="formDiv">
            <div class="formtext"> key{{ localeColon }}</div>
            <el-input v-model="item.keyValue" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false" :disabled="item.level == '2'"></el-input>
          </div>

          <div class="formDiv">
            <div class="formtext">{{ t('field.name') }}{{ localeColon }}</div>
            <el-input :model-value="resolveParameterName(item)" @update:model-value="(val) => { item.nameValue = val; item.nameI18nKey = '' }" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
          </div>

          <div v-if="item.showMore">
            <div class="formDiv">
              <div class="formtext">{{ t('glossary.defaultValue') }}{{ localeColon }}</div>
              <el-input v-model="item.defaultValue" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>

            <div class="formDiv">
              <div class="formtext"> {{ t('field.description') }}{{ localeColon }}</div>
              <el-input v-model="item.describe" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>

            <div class="formDiv">
              <div class="formtext"> {{ t('glossary.abnormalTip') }}{{ localeColon }}</div>
              <el-input v-model="item.abnormity" :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>

            <div class="formDiv">
              <div class="formtext"> {{ t('glossary.regexValidation') }}{{ localeColon }}</div>
              <el-input v-model="item.regularVerify" :placeholder="t('validate.pleaseEnter', { name: '' })" size="small" :disabled="item.moduleType==='confidenceConfig'" class="formR" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>

            <div class="formDiv" v-if="item.moduleType=='slider'">
              <div class="formtext">{{ t('glossary.range') }}{{ localeColon }}</div>
              <el-input v-model="item.scopeMinValue" :placeholder="t('glossary.minValue')" style="width:100px;" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
              <div style="margin: 0 5px;">-</div>
              <el-input v-model="item.scopeMaxValue" :placeholder="t('glossary.maxValue')" style="width:100px;" size="small" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>

            <div class="formDiv" v-if="item.moduleType=='select'||item.moduleType=='check'||item.moduleType=='radio'">
              <div class="formtext">{{ t('glossary.enumOptions') }}{{ localeColon }}</div>
              <el-input v-model="item.enumeration" type="textarea" autosize :placeholder="t('validate.pleaseEnter', { name: '' })" class="formR custom-textarea" @focus="isEditing = true" @blur="isEditing = false"></el-input>
            </div>
            <div class="tip-div" v-if="item.moduleType=='select'||item.moduleType=='check'||item.moduleType=='radio'">
              {{ t('glossary.enumFormatTip') }}
            </div>

            <div class="add-component-btn">
              <div class="btn-item" v-if="!item.dependOn && !item.showFather">
                <el-button type="success" @click="fatherClick(item)" size="small">{{ t('action.addParent') }}</el-button>
              </div>
              <div class="btn-item" v-if="item.moduleType=='select'||item.moduleType=='switch'">
                <el-button type="success" size="small" @click="sonClick(item)">{{ t('action.addChild') }}</el-button>
              </div>
            </div>
          </div>

          <span class="el-icon-d-arrow-left down-btn" :class="{'up-btn': item.showMore}" @click="showMoreClick(item)"></span>
        </template>
      </div>

      <div v-if="viewMode === 'simple'" class="simple-tips">
        {{ t('glossary.simpleTip') }}
      </div>
      <!-- 添加组件 -->
      <div v-if="viewMode==='detail'" class="btn">
        <button type="button" class="add-card" @click="addModule">
          <el-icon class="add-card-icon">
            <Plus />
          </el-icon>
        </button>
        <div class="add-card-label">{{ t('action.addComponent') }}</div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, watch, onMounted, onBeforeUnmount, nextTick, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveI18nOptionLabel, resolveI18nText } from '@/utils/i18nResource'

// Props
const props = defineProps({
  algorithmMetadata: {
    type: Object,
    default: () => ({})
  }
})

// Data
const isSelecting = ref(false)
const viewMode = ref('simple')
const showMore = ref(false)
const options = computed(() => [
  {
    value: 'text',
    label: t('glossary.textInput')
  },
  {
    value: 'switch',
    label: t('glossary.switchComp')
  },
  {
    value: 'slider',
    label: t('glossary.sliderComp')
  },
  {
    value: 'select',
    label: t('glossary.selectComp')
  },
  {
    value: 'check',
    label: t('glossary.checkboxComp')
  },
  {
    value: 'radio',
    label: t('glossary.radioComp')
  }
])
const formData = ref([])
const dragIndex = ref('')
const enterIndex = ref('')
const timeout = ref(null)
const dependFatherOptions = ref({})
const disabledTypes = ref([
  'confidenceConfig',
  'faceSet',
  'workClothesSet',
  'commoditySet',
  'retroDirect'
])
const isEditing = ref(false)

const copyParamI18nFields = (source, target) => {
  for (const field of [
    'nameI18nKey',
    'descriptionI18nKey',
    'failedTipI18nKey',
    'placeholderI18nKey'
  ]) {
    if (source?.[field]) {
      target[field] = source[field]
    }
  }
}

const resolveParameterName = (item) => {
  return resolveI18nText(
    {
      name: item?.nameValue,
      nameI18nKey: item?.nameI18nKey
    },
    'name',
    t('glossary.unnamed')
  )
}

const resolveParameterDescription = (item) => {
  return resolveI18nText(
    {
      description: item?.describe,
      descriptionI18nKey: item?.descriptionI18nKey
    },
    'description'
  )
}

// Lifecycle
onBeforeUnmount(() => {
  clearInterval(timeout.value)
  timeout.value = null
})

onMounted(() => {
  init()
})

// Watch
watch(
  () => props.algorithmMetadata,
  (newValue) => {
    if (newValue) {
      formData.value = []
      init()
    }
  },
  { deep: true }
)

// 添加标志防止递归更新
const isUpdatingFormData = ref(false)

watch(
  [formData, currentLocale],
  () => {
    if (isUpdatingFormData.value) return

    isUpdatingFormData.value = true
    dependFatherOptions.value = {}
    formData.value.forEach((item) => {
      if (item.moduleType == 'select') {
        if (item.enumeration) {
          const optionI18nByValue = new Map(
            (Array.isArray(item.options) ? item.options : [])
              .filter((option) => option?.value !== undefined && option?.labelI18nKey)
              .map((option) => [String(option.value), option.labelI18nKey])
          )
          let arr = item.enumeration.split(',')
          let options = []
          arr.forEach((el) => {
            let a = el.split(':')
            const option = {
              name: a[0],
              value: a[1]
            }
            const labelI18nKey = optionI18nByValue.get(String(a[1]))
            if (labelI18nKey) {
              option.labelI18nKey = labelI18nKey
            }
            options.push(option)
          })
          item.options = options
        }
        dependFatherOptions.value[item.keyValue] = item.options
      } else if (item.moduleType == 'switch') {
        dependFatherOptions.value[item.keyValue] = [
          {
            name: t('glossary.switchOff'),
            value: '0'
          },
          {
            name: t('glossary.switchOn'),
            value: '1'
          }
        ]
      }
    })

    // Rebuild enumerationOptions for child components to react to locale changes
    formData.value.forEach((item) => {
      if (item.dependOn && item.enumerationOptions) {
        let relevance = formData.value.find((obj) => item.dependOn == obj.keyValue)
        if (relevance) {
          let str = []
          if (relevance.moduleType == 'switch') {
            str = [
              { value: '0', label: t('glossary.switchOff') },
              { value: '1', label: t('glossary.switchOn') }
            ]
          } else if (relevance.options) {
            relevance.options.forEach((el) => {
              str.push({
                value: el.value,
                label: resolveI18nOptionLabel(el)
              })
            })
          }
          if (str.length > 0) {
            item.enumerationOptions = str
          }
        }
      }
    })

    nextTick(() => {
      isUpdatingFormData.value = false
    })
  },
  { deep: true }
)

watch(viewMode, (newMode) => {
  handleModeChange(newMode)
})

// Methods
const init = () => {
  if (
    props.algorithmMetadata.params &&
    props.algorithmMetadata.params.length > 0
  ) {
    props.algorithmMetadata.params.forEach((item) => {
      let a = []
      let arr1 = {
        moduleType: item.type,
        keyValue: item.key,
        nameValue: item.name,
        defaultValue:
          item.type === 'check'
            ? item.defaultValue
              ? item.defaultValue.split(',').filter(Boolean)
              : []
            : item.type === 'slider'
            ? Number(item.defaultValue || 0)
            : item.defaultValue || '',
        describe: item.description ? item.description : '',
        abnormity: item.failedTip ? item.failedTip : '',
        regularVerify: item.regexpr ? item.regexpr : '',
        dependOn: '',
        level: item.level,
        showMore: false,
        showFather: false
      }
      copyParamI18nFields(item, arr1)
      if (item.type == 'slider') {
        if (item.range) {
          a = item.range.split(',')
          arr1.scopeMinValue = Number(a[0])
          arr1.scopeMaxValue = Number(a[1])
        }
      }
      if (item.dependsOn && item.dependsOn.key) {
        arr1.dependOn = item.dependsOn.key
        arr1.matchingValue = item.dependsOn.value
        let relevance = props.algorithmMetadata.params.filter((obj) => {
          return item.dependsOn.key == obj.key
        })
        if (relevance && relevance.length > 0) {
          let str = []
          if (relevance[0].type == 'switch') {
            str = [
              {
                value: '0',
                label: t('glossary.switchOff')
              },
              {
                value: '1',
                label: t('glossary.switchOn')
              }
            ]
          } else {
            relevance[0]?.options.forEach((el) => {
              str.push({
                value: el.value,
                label: resolveI18nOptionLabel(el)
              })
            })
          }
          arr1.enumerationOptions = str
        }
      }
      if (item.options && item.options.length > 0) {
        let str = ''
        item.options.forEach((el, index) => {
          if (index == item.options.length - 1) {
            str = str + `${el.name}:${el.value}`
          } else {
            str = str + `${el.name}:${el.value},`
          }
        })
        arr1.enumeration = str
        // 同时设置 options，避免在 watch 中修改
        arr1.options = item.options
      }
      if (item.senior !== null) {
        arr1.checkedClient = item.senior
      }
      formData.value.push(arr1)
    })
  }
}

const dragstart = (e, index) => {
  if (isEditing.value) return
  dragIndex.value = index
}

const dragenter = (e, index) => {
  if (isEditing.value) return
  e.preventDefault()
  enterIndex.value = index
  if (timeout.value !== null) {
    clearTimeout(timeout.value)
  }
  timeout.value = setTimeout(() => {
    if (dragIndex.value !== index) {
      const source = formData.value[dragIndex.value]
      formData.value.splice(dragIndex.value, 1)
      formData.value.splice(index, 0, source)
      dragIndex.value = index
    }
  }, 100)
}

const dragover = (e) => {
  e.preventDefault()
}

const addModule = () => {
  let arr = {
    moduleType: 'text',
    keyValue: '',
    nameValue: '',
    defaultValue: '',
    describe: '',
    abnormity: '',
    regularVerify: '',
    scopeMinValue: '',
    scopeMaxValue: '',
    enumeration: '',
    checkedClient: 0,
    dependOn: '',
    showMore: true,
    showFather: false
  }
  formData.value.push(arr)
}

const forkClick = (data, val) => {
  formData.value.splice(val, 1)
}

const selectChange = (val) => {
  console.log(val, formData.value)
}

const fatherClick = (data) => {
  data.showFather = true
}

const sonClick = (data) => {
  console.log(data)
  if (data.moduleType == 'switch' && data.keyValue) {
    let arr1 = {
      moduleType: 'text',
      keyValue: '',
      nameValue: '',
      defaultValue: '',
      describe: '',
      abnormity: '',
      regularVerify: '',
      scopeMinValue: '',
      scopeMaxValue: '',
      checkedClient: 0,
      dependOn: data.keyValue,
      dependOnType: data.moduleType,
      showValue: '1',
      showMore: true,
      showFather: false
    }
    formData.value.push(arr1)
  }
  if (data.keyValue && data.enumeration) {
    let str = data.enumeration.split(',')
    let arr = []
    str.forEach((item) => {
      let val = item.split(':')
      arr.push({
        value: val[1],
        label: val[0]
      })
    })
    console.log(arr)
    let arr1 = {
      moduleType: 'text',
      keyValue: '',
      nameValue: '',
      defaultValue: '',
      describe: '',
      abnormity: '',
      regularVerify: '',
      scopeMinValue: '',
      scopeMaxValue: '',
      enumeration: '',
      checkedClient: 0,
      dependOn: data.keyValue,
      enumerationOptions: arr,
      matchingValue: '',
      showMore: true,
      showFather: false
    }
    formData.value.push(arr1)
  }

  if (data.moduleType == 'switch' && data.keyValue == '') {
    return ElMessage.error(t('validate.childKeyRequired'))
  }
  if (
    (data.moduleType != 'switch' && data.keyValue == '') ||
    data.enumeration == ''
  ) {
    return ElMessage.error(t('validate.childKeyEnumRequired'))
  }
}

const saveParamConfig = () => {
  formData.value.forEach((item) => {
    if (!item.level) {
      if (!item.keyValue.includes('custom.')) {
        item.keyValue = 'custom.' + item.keyValue
      }
    }
  })

  let params = []
  formData.value.forEach((element) => {
    let param = {
      beginValue: null,
      defaultValue:
        element.moduleType === 'check'
          ? Array.isArray(element.defaultValue)
            ? element.defaultValue.join(',')
            : element.defaultValue
          : element.defaultValue,
      dependsOn: null,
      description: element.describe,
      endValue: null,
      failedTip: element.abnormity,
      group: null,
      key: element.keyValue,
      maxValue: null,
      minValue: null,
      name: element.nameValue,
      negative: null,
      options: null,
      range: null,
      senior: element.checkedClient,
      regexpr: element.regularVerify,
      step: null,
      type: element.moduleType,
      value:
        element.moduleType === 'check'
          ? Array.isArray(element.defaultValue)
            ? element.defaultValue.join(',')
            : element.defaultValue
          : element.defaultValue,
      level: element ? element.level : null
    }
    copyParamI18nFields(element, param)

    if (element.scopeMaxValue && element.scopeMinValue) {
      param.range = `${element.scopeMinValue},${element.scopeMaxValue}`
    }

    if (element.dependOn) {
      param.dependsOn = {
        key: element.dependOn,
        value: element.matchingValue
      }
    }
    if (element.enumeration) {
      let b = element.enumeration.split(',')
      let d = []
      const optionI18nByValue = new Map(
        (Array.isArray(element.options) ? element.options : [])
          .filter((option) => option?.value !== undefined && option?.labelI18nKey)
          .map((option) => [String(option.value), option.labelI18nKey])
      )
      b.forEach((i) => {
        let c = i.split(':')
        let ad = {
          name: c[0],
          value: c[1]
        }
        const labelI18nKey = optionI18nByValue.get(String(c[1]))
        if (labelI18nKey) {
          ad.labelI18nKey = labelI18nKey
        }
        d.push(ad)
      })
      param.options = d
    }
    params.push(param)
  })
  return params
}

const showMoreClick = (obj) => {
  obj.showMore = !obj.showMore
}

const getModuleTypeName = (type) => {
  const typeMap = {
    text: t('glossary.textInput'),
    switch: t('glossary.switchComp'),
    slider: t('glossary.sliderComp'),
    select: t('glossary.selectComp'),
    check: t('glossary.checkboxComp'),
    radio: t('glossary.radioComp')
  }
  return typeMap[type] || type
}

const handleMouseDown = (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') {
    isSelecting.value = true
  }
}

const handleMouseUp = () => {
  isSelecting.value = false
}

const getEnumOptions = (enumStr) => {
  const enumValue = typeof enumStr === 'string' ? enumStr : enumStr?.enumeration
  if (!enumValue) return []
  const optionI18nByValue = new Map(
    (Array.isArray(enumStr?.options) ? enumStr.options : [])
      .filter((option) => option?.value !== undefined && option?.labelI18nKey)
      .map((option) => [String(option.value), option.labelI18nKey])
  )
  return enumValue.split(',').map((item) => {
    const [name, value] = item.split(':')
    const option = {
      name,
      value,
      labelI18nKey: optionI18nByValue.get(String(value))
    }
    return {
      name: resolveI18nOptionLabel(option),
      value
    }
  })
}

const handleModeChange = (mode) => {
  formData.value.forEach((item) => {
    if (item.moduleType === 'check') {
      if (mode === 'detail') {
        item.defaultValue = Array.isArray(item.defaultValue)
          ? item.defaultValue.join(',')
          : item.defaultValue
      } else {
        item.defaultValue =
          typeof item.defaultValue === 'string'
            ? item.defaultValue.split(',').filter(Boolean)
            : []
      }
    }
  })
}

// Expose methods
defineExpose({
  saveParamConfig
})
</script>

<style scoped lang="scss">
.box {
  position: relative;
  padding: 0 30px 20px 30px;
  width: 330px;
  margin: 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  background: var(--bg-white);
  box-shadow: var(--shadow-sm);

  &.simple-box {
    width: 100%;
    max-width: none;
    border: none;
    box-shadow: none;
    padding: 0;
    margin: 0;
  }

  .formDiv {
    display: flex;
    height: 30px;
    margin-top: 8px;
    align-items: center;
    .formtext {
      font-size: 14px;
      width: 100px;
      text-align: right;
      color: var(--text-secondary);
    }
    .formR {
      width: 200px;
    }
  }

  .hidden-top {
    margin-top: 15px;
  }

  .delete-icon {
    position: absolute;
    top: 0;
    right: 0;
    width: 50px;
    height: 50px;
    background: var(--danger-color);
    clip-path: circle(40% at 98% 0);
    cursor: pointer;

    span {
      position: absolute;
      top: -2px;
      right: 4px;
      display: inline-block;
      color: #fff;
      font-size: 16px;
    }
  }
}

.delete-icon:hover {
  span {
    transform: scale(1.1);
  }
}

.btn {
  position: relative;
  height: 173px;
  padding: 30px;
  width: 300px;
  margin: 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  background: var(--bg-white);
  box-shadow: var(--shadow-sm);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}
.fork {
  border: none;
  padding: 0;
  position: absolute;
  right: 10px;
  top: 5px;
  font-size: 30px;
  cursor: pointer;
}

.hide-btn {
  position: absolute;
  top: 10px;
}

.tip-div {
  margin-top: 10px;
  color: var(--text-muted);
}

.add-component-btn {
  display: flex;
  justify-content: space-around;
  margin-top: 20px;

  .btn-item {
    text-align: center;
  }
}

.add-card {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 96px;
  height: 40px;
  border: none;
  border-radius: 9999px;
  background: linear-gradient(90deg, #3182ce 0%, #4299e1 100%);
  color: #fff;
  cursor: pointer;
  box-shadow: 0 8px 18px rgba(49, 130, 206, 0.25);
}
.add-card:hover {
  box-shadow: 0 10px 22px rgba(49, 130, 206, 0.32);
  transform: translateY(-1px);
}
.add-card:active {
  transform: translateY(0);
}
.add-card-icon {
  font-size: 22px;
}
.add-card-label {
  margin-top: 10px;
  font-size: 14px;
  color: #111827;
}

.down-btn {
  position: relative;
  left: calc(50% - 10px);
  bottom: -15px;
  display: inline-block;
  font-size: 20px;
  transform: rotate(-90deg);
  cursor: pointer;
}

.up-btn {
  transform: rotate(90deg);
}

.custom-textarea :deep(textarea) {
  min-height: 40px !important;
}

.mode-switch {
  position: fixed;
  top: 136px;
  right: 32px;
  z-index: 1000;
  padding: 6px 8px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 6px 16px rgba(15, 23, 42, 0.06);
  border: 1px solid var(--border-color);
  border-radius: 9999px;
  backdrop-filter: saturate(130%) blur(2px);
  display: flex;
  align-items: center;
}

:deep(.mode-switch .el-radio-group) {
  display: inline-flex;
  gap: 2px;
}

/* 优化按钮外观为小胶囊 */
:deep(.mode-switch .el-radio-button .el-radio-button__inner) {
  border: none;
  box-shadow: none;
  border-radius: 9999px !important;
  padding: 6px 12px;
  height: auto;
  line-height: 1;
}

/* 激活态色彩更清晰 */
:deep(.mode-switch .el-radio-button.is-active .el-radio-button__inner) {
  background: var(--primary-color, #3182ce);
  color: #fff;
}

/* 非激活态悬停反馈 */
:deep(.mode-switch
    .el-radio-button:not(.is-active)
    .el-radio-button__inner:hover) {
  background: #f3f4f6;
}

.simple-mode {
  display: flex;
  align-items: center;
  padding: 15px;
  width: 100%;
  background: var(--bg-primary);
  border-bottom: 1px solid var(--border-light);
}

.show-level {
  width: 160px;
  margin-left: 80px;
}

.param-name {
  display: inline-block;
  width: 200px;
  text-align: right;
  font-size: 14px;
  color: var(--text-secondary);
}

.simple-item {
  display: flex;
  align-items: center;
}

.param-value {
  display: inline-block;
  width: 250px;

  &.slider-value {
    padding-top: 8px; // 微调滑块位置
  }
}

.param-header {
  text-align: center;
  font-size: 18px;
  color: var(--text-primary);
  font-weight: 600;
}

.simple-tips {
  margin-left: 20px;
  margin-top: 40px;
  color: var(--text-muted);
}

// 修改单选按钮组样式
:deep(.el-radio-group) {
  .el-radio {
    margin-right: 15px;
    .el-radio__label {
      font-size: 13px;
    }
  }
}

// 详细模式内表单控件主题适配
:deep(.el-input__wrapper),
:deep(.el-select .el-input__wrapper),
:deep(.el-textarea__inner) {
  border-radius: var(--radius-sm);
}
</style>
