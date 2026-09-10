import { sha256Buffer, stableStringify } from './utils.js';

export const ACCURACY_EXECUTION_PROFILES = new Set(['full', 'quick']);

export function selectAccuracyCases(suite, {
  profile = 'full',
  caseIds = null,
  taskIds = null,
  tags = null,
} = {}) {
  if (!ACCURACY_EXECUTION_PROFILES.has(profile)) {
    throw new Error(`accuracy profile must be one of: ${[...ACCURACY_EXECUTION_PROFILES].join(', ')}`);
  }
  const caseSet = caseIds ? new Set(caseIds) : null;
  const taskSet = taskIds ? new Set(taskIds) : null;
  const tagSet = tags ? new Set(tags) : null;
  const taskById = new Map(suite.tasks.map((task) => [task.id, task]));
  const selected = suite.cases
    .map((item, index) => ({ ...item, _index: index }))
    .filter((item) => !caseSet || caseSet.has(item.id))
    .filter((item) => !taskSet || taskSet.has(item.task))
    .filter((item) => !tagSet || item.tags?.some((tag) => tagSet.has(tag)))
    .filter((item) => profile !== 'quick' || item.critical || item.tags?.includes('quick'))
    .sort((a, b) => {
      const ak = taskById.get(a.task)?.kind === 'vlm' ? 1 : 0;
      const bk = taskById.get(b.task)?.kind === 'vlm' ? 1 : 0;
      return ak - bk || a._index - b._index;
    })
    .map(({ _index, ...item }) => item);
  if (!selected.length) {
    throw new Error(profile === 'quick'
      ? 'quick profile selected no cases; tag reviewed cases with "quick"'
      : 'accuracy run selected no cases');
  }
  return selected;
}

export function selectedCaseIdentity(cases) {
  return {
    count: cases.length,
    sha256: sha256Buffer(Buffer.from(stableStringify(cases.map((item) => ({
      id: item.id,
      task: item.task,
      sha256: item.sha256,
      expectation: item.expectation,
      observeSec: item.observeSec ?? null,
    }))))),
  };
}
