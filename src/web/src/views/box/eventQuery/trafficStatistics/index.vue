<template>
  <div class="mv-wrap">
    <div class="mv-wrap-top">
      <div class="mvtitle">
        <span class="mv-table-title">{{ t('event.queryConditions') }}</span>
      </div>

      <el-tabs class="tabs" v-model="timeGranularity">
        <!--------- 时数据 --------->
        <el-tab-pane :label="t('event.hourlyData')" name="hour">
          <el-form inline label-position="left">

            <el-form-item :label="t('event.queryDate') + localeColon">
              <el-date-picker v-model="hourParams.hourDay" value-format="YYYY-MM-DD" type="date" :editable="false"
                :picker-options="pickerOptions" :placeholder="t('event.selectDatePlaceholder')" size="small" />
            </el-form-item>

            <el-form-item :label="t('event.timeRange') + localeColon">
              <el-time-select v-model="hourParams.hourStart" class="time-picker" :editable="false"
                :picker-options="{ start: '00:00', step: '01:00', end: '24:00', maxTime: hourParams.hourEnd }"
                :placeholder="t('event.startTimePlaceholder')" size="small" />

              <div class="separator"></div>

              <el-time-select v-model="hourParams.hourEnd" class="time-picker" :editable="false"
                :picker-options="{ start: '00:00', step: '01:00', end: '24:00', minTime: hourParams.hourStart }"
                :placeholder="t('event.endTimePlaceholder')" size="small" />
            </el-form-item>

            <el-form-item :label="t('field.channelName') + localeColon">
              <el-select v-model="hourParams.channelId" style="width: 160px;" size="small">
                <el-option v-for="item in channelList" :key="item.id" :label="item.name" :value="item.id"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item :label="t('event.algorithmService') + localeColon">
              <el-select v-model="hourParams.algorithmCode" style="width: 160px;" size="small">
                <el-option v-for="item in algorithmList" :key="item.algorithmId" :label="resolveResourceAlgorithmName(item)"
                  :value="item.algorithmId"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item class="search-btns">
              <el-button type="primary" @click="getData" size="small">{{ t('action.search') }}</el-button>
              <el-button @click="resetParams('hour')" size="small">{{ t('action.reset') }}</el-button>
            </el-form-item>
          </el-form>
        </el-tab-pane>

        <!--------- 天数据 --------->
        <el-tab-pane :label="t('event.dailyData')" name="day">
          <el-form inline label-position="left">

            <el-form-item :label="t('event.timeRange') + localeColon">
              <el-date-picker v-model="dayParams.dayRange" value-format="YYYY-MM-DD" :editable="false" type="daterange"
                :start-placeholder="t('event.startTimePlaceholder')" :end-placeholder="t('event.endTimePlaceholder')" :picker-options="pickerOptions" size="small">
              </el-date-picker>
            </el-form-item>

            <el-form-item :label="t('field.channelName') + localeColon">
              <el-select v-model="dayParams.channelId" style="width: 160px;" size="small">
                <el-option v-for="item in channelList" :key="item.id" :label="item.name" :value="item.id"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item :label="t('event.algorithmService') + localeColon">
              <el-select v-model="dayParams.algorithmCode" style="width: 160px;" size="small">
                <el-option v-for="item in algorithmList" :key="item.algorithmId" :label="resolveResourceAlgorithmName(item)"
                  :value="item.algorithmId"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item class="search-btns">
              <el-button type="primary" @click="getData" size="small">{{ t('action.search') }}</el-button>
              <el-button @click="resetParams('day')" size="small">{{ t('action.reset') }}</el-button>
            </el-form-item>

          </el-form>
        </el-tab-pane>

        <!--------- 月数据 --------->
        <el-tab-pane :label="t('event.monthlyData')" name="month">
          <el-form inline label-position="left">

            <el-form-item :label="t('event.timeRange') + localeColon">
              <el-date-picker v-model="monthParams.monthRange" type="monthrange" :start-placeholder="t('event.startMonth')"
                :end-placeholder="t('event.endMonth')" :picker-options="pickerOptions" size="small">
              </el-date-picker>
            </el-form-item>

            <el-form-item :label="t('field.channelName') + localeColon">
              <el-select v-model="monthParams.channelId" style="width: 160px;" size="small">
                <el-option v-for="item in channelList" :key="item.id" :label="item.name" :value="item.id"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item :label="t('event.algorithmService') + localeColon">
              <el-select v-model="monthParams.algorithmCode" style="width: 160px;" size="small">
                <el-option v-for="item in algorithmList" :key="item.algorithmId" :label="resolveResourceAlgorithmName(item)"
                  :value="item.algorithmId"></el-option>
              </el-select>
            </el-form-item>

            <el-form-item class="search-btns">
              <el-button type="primary" @click="getData" size="small">{{ t('action.search') }}</el-button>
              <el-button @click="resetParams('month')" size="small">{{ t('action.reset') }}</el-button>
            </el-form-item>

          </el-form>
        </el-tab-pane>
      </el-tabs>
    </div>

    <!-- 统计摘要卡片 -->
    <div class="stats-overview" v-if="chartList.length">
      <div class="stat-card enter-card">
        <div class="stat-icon-wrapper">
          <svg viewBox="0 0 1024 1024" width="28" height="28" xmlns="http://www.w3.org/2000/svg"><path d="M512 85.333333c235.648 0 426.666667 191.018667 426.666667 426.666667s-191.018667 426.666667-426.666667 426.666667S85.333333 747.648 85.333333 512 276.352 85.333333 512 85.333333z m97.450667 453.674667H304.597333a36.650667 36.650667 0 1 0 0 73.301333h304.853334l-89.258667 89.258667a36.693333 36.693333 0 0 0 51.84 51.882667l150.357333-150.357334a36.565333 36.565333 0 0 0 0-51.84l-150.357333-150.357333a36.736333 36.736333 0 0 0-51.84 51.882667l89.258667 89.258666z" fill="#1890FF"/></svg>
        </div>
        <div class="stat-info">
          <div class="stat-label">{{ t('event.totalEnterFlow') }}</div>
          <div class="stat-value">{{ totalEnter }} <span class="stat-unit">{{ t('event.personUnit') }}</span></div>
        </div>
      </div>
      <div class="stat-card leave-card">
        <div class="stat-icon-wrapper">
          <svg viewBox="0 0 1024 1024" width="28" height="28" xmlns="http://www.w3.org/2000/svg"><path d="M512 85.333333A426.666667 426.666667 0 1 0 938.666667 512 426.666667 426.666667 0 0 0 512 85.333333zM719.402667 480.64a36.650667 36.650667 0 1 1 0 73.301333H414.549333l89.258667 89.258667a36.693333 36.693333 0 1 1-51.84 51.882667l-150.357333-150.357334a36.565333 36.565333 0 0 1 0-51.84l150.357333-150.357333a36.736333 36.736333 0 1 1 51.84 51.882667l-89.258667 89.258666h304.853334z" fill="#FFB440"/></svg>
        </div>
        <div class="stat-info">
          <div class="stat-label">{{ t('event.totalLeaveFlow') }}</div>
          <div class="stat-value">{{ totalLeave }} <span class="stat-unit">{{ t('event.personUnit') }}</span></div>
        </div>
      </div>
      <div class="stat-card net-card">
        <div class="stat-icon-wrapper">
          <svg viewBox="0 0 1024 1024" width="28" height="28" xmlns="http://www.w3.org/2000/svg"><path d="M512 85.333333c235.648 0 426.666667 191.018667 426.666667 426.666667s-191.018667 426.666667-426.666667 426.666667S85.333333 747.648 85.333333 512 276.352 85.333333 512 85.333333z m162.773333 362.496a36.650667 36.650667 0 0 0-51.84-51.882666L512 506.88l-110.933333-110.933333a36.650667 36.650667 0 0 0-51.84 51.84l136.832 136.832a36.565333 36.565333 0 0 0 51.84 0l136.874666-136.789334z" fill="#67C23A"/></svg>
        </div>
        <div class="stat-info">
          <div class="stat-label">{{ t('event.netInflowCount') }}</div>
          <div class="stat-value">{{ totalNet }} <span class="stat-unit">{{ t('event.personUnit') }}</span></div>
        </div>
      </div>
    </div>

    <div class="mv-wrap-body">
      <div class="mvtitle">
        <span class="mv-table-title">{{ t('event.flowTrendAndDetail') }}</span>
        <el-button type="primary" size="small" :disabled="!chart" @click="showData" plain>{{ t('event.chartToggleLabel') }}</el-button>
      </div>

      <div class="content-split" v-if="chartList.length">
        <div class="chart-section">
          <div ref="chartRef" class="chart"></div>
        </div>
        
        <div class="table-section">
          <el-table :data="chartList" height="400" stripe size="small" border class="data-table" :header-cell-style="{background:'#f0f4ff',color:'#333'}">
            <el-table-column prop="timeString" :label="t('event.tooltipTime')" min-width="140"></el-table-column>
            <el-table-column prop="enterNumber" :label="t('event.enterCount')" width="90" align="center"></el-table-column>
            <el-table-column prop="leaveNumber" :label="t('event.leaveCount')" width="90" align="center"></el-table-column>
            <el-table-column :label="t('event.netInflow')" width="90" align="center">
              <template #default="{ row }">
                <span :class="{'positive-flow': (row.enterNumber - row.leaveNumber) > 0, 'negative-flow': (row.enterNumber - row.leaveNumber) < 0}">
                  {{ (row.enterNumber - row.leaveNumber) > 0 ? '+' : '' }}{{ row.enterNumber - row.leaveNumber }}
                </span>
              </template>
            </el-table-column>
          </el-table>
        </div>
      </div>
      <el-empty v-else :description="t('event.noFlowData')"></el-empty>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, watch, onMounted, nextTick, computed, getCurrentInstance } from 'vue'
