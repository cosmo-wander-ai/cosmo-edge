export function accuracyExitCode(summary) {
  return summary?.executionOutcome === 'COMPLETED' ? 0 : 2;
}

export function hasMeasuredFailures(summary) {
  return summary?.executionOutcome === 'COMPLETED'
    && (summary.cases ?? []).some((item) => item.status === 'FAIL');
}
