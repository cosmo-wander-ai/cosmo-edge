// Older devices omit alarmOutputs; keep their existing parameter form intact.
export function withAlarmOutputChoices(actions, outputs) {
  return actions.map(item => {
    const result = { ...item, actionType: item.actionType || item.businessCategory }
    if (item.id !== 'LA_AlarmOutput_Code' || !Array.isArray(outputs)) return result
    try {
      const params = JSON.parse(item.inputParamConfig)
      const choices = outputs.filter(output => output.status === 'ready').map(output => ({
        label: String(output.channel), value: String(output.channel)
      }))
      result.inputParamConfig = JSON.stringify(params.map(param => param.key === 'outputChannel'
        ? { ...param, type: 'select', options: choices, defaultValue: choices[0]?.value || '' }
        : param))
    } catch { /* Keep malformed resource handling in the existing form renderer. */ }
    return result
  })
}