import moment from 'moment'
import * as echarts from 'echarts'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const { proxy } = getCurrentInstance()

const params = ref(null)
const timeGranularity = ref('hour') // 时间筛选粒度 'hour'、'day'、'week'、'month'
const channelList = ref([])
const chartList = ref([])

const totalEnter = computed(() => chartList.value.reduce((sum, item) => sum + item.enterNumber, 0))
const totalLeave = computed(() => chartList.value.reduce((sum, item) => sum + item.leaveNumber, 0))
const totalNet = computed(() => totalEnter.value - totalLeave.value)

// timeGranularity === 'hour'
const hourParams = reactive({
  hourDay: moment(new Date()).format('YYYY-MM-DD'), //时数据日期选择器
  hourStart: '00:00', // 时数据开始时间
  hourEnd: '24:00', // 时数据结束时间
  channelId: '',
  algorithmCode: ''
})

// timeGranularity === 'day'
const dayParams = reactive({
  dayRange: [], // 天数据开始和结束时间数组
  channelId: '',
  algorithmCode: ''
})

// timeGranularity === 'month'
const monthParams = reactive({
  monthRange: [], // 月数据开始和结束时间数组
  channelId: '',
  algorithmCode: ''
})

const pickerOptions = {
  firstDayOfWeek: 1,
  disabledDate(time) {
    return time.getTime() > Date.now()
  }
}

