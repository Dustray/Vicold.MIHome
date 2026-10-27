<script setup lang="ts">
/**
 * 环境页 —— 仪表板（Dashboard）。
 *
 * 布局：左侧是纯读数的仪表板，右侧是 ControlCenter（可操作设备）。
 * 仪表板分四层：
 *   1. 仪表盘区：一块「全屋空气」卡片，内部按可用宽度自动切换排布：
 *      温湿度用大号弧形表盘，污染物用「分段刻度条 + 迷你趋势」指标块，
 *      两种仪表形式混搭；卡片宽度变化时列数与占比跟着变，不会留大片空白；
 *   2. 传感器钉子板：用户从任意设备挑读数钉在这里，逐条切「仪表指针 / 进度条」，可拖动排序；
 *   3. 房间矩阵：每个房间一格，点一下就切换下方曲线与右侧控制中心；
 *   4. 趋势区：选中房间（或全屋）的合并曲线
 */
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import ArcGauge from './ArcGauge.vue'
import MetricTile from './MetricTile.vue'
import PinTile from './PinTile.vue'
import TrendChart from './TrendChart.vue'
import ControlCenter from './ControlCenter.vue'
import type { Pt } from '../lib/history'
import { useDragSort } from '../lib/sortable'
import {
  isPinned,
  movePin,
  pinsOf,
  setAllPinStyle,
  setPinStyle,
  togglePin,
  type PinStyle
} from '../lib/sensorPins'
import {
  AQ_LABEL,
  COMFORT_COLOR,
  LEVEL_COLOR,
  LEVEL_LABEL,
  METRICS,
  METRIC_RANGE,
  comfortLevel,
  comfortText,
  fitRange,
  levelOf,
  mergeTrend,
  metricState,
  trendOf,
  type EnvAdjust,
  type EnvControl,
  type EnvSensor,
  type MetricKey,
  type RoomEnv,
  type SensorPinCandidate
} from '../lib/env'

const props = defineProps<{
  rooms: RoomEnv[]
  controls: EnvControl[]
  /** 可钉到传感器钉子板的读数（设备 + 指标） */
  sensors: SensorPinCandidate[]
  /** 当前家庭 id —— 钉子板按家庭分开存 */
  homeId: string
  loading?: boolean
  busy?: Record<string, boolean>
  /** 当前聚焦的房间（空 = 全屋），与控制中心共用 */
  roomFilter?: string
  /** 用户是否自己排过房间顺序（决定要不要显示「重置顺序」） */
  sorted?: boolean
}>()

const emit = defineEmits<{
  (e: 'toggle', c: EnvControl): void
  (e: 'adjust', c: EnvControl, adj: EnvAdjust, value: unknown): void
  (e: 'update:roomFilter', roomId: string): void
  (e: 'refresh'): void
  /** 拖动房间卡片排序，把结果交给 App.vue 落库 */
  (e: 'reorder-room', dragId: string, overId: string): void
  /** 恢复米家给的默认房间顺序 */
  (e: 'reset-order'): void
}>()

/**
 * 房间卡片拖拽排序。
 *
 * 只有卡片标题栏上的把手能拖，卡片本身点了还是「聚焦这个房间」。
 * 排序结果 emit 给上层，由上层统一写库（侧栏和环境页共用一份顺序）。
 */
const {
  dragId: roomDragId,
  overId: roomDragOver,
  onDragStart: roomDragStart,
  onDragOver: roomDragOverAt,
  onDrop: roomDrop,
  onDragEnd: roomDragEnd
} = useDragSort((dragId, overId) => emit('reorder-room', dragId, overId))

// ---------------------------------------------------------------- 仪表盘

/** 表盘指标定义：标签、小数位之外，量程统一取自 METRIC_RANGE */
interface DialMeta {
  key: MetricKey
  label: string
  decimals: number
  unit: string
}

interface DialDef extends DialMeta {
  min: number
  max: number
}

const DIAL_META: DialMeta[] = [
  { key: 't', label: '温度', decimals: 1, unit: '°C' },
  { key: 'h', label: '湿度', decimals: 0, unit: '%' },
  { key: 'pm25', label: 'PM2.5', decimals: 0, unit: '' },
  { key: 'pm10', label: 'PM10', decimals: 0, unit: '' },
  { key: 'hcho', label: '甲醛', decimals: 3, unit: '' },
  { key: 'tvoc', label: 'TVOC', decimals: 3, unit: '' },
  { key: 'co2', label: 'CO₂', decimals: 0, unit: 'ppm' },
  { key: 'aq', label: '空气质量', decimals: 0, unit: '级' }
]

const DIALS: DialDef[] = DIAL_META.map((m) => ({
  ...m,
  min: METRIC_RANGE[m.key].min,
  max: METRIC_RANGE[m.key].max
}))

/** 指标键 → RoomEnv / summary 上的字段名（温度湿度字段名跟指标键不一样） */
const FIELD: Record<MetricKey, string> = {
  t: 'temp',
  h: 'humidity',
  pm25: 'pm25',
  pm10: 'pm10',
  hcho: 'hcho',
  tvoc: 'tvoc',
  co2: 'co2',
  aq: 'aq'
}

/** 全屋汇总：温湿度取平均，污染物取最差 */
const summary = computed(() => {
  const temps = props.rooms.map((r) => r.temp).filter((v): v is number => v !== undefined)
  const hums = props.rooms.map((r) => r.humidity).filter((v): v is number => v !== undefined)
  const avg = (a: number[]) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : undefined)
  const worst = (key: MetricKey) => {
    const vals = props.rooms
      .map((r) => (r as unknown as Record<string, number | undefined>)[FIELD[key]])
      .filter((v): v is number => v !== undefined)
    return vals.length ? Math.max(...vals) : undefined
  }
  const levels = props.rooms.map((r) => r.level)
  return {
    temp: avg(temps),
    humidity: avg(hums),
    level: levels.length ? Math.max(...levels) : 0,
    pm25: worst('pm25'),
    pm10: worst('pm10'),
    hcho: worst('hcho'),
    tvoc: worst('tvoc'),
    co2: worst('co2'),
    aq: worst('aq'),
    rooms: props.rooms.length,
    sensors: props.rooms.reduce((a, r) => a + r.sensors.length, 0)
  }
})

