<template>
	<div class="dwt-container" ref="containerRef">
		<table class="dwt-table">
			<thead>
				<tr>
					<th rowspan="2">{{ t('boxOther.weekdayTime') }}</th>
					<th colspan="24">00:00 - 12:00</th>
					<th colspan="24">12:00 - 24:00</th>
				</tr>
				<tr>
					<td v-for="(_item, index) in new Array(24)" colspan="2" :key="index">
						<div class="dwt-table-col">
							<span>{{ index }}</span>
						</div>
					</td>
				</tr>
			</thead>
			<tbody>
				<tr v-for="item in data" :key="item.row">
					<td>{{ item.name }}</td>
					<td v-for="fragment in item.fragments" :key="`${fragment.row}-${fragment.col}`" :data-row="fragment.row" :data-col="fragment.col" class="unselect" @mouseenter="cellEnter" @mousedown="cellDown($event, fragment)"></td>
				</tr>
				<tr>
					<td colspan="49" class="dwt-select-td">
						<div class="dwt-select-title">{{ props.modelValue.length ? t('boxOther.selectedTimeRange') : t('boxOther.dragToSelectTimeRange') }}</div>
						<div class="dwt-select-list">
							<template v-for="item in showSelectInfo">
								<div v-if="item.fragments.length" :key="item.weekValue" class="dwt-select-item">
									<div class="dwt-select-label">{{ item.weekName }}{{ localeColon }}</div>
									<div class="dwt-sleect-info">
										<span @click="openTimeConfig(el)" v-for="(el, i) in item.fragments" :key="i">{{ el.begin }}~{{ el.end }}{{ i == item.fragments.length - 1 ? '' : '、' }}</span>
									</div>
								</div>
							</template>
						</div>
					</td>
				</tr>
			</tbody>
		</table>
		<div v-show="canSelect" class="dwt-layer" :style="{ width: layer.width + 'px', height: layer.height + 'px', left: layer.left + 'px', top: layer.top + 'px' }"></div>
		<!-- 时间调整弹窗 -->
		<el-dialog :title="t('boxOther.timeRangeConfig')" v-model="timeConfig.show" width="400px" center append-to-body>
			<el-form ref="timeConfigForm" :model="timeConfig.form" :rules="timeConfig.rules">
				<el-form-item prop="value">
					<el-time-picker
						is-range
						v-model="timeConfig.form.value"
						:start-placeholder="t('boxOther.startTime')"
						:end-placeholder="t('boxOther.endTime')"
						:default-value="timeConfig.form.value"
						value-format="HH:mm"
						format="HH:mm"
						:picker-options="{
							selectableRange: '00:00 - 23:59',
							format: 'HH:mm'
						}">
					</el-time-picker>
				</el-form-item>
			</el-form>
			<template #footer>
				<el-button type="primary" @click="timeConfigConfirm">{{ t('action.ok') }}</el-button>
				<el-button @click="timeConfig.show = false">{{ t('action.cancel') }}</el-button>
			</template>
		</el-dialog>
	</div>
</template>

<script setup>
import { ref, computed, watch, nextTick, onMounted, onBeforeUnmount } from 'vue'
import moment from 'moment'
import { t, localeColon } from '@/i18n'

const props = defineProps({
	modelValue: {
		type: Array,
		default: () => []
	}
})

const emit = defineEmits(['update:modelValue'])

// Week i18n key map — stable numeric IDs, display resolved at render time
const WEEK_I18N_MAP = {
	1: 'boxOther.monday',
	2: 'boxOther.tuesday',
	3: 'boxOther.wednesday',
	4: 'boxOther.thursday',
	5: 'boxOther.friday',
	6: 'boxOther.saturday',
	0: 'boxOther.sunday'
}

const initData = function () {
	const weekData = [
		{ value: 1 },
		{ value: 2 },
		{ value: 3 },
		{ value: 4 },
		{ value: 5 },
		{ value: 6 },
		{ value: 0 }
	]
	return weekData.map((item, index) => {
		const weekName = t(WEEK_I18N_MAP[item.value])
		return {
			name: weekName,
			value: item.value,
			row: index,
			fragments: (function () {
				let arr = []
				for (let i = 0; i < 48; i++) {
					const startTime = moment('2022-01-01 00:00').add(i * 30, 'minutes')
					const endTime = moment('2022-01-01 00:00').add((i + 1) * 30 - (i == 47 ? 1 : 0), 'minutes')
					let o = {
						weekName: weekName,
						weekValue: item.value,
						row: index,
						col: i,
						begin: startTime.format('HH:mm'),
						end: endTime.format('HH:mm')
					}
					arr.push(o)
				}
				return arr
			})()
		}
	})
}