const chart = ref(null)
const chartRef = ref(null)
const algorithmList = ref([])

// Watch timeGranularity
watch(timeGranularity, (newVal) => {
  switch (newVal) {
    case 'hour':
      resetDayParams()
      resetMonthParams()
      break
    case 'day':
      resetHourParams()
      resetMonthParams()
      break
    case 'month':
      resetHourParams()
      resetDayParams()
      break
  }
  if (newVal) {
    chartList.value = []
  }
})

// Methods
const resetParams = (timeGranularityType) => {
  switch (timeGranularityType) {
    case 'hour':
      Object.assign(hourParams, {
        hourDay: moment(new Date()).format('YYYY-MM-DD'),
        hourStart: '00:00',
        hourEnd: '24:00',
        channelId: '',
        algorithmCode: ''
      })
      break
    case 'day':
      Object.assign(dayParams, {
        dayRange: [],
        channelId: '',
        algorithmCode: ''
      })
      break
    case 'month':
      Object.assign(monthParams, {
        monthRange: [],
        channelId: '',
        algorithmCode: ''
      })
      break
  }
}

// 获取通道列表
const getChannelList = () => {
  proxy.$API.getChannelList({ pageNum: 1, pageSize: 1000 }).then((res) => {
    const { resData } = res
    const list = resData.rows.map((item) => {
      return {
        name: item.channelName,
        id: item.videoChannelId
      }
    })
    channelList.value = [...list]
  })
}

// 流量算法列表查询
const queryPassFlowList = () => {
  proxy.$API.queryPassFlowList().then((res) => {
    const { resData } = res
    algorithmList.value = resData.list || []
  })
}