/** 每个表盘要显示的值与配色 */
const dialData = computed(() =>
  DIALS.map((d) => {
    const raw = (summary.value as unknown as Record<string, number | undefined>)[FIELD[d.key]]
    // 读数越界（冬天零下 / 西晒 42°C / 刚装修甲醛爆表）时把量程扩到能正常显示
    const range = fitRange(d.key, raw)
    // 温湿度走双侧区间、污染物走档位，判定口径统一在 metricState 里
    const st = metricState(d.key, raw)
    const def = METRICS[d.key]
    return {
      ...d,
      min: range.min,
      max: range.max,
      value: raw,
      color: st.color,
      note: st.note,
      /**
       * 表盘中间刻度：必须是**量程中点**，因为 0°（弧顶）就是 frac=0.5 的位置。
       * 这里以前填的是舒适区中点（温度 22），指针在弧顶时实际是 20，
       * 刻度数字比真实位置低 2°；改成量程中点后数字与指针一致。
       */
      mid: (range.min + range.max) / 2,
      /** 指标块的分段阈值 */
      good: def.good,
      ok: def.ok,
      bad: def.bad
    }
  })
)

/** 温湿度走大号弧形表盘 */
const heroDials = computed(() => dialData.value.filter((d) => d.key === 't' || d.key === 'h'))

/** 污染物 + 空气质量走分段刻度条指标块 */
const tileDials = computed(() => dialData.value.filter((d) => d.key !== 't' && d.key !== 'h'))

// ---------------------------------------------------------------- 自适应布局

/**
 * 「全屋空气」卡片的排布档位。
 *
 * 卡片被拉宽时如果还用固定像素的表盘，两侧就会空一大片；
 * 这里直接量卡片实际宽度来切档：
 *   stack —— 窄（单列竖排，表盘限宽居中）
 *   dual  —— 中（温湿度并排 + 指标块三列）
 *   wide  —— 宽（左右分栏：温湿度在左，指标块在右）
 */
const clusterRef = ref<HTMLElement | null>(null)
const clusterW = ref(0)

type Layout = 'stack' | 'dual' | 'wide'
const layout = computed<Layout>(() => {
  const w = clusterW.value
  if (w < 520) return 'stack'
  if (w < 1000) return 'dual'
  return 'wide'
})

/** 指标块列数：跟着档位走，保证 6 个指标排成整齐的 2×3 / 3×2 */
const tileCols = computed(() => {
  if (layout.value === 'stack') return 2
  if (layout.value === 'dual') return 3
  return 3
})

let clusterRO: ResizeObserver | null = null
onMounted(() => {
  if (!clusterRef.value) return
  clusterRO = new ResizeObserver(() => {
    if (clusterRef.value) clusterW.value = clusterRef.value.clientWidth
  })
  clusterRO.observe(clusterRef.value)
  clusterW.value = clusterRef.value.clientWidth
})
onBeforeUnmount(() => clusterRO?.disconnect())

/** 在线传感器数量（离线设备的读数是缓存值，不参与统计） */
const onlineCount = computed(() =>
  props.rooms.reduce((a, r) => a + r.sensors.filter((s) => s.online).length, 0)
)

/** 一句人话摘要放在标题下面（空气质量的档位已经在指标块里了，这里不重复） */
const digest = computed(() => {
  const t = summary.value.temp
  const h = summary.value.humidity
  const bits: string[] = []
  if (t !== undefined) bits.push(`均温 ${t.toFixed(1)}°C`)
  if (h !== undefined) bits.push(`均湿 ${Math.round(h)}%`)
  return bits.join(' · ')
})

// ---------------------------------------------------------------- 房间矩阵

const focusRoom = computed(() => props.roomFilter || '')

function selectRoom(id: string) {
  emit('update:roomFilter', focusRoom.value === id ? '' : id)
}

/** 房间内的可控制设备数 */
function controlCount(roomId: string) {
  return props.controls.filter((c) => c.device.room_id === roomId).length
}

/** 房间格里的读数行 */
function roomMetrics(room: RoomEnv) {
  const out: { key: MetricKey; label: string; text: string; color: string }[] = []
  const push = (key: MetricKey, label: string, text: string) => {
    const v = (room as unknown as Record<string, number | undefined>)[FIELD[key]]
    const lv = levelOf(key, v)
    out.push({ key, label, text, color: lv === null ? '143,160,186' : LEVEL_COLOR[lv] })
  }
  push('pm25', 'PM2.5', room.pm25 === undefined ? '—' : String(room.pm25))
  push('pm10', 'PM10', room.pm10 === undefined ? '—' : String(room.pm10))
  push('hcho', '甲醛', room.hcho === undefined ? '—' : room.hcho.toFixed(3))
  push('co2', 'CO₂', room.co2 === undefined ? '—' : String(room.co2))
  return out
}

/** 房间温湿度配色 */
function roomTempColor(room: RoomEnv) {
  const lv = comfortLevel('t', room.temp)
  return lv === null ? '143,160,186' : COMFORT_COLOR[lv]
}

/** 房间湿度配色（房间卡片上的湿度条也用同一套档位） */
function roomHumColor(room: RoomEnv) {
  const lv = comfortLevel('h', room.humidity)
  return lv === null ? '143,160,186' : COMFORT_COLOR[lv]
}

// ---------------------------------------------------------------- 传感器钉子板