// Data — use computed so week names update on locale change
const data = computed(() => initData())
const canSelect = ref(false)
const layer = ref({
	firstTop: 0,
	firstLeft: 0,
	left: 0,
	top: 0,
	width: 0,
	height: 0,
	fragment: null
})
const timeConfig = ref({
	show: false,
	index: -1,
	rules: {
		value: [
			{
				trigger: 'blur',
				validator: (_rule, val, callback) => {
					if (val[0] == val[1]) {
						return callback(new Error(t('boxOther.startEndTimeSame')))
					}
					callback()
				}
			}
		]
	},
	form: {
		value: []
	}
})
const timeConfigForm = ref(null)
const containerRef = ref(null)

// Computed
const showSelectInfo = computed(() => {
	return data.value.map(day => {
		return {
			weekName: day.name,
			weekValue: day.value,
			fragments: props.modelValue.filter(item => {
				return item.week == day.value
			})
		}
	})
})

// Watch
watch(() => props.modelValue, () => {
	nextTick(() => {
		if (!containerRef.value) return
		
		const compareResult = compare(props.modelValue)
		data.value.forEach(day => {
			day.fragments.forEach(fragment => {
				const elm = containerRef.value.querySelector(`[data-row="${fragment.row}"][data-col="${fragment.col}"]`)
				if (!elm) return
				
				const result = compareResult.find(item => {
					return item.row == fragment.row && item.col == fragment.col
				})
				
				if (result) {
					elm.style.background = getBackground(result)
				} else {
					elm.style.background = ''
				}
			})
		})
	})
}, { immediate: true, deep: true })

// Methods
const openTimeConfig = (el) => {
	const index = props.modelValue.findIndex(item => {
		return item.week == el.week && item.begin == el.begin && item.end == el.end
	})
	timeConfig.value.index = index
	timeConfig.value.form.value = [el.begin, el.end]
	if (timeConfigForm.value) {
		timeConfigForm.value.clearValidate()
	}
	timeConfig.value.show = true
}

const timeConfigConfirm = () => {
	timeConfigForm.value.validate(valid => {
		if (!valid) {
			return
		}
		const el = props.modelValue[timeConfig.value.index]
		const o = {
			week: el.week,
			begin: timeConfig.value.form.value[0],
			end: timeConfig.value.form.value[1]
		}
		let selectFragments = clone(props.modelValue)
		selectFragments.splice(timeConfig.value.index, 1, o)
		selectFragments = mergeSelectFragments(selectFragments)
		emit('update:modelValue', selectFragments)
		timeConfig.value.show = false
	})
}

const handleUnselectFragments = (unSelectFragments, selectFragments) => {
	let i = 0
	while (i < selectFragments.length) {
		let count = 0
		unSelectFragments.forEach(el => {
			const unSelectBegin = moment(`2022-01-01 ${el.begin}`)
			const unSelectEnd = moment(`2022-01-01 ${el.end}`)
			const begin = moment(`2022-01-01 ${selectFragments[i].begin}`)
			const end = moment(`2022-01-01 ${selectFragments[i].end}`)
			if (el.week == selectFragments[i].week) {
				if (begin.isSame(unSelectBegin) && end.isSame(unSelectEnd)) {
					selectFragments.splice(i, 1)
					count++
				} else if (begin.isSame(unSelectBegin) && end.isAfter(unSelectEnd)) {
					selectFragments[i].begin = el.end
					count++
				} else if (begin.isBefore(unSelectBegin) && end.isSame(unSelectEnd)) {
					selectFragments[i].end = el.begin
					count++
				} else if (begin.isSameOrBefore(unSelectBegin) && end.isSameOrAfter(unSelectEnd)) {
					const a = {
						week: selectFragments[i].week,
						begin: selectFragments[i].begin,
						end: el.begin
					}
					const b = {
						week: selectFragments[i].week,
						begin: el.end,
						end: selectFragments[i].end
					}
					selectFragments.splice(i, 1, a, b)
					count++
				}
			}
		})
		if (count == 0) {
			i++
		}
	}
	emit('update:modelValue', selectFragments)
}

