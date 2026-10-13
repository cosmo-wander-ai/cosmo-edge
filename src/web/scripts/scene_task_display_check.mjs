import assert from 'node:assert/strict'
import { summarizeTaskChannels } from '../src/utils/sceneTaskDisplay.js'

const channel = (id, status, algorithmId = 'scene-a') => ({ videoChannelId: id, taskList: [{ algorithmId, status }] })
const states = summarizeTaskChannels({ total: 4, rows: [channel('a', 1), channel('b', -1), channel('c', 2), channel('d', 0), channel('a', 1)] })
assert.equal(states.complete, true)
assert.deepEqual(states.byAlgorithm['scene-a'], { total: 4, running: 1, paused: 1, error: 1, stopped: 1, unknown: 0 }, 'paused, failed and stopped associations must not count as running; duplicate channels count once')

const duplicateTasks = { videoChannelId: 'a', taskList: [{ algorithmId: 'scene-a', status: '1' }, { algorithmId: 'scene-a', status: '1' }, { algorithmId: 'scene-b', status: '0' }] }
const distinct = summarizeTaskChannels({ total: 1, rows: [duplicateTasks] })
assert.equal(distinct.byAlgorithm['scene-a'].total, 1)
assert.equal(distinct.byAlgorithm['scene-a'].running, 1)
assert.equal(distinct.byAlgorithm['scene-b'].stopped, 1)
const conflict = summarizeTaskChannels({ total: 1, rows: [channel('a', 1), channel('a', 2)] })
assert.equal(conflict.byAlgorithm['scene-a'].unknown, 1, 'conflicting duplicates do not establish a running state')
assert.equal(conflict.byAlgorithm['scene-a'].running, 0)

assert.equal(summarizeTaskChannels({ total: 501, rows: [channel('a', 1)] }).complete, false, 'a paginated subset must be marked as lower bounds')
assert.equal(summarizeTaskChannels({ rows: [channel('a', 1)] }).complete, false, 'missing total does not establish a complete result')
assert.equal(summarizeTaskChannels({ total: 2, rows: [channel('a', 1), channel('a', 1)] }).complete, false, 'duplicate rows cannot satisfy the advertised total')
assert.equal(summarizeTaskChannels({ total: 1, rows: [{ videoChannelId: 'a', taskList: Array.from({ length: 100 }, (_, i) => ({ algorithmId: `scene-${i}`, status: 1 })) }] }).complete, false, 'the backend task-list limit can hide associations')
assert.equal(summarizeTaskChannels({ total: 1, rows: [{ videoChannelId: 'a' }] }).complete, false)
assert.equal(summarizeTaskChannels({ total: 1, rows: [{ taskList: [{ algorithmId: 'scene-a', status: 1 }] }] }).complete, false)
assert.equal(summarizeTaskChannels(undefined).available, false, 'missing data is unknown, not a successful empty list')
assert.equal(summarizeTaskChannels({}).available, false)
assert.deepEqual(summarizeTaskChannels({ total: 0, rows: [] }), { available: true, complete: true, byAlgorithm: {} }, 'a verified empty result can establish zero associations')
assert.equal(summarizeTaskChannels({ total: 1, rows: [channel('a', undefined)] }).byAlgorithm['scene-a'].unknown, 1)
assert.equal(summarizeTaskChannels({ total: 1, rows: [channel('a', 'constructor')] }).byAlgorithm['scene-a'].unknown, 1)

console.log('Scene task display checks passed: task states, deduplication, and partial/failed lists')