/** 钉子板按家庭分开存；家庭还没加载出来时不落库（避免写进空 key） */
const hid = computed(() => props.homeId || '')

const pins = computed(() => pinsOf(hid.value))

const candidateMap = computed(() => new Map(props.sensors.map((s) => [s.id, s])))

/** 钉子 → 渲染用的完整定义（量程、配色、评价词、迷你趋势） */
const pinnedTiles = computed(() => {
  const map = candidateMap.value
  const out: {
    pid: string
    style: PinStyle
    label: string
    device: string
    value?: number
    min: number
    max: number
    decimals: number
    unit: string
    color: string
    note: string
    band: [number, number] | null
    good: number
    ok: number
    bad: number
    online: boolean
    points: Pt[]
  }[] = []
  for (const p of pins.value) {
    const c = map.get(p.id)
    // 设备被删 / 换了家庭，读数就没了：跳过但不删记录，设备回来还能复原
    if (!c) continue
    const def = METRICS[c.key]
    // 与「全屋空气」表盘同一套规则：读数越界就把那一端扩出去，别让指针顶死
    const range = fitRange(c.key, c.value)
    const st = metricState(c.key, c.value)
    out.push({
      pid: p.id,
      style: p.style,
      label: def.label,
      device: c.device,
      value: c.value,
      min: range.min,
      max: range.max,
      decimals: def.decimals,
      unit: def.unit,
      color: st.color,
      note: st.note,
      band: def.band ?? null,
      good: def.good,
      ok: def.ok,
      bad: def.bad,
      online: c.online,
      // 24 小时：卡片上要看的是「一天里怎么变的」，三个时间刻度也是按这个跨度标
      points: trendOf(c.did, c.key, 24)
    })
  }
  return out
})

/** 表头「样式」开关的当前状态：全部一致才高亮，混用时都不亮 */
const uniformStyle = computed<PinStyle | ''>(() => {
  const list = pins.value
  if (!list.length) return ''
  const first = list[0].style
  return list.every((p) => p.style === first) ? first : ''
})

function applyAllStyle(style: PinStyle) {
  if (!hid.value) return
  setAllPinStyle(hid.value, style)
}

function applyPinStyle(pid: string, style: PinStyle) {
  if (!hid.value) return
  setPinStyle(hid.value, pid, style)
}

function removePin(pid: string) {
  if (!hid.value) return
  togglePin(hid.value, pid)
}

const {
  dragId: pinDragId,
  overId: pinDragOver,
  onDragStart: pinDragStart,
  onDragOver: pinDragOverAt,
  onDrop: pinDrop,
  onDragEnd: pinDragEnd
} = useDragSort((from, to) => {
  if (!hid.value) return
  // 传完整的钉子 id 列表：被跳过的（设备离线/已删）也要写回去，否则会丢
  movePin(hid.value, pins.value.map((p) => p.id), from, to)
})

const pickerOpen = ref(false)
const pickKw = ref('')

/**
 * 挑选面板按设备分组。
 *
 * 平铺列表里同一个设备名会重复十几遍（一台空气监测仪就有 6 个指标），
 * 分组后设备名只说一次，指标做成小胶囊，一屏能看完。
 */
const pickGroups = computed(() => {
  const kw = pickKw.value.trim().toLowerCase()
  const groups = new Map<
    string,
    { did: string; device: string; room: string; online: boolean; items: SensorPinCandidate[] }
  >()
  for (const c of props.sensors) {
    if (kw) {
      const hit =
        c.device.toLowerCase().includes(kw) ||
        c.room.toLowerCase().includes(kw) ||
        METRICS[c.key].label.toLowerCase().includes(kw)
      if (!hit) continue
    }
    let g = groups.get(c.did)
    if (!g) {
      g = { did: c.did, device: c.device, room: c.room, online: c.online, items: [] }
      groups.set(c.did, g)
    }
    g.items.push(c)
  }
  return [...groups.values()]
})

function isRowPinned(id: string) {
  return isPinned(hid.value, id)
}

function toggleRow(id: string) {
  if (!hid.value) return
  togglePin(hid.value, id)
}

/** 挑选面板里显示的读数文本 */
function candidateText(c: SensorPinCandidate) {
  if (c.value === undefined) return '—'
  const def = METRICS[c.key]
  return `${def.decimals > 0 ? c.value.toFixed(def.decimals) : Math.round(c.value)}${def.unit}`
}

// ---------------------------------------------------------------- 趋势区

const trendMetric = ref<MetricKey>('t')
const trendHours = ref(24)

const HOURS = [
  { v: 1, label: '1 时' },
  { v: 6, label: '6 时' },
  { v: 24, label: '24 时' },
  { v: 168, label: '7 天' }
]

/** 指标页签：只列出真正有数据的指标 */
const metricTabs = computed(() => {
  const keys: MetricKey[] = ['t', 'h', 'pm25', 'pm10', 'hcho', 'tvoc', 'co2']
  const has = (k: MetricKey) =>
    props.rooms.some(
      (r) => (r as unknown as Record<string, number | undefined>)[FIELD[k]] !== undefined
    )
  return keys.filter(has)
})

/**
 * 曲线数据。
 *
 * 聚焦某个房间时画该房间所有探头的合并曲线，
 * 未聚焦时画全屋合并曲线（按时间桶取平均，不是数组下标拼接）。
 */
const trendPoints = computed(() => {
  const room = props.rooms.find((r) => r.id === focusRoom.value)
  const dids = room
    ? room.sensors.map((s) => s.did)
    : props.rooms.flatMap((r) => r.sensors.map((s) => s.did))
  if (!dids.length) return []
  const merged = mergeTrend(dids, trendMetric.value, trendHours.value)
  return merged.length >= 2 ? merged : trendOf(dids[0], trendMetric.value, trendHours.value)
})

