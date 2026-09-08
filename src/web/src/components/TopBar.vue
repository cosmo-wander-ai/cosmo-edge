<template>
  <div ref="topBarRef" class="topBar-wrap" :class="{ expanded: isOpen }" :style="{ height: topBarHeight + 'px', paddingBottom: isOpen ? '16px' : '0' }">
    <div ref="searchRef" class="form-search">
      <div class="formDiv" v-for="(form, index) in dataSouce.formList" :key="index">
        <span class="formTitle">
          <span class="star" :style="{ width: myLabelWidth ? myLabelWidth + 'px' : '85px' }"
            v-if="form.require">*</span>
          {{ formLabel(form) }}:
        </span>

        <!-- 文本输入框 -->
        <el-input class="el-form" v-if="!form.type || form.type == 'text'" v-model.trim="formData[form.model]"
          :placeholder="form.placeholder || placeholderText('enter', formLabel(form))"
          :title="form.placeholder || placeholderText('enter', formLabel(form))"
          :clearable="form.clearable === false ? false : true" :maxlength="form.maxLength" :disabled="!!form.disabled"
          size="small" />

        <!-- 下拉选择 -->
        <el-select class="el-form" v-if="form.type == 'select'"
          v-model.trim="formData[form.model]" :placeholder="form.placeholder || placeholderText('select', formLabel(form))"
          :title="form.placeholder || placeholderText('select', formLabel(form))"
          :clearable="form.clearable === false ? false : true" :filterable="!!form.filterable"
          :multiple="!!form.multiple" collapse-tags size="small" :disabled="!!form.disabled">
          <el-option v-for="(item, idx) in form.dataList" :key="idx"
            :label="optionLabel(form, item)"
            :value="form.valueKey ? item[form.valueKey] : item.value" />
        </el-select>

        <!-- 日期时间范围选择器 -->
        <el-date-picker class="el-formDate" :style="{ width: myLabelWidth !== 0 ? myLabelWidth + 340 + 'px' : '400px' }"
          v-if="form.type == 'datetimerange'" v-model.trim="formData[form.model]" type="datetimerange"
          :range-separator="form.rangeSeparator || '-'" :start-placeholder="form.startPlaceholder || t('placeholder.startTime')"
          :end-placeholder="form.endPlaceholder || t('placeholder.endTime')" :value-format="form.valueFormat || 'YYYY-MM-DD HH:mm:ss'"
          :clearable="form.clearable === false ? false : true" align="right" size="small"
          :default-time="[new Date(2000, 0, 1, 0, 0, 0), new Date(2000, 0, 1, 23, 59, 59)]" :disabled="!!form.disabled"
          @change="(date) => form.change && form.change(date)" />
      </div>
    </div>

    <!-- 按钮栏 -->
    <div class="btnBar">
      <slot name="btnTools"></slot>

      <el-button class="mv-el-button" type="primary" size="small" @click="getFormData">
        {{ t('action.search') }}
      </el-button>

      <el-button v-if="reset" size="small" @click="resetFormData">
        {{ t('action.reset') }}
      </el-button>

      <div
        v-if="showOpen"
        class="optionBtn"
        @click="openFn"
      >
        {{ !isOpen ? t('action.expand') : t('action.collapse') }}
        <el-icon :class="{ retract: isOpen }"><ArrowDown /></el-icon>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, watch } from 'vue'
import { ArrowDown } from '@element-plus/icons-vue'
import { currentLocale, t, tShort } from '@/i18n'

// TODO(i18n): Phase 2 should pass labelKey instead of raw form.label so
// generated placeholders do not become half-translated strings.

