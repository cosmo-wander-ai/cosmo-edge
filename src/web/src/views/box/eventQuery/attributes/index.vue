<template>
  <div class="attribute-events">
    <el-alert :title="t('attributeAnalysis.trackingHint')" type="info" :closable="false" />
    <el-form inline class="query" @submit.prevent="search">
      <el-form-item :label="t('attributeAnalysis.scene')">
        <el-select v-model="sceneId" filterable @change="changeScene" style="width: 240px" :placeholder="t('attributeAnalysis.selectScene')">
          <el-option v-for="scene in scenes" :key="scene.algorithmId" :value="String(scene.algorithmId)" :label="scene.algorithmName" />
        </el-select>
      </el-form-item>
      <el-form-item :label="t('field.channelName')">
        <el-select v-model="channelIds" multiple filterable collapse-tags clearable style="width: 240px" :placeholder="t('common.all')">
          <el-option v-for="channel in channels" :key="channel.videoChannelId" :value="String(channel.videoChannelId)" :label="channel.channelName" />
        </el-select>
      </el-form-item>
      <el-form-item :label="t('common.date')"><el-date-picker v-model="times" type="datetimerange" :clearable="false" /></el-form-item>
      <el-form-item v-if="versions.length" :label="t('attributeAnalysis.version')">
        <el-select v-model="schemaId" style="width: 260px" @change="changeVersion"><el-option v-for="version in versions" :key="version.id" :value="version.id" :label="version.label" /></el-select>
      </el-form-item>
      <el-form-item v-for="definition in schema.attributes" :key="definition.key" :label="definition.name">
        <el-select v-model="filters[definition.key]" clearable style="width: 180px" :placeholder="t('common.all')">
          <el-option v-for="option in definition.options" :key="option.value" :value="`value:${option.value}`" :label="option.name" />
          <el-option value="status:unknown" :label="t('attributeAnalysis.unknown')" />
          <el-option value="status:failed" :label="t('attributeAnalysis.failed')" />
        </el-select>
      </el-form-item>
      <el-form-item><el-button type="primary" :disabled="!sceneId" :loading="loading" @click="search">{{ t('action.search') }}</el-button></el-form-item>
      <el-form-item><el-button :disabled="!lastQuery || !total || loading" :loading="exporting" @click="exportRecords">{{ t('event.dataExport') }}</el-button></el-form-item>
    </el-form>
    <el-empty v-if="!scenes.length" :description="t('attributeAnalysis.emptyScenes')" />
    <template v-else>
      <el-alert :title="t('attributeAnalysis.statsHint')" type="info" :closable="false" />
      <div class="statistics">
        <el-card v-for="definition in resultSchema.attributes" :key="definition.key" shadow="never">
          <template #header>{{ definition.name }}</template>
          <div v-for="entry in statisticsFor(definition)" :key="`${entry.status}:${entry.value}`" class="stat-row">
            <span>{{ statLabel(entry, definition) }}</span><strong>{{ entry.count }}</strong><span>{{ entry.status === 'valid' && entry.validCount ? `${(100 * entry.count / entry.validCount).toFixed(1)}%` : '—' }}</span>
          </div>
        </el-card>
      </div>
      <el-table :data="rows" v-loading="loading" border>
        <el-table-column prop="channelName" :label="t('field.channelName')" min-width="150" />
        <el-table-column :label="t('event.fullImage')" width="100"><template #default="{ row }"><el-image v-if="row.fullPicture" :src="row.fullPicture" :preview-src-list="[row.fullPicture]" preview-teleported style="width: 70px; height: 50px" fit="contain" /></template></el-table-column>
        <el-table-column :label="t('attributeAnalysis.firstSeen')" width="180"><template #default="{ row }">{{ date(row.record?.firstSeen) }}</template></el-table-column>
        <el-table-column :label="t('attributeAnalysis.lastSeen')" width="180"><template #default="{ row }">{{ date(row.record?.lastSeen) }}</template></el-table-column>
        <el-table-column v-for="definition in resultSchema.attributes" :key="definition.key" :label="definition.name" min-width="130"><template #default="{ row }">{{ display(row.record, definition) }}</template></el-table-column>
        <el-table-column :label="t('field.actions')" fixed="right" width="90"><template #default="{ row }"><el-button link type="primary" @click="detail = row">{{ t('action.details') }}</el-button></template></el-table-column>
      </el-table>
      <el-pagination :current-page="page" :page-size="20" :total="total" layout="total, prev, pager, next" @current-change="changePage" />
    </template>
    <el-dialog :model-value="!!detail" :title="t('attributeAnalysis.detail')" width="760px" @close="detail = null">
      <template v-if="detail?.record">
        <el-descriptions :column="1" border>
          <el-descriptions-item :label="t('field.channelName')">{{ detail.channelName }}</el-descriptions-item>
          <el-descriptions-item :label="t('attributeAnalysis.track')">{{ detail.record.trackId }}</el-descriptions-item>
          <el-descriptions-item :label="t('attributeAnalysis.firstSeen')">{{ date(detail.record.firstSeen) }}</el-descriptions-item>
          <el-descriptions-item :label="t('attributeAnalysis.lastSeen')">{{ date(detail.record.lastSeen) }}</el-descriptions-item>
        </el-descriptions>
        <el-table :data="detail.record.schema.attributes">
          <el-table-column prop="name" :label="t('attributeAnalysis.definition')" />
          <el-table-column :label="t('attributeAnalysis.value')"><template #default="{ row }">{{ display(detail.record, row) }}</template></el-table-column>
          <el-table-column :label="t('attributeAnalysis.samples')"><template #default="{ row }">{{ detailValue(row)?.samples || 0 }}</template></el-table-column>
          <el-table-column :label="t('attributeAnalysis.confidence')"><template #default="{ row }">{{ detailValue(row)?.status === 'valid' ? `${(detailValue(row).confidence * 100).toFixed(1)}%` : '—' }}</template></el-table-column>
        </el-table>
      </template>
    </el-dialog>
  </div>