const trendDef = computed(() => METRICS[trendMetric.value])

/**
 * 全屋各污染物的迷你趋势（合并所有探头的同指标曲线）。
 *
 * 指标块上的迷你趋势线用；一次算完六个指标，避免每个块各算一遍。
 */
const sparks = computed<Partial<Record<MetricKey, Pt[]>>>(() => {
  const dids = props.rooms.flatMap((r) => r.sensors.map((s) => s.did))
  const out: Partial<Record<MetricKey, Pt[]>> = {}
  if (!dids.length) return out
  for (const k of ['pm25', 'pm10', 'hcho', 'tvoc', 'co2'] as MetricKey[]) {
    out[k] = mergeTrend(dids, k, trendHours.value)
  }
  return out
})

// ---------------------------------------------------------------- 传感器明细

const expanded = ref('')

function sensorMetrics(s: EnvSensor): MetricKey[] {
  const keys: MetricKey[] = []
  for (const k of ['t', 'h', 'pm25', 'pm10', 'hcho', 'tvoc', 'co2', 'aq'] as MetricKey[]) {
    if (s.reading[k] !== undefined) keys.push(k)
  }
  return keys
}

function sensorText(s: EnvSensor) {
  const parts: string[] = []
  for (const k of sensorMetrics(s)) {
    if (k === 'aq') parts.push(`空气 ${AQ_LABEL[Math.round(s.reading.aq as number)]}`)
    else {
      const m = METRICS[k]
      const v = s.reading[k] as number
      parts.push(`${m.label} ${m.decimals > 0 ? v.toFixed(m.decimals) : Math.round(v)}${m.unit}`)
    }
  }
  return parts.join(' · ')
}

function toggleExpand(did: string) {
  expanded.value = expanded.value === did ? '' : did
}

function sensorCurve(s: EnvSensor) {
  return trendOf(s.did, trendMetric.value, trendHours.value)
}
</script>