const cellEnter = (event) => {
	if (!canSelect.value) {
		return
	}
	if (!containerRef.value) return
	
	if (layer.value.left + layer.value.width >= containerRef.value.offsetWidth) {
		return
	}
	if (layer.value.top + layer.value.height >= containerRef.value.offsetHeight) {
		return
	}
	const maxLeft = Math.max(layer.value.firstLeft, event.currentTarget.offsetLeft)
	const minLeft = Math.min(layer.value.firstLeft, event.currentTarget.offsetLeft)
	const maxTop = Math.max(layer.value.firstTop, event.currentTarget.offsetTop)
	const minTop = Math.min(layer.value.firstTop, event.currentTarget.offsetTop)
	layer.value.left = minLeft
	layer.value.top = minTop
	layer.value.width = event.currentTarget.offsetWidth + maxLeft - minLeft
	layer.value.height = event.currentTarget.offsetHeight + maxTop - minTop
}

const cellDown = (event, fragment) => {
	layer.value.left = event.currentTarget.offsetLeft
	layer.value.top = event.currentTarget.offsetTop
	layer.value.width = event.currentTarget.offsetWidth
	layer.value.height = event.currentTarget.offsetHeight
	layer.value.firstLeft = event.currentTarget.offsetLeft
	layer.value.firstTop = event.currentTarget.offsetTop
	layer.value.fragment = clone(fragment)
	canSelect.value = true
}

const cellUp = (event) => {
	if (!canSelect.value) {
		return
	}
	canSelect.value = false
	let row = event.target.getAttribute('data-row')
	let col = event.target.getAttribute('data-col')
	if (row && col) {
		row = Number(row)
		col = Number(col)
		const minRow = Math.min(row, layer.value.fragment.row)
		const maxRow = Math.max(row, layer.value.fragment.row)
		const minCol = Math.min(col, layer.value.fragment.col)
		const maxCol = Math.max(col, layer.value.fragment.col)
		let newSelectFragments = []
		data.value.forEach(day => {
			day.fragments.forEach(item => {
				if (item.row >= minRow && item.row <= maxRow && item.col >= minCol && item.col <= maxCol) {
					newSelectFragments.push({
						week: item.weekValue,
						begin: item.begin,
						end: item.end
					})
				}
			})
		})
		const isAllSelect = newSelectFragments.every(item => {
			return props.modelValue.some(el => {
				const selectBegin = moment(`2022-01-01 ${item.begin}`)
				const selectEnd = moment(`2022-01-01 ${item.end}`)
				const begin = moment(`2022-01-01 ${el.begin}`)
				const end = moment(`2022-01-01 ${el.end}`)
				return item.week == el.week && begin.isSameOrBefore(selectBegin) && end.isSameOrAfter(selectEnd)
			})
		})
		let selectFragments = clone(props.modelValue)
		if (isAllSelect) {
			handleUnselectFragments(mergeSelectFragments(newSelectFragments), selectFragments)
		} else {
			selectFragments = [...selectFragments, ...newSelectFragments]
			selectFragments = mergeSelectFragments(selectFragments)
			emit('update:modelValue', selectFragments)
		}
	}
}

const clone = (data) => {
	if (typeof data == 'object' && data) {
		return JSON.parse(JSON.stringify(data))
	}
	return data
}

const getBackground = (result) => {
	let background = ''
	if (result.left == 0 && result.right == 0) {
		background = '#598fe6'
	} else if (result.left == 0) {
		background = `linear-gradient(to right,#598fe6 0%, #598fe6 ${(1 - result.right / 30) * 100}%, #f5f5f5 ${(1 - result.right / 30) * 100}%, #f5f5f5 100%)`
	} else if (result.right == 0) {
		background = `linear-gradient(to right,#f5f5f5 0%, #f5f5f5 ${(result.left / 30) * 100}%, #598fe6 ${(result.left / 30) * 100}%, #598fe6 100%)`
	} else {
		background = `linear-gradient(to right,#f5f5f5 0%, #f5f5f5 ${(result.left / 30) * 100}%,#598fe6 ${(result.left / 30) * 100}%, #598fe6 ${(1 - result.right / 30) * 100}%, #f5f5f5 ${(1 - result.right / 30) * 100}%,#f5f5f5 100%)`
	}
	return background
}