</template>
<script setup>
import { computed, getCurrentInstance, onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import moment from 'moment'
import { t, currentLocale } from '@/i18n'
import { attributeDisplay, attributeNodes, attributeParam, attributeRecord, emptyAttributeSchema, parseAttributeSchema } from '@/utils/attributeAnalysis'
const { proxy } = getCurrentInstance()
const scenes = ref([]), channels = ref([]), sceneId = ref(''), channelIds = ref([])
const times = ref([moment().startOf('day').toDate(), moment().endOf('day').toDate()])
const currentSchemaId = ref('')
const currentSchema = ref(emptyAttributeSchema()), versions = ref([]), schemaId = ref(''), filters = ref({})
const rows = ref([]), statistics = ref([]), total = ref(0), page = ref(1), loading = ref(false), exporting = ref(false), detail = ref(null), lastQuery = ref(null)
const resultSchema = ref(emptyAttributeSchema())
const schema = computed(() => versions.value.find(v => v.id === schemaId.value)?.schema || currentSchema.value)
let sequence = 0
const date = value => Number.isFinite(value) ? moment(value).format('YYYY-MM-DD HH:mm:ss') : '—'
const display = (record, definition) => attributeDisplay(record, definition, t('attributeAnalysis.unknown'), t('attributeAnalysis.failed'), t('attributeAnalysis.notApplicable'))
const detailValue = definition => detail.value?.record?.attributes.find(a => a.key === definition.key)
const statisticsFor = definition => statistics.value.filter(s => s.key === definition.key && s.schemaId === schemaId.value)
const statLabel = (entry, definition) => entry.status === 'valid' ? definition.options.find(o => o.value === entry.value)?.name || entry.value : entry.status === 'failed' ? t('attributeAnalysis.failed') : t('attributeAnalysis.unknown')
function clearResults() { rows.value = []; statistics.value = []; total.value = 0; lastQuery.value = null; resultSchema.value = emptyAttributeSchema(); detail.value = null }
async function changeScene() {
  const ticket = ++sequence
  loading.value = false; clearResults(); versions.value = []; schemaId.value = ''; currentSchemaId.value = ''; filters.value = {}; currentSchema.value = emptyAttributeSchema()
  try {
    const response = await proxy.$API.algorithmLayoutDetail({ id: sceneId.value })
    if (ticket !== sequence) return
    const node = attributeNodes(response.resData?.algorithmProcessdata)[0]
    currentSchema.value = parseAttributeSchema(attributeParam(node, 'attributeSchema'))
    currentSchemaId.value = response.resData?.attributeSchemaId || ''
    schemaId.value = currentSchemaId.value
    await search()
  } catch { if (ticket === sequence) ElMessage.error(t('attributeAnalysis.queryFailed')) }
}
function changeVersion() { ++sequence; filters.value = {}; clearResults(); search() }
function queryParams() {
  return { algorithmCodes: [sceneId.value], categorys: ['12'], channelIds: [...channelIds.value], timeBegin: +times.value[0], timeEnd: +times.value[1], attributeSchemaId: schemaId.value, attributeFilters: Object.entries(filters.value).filter(([, value]) => value).map(([key, value]) => value.startsWith('value:') ? { key, value: value.slice(6), status: 'valid' } : { key, status: value.slice(7), value: '' }), includeAttributeSummary: true, pageSize: 20, pageNum: 1 }
}
async function load(query) {
  const ticket = ++sequence
  loading.value = true
  try {
    const { resData } = await proxy.$API.boxQueryEvent(query)
    if (ticket !== sequence) return
    const discovered = resData.attributeSummary?.schemas || []
    const known = new Map(versions.value.map(v => [v.id, v]))
    discovered.forEach(v => known.set(v.id, v))
    if (currentSchemaId.value) known.set(currentSchemaId.value, { id: currentSchemaId.value, schema: currentSchema.value })
    versions.value = [...known.values()].map(v => ({ ...v, label: `${v.id === currentSchemaId.value ? t('attributeAnalysis.current') : t('attributeAnalysis.history')} · ${v.id}` }))
    if (!query.attributeSchemaId && discovered.length) {
      schemaId.value = discovered[0].id
      await load({ ...query, attributeSchemaId: schemaId.value, attributeFilters: [], pageNum: 1 })
      return
    }
    rows.value = (resData.rows || []).map(row => ({ ...row, record: attributeRecord(row) }))
    statistics.value = resData.attributeSummary?.statistics || []
    resultSchema.value = schema.value
    total.value = resData.total || 0; page.value = query.pageNum; lastQuery.value = query
  } catch { if (ticket === sequence) { clearResults(); ElMessage.error(t('attributeAnalysis.queryFailed')) } }
  finally { if (ticket === sequence) loading.value = false }
}
function search() { if (sceneId.value) return load(queryParams()) }
function changePage(value) { if (lastQuery.value) load({ ...lastQuery.value, pageNum: value }) }
async function exportRecords() {
  exporting.value = true
  try {
    const response = await proxy.$API.boxExportAlarm({ ...lastQuery.value, includeAttributeSummary: false, pageNum: 1, pageSize: 0, language: currentLocale.value })
    const url = response.resData?.fileUrl
    if (!url) throw new Error('Missing export')
    const file = await fetch(url)
    if (!file.ok) throw new Error('Export unavailable')
    const objectUrl = URL.createObjectURL(await file.blob()), link = document.createElement('a')
    link.href = objectUrl; link.download = url.split('/').pop(); link.click(); URL.revokeObjectURL(objectUrl)
  } catch { ElMessage.error(t('event.fileFetchFailed')) }
  finally { exporting.value = false }
}
onMounted(async () => {
  try {
    const [sceneResponse, channelResponse] = await Promise.all([proxy.$API.algorithmInquire({ algorithmUsage: '1', algorithmCategory: '12', pageNum: 1, pageSize: 1000 }), proxy.$API.getChannelList({ pageNum: 1, pageSize: 1000 })])
    scenes.value = (sceneResponse.resData?.rows || []).filter(s => String(s.algorithmCategory) === '12')
    channels.value = channelResponse.resData?.rows || []
  } catch { ElMessage.error(t('attributeAnalysis.queryFailed')) }
})
</script>
<style scoped>
.attribute-events { padding: 16px; overflow: auto; }
.query { margin-top: 16px; }
.statistics { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; margin: 16px 0; }
.stat-row { display: flex; justify-content: space-between; gap: 12px; margin: 6px 0; }
.el-pagination { margin: 16px 0; justify-content: flex-end; }
</style>
