import assert from 'node:assert/strict'
import { insertNodeEdges } from '../src/utils/graphEdges.js'

function checkInsertion(edges, endpoints, expected, expectedCalls) {
  let calls = 0
  const result = insertNodeEdges(edges, endpoints, 'X', () => `edge-${++calls}`)
  assert.deepEqual(result, expected)
  assert.equal(calls, expectedCalls)
  return result
}

// Normal insertion inherits the type and creates the incoming edge first.
checkInsertion(
  [{ id: 'old', type: 'custom', source: 'A', target: 'B' }],
  { edgeId: 'old', source: 'A', target: 'B' },
  [
    { id: 'edge-1', type: 'custom', source: 'A', target: 'X' },
    { id: 'edge-2', type: 'custom', source: 'X', target: 'B' }
  ],
  2
)

// Frozen inputs remain intact; bypass edges retain their order and identity.
{
  const first = Object.freeze({ id: 'first', source: 'A', target: 'C' })
  const last = Object.freeze({ id: 'last', source: 'C', target: 'D' })
  const old = Object.freeze({
    id: 'old', type: 'custom', source: 'A', target: 'B',
    data: Object.freeze({ label: 'Old edge' }),
    style: Object.freeze({ color: 'red' }), sourceHandle: 'output'
  })
  const edges = Object.freeze([first, old, last])
  const result = checkInsertion(
    edges,
    { edgeId: 'old', source: 'A', target: 'B' },
    [first, last,
      { id: 'edge-1', type: 'custom', source: 'A', target: 'X' },
      { id: 'edge-2', type: 'custom', source: 'X', target: 'B' }],
    2
  )
  assert.notEqual(result, edges)
  assert.equal(result[0], first)
  assert.equal(result[1], last)
  assert.deepEqual(edges, [first, old, last])
}

// A missing source does not fall back to the old edge's source.
checkInsertion(
  [{ id: 'old', source: 'A', target: 'B' }],
  { edgeId: 'old' },
  [{ id: 'edge-1', type: 'action', source: 'X', target: 'B' }],
  1
)

// Explicit targets override; nullish targets fall back; an empty target stops.
for (const [target, expectedTarget] of [['C', 'C'], [null, 'B'], [undefined, 'B'], ['', null]]) {
  const expected = [{ id: 'edge-1', type: 'action', source: 'A', target: 'X' }]
  if (expectedTarget !== null) {
    expected.push({ id: 'edge-2', type: 'action', source: 'X', target: expectedTarget })
  }
  checkInsertion(
    [{ id: 'old', source: 'A', target: 'B' }],
    { edgeId: 'old', source: 'A', target },
    expected,
    expectedTarget === null ? 1 : 2
  )
}

// Missing old edges or types still permit explicit endpoints with action type.
{
  const other = { id: 'other', source: 'C', target: 'D' }
  for (const edges of [[other], [other, { id: 'old', source: 'A', target: 'B' }]]) {
    const result = checkInsertion(
      edges,
      { edgeId: 'old', source: 'A', target: 'B' },
      [other,
        { id: 'edge-1', type: 'action', source: 'A', target: 'X' },
        { id: 'edge-2', type: 'action', source: 'X', target: 'B' }],
      2
    )
    assert.equal(result[0], other)
  }
}

// With neither endpoint connectable, removal still returns a new array.
{
  const edges = [{ id: 'old', source: 'A', target: 'B' }]
  const result = checkInsertion(edges, { edgeId: 'old', target: '' }, [], 0)
  assert.notEqual(result, edges)
  assert.deepEqual(edges, [{ id: 'old', source: 'A', target: 'B' }])
}

console.log('Graph edge checks passed (6 groups).')