const props = defineProps({
  dataSouce: {
    type: Object,
    default: () => ({
      title: '',
      formList: []
    })
  },
  formData: {
    type: Object,
    default: () => ({})
  },
  reset: {
    type: Boolean,
    default: true
  },
  labelWidth: {
    type: Number,
    default: 0
  },
  // 默认展开且不显示折叠按钮
  defaultExpand: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['search', 'reset'])

const searchHeight = 56

const topBarRef = ref(null)
const searchRef = ref(null)

const propsFromData = { ...props.formData }
const topBarHeight = ref(searchHeight)
const myLabelWidth = ref(null)
const showOpen = ref(false)
const isOpen = ref(false)
const formListChildCount = ref(0)
const defaultTimes = reactive({})

const vowel = {
  a: true,
  e: true,
  i: true,
  o: true,
  u: true
}

// 转换标签为小写
const toLowerCaseLabel = (label) => {
  const language = currentLocale.value
  if (language === 'en-US' || language === 'en_US' || (import.meta.env.DEV && language === 'xx-pseudo')) {
    const firstLetter = label.toLowerCase().trim()[0]
    if (vowel[firstLetter]) {
      return `an ${label.toLowerCase()}`
    }
    return `a ${label.toLowerCase()}`
  } else {
    return label
  }
}

const placeholderText = (type, label) => {
  const key = type === 'select' ? 'placeholder.select' : 'placeholder.enter'
  return tShort(key, 'placeholder', { field: toLowerCaseLabel(label) })
}

const formLabel = (form) => {
  return form.labelI18nKey ? t(form.labelI18nKey) : form.label
}

const optionLabel = (form, item) => {
  if (item.labelI18nKey) return t(item.labelI18nKey)
  return form.labelKey ? item[form.labelKey] : item.label
}

// 获取标签最大宽度
const getLabelMaxWidth = () => {
  myLabelWidth.value = props.labelWidth || 0
}

// 获取高度
const getHeight = () => {
  const nodeList = topBarRef.value?.childNodes[0]?.childNodes
  if (!nodeList || !nodeList.length) {
    setTimeout(() => {
      if (formListChildCount.value > 50) {
        return
      }
      formListChildCount.value++
      getHeight()
    }, 1)
  } else {
    setTimeout(() => {
      if (props.defaultExpand) {
        // 默认展开：直接展开到完整高度，不显示折叠按钮
        isOpen.value = true
        topBarHeight.value = searchRef.value.scrollHeight + 24
        showOpen.value = false
        // Recalculate after fonts/i18n text fully rendered
        setTimeout(() => {
          if (searchRef.value) {
            topBarHeight.value = searchRef.value.scrollHeight + 24
          }
        }, 300)
      } else if (topBarRef.value?.scrollHeight > 72) {
        showOpen.value = true
      }
    }, 100)
  }
}

// 获取表单数据
const getFormData = () => {
  emit('search', props.formData)
}

// 重置表单数据
const resetFormData = () => {
  let index = 0
  for (const key in props.formData) {
    if (props.formData.hasOwnProperty(key)) {
      if (defaultTimes.hasOwnProperty(key)) {
        props.formData[key] = propsFromData[key]
      }
      const element = props.dataSouce.formList[index]
      // 不可清空并且disabled的文本框重置时不清除
      if (element && (element.clearable === false || element.disabled)) {
        index++
        continue
      }
      if (!defaultTimes.hasOwnProperty(key)) {
        props.formData[key] = propsFromData[key]
        if (props.formData[key] === localStorage.getItem('currentCustId')) {
          props.formData[key] = ''
          propsFromData[key] = ''
          localStorage.setItem('currentCustId', '')
        }
      }
      index++
    }
  }
  emit('reset')
}

// 展开/收起
const openFn = () => {
  isOpen.value = !isOpen.value
  if (isOpen.value) {
    topBarHeight.value = searchRef.value.scrollHeight + 16
  } else {
    topBarHeight.value = searchHeight
  }
}

onMounted(() => {
  getHeight()
  getLabelMaxWidth()

  const formItems = props.dataSouce.formList
    .map((item) => {
      if (item.type === 'datetimerange') {
        return {
          type: item.type,
          model: item.model
        }
      }
    })
    .filter((v) => v)

  formItems.forEach((item) => {
    const unwatch = watch(
      () => props.formData[item.model],
      (newValue) => {
        defaultTimes[item.model] = Object.freeze(newValue)
        unwatch()
      }
    )
  })
})
</script>

<style scoped lang="scss">
.topBar-wrap {
  flex-shrink: 0;
  position: relative;
  background: #fff;
  padding: 0 20px 0 24px;
  border-radius: 2px;
  display: flex;
  overflow: hidden;
  transition: all 300ms;

  &.expanded {
    padding-bottom: 16px !important;
  }

  .form-search {
    flex: 1;
    display: flex;
    flex-wrap: wrap;
    align-content: flex-start;

    .formDiv {
      display: flex;
      align-items: center;
      margin: 12px 24px 12px 0px;

      .formTitle {
        display: inline-block;
        margin-right: 8px;
        font-size: 14px;
        text-align: right;
        color: #303133;
        flex-shrink: 0;
        max-width: 150px;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .el-form {
        width: 160px;
      }

      .el-formDate {
        width: 420px;
      }
    }
  }

  .btnBar {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    padding: 12px 0;
    transition: all 300ms;

    .optionBtn {
      margin-left: 10px;
      line-height: 32px;
      color: var(--primary-color);
      font-size: 14px;
      cursor: pointer;

      .el-icon {
        display: inline-block;
        transition: all 300ms;

        &.retract {
          transform: rotate(180deg);
        }
      }
    }
  }
}

.star {
  color: red;
  padding-right: 3px;
}
</style>
