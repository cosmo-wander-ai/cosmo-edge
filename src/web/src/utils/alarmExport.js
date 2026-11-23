// The server generates the CSV only for the authenticated HTTP request.
export async function downloadAlarmExport(params) {
  const token = localStorage.getItem('mtk') || ''
  const response = await fetch('/gtw/cwai/Event/DownloadAlarm', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', token, mtk: token },
    body: JSON.stringify(params)
  })
  if (!response.ok || (response.headers.get('content-type') || '').includes('application/json')) {
    throw new Error('Event export unavailable')
  }
  const blob = await response.blob()
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.style.display = 'none'
  link.href = url
  link.download = 'alarm-record.csv'
  document.body.appendChild(link)
  try {
    link.click()
  } finally {
    document.body.removeChild(link)
    window.URL.revokeObjectURL(url)
  }
}