<template>
  <div class="env">
    <!-- ==================== 左：仪表板 ==================== -->
    <div class="dash">
      <!-- 仪表盘区 -->
      <section
        ref="clusterRef"
        class="cluster card"
        :class="`lay-${layout}`"
        :style="{ '--tone': LEVEL_COLOR[summary.level] }"
      >
        <div class="cluster-top">
          <div class="ct-main">
            <span class="ct-cap">全屋空气</span>
            <b class="ct-val">{{ LEVEL_LABEL[summary.level] }}</b>
            <span class="ct-digest">{{ digest }}</span>
          </div>
          <div class="ct-pills">
            <span class="pill">{{ summary.rooms }} 个房间</span>
            <span class="pill">{{ summary.sensors }} 个传感器</span>
            <span class="pill">{{ controls.length }} 台可控制</span>
          </div>
          <div class="ct-aq">
            <span>在线探头</span>
            <b>{{ onlineCount }}<i>/{{ summary.sensors }}</i></b>
          </div>
        </div>

        <div class="cluster-body">
          <!-- 温湿度：大号弧形表盘 -->
          <div class="hero">
            <div v-for="d in heroDials" :key="d.key" class="hero-cell">
              <ArcGauge
                :value="d.value"
                :min="d.min"
                :max="d.max"
                :mid="d.mid"
                :decimals="d.decimals"
                :unit="d.unit || METRICS[d.key].unit"
                :label="d.label"
                :color="d.color"
                :note="d.note"
                :band="METRICS[d.key].band ?? null"
              />
            </div>
          </div>

          <!-- 污染物：分段刻度条 + 迷你趋势 -->
          <div class="tiles" :style="{ '--cols': tileCols }">
            <MetricTile
              v-for="d in tileDials"
              :key="d.key"
              :label="d.label"
              :value="d.value"
              :min="d.min"
              :max="d.max"
              :decimals="d.decimals"
              :unit="d.unit || METRICS[d.key].unit"
              :color="d.color"
              :note="d.note"
              :good="d.good"
              :ok="d.ok"
              :bad="d.bad"
              :points="sparks[d.key] ?? []"
            />
          </div>
        </div>
      </section>

      <!-- 传感器钉子板 -->
      <section class="pins card">
        <div class="pins-head">
          <h3>传感器</h3>
          <span class="pins-count">
            {{ pinnedTiles.length ? `${pinnedTiles.length} 个读数` : '未钉任何读数' }}
          </span>
          <div v-if="pinnedTiles.length" class="ssw" role="group" aria-label="显示样式">
            <span class="ssw-cap">样式</span>
            <button
              class="ssb"
              :class="{ active: uniformStyle === 'gauge' }"
              title="全部改成仪表指针"
              @click="applyAllStyle('gauge')"
            >
              仪表指针
            </button>
            <button
              class="ssb"
              :class="{ active: uniformStyle === 'bar' }"
              title="全部改成进度条"
              @click="applyAllStyle('bar')"
            >
              进度条
            </button>
          </div>
          <button
            class="btn btn-ghost btn-sm pin-add"
            :aria-expanded="pickerOpen"
            @click="pickerOpen = !pickerOpen"
          >
            {{ pickerOpen ? '收起' : '+ 添加读数' }}
          </button>
        </div>

        <!-- 挑选面板：勾选即钉，避免跳到设备抽屉里一个个翻 -->
        <div v-if="pickerOpen" class="picker">
          <div class="pick-top">
            <input v-model="pickKw" class="pick-kw" type="text" placeholder="搜索设备 / 房间 / 指标" />
            <span class="pick-hint">勾选后钉到上面，可拖动排序</span>
          </div>
          <div class="pick-list">
            <div v-for="g in pickGroups" :key="g.did" class="pick-group">
              <div class="pg-head">
                <span class="pg-dev" :title="g.device">{{ g.device }}</span>
                <span class="pg-room">{{ g.room }}</span>
                <span v-if="!g.online" class="pg-off">离线</span>
              </div>
              <div class="pg-items">
                <label
                  v-for="c in g.items"
                  :key="c.id"
                  class="pick-item"
                  :class="{ on: isRowPinned(c.id) }"
                >
                  <input type="checkbox" :checked="isRowPinned(c.id)" @change="toggleRow(c.id)" />
                  <span class="pi-met">{{ METRICS[c.key].label }}</span>
                  <span class="pi-val">{{ candidateText(c) }}</span>
                </label>
              </div>
            </div>
            <div v-if="!pickGroups.length" class="pick-empty">
              {{ sensors.length ? '没有匹配的读数' : '还没读到任何传感器数值，先点右上角「刷新」采集一次' }}
            </div>
          </div>
        </div>

        <div v-if="pinnedTiles.length" class="pin-grid">
          <PinTile
            v-for="t in pinnedTiles"
            :key="t.pid"
            :class="{ dragging: pinDragId === t.pid, 'drop-target': pinDragOver === t.pid }"
            :label="t.label"
            :device="t.device"
            :value="t.value"
            :min="t.min"
            :max="t.max"
            :decimals="t.decimals"
            :unit="t.unit"
            :color="t.color"
            :note="t.note"
            :band="t.band"
            :good="t.good"
            :ok="t.ok"
            :bad="t.bad"
            :mode="t.style"
            :online="t.online"
            :points="t.points"
            @toggle-style="(s) => applyPinStyle(t.pid, s)"
            @remove="removePin(t.pid)"
            @dragstart="pinDragStart($event, t.pid)"
            @dragend="pinDragEnd()"
            @dragover="pinDragOverAt($event, t.pid)"
            @drop="pinDrop($event, t.pid)"
          />
        </div>
        <div v-else-if="!pickerOpen" class="pin-empty">
          还没有钉住的读数。点「+ 添加读数」，把任意设备上的温度、湿度、PM、甲醛、CO₂
          挑出来放在这里 —— 每条都能单独切「仪表指针 / 进度条」。
        </div>
      </section>

      <!-- 房间矩阵 -->
      <section class="matrix">
        <div class="sec-head">
          <h3>房间</h3>
          <div class="sec-actions">
            <button v-if="sorted" class="btn btn-ghost btn-sm" @click="emit('reset-order')">重置顺序</button>
            <button v-if="focusRoom" class="btn btn-ghost btn-sm" @click="selectRoom(focusRoom)">
              回到全屋
            </button>
          </div>
        </div>
        <div class="rooms">
          <article
            v-for="r in rooms"
            :key="r.id"
            class="rcard"
            :class="{
              active: focusRoom === r.id,
              dragging: roomDragId === r.id,
              'drop-target': roomDragOver === r.id
            }"
            :style="{ '--tone': LEVEL_COLOR[r.level] }"
            @click="selectRoom(r.id)"
            @dragover="roomDragOverAt($event, r.id)"
            @drop="roomDrop($event, r.id)"
          >
            <div class="rc-top" />
            <header class="rc-head">
              <span
                class="rc-grip"
                draggable="true"
                title="拖动调整房间顺序"
                @dragstart="roomDragStart($event, r.id)"
                @dragend="roomDragEnd()"
                @click.stop
              >
                <svg width="10" height="14" viewBox="0 0 10 14" fill="currentColor" aria-hidden="true">
                  <circle cx="2.5" cy="3" r="1.1" /><circle cx="7.5" cy="3" r="1.1" />
                  <circle cx="2.5" cy="7" r="1.1" /><circle cx="7.5" cy="7" r="1.1" />
                  <circle cx="2.5" cy="11" r="1.1" /><circle cx="7.5" cy="11" r="1.1" />
                </svg>
              </span>
              <span class="rc-name">{{ r.name }}</span>
              <span class="rc-badge" :style="{ '--c': LEVEL_COLOR[r.level] }">{{ LEVEL_LABEL[r.level] }}</span>
            </header>

            <div class="rc-read">
              <div class="rc-temp" :style="{ '--c': roomTempColor(r) }">
                <b>{{ r.temp === undefined ? '—' : r.temp.toFixed(1) }}</b>
                <em v-if="r.temp !== undefined">°C</em>
              </div>
              <div
                v-if="r.humidity !== undefined"
                class="rc-hum"
                :style="{ '--c': roomHumColor(r) }"
                :title="comfortText('h', r.humidity).text"
              >
                <span class="rc-hum-cap">湿度</span>
                <b>{{ r.humidity }}<em>%</em></b>
                <span class="rc-hum-bar"><i :style="{ width: `${r.humidity}%` }" /></span>
              </div>
              <span v-else class="rc-nosensor">无环境传感器</span>
            </div>

            <div class="rc-pols">
              <span
                v-for="m in roomMetrics(r)"
                :key="m.key"
                class="rc-pol"
                :class="{ none: m.text === '—' }"
                :style="{ '--c': m.color }"
              >
                <i>{{ m.label }}</i><b>{{ m.text }}</b>
              </span>
            </div>

            <footer class="rc-foot">
              <span class="rc-ctl">{{ controlCount(r.id) }} 台可控制</span>
              <span class="rc-sen">{{ r.sensors.length }} 传感器</span>
            </footer>
          </article>

          <div v-if="loading && !rooms.length" class="blank">正在读取环境数据…</div>
          <div v-else-if="!rooms.length" class="blank">当前家庭还没有环境传感器</div>
        </div>
      </section>

      <!-- 趋势区 -->
      <section class="card trend-box">
        <div class="trend-head">
          <h3>
            {{ focusRoom ? rooms.find((r) => r.id === focusRoom)?.name : '全屋' }} · 历史曲线
          </h3>
          <div class="trend-tabs">
            <button
              v-for="k in metricTabs"
              :key="k"
              class="tt"
              :class="{ active: trendMetric === k }"
              @click="trendMetric = k"
            >
              {{ k === 'aq' ? '空气' : METRICS[k].label }}
            </button>
          </div>
          <div class="hours">
            <button
              v-for="h in HOURS"
              :key="h.v"
              class="hb"
              :class="{ active: trendHours === h.v }"
              @click="trendHours = h.v"
            >
              {{ h.label }}
            </button>
          </div>
        </div>
        <TrendChart
          :points="trendPoints"
          :unit="trendDef.unit"
          :color="trendDef.color"
          :decimals="trendDef.decimals"
          :min-span="trendDef.scaleMax * 0.06"
          :band="trendDef.band ?? null"
          :height="150"
          :hours="trendHours"
          :range-label="`最近 ${trendHours >= 168 ? '7 天' : trendHours + ' 小时'}`"
        />

        <!-- 传感器明细 -->
        <div v-if="focusRoom" class="sens">
          <div
            v-for="s in rooms.find((r) => r.id === focusRoom)?.sensors || []"
            :key="s.did"
            class="sen"
            :class="{ off: !s.online, open: expanded === s.did }"
          >
            <button class="sen-row" @click="toggleExpand(s.did)">
              <span class="dot" :class="{ live: s.online }" />
              <span class="nm">{{ s.name }}</span>
              <span class="val">{{ sensorText(s) }}</span>
              <svg class="chev" viewBox="0 0 24 24" fill="none">
                <path d="M9 6l6 6-6 6" stroke="currentColor" stroke-width="2" stroke-linecap="round"
                  stroke-linejoin="round" />
              </svg>
            </button>
            <div v-if="expanded === s.did" class="sen-detail">
              <TrendChart
                :points="sensorCurve(s)"
                :unit="trendDef.unit"
                :color="trendDef.color"
                :decimals="trendDef.decimals"
                :min-span="trendDef.scaleMax * 0.06"
                :height="96"
                :hours="trendHours"
                :range-label="`最近 ${trendHours >= 168 ? '7 天' : trendHours + ' 小时'}`"
              />
            </div>
          </div>
        </div>
      </section>
    </div>

    <!-- ==================== 右：控制中心 ==================== -->
    <ControlCenter
      class="cc"
      :controls="controls"
      :busy="busy"
      :room-filter="focusRoom"
      @toggle="emit('toggle', $event)"
      @adjust="(c, a, v) => emit('adjust', c, a, v)"
      @update:room-filter="emit('update:roomFilter', $event)"
    />
  </div>
