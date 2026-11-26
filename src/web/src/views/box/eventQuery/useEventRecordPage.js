import { computed, nextTick, ref } from 'vue'
import { currentLocale, t } from '@/i18n'

// Shared by the alarm, face/body, and vehicle record pages.
export function useEventRecordPage({ proxy, pageData, tableData, buildQuery, isGrid = () => false, onRemoved }) {
  const eventTableRef = ref(null)
  const selectedIds = ref([])
  const deleting = ref(false)
  let queryVersion = 0
  let suppressSelection = 0

  const allGridSelected = computed(() => tableData.value.length > 0 &&
    tableData.value.every(row => selectedIds.value.includes(row.id)))
  const someGridSelected = computed(() => selectedIds.value.length > 0 && !allGridSelected.value)

  const clearSelection = () => {
    selectedIds.value = []
    if (eventTableRef.value) {
      suppressSelection++
      try { eventTableRef.value.clearSelection() } finally { suppressSelection-- }
    }
  }

  const handleSelectionChange = rows => {
    if (!suppressSelection && !isGrid()) selectedIds.value = rows.map(row => row.id)
  }
  const handleGridSelect = (checked, row) => {
    if (deleting.value) return
    selectedIds.value = checked
      ? [...new Set([...selectedIds.value, row.id])]
      : selectedIds.value.filter(id => id !== row.id)
  }
  const handleGridSelectAll = checked => {
    if (!deleting.value) selectedIds.value = checked ? tableData.value.map(row => row.id) : []
  }

  const init = async (retainIds = []) => {
    const version = ++queryVersion
    try {
      const response = await proxy.$API.boxQueryEvent(buildQuery())
      if (version !== queryVersion) return false
      const rows = response.resData?.rows || []
      const total = response.resData?.total || 0
      const lastPage = Math.max(1, Math.ceil(total / pageData.pageSize))
      if (pageData.pageNum > lastPage) {
        pageData.pageNum = lastPage
        return init(retainIds)
      }
      suppressSelection++
      try {
        tableData.value = rows
        pageData.total = total
        await nextTick()
        if (version !== queryVersion) return false
        const retained = new Set(retainIds)
        const selected = rows.filter(row => retained.has(row.id))
        if (!isGrid() && eventTableRef.value) {
          eventTableRef.value.clearSelection()
          selected.forEach(row => eventTableRef.value.toggleRowSelection(row, true))
          await nextTick()
        }
        if (version !== queryVersion) return false
        selectedIds.value = selected.map(row => row.id)
      } finally {
        suppressSelection--
      }
      return true
    } catch {
      return false
    }
  }

  const handleSizeChange = size => {
    pageData.pageSize = size
    pageData.pageNum = 1
    clearSelection()
    init()
  }
  const handleCurrentChange = page => {
    if (page === pageData.pageNum) return
    pageData.pageNum = page
    clearSelection()
    init()
  }
  const searchList = () => {
    pageData.pageNum = 1
    clearSelection()
    init()
  }

  const handleExport = async () => {
    let result
    try {
      result = await proxy.$API.boxExportAlarm({
        ...buildQuery(),
        pageNum: 1,
        pageSize: 0,
        language: currentLocale.value
      })
    } catch {
      // The API client reports request errors.
      return
    }
    const fileUrl = result.resData?.fileUrl
    if (!fileUrl) {
      proxy.$message.error(t('event.fileExpired'))
      return
    }

    try {
      const response = await fetch(fileUrl)
      if (!response.ok) throw new Error(`HTTP ${response.status}`)
      const blob = await response.blob()
      const url = window.URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.style.display = 'none'
      link.href = url
      link.download = fileUrl.split('/').pop() || t('event.alarmRecordCsv')
      document.body.appendChild(link)
      try {
        link.click()
      } finally {
        link.remove()
        window.URL.revokeObjectURL(url)
      }
    } catch {
      proxy.$message.error(t('event.fileFetchFailed'))
    }
  }

  const deleteRecords = async values => {
    const ids = [...new Set(values)]
    if (!ids.length || deleting.value) return
    deleting.value = true
    try {
      try {
        const key = ids.length === 1 ? 'event.deleteConfirmSingle' : 'event.deleteConfirm'
        await proxy.$confirm(t(key, { n: ids.length }), t('common.notice'), { type: 'warning' })
      } catch {
        return
      }
      ++queryVersion
      let result
      try {
        result = (await proxy.$API.boxDeleteEvent({ ids })).resData
        if (!Array.isArray(result?.deletedIds) || !Array.isArray(result?.missingIds) ||
            !Array.isArray(result?.failedItems)) throw new Error('invalid deletion result')
        const returned = [...result.deletedIds, ...result.missingIds, ...result.failedItems.map(item => item?.id)]
        if (returned.length !== ids.length || new Set(returned).size !== ids.length ||
            returned.some(id => !ids.includes(id)) || result.failedItems.some(item => typeof item?.reason !== 'string')) {
          throw new Error('incomplete deletion result')
        }
      } catch {
        proxy.$message.error(t('event.deleteRequestFailed'))
        await init(ids)
        return
      }
      const removed = [...result.deletedIds, ...result.missingIds]
      onRemoved?.(removed)
      const summary = t('event.deleteResult', {
        deleted: result.deletedIds.length,
        missing: result.missingIds.length,
        failed: result.failedItems.length
      })
      if (result.failedItems.length) {
        const busy = result.failedItems.some(item => item.reason === 'RECORDING')
        const other = result.failedItems.some(item => item.reason !== 'RECORDING')
        const detail = [busy ? t('event.deleteRecording') : '', other ? t('event.deleteIncomplete') : '']
          .filter(Boolean).join(' ')
        proxy.$message[removed.length ? 'warning' : 'error'](`${summary} ${detail}`)
      } else {
        proxy.$message.success(summary)
      }
      if (!await init(result.failedItems.map(item => item.id))) proxy.$message.warning(t('event.deleteRefreshFailed'))
    } finally {
      deleting.value = false
    }
  }

  const handleDelete = row => deleteRecords([row.id])
  const handleBatchDelete = () => deleteRecords(selectedIds.value)
  const getIndex = index => (pageData.pageNum - 1) * pageData.pageSize + index + 1
  return { eventTableRef, selectedIds, deleting, allGridSelected, someGridSelected, clearSelection, init,
    handleSelectionChange, handleGridSelect, handleGridSelectAll, handleSizeChange, handleCurrentChange,
    searchList, handleExport, handleDelete, handleBatchDelete, getIndex }
}