const validateParams = () => {
  switch (timeGranularity.value) {
    case 'hour':
      {
        if (!hourParams.hourDay) {
          proxy.$message.warning(t('event.selectDateWarning'))
          return false
        }

        if (!hourParams.hourStart || !hourParams.hourEnd) {
          proxy.$message.warning(t('event.selectTimeRangeWarning'))
          return false
        }

        if (!hourParams.channelId) {
          proxy.$message.warning(t('event.selectChannelWarning'))
          return false
        }

        if (!hourParams.algorithmCode) {
          proxy.$message.warning(t('event.selectAlgorithmWarning'))
          return false
        }

        params.value = {
          type: 1,
          channelId: hourParams.channelId,
          algorithmCode: hourParams.algorithmCode,
          startTime: `${hourParams.hourDay} ${hourParams.hourStart}:00`
        }

        if (hourParams.hourEnd === '24:00') {
          params.value.endTime = `${moment(hourParams.hourDay)
            .add(1, 'days')
            .format('YYYY-MM-DD')} 00:00:00`
        } else {
          params.value.endTime = `${hourParams.hourDay} ${hourParams.hourEnd}:00`
        }
      }
      break
    case 'day':
      {
        if (!dayParams.dayRange || !dayParams.dayRange.length) {
          proxy.$message.warning(t('event.selectTimeRangeWarning'))
          return false
        }

        if (!dayParams.channelId) {
          proxy.$message.warning(t('event.selectChannelWarning'))
          return false
        }

        if (!dayParams.algorithmCode) {
          proxy.$message.warning(t('event.selectAlgorithmWarning'))
          return false
        }

        params.value = {
          type: 2,
          channelId: dayParams.channelId,
          algorithmCode: dayParams.algorithmCode,
          startTime: `${dayParams.dayRange[0]} 00:00:00`,
          endTime: `${moment(dayParams.dayRange[1])
            .add(1, 'days')
            .format('YYYY-MM-DD')} 00:00:00`
        }
      }
      break
    case 'month':
      {
        if (
          !monthParams.monthRange ||
          !monthParams.monthRange.length
        ) {
          proxy.$message.warning(t('event.selectTimeRangeWarning'))
          return false
        }

        if (!monthParams.channelId) {
          proxy.$message.warning(t('event.selectChannelWarning'))
          return false
        }

        if (!monthParams.algorithmCode) {
          proxy.$message.warning(t('event.selectAlgorithmWarning'))
          return false
        }

        params.value = {
          type: 4,
          channelId: monthParams.channelId,
          algorithmCode: monthParams.algorithmCode,
          startTime: moment(monthParams.monthRange[0]).format(
            'YYYY-MM-DD HH:mm:ss'
          ),
          endTime: moment(monthParams.monthRange[1])
            .add(1, 'month')
            .format('YYYY-MM-DD HH:mm:ss')
        }
      }
      break
  }
  return true
}

const getData = () => {
  if (!validateParams()) return
  proxy.$API.queryPassengerFlowNumber(params.value).then((res) => {
    chartList.value = res.resData.numberList
    if (!chartList.value.length) return
    const timeData = []
    const inData = []
    const outData = []
    chartList.value.forEach((data) => {
      timeData.push(data.timeString)
      inData.push(data.enterNumber)
      outData.push(data.leaveNumber)
    })

    nextTick(() => {
      drawCharts(timeData, inData, outData)
    })
  })
}

const resetDayParams = () => {
  dayParams.algorithmCode = ''
  dayParams.channelId = ''
  dayParams.dayRange = []
}

const resetHourParams = () => {
  hourParams.algorithmCode = ''
  hourParams.channelId = ''
  hourParams.hourDay = moment(new Date()).format('YYYY-MM-DD')
  hourParams.hourStart = '00:00'
  hourParams.hourEnd = '24:00'
}

const resetMonthParams = () => {
  monthParams.algorithmCode = ''
  monthParams.channelId = ''
  monthParams.monthRange = []
}