</template>

<style scoped>
.env {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 372px;
  /*
   * 行高锁死在容器高度内（minmax(0, 1fr)），否则 auto 行会被内容撑高，
   * 两栏的 overflow 就永远不触发、又变回整页滚。
   */
  grid-template-rows: minmax(0, 1fr);
  gap: 16px;
  height: 100%;
  min-height: 0;
}

.dash {
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-width: 0;
  min-height: 0;
  /* 左栏自己滚：外层 .env-wrap 已经不再滚 */
  overflow-y: auto;
  overscroll-behavior: contain;
  scrollbar-gutter: stable;
  padding-bottom: 24px;
}

/* 滚动容器里的卡片不能被 flex 压扁：默认 flex-shrink:1 会把它们挤成一条 */
.dash > * {
  flex: none;
}

/* ---------------- 仪表盘区 ---------------- */
.cluster {
  padding: 16px 20px 18px;
  border-radius: var(--radius-lg);
  background:
    radial-gradient(130% 150% at 0% 0%, rgba(47, 109, 246, 0.10) 0%, transparent 58%),
    var(--grad-surface);
  border: 1px solid var(--border);
  box-shadow: var(--shadow-md);
  position: relative;
  overflow: hidden;
}

.cluster::before {
  content: '';
  position: absolute;
  left: 0;
  top: 0;
  height: 3px;
  width: 100%;
  background: linear-gradient(90deg, var(--tone), transparent 78%);
  opacity: 0.9;
}

.cluster-top {
  display: flex;
  align-items: flex-start;
  gap: 18px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}

.ct-main {
  display: flex;
  flex-direction: column;
  line-height: 1.15;
  min-width: 0;
}

.ct-cap {
  font-size: 10.5px;
  color: var(--text-3);
  letter-spacing: 1.5px;
}

.ct-val {
  font-size: 22px;
  font-weight: 700;
  color: rgb(var(--tone));
  letter-spacing: 1px;
}

.ct-digest {
  margin-top: 3px;
  font-size: 11px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.ct-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  align-self: center;
}

.pill {
  font-size: 11px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 3px 10px;
  white-space: nowrap;
}

.ct-aq {
  margin-left: auto;
  align-self: center;
  text-align: right;
  padding-left: 18px;
  border-left: 1px solid var(--border);
}

.ct-aq span {
  display: block;
  font-size: 10.5px;
  color: var(--text-3);
}