const mergeSelectFragments = (selectFragments) => {
	selectFragments = selectFragments.sort((a, b) => {
		if (a.week > b.week) {
			return 1
		} else if (a.week < b.week) {
			return -1
		} else {
			return moment(`2022-01-01 ${a.begin}`).diff(moment(`2022-01-01 ${b.begin}`), 'minutes')
		}
	})
	let i = 0
	while (i < selectFragments.length - 1) {
		const a = selectFragments[i]
		const b = selectFragments[i + 1]
		if (a.week == b.week) {
			const beginA = moment(`2022-01-01 ${a.begin}`)
			const endA = moment(`2022-01-01 ${a.end}`)
			const beginB = moment(`2022-01-01 ${b.begin}`)
			const endB = moment(`2022-01-01 ${b.end}`)
			if (beginA.isSameOrBefore(beginB) && endA.isSameOrAfter(endB)) {
				selectFragments.splice(i + 1, 1)
				continue
			}
			if (beginB.isSameOrBefore(beginA) && endB.isSameOrAfter(endA)) {
				selectFragments.splice(i, 1)
				continue
			}
			if (beginB.isSameOrBefore(beginA) && endB.isSameOrAfter(beginA)) {
				a.begin = b.begin
				selectFragments.splice(i + 1, 1)
				continue
			}
			if (beginA.isSameOrBefore(beginB) && endA.isSameOrAfter(beginB)) {
				a.end = b.end
				selectFragments.splice(i + 1, 1)
				continue
			}
		}
		i++
	}
	return selectFragments
}

const compare = (selectFragments) => {
	let arr = []
	selectFragments.forEach(item => {
		const day = data.value.find(item2 => {
			return item.week == item2.value
		})
		if (!day) return
		
		const fragments = day.fragments
		const selectBegin = moment(`2022-01-01 ${item.begin}`)
		const selectEnd = moment(`2022-01-01 ${item.end}`)
		fragments.forEach(fragment => {
			const begin = moment(`2022-01-01 ${fragment.begin}`)
			const end = moment(`2022-01-01 ${fragment.end}`)
			const left = selectBegin.diff(begin, 'minutes')
			const right = end.diff(selectEnd, 'minutes')
			if (begin.isSameOrAfter(selectEnd) || end.isSameOrBefore(selectBegin)) {
				return
			}
			if (selectBegin.isSameOrAfter(begin) && selectBegin.isSameOrBefore(end)) {
				arr.push({
					...fragment,
					left: left,
					right: right < 0 ? 0 : right
				})
				return
			}
			if (selectEnd.isSameOrAfter(begin) && selectEnd.isSameOrBefore(end)) {
				arr.push({
					...fragment,
					left: left < 0 ? 0 : left,
					right: right
				})
				return
			}
			if (begin.isSameOrAfter(selectBegin) && end.isSameOrBefore(selectEnd)) {
				arr.push({
					...fragment,
					left: 0,
					right: 0
				})
				return
			}
		})
	})
	return arr
}

// Expose methods for parent component
defineExpose({
	mergeSelectFragments
})

onMounted(() => {
	window.addEventListener('mouseup', cellUp)
})

onBeforeUnmount(() => {
	window.removeEventListener('mouseup', cellUp)
})
</script>
<style lang="scss" scoped>
.dwt-container {
	display: block;
	width: 100%;
	min-width: 800px;
	position: relative;
	box-sizing: border-box;
}

.dwt-table {
	width: 100%;
	border-collapse: collapse;
	box-sizing: border-box;

	thead {
		th {
			vertical-align: inherit;
			font-weight: bold;

			&:first-child {
				width: 140px;
			}
		}
		.dwt-table-col {
			display: flex;
			justify-content: center;
			align-items: center;
			width: 100%;

			span {
				display: block;
				width: 16px;
			}
		}
	}

	tr,
	td,
	th {
		box-sizing: border-box;
		font-size: 12px;
		user-select: none;
		border: 1px solid #dee4f5;
		text-align: center;
		min-width: 6px;
		line-height: 1.8em;
		height: 30px;
	}

	tbody td {
		padding: 0;
		&.unselect {
			background: #f5f5f5;
		}
	}

	.dwt-select-td {
		padding: 0 10px;

		.dwt-select-title {
			display: block;
			width: 100%;
			padding: 10px 0;
			text-align: left;
			font-size: 14px;
		}

		.dwt-select-list {
			display: block;
			width: 100%;
			text-align: left;
			font-size: 14px;

			.dwt-select-item {
				display: flex;
				justify-content: flex-start;
				align-items: flex-start;
				width: 100%;
				margin-bottom: 15px;

				&:last-child {
					margin-bottom: 0;
				}

				.dwt-select-label {
					width: 110px;
					color: #999;
				}

				.dwt-sleect-info {
					display: flex;
					justify-content: flex-start;
					align-items: center;
					width: 100%;
					flex-wrap: wrap;

					& > span:hover {
						color: #00a6ff;
						cursor: pointer;
					}
				}
			}
		}
	}
}

.dwt-layer {
	position: absolute;
	left: 0;
	top: 0;
	z-index: 1;
	width: 0;
	height: 0;
	background: #598fe6;
	opacity: 0.6;
	pointer-events: none;
	transition: width 0.12s ease, height 0.12s ease, top 0.12s ease, left 0.12s ease;
}
</style>