const drawCharts = (timeData, inData, outData) => {
  chart.value = echarts.init(chartRef.value)
  const option = {
    tooltip: {
      trigger: 'item',
      padding: 0,
      backgroundColor: '#fff',
      extraCssText: 'opacity: 0.9',
      backgroundColor: 'rgba(255, 255, 255, 0.95)',
      borderColor: '#ebeef5',
      borderWidth: 1,
      padding: [12, 16],
      textStyle: {
        color: '#333'
      },
      extraCssText: 'box-shadow: 0 4px 16px rgba(0, 0, 0, 0.1); border-radius: 8px;',
      formatter: function(params) {
        if (!params) return ''
        const seriesName = params.seriesName === t('event.enter') ? t('event.enterCount') : t('event.leaveCount')
        return `
          <div style="font-weight: bold; margin-bottom: 8px; font-size: 14px; border-bottom: 1px solid #ebeef5; padding-bottom: 8px;">${t('event.tooltipTime')}: ${params.name}</div>
          <div style="display: flex; align-items: center; font-size: 13px;">
            ${params.marker}
            <span style="flex: 1; margin-right: 24px; color: #666;">${seriesName}:</span>
            <span style="font-weight: 600; font-size: 16px; color: ${params.color};">${params.value} <span style="font-size: 12px; font-weight: normal; color: #999;">${t('event.personUnit')}</span></span>
          </div>
        `
      }
    },
    legend: {
      data: [t('event.enter'), t('event.leave')],
      top: 8,
      left: 'center',
      itemGap: 28,
      textStyle: {
        color: '#666'
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      top: 56,
      bottom: '3%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      data: timeData,
      axisLine: {
        lineStyle: {
          color: '#EBEEF5'
        }
      },
      axisTick: {
        show: false
      },
      axisLabel: {
        color: '#999'
      },
      axisPointer: {
        lineStyle: {
          color: '#EBEEF5'
        }
      }
    },
    yAxis: {
      type: 'value',
      axisLine: {
        show: false
      },
      axisTick: {
        show: false
      },
      axisLabel: {
        color: '#999'
      },
      splitLine: {
        lineStyle: {
          type: 'dashed'
        }
      }
    },
    series: [
      {
        name: t('event.enter'),
        type: 'line',
        data: inData,
        label: {
          color: '#666'
        },
        itemStyle: {
          color: '#1890FF'
        },
        lineStyle: {
          color: '#1890FF'
        }
      },
      {
        name: t('event.leave'),
        type: 'line',
        data: outData,
        label: {
          color: '#666'
        },
        itemStyle: {
          color: '#FFB440'
        },
        lineStyle: {
          color: '#FFB440'
        }
      }
    ]
  }
  chart.value.setOption(option)
}

const showData = () => {
  const option = chart.value.getOption()
  for (let i = 0; i < option.series.length; i++) {
    const element = option.series[i]
    element.label.show = !element.label.show
  }

  chart.value.setOption(option)
}

// Lifecycle
onMounted(() => {
  getChannelList()
  queryPassFlowList()
})
</script>

<style scoped lang="scss">
.mv-wrap-top {
  margin-bottom: 20px;
  background: white;
}

.mv-wrap-body {
  background: white;
}

.tabs {
  padding: 0 30px;

  .time-picker {
    width: 125px;
  }

  .separator {
    display: inline-block;
    vertical-align: middle;
    width: 12px;
    height: 1px;
    background-color: #dcdfe6;
    margin: 0 10px;
  }

  .search-btns {
    margin: 0 !important;
    float: right;
    padding-bottom: 22px;
  }
}

.chart {
  height: 400px;
}

.no-data {
  color: #999;
  text-align: center;
  height: 60px;
  line-height: 60px;
  font-size: 14px;
}

.mvtitle {
  display: flex;
  align-items: center;
  height: 57px;
  padding: 0 30px 0 20px;

  .mv-table-title {
    flex: 1;
  }
}

.stats-overview {
  display: flex;
  gap: 20px;
  margin-bottom: 20px;
  
  .stat-card {
    flex: 1;
    background: #fff;
    border-radius: 8px;
    padding: 24px;
    display: flex;
    align-items: center;
    box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.05);
    transition: all 0.3s ease;
    
    &:hover {
      box-shadow: 0 4px 16px 0 rgba(0, 0, 0, 0.1);
      transform: translateY(-2px);
    }

    .stat-icon-wrapper {
      width: 56px;
      height: 56px;
      border-radius: 12px;
      display: flex;
      align-items: center;
      justify-content: center;
      margin-right: 20px;
    }
    
    &.enter-card .stat-icon-wrapper {
      background: rgba(24, 144, 255, 0.1);
    }
    
    &.leave-card .stat-icon-wrapper {
      background: rgba(255, 180, 64, 0.1);
    }
    
    &.net-card .stat-icon-wrapper {
      background: rgba(103, 194, 58, 0.1);
    }

    .stat-info {
      flex: 1;
      
      .stat-label {
        font-size: 14px;
        color: #909399;
        margin-bottom: 8px;
      }
      
      .stat-value {
        font-size: 28px;
        font-weight: 600;
        color: #303133;
        line-height: 1;
        
        .stat-unit {
          font-size: 14px;
          color: #909399;
          font-weight: normal;
          margin-left: 2px;
        }
      }
    }
  }
}

.content-split {
  display: flex;
  gap: 20px;
  padding: 0 20px 20px;

  .chart-section {
    flex: 1;
    min-width: 0;
    border: 1px solid #ebeef5;
    border-radius: 4px;
    padding: 16px;
  }
  
  .table-section {
    width: 400px;
    border: 1px solid #ebeef5;
    border-radius: 4px;
    padding: 16px;
    background: #fff;
    
    .data-table {
      width: 100%;
    }
    
    .positive-flow {
      color: #F56C6C;
      font-weight: bold;
    }
    
    .negative-flow {
      color: #67C23A;
    }
  }
}

.week-picker {
  width: 180px;
}
</style>