.ct-aq b {
  font-size: 17px;
  font-weight: 650;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.ct-aq b i {
  font-style: normal;
  font-size: 11px;
  font-weight: 500;
  color: var(--text-3);
}

/* ---------------- 仪表主体：按宽度分档 ---------------- */

.cluster-body {
  display: grid;
  gap: 16px;
  min-width: 0;
}

/* 窄：上下堆叠 */
.lay-stack .cluster-body {
  grid-template-columns: minmax(0, 1fr);
}

/* 中：仍上下两块，但各自内部横向排开 */
.lay-dual .cluster-body {
  grid-template-columns: minmax(0, 1fr);
}

/* 宽：左右分栏 —— 温湿度在左，指标块在右 */
.lay-wide .cluster-body {
  grid-template-columns: minmax(260px, 0.95fr) minmax(340px, 1.05fr);
  gap: 22px;
  align-items: start;
}

/* 温湿度表盘区 */
.hero {
  display: grid;
  gap: 10px;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  min-width: 0;
}

.lay-stack .hero {
  grid-template-columns: minmax(0, 1fr);
}

/* 表盘限宽居中：SVG 拉太宽会显得扁，居中比撑满更协调 */
.hero-cell {
  min-width: 0;
  width: 100%;
  max-width: 280px;
  margin: 0 auto;
}

/* 指标块区 */
.tiles {
  display: grid;
  gap: 10px;
  grid-template-columns: repeat(var(--cols, 3), minmax(0, 1fr));
  min-width: 0;
}

/* ---------------- 传感器钉子板 ---------------- */
.pins {
  padding: 13px 16px 15px;
  border-radius: var(--radius-lg);
}

.pins-head {
  display: flex;
  align-items: center;
  gap: 9px;
  flex-wrap: wrap;
}

.pins-head h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 650;
  color: var(--text);
}

/* 右推：让「样式」开关和「添加」都靠右，中间留白 */
.pins-count {
  margin-right: auto;
  font-size: 10.5px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.ssw {
  display: flex;
  align-items: center;
  gap: 2px;
  padding: 2px;
  border-radius: 999px;
  background: var(--surface-2);
  border: 1px solid var(--border);
}

.ssw-cap {
  padding: 0 5px;
  font-size: 10px;
  color: var(--text-3);
}

.ssb {
  padding: 2px 9px;
  border: 0;
  border-radius: 999px;
  background: transparent;
  font-size: 10.5px;
  color: var(--text-2);
  cursor: pointer;
  transition: color 0.16s var(--ease), background-color 0.16s var(--ease);
}

.ssb:hover {
  color: var(--brand);
}

.ssb.active {
  color: #fff;
  background: var(--grad-brand);
}

.pin-add {
  flex: none;
}

/* ---- 挑选面板 ---- */
.picker {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.pick-top {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  margin-bottom: 8px;
}

.pick-kw {
  flex: 0 1 240px;
  height: 26px;
  padding: 0 10px;
  font-size: 11.5px;
  color: var(--text);
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 8px;
  outline: none;
}

.pick-kw:focus {
  border-color: var(--brand);
  box-shadow: var(--glow-brand-soft);
}

.pick-hint {
  font-size: 10px;
  color: var(--text-3);
}

.pick-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(232px, 1fr));
  gap: 8px 14px;
  max-height: 216px;
  overflow-y: auto;
  overscroll-behavior: contain;
  scrollbar-gutter: stable;
  padding-right: 2px;
}

.pick-group {
  min-width: 0;
  padding-bottom: 6px;
  border-bottom: 1px dashed var(--border);
}

.pg-head {
  display: flex;
  align-items: baseline;
  gap: 6px;
  min-width: 0;
  margin-bottom: 5px;
}

.pg-dev {
  min-width: 0;
  font-size: 11px;
  font-weight: 600;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.pg-room {
  flex: none;
  font-size: 9.5px;
  color: var(--text-3);
}

.pg-off {
  flex: none;
  font-size: 9.5px;
  color: var(--text-3);
  background: var(--surface-3);
  border-radius: 999px;
  padding: 0 5px;
}

.pg-items {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.pick-item {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 7px 2px 5px;
  border-radius: 999px;
  border: 1px solid var(--border);
  background: var(--surface);
  font-size: 10.5px;
  color: var(--text-2);
  cursor: pointer;
  transition: border-color 0.15s var(--ease), color 0.15s var(--ease), background-color 0.15s var(--ease);
}

.pick-item:hover {
  border-color: var(--border-strong);
  color: var(--text);
}

.pick-item.on {
  border-color: var(--brand);
  background: var(--brand-soft);
  color: var(--brand);
}

.pick-item input {
  width: 11px;
  height: 11px;
  margin: 0;
  flex: none;
  accent-color: var(--brand);
}

.pi-met {
  white-space: nowrap;
}

.pi-val {
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.pick-item.on .pi-val {
  color: var(--brand);
  opacity: 0.8;
}

.pick-empty {
  grid-column: 1 / -1;
  padding: 8px 4px;
  font-size: 11px;
  color: var(--text-3);
}

/* ---- 钉子网格 ---- */
.pin-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(172px, 1fr));
  gap: 10px;
  align-items: stretch;
  margin-top: 11px;
}

.pin-empty {
  margin-top: 11px;
  padding: 14px 16px;
  border-radius: var(--radius-md);
  border: 1px dashed var(--border-strong);
  background: var(--surface-2);
  font-size: 11.5px;
  line-height: 1.7;
  color: var(--text-3);
}

/* ---------------- 房间矩阵 ---------------- */
.sec-head {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.sec-head h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 650;
  color: var(--text);
}

.sec-actions {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 8px;
}

/* 房间卡片上的拖动把手：平时隐形，鼠标移到卡片上才出现 */
.rc-grip {
  flex: none;
  width: 14px;
  height: 20px;
  display: grid;
  place-items: center;
  margin-left: -4px;
  border-radius: 5px;
  color: var(--text-3);
  opacity: 0;
  pointer-events: none;
  cursor: grab;
  user-select: none;
  transition: opacity 0.15s ease, color 0.15s ease, background-color 0.15s ease;
}

.rcard:hover .rc-grip {
  opacity: 0.7;
  pointer-events: auto;
}

.rc-grip:hover {
  opacity: 1;
  color: var(--brand);
  background: var(--brand-soft);
}

.rc-grip:active {
  cursor: grabbing;
}

.rcard.dragging {
  opacity: 0.4;
}

.rcard.drop-target {
  border-color: var(--brand);
  box-shadow: 0 0 0 2px var(--brand-soft), var(--shadow-md);
}

.btn-sm {
  height: 26px;
  padding: 0 11px;
  font-size: 11.5px;
  border-radius: 8px;
}

.rooms {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(198px, 1fr));
  gap: 12px;
}

