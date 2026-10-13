import assert from 'node:assert/strict'
import * as Vue from 'vue'
import * as echarts from 'echarts'
import moment from 'moment'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// Compile and mount the production SFC. Use ECharts' real SSR renderer so its
// option merge semantics, legend state and series data are not reimplemented.
const plain = value => JSON.parse(JSON.stringify(value))
const first = value => Array.isArray(value) ? value[0] : value
const text = node => [node.text, ...node.children.map(text)].join(' ')
const modernDark = {
  '--bg-white': '#20252E', '--text-primary': '#E8EBF1',
  '--text-secondary': '#ABB4C3', '--text-muted': '#929DAF',
  '--border-color': '#404957', '--border-light': '#333C49',
  '--primary-color': '#9B8CFF', '--warning-color': '#F4C471'
}
const classicLight = {
  '--bg-white': '#FFFFFF', '--text-primary': '#202631',
  '--text-secondary': '#57687B', '--text-muted': '#657489',
  '--border-color': '#D3DFEB', '--border-light': '#E4EBF3',
  '--primary-color': '#2875C7', '--warning-color': '#FFB440'
}
const classicDark = { ...modernDark, '--primary-color': '#83BEFF' }
const activePalette = appearance => appearance.palette === 'classic-blue'
  ? appearance.resolved === 'dark' ? classicDark : classicLight
  : modernDark
const fixtureRows = [
  { timeString: '08:00', enterNumber: 12, leaveNumber: 3 },
  { timeString: '09:00', enterNumber: 7, leaveNumber: 11 }
]

function assertColorMerge(call) {
  assert.equal(call.args.length, 0, 'theme update must use a normal merge, not replace/reset options')
  let leaves = 0
  const visit = (value, path = '') => {
    for (const [key, child] of Object.entries(value)) {
      const at = path ? `${path}.${key}` : key
      if (['color', 'backgroundColor', 'borderColor'].includes(key)) {
        leaves++
      } else if (child && typeof child === 'object') {
        visit(child, at)
      } else {
        assert.fail(`theme update contains a non-color option: ${at}`)
      }
    }
  }
  visit(call.option)
  assert.ok(leaves > 0, 'theme update must actually change chart colors')
}

const businessState = option => plain({
  xData: option.xAxis[0].data,
  series: option.series.map(series => ({
    name: series.name, type: series.type, data: series.data, labelShow: series.label.show
  })),
  selected: option.legend[0].selected,
  zoom: option.dataZoom?.map(zoom => ({ start: zoom.start, end: zoom.end }))
})

const chartColors = option => plain({
  tooltip: [first(option.tooltip).backgroundColor, first(option.tooltip).borderColor, first(option.tooltip).textStyle.color],
  legend: option.legend[0].textStyle.color,
  x: [option.xAxis[0].axisLine.lineStyle.color, option.xAxis[0].axisLabel.color],
  y: [option.yAxis[0].axisLabel.color, option.yAxis[0].splitLine.lineStyle.color],
  series: option.series.map(series => [series.label.color, series.itemStyle.color, series.lineStyle.color])
})

