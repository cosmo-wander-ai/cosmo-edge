import assert from 'node:assert/strict'
import { test } from 'node:test'
import moment from 'moment'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// Compiles and mounts the real SFC. Only API transport and ECharts are faked.
// WEB_BEHAVIOR_SOURCE_ROOT can point at an independent candidate source tree.
const plain = value => JSON.parse(JSON.stringify(value))
const text = node => [node.text, ...node.children.map(text)].join(' ')
const row = (name, count) => ({ timeString: name, enterNumber: count, leaveNumber: 0 })
const response = (...rows) => ({ resData: { numberList: rows } })

async function fixture() {
  const requests = []
  const instances = []
  const warnings = []
  const view = await mountComponent('views/box/eventQuery/trafficStatistics/index.vue', {
    wrap: component => ({ ...component, components: { ...component.components, ElTableColumn: { render: () => null } } }),
    mocks: {
      moment: { default: moment },
      '@/utils/i18nResource': { resolveResourceAlgorithmName: item => item.name },
      echarts: { init() {
        const chart = { disposed: false, option: { yAxis: [{}], series: [] },
          setOption(option) { this.option = { ...this.option, ...option } },
          getOption() { return this.option }, isDisposed() { return this.disposed },
          dispose() { this.disposed = true }, clear() { this.option = {} } }
        instances.push(chart)
        return chart
      } }
    },
    globals: { document: { documentElement: {} }, getComputedStyle: () => ({ getPropertyValue: () => '' }) },
    api: {
      getChannelList: async () => ({ resData: { rows: [
        { videoChannelId: 'channel-a', channelName: 'Channel A' },
        { videoChannelId: 'channel-b', channelName: 'Channel B' }
      ] } }),
      queryPassFlowList: async () => ({ resData: { list: [{ algorithmId: 'flow', name: 'Flow' }] } }),
      queryPassengerFlowNumber(params) {
        let resolve
        const promise = new Promise(r => { resolve = r })
        requests.push({ params: plain(params), resolve })
        return promise
      }
    },
    message: { warning: value => warnings.push(value) }
  })
  const selects = () => view.all(n => n.type === 'el-select')
  const button = (name, i = 0) => view.all(n => n.type === 'el-button' && text(n).includes(name))[i]
  const data = () => plain(view.all(n => n.type === 'el-table')[0]?.props.data || [])
  const readyHour = async (channel = 'channel-a') => {
    selects()[0].props.activate(channel); selects()[1].props.activate('flow'); await view.settle()
  }
  const search = () => button('action.search').props.onClick()
  const reset = () => button('action.reset').props.onClick()
  return { view, requests, instances, warnings, selects, button, data, readyHour, search, reset }
}

test('reset clears displayed results and leaves required defaults unqueried', async () => {
  const f = await fixture()
  try {
    await f.readyHour(); f.search(); f.requests[0].resolve(response(row('old', 8))); await f.view.settle()
    assert.equal(f.data()[0]?.timeString, 'old')
    f.reset(); await f.view.settle()
    assert.deepEqual(f.data(), [], 'old chart, table and cards cannot remain under blank filters')
    assert.equal(f.instances[0]?.disposed, true, 'the previous chart instance is released')
    assert.equal(f.selects()[0].props.modelValue, '', 'channel returns to its unset default')
    assert.equal(f.selects()[1].props.modelValue, '', 'algorithm returns to its unset default')
    assert.equal(f.requests.length, 1, 'reset must not query without channel and algorithm')
    f.search(); await f.view.settle()
    assert.equal(f.requests.length, 1, 'blank required defaults must still prevent a query')
    assert.ok(f.warnings.length)
  } finally { f.view.unmount() }
})

test('reset invalidates an earlier response', async () => {
  const f = await fixture()
  try {
    await f.readyHour(); f.search(); f.reset(); await f.view.settle()
    f.requests[0].resolve(response(row('stale', 8))); await f.view.settle()
    assert.deepEqual(f.data(), [], 'late responses cannot restore results after reset')
  } finally { f.view.unmount() }
})

test('latest query wins when replies arrive out of order', async () => {
  const f = await fixture()
  try {
    await f.readyHour('channel-a'); f.search()
    await f.readyHour('channel-b'); f.search()
    f.requests[1].resolve(response(row('new', 2))); await f.view.settle()
    f.requests[0].resolve(response(row('old', 1))); await f.view.settle()
    assert.equal(f.data()[0]?.timeString, 'new', 'late old query must not replace the newer query')
  } finally { f.view.unmount() }
})

test('changing granularity invalidates the prior response', async () => {
  const f = await fixture()
  try {
    await f.readyHour(); f.search()
    const tabs = f.view.all(n => n.type === 'el-tabs')[0]
    tabs.props['onUpdate:modelValue']('day'); await f.view.settle()
    f.requests[0].resolve(response(row('hour-result', 8))); await f.view.settle()
    assert.deepEqual(f.data(), [], 'hour result must not appear under daily conditions')
  } finally { f.view.unmount() }
})