.rcard {
  position: relative;
  padding: 13px 14px 11px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface);
  box-shadow: var(--shadow-sm);
  cursor: pointer;
  overflow: hidden;
  transition: box-shadow 0.2s var(--ease), transform 0.2s var(--ease), border-color 0.2s var(--ease);
}

.rcard:hover {
  box-shadow: var(--shadow-md);
  transform: translateY(-1px);
}

.rcard.active {
  border-color: var(--brand);
  box-shadow: var(--glow-brand-soft), var(--shadow-md);
}

.rc-top {
  position: absolute;
  inset: 0 0 auto 0;
  height: 3px;
  background: linear-gradient(90deg, rgb(var(--tone)), transparent 74%);
}

.rc-head {
  display: flex;
  align-items: center;
  gap: 7px;
}

.rc-name {
  font-size: 13.5px;
  font-weight: 650;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.rc-badge {
  margin-left: auto;
  flex: none;
  font-size: 10px;
  font-weight: 600;
  color: rgb(var(--c));
  background: rgba(var(--c), 0.12);
  border-radius: 999px;
  padding: 1px 8px;
}

.rc-read {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  margin: 10px 0 8px;
}

.rc-temp {
  display: flex;
  align-items: flex-start;
  line-height: 1;
  color: rgb(var(--c));
}

.rc-temp b {
  font-size: 30px;
  font-weight: 700;
  letter-spacing: -1px;
  font-variant-numeric: tabular-nums;
}

.rc-temp em {
  font-style: normal;
  font-size: 13px;
  font-weight: 600;
  margin-top: 3px;
}

.rc-hum {
  flex: 1;
  min-width: 0;
  padding-bottom: 2px;
}

.rc-hum-cap {
  display: block;
  font-size: 10px;
  color: var(--text-3);
}

.rc-hum b {
  font-size: 16px;
  font-weight: 650;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.rc-hum b em {
  font-style: normal;
  font-size: 10px;
  color: var(--text-3);
}

.rc-hum-bar {
  display: block;
  width: 64px;
  height: 4px;
  border-radius: 3px;
  background: var(--surface-3);
  margin-top: 5px;
  overflow: hidden;
}

/* 湿度条按舒适档位着色：30% 这种「很干」不该和舒适的绿条长得一样 */
.rc-hum-bar i {
  display: block;
  height: 100%;
  border-radius: 3px;
  background: linear-gradient(90deg, rgba(var(--c), 0.4), rgb(var(--c)));
}

.rc-pols {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 10px;
}

.rc-pol {
  display: inline-flex;
  align-items: baseline;
  gap: 4px;
  font-size: 10px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.rc-pol b {
  font-size: 11.5px;
  font-weight: 650;
  color: rgb(var(--c));
}

.rc-pol.none {
  opacity: 0.4;
}

.rc-pol.none b {
  color: var(--text-3);
}

.rc-nosensor {
  font-size: 10.5px;
  color: var(--text-3);
  padding-bottom: 3px;
}

.rc-foot {
  display: flex;
  gap: 8px;
  margin-top: 9px;
  padding-top: 7px;
  border-top: 1px dashed var(--border);
  font-size: 10px;
  color: var(--text-3);
}

.rc-ctl {
  color: var(--brand);
}

/* ---------------- 趋势区 ---------------- */
.trend-box {
  padding: 14px 18px 16px;
  border-radius: var(--radius-lg);
}

.trend-head {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 10px;
  flex-wrap: wrap;
}

.trend-head h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 650;
  color: var(--text);
}

.trend-tabs {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.tt {
  font-size: 11px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 2px 10px;
  transition: all 0.16s var(--ease);
}

.tt:hover {
  border-color: var(--border-strong);
}

.tt.active {
  color: #fff;
  background: var(--grad-brand);
  border-color: transparent;
}

.hours {
  margin-left: auto;
  display: flex;
  gap: 2px;
}

.hb {
  font-size: 10.5px;
  color: var(--text-3);
  border-radius: 6px;
  padding: 2px 6px;
  transition: all 0.16s var(--ease);
}

.hb.active {
  color: var(--brand);
  background: var(--brand-soft);
}

/* 传感器明细 */
.sens {
  margin-top: 12px;
  padding-top: 10px;
  border-top: 1px dashed var(--border);
}

.sen + .sen {
  margin-top: 2px;
}

.sen-row {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  padding: 5px 4px;
  background: none;
  border: 0;
  border-radius: 8px;
  text-align: left;
  color: var(--text-2);
}

.sen-row:hover {
  background: var(--surface-2);
}

.dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex: none;
  background: var(--border-strong);
}

.dot.live {
  background: var(--accent);
  box-shadow: 0 0 0 3px rgba(23, 178, 106, 0.15);
}

.sen .nm {
  font-size: 12px;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 160px;
}

.sen .val {
  font-size: 11px;
  color: var(--text-3);
  margin-left: auto;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.chev {
  width: 13px;
  height: 13px;
  flex: none;
  color: var(--text-3);
  transition: transform 0.22s var(--ease);
}

.sen.open .chev {
  transform: rotate(90deg);
}

.sen.off .nm,
.sen.off .val {
  opacity: 0.55;
}

.sen-detail {
  padding: 4px 4px 8px;
}

@media (max-width: 1180px) {
  .env {
    grid-template-columns: minmax(0, 1fr);
    /* 退化单栏后两栏是上下叠的，不能让它们各自滚 —— 那样会出现两个小滚动区 */
    grid-template-rows: auto;
    height: auto;
  }

  .dash {
    overflow: visible;
    scrollbar-gutter: auto;
    padding-bottom: 0;
  }
}
</style>