for (const rows of [[], fixtureRows]) {
  const appearance = Vue.shallowRef({ preference: 'light', resolved: 'light', palette: 'modern' })
  const calls = []
  const apiCalls = []
  const instances = []
  const documentElement = {}
  const view = await mountComponent('views/box/eventQuery/trafficStatistics/index.vue', {
    // Table rows are outside this check. Do not invoke Element Plus' scoped
    // column slot as an unknown DOM element without its row argument.
    wrap: component => ({
      ...component,
      components: { ...component.components, ElTableColumn: { render: () => null } }
    }),
    mocks: {
      moment: { default: moment },
      '@/utils/i18nResource': { resolveResourceAlgorithmName: item => item.name },
      '@/composables/useAppearance': {
        useAppearance: () => ({ appearance: Vue.readonly(appearance), setAppearance() {} })
      },
      echarts: {
        init(container) {
          assert.ok(container, 'the actual chart container must mount before initialization')
          const chart = echarts.init(null, null, { renderer: 'svg', ssr: true, width: 900, height: 400 })
          instances.push(chart)
          // A tiny adapter records calls while delegating all state to ECharts.
          return {
            setOption(option, ...args) {
              calls.push({ option: plain(option), args })
              chart.setOption(option, ...args)
            },
            getOption: () => chart.getOption(),
            isDisposed: () => chart.isDisposed()
          }
        }
      }
    },
    globals: {
      document: { documentElement },
      getComputedStyle(element) {
        assert.equal(element, documentElement)
        return { getPropertyValue: token => activePalette(appearance.value)[token] || '' }
      }
    },
    api: {
      async getChannelList() {
        apiCalls.push('channels')
        return { resData: { rows: [{ videoChannelId: 'fixture-channel', channelName: 'Fixture channel' }] } }
      },
      async queryPassFlowList() {
        apiCalls.push('algorithms')
        return { resData: { list: [{ algorithmId: 'fixture-flow', name: 'Fixture flow' }] } }
      },
      async queryPassengerFlowNumber(params) {
        apiCalls.push('flow')
        assert.equal(params.type, 1)
        assert.equal(params.channelId, 'fixture-channel')
        assert.equal(params.algorithmCode, 'fixture-flow')
        return { resData: { numberList: plain(rows) } }
      }
    },
    message: { warning(message) { assert.fail(`unexpected validation warning: ${message}`) } }
  })

  let unmounted = false
  try {
    const selects = view.all(node => node.type === 'el-select')
    selects[0].props.activate('fixture-channel')
    selects[1].props.activate('fixture-flow')
    await view.settle()
    const search = view.all(node => node.type === 'el-button' && text(node).includes('action.search'))[0]
    assert.ok(search, 'query uses the real rendered search button')
    search.props.onClick()
    await view.settle()
    assert.deepEqual(apiCalls, ['channels', 'algorithms', 'flow'])
    assert.equal(instances.length, rows.length ? 1 : 0)

    const chart = instances[0]
    let preserved
    let lightColors
    if (chart) {
      assert.deepEqual(plain(chart.getOption().series.map(series => series.data)), [[12, 7], [3, 11]])
      // Exercise an actual chart interaction and the production label toggle.
      chart.dispatchAction({ type: 'legendUnSelect', name: 'event.leave' })
      chart.setOption({ dataZoom: [{ type: 'inside', start: 10, end: 80 }] })
      const labels = view.all(node => node.type === 'el-button' && text(node).includes('event.chartToggleLabel'))[0]
      labels.props.onClick()
      await view.settle()
      preserved = businessState(chart.getOption())
      assert.equal(preserved.selected['event.leave'], false)
      assert.ok(preserved.series.every(series => series.labelShow === true))
      assert.deepEqual(preserved.zoom, [{ start: 10, end: 80 }])
      lightColors = chartColors(chart.getOption())
    }
    const reads = apiCalls.length
    let previous = { resolved: 'light', palette: 'modern' }
    for (const [preference, resolved, palette] of [
      ['light', 'light', 'classic-blue'], ['system', 'light', 'classic-blue'],
      ['dark', 'dark', 'classic-blue'], ['dark', 'dark', 'modern'],
      ['system', 'dark', 'modern'], ['system', 'light', 'modern'],
      ['light', 'light', 'modern'], ['dark', 'dark', 'modern'],
      ['dark', 'dark', 'classic-blue'], ['light', 'light', 'classic-blue'],
      ['light', 'light', 'modern']
    ]) {
      const before = calls.length
      appearance.value = { preference, resolved, palette }
      await view.settle()
      const added = calls.slice(before)
      const changed = resolved !== previous.resolved || palette !== previous.palette
      assert.equal(added.length, chart && changed ? 1 : 0,
        'only a resolved-mode or palette change updates an existing chart')
      added.forEach(assertColorMerge)
      assert.equal(instances.length, rows.length ? 1 : 0, 'switching theme must not initialize another chart')
      assert.equal(apiCalls.length, reads, 'switching theme must not make any API request')
      assert.equal(selects[0].props.modelValue, 'fixture-channel')
      assert.equal(selects[1].props.modelValue, 'fixture-flow')
      if (chart) {
        const option = chart.getOption()
        assert.deepEqual(businessState(option), preserved, 'data, legend selection, labels and zoom must survive')
        if (resolved === 'dark' || palette === 'classic-blue') {
          const colors = activePalette(appearance.value)
          assert.equal(first(option.tooltip).backgroundColor, colors['--bg-white'])
          assert.equal(option.xAxis[0].axisLabel.color, colors['--text-muted'])
          assert.equal(option.yAxis[0].splitLine.lineStyle.color, colors['--border-light'])
          assert.equal(option.series[0].lineStyle.color, colors['--primary-color'])
          assert.equal(option.series[1].lineStyle.color, colors['--warning-color'])
        } else {
          assert.deepEqual(chartColors(option), lightColors, 'light theme restores the original colors including grid lines')
        }
      }
      previous = { resolved, palette }
    }
    const beforeUnmount = calls.length
    view.unmount()
    unmounted = true
    appearance.value = { preference: 'dark', resolved: 'dark', palette: 'classic-blue' }
    await view.settle()
    assert.equal(calls.length, beforeUnmount, 'unmounted chart must no longer react to theme changes')
    assert.equal(apiCalls.length, reads)
  } finally {
    if (!unmounted) view.unmount()
    instances.forEach(chart => chart.dispose())
  }
}

console.log('Appearance chart behavior passed: real SFC + ECharts SSR, empty/populated results, color-only mode/palette merges, original modern/light restoration, retained data/legend/labels/zoom/filters, no chart recreation/API refetch and watcher cleanup')
