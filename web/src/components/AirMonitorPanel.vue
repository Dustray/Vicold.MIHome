<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { airProps, mainPollutant } from '../lib/spec'
import PropControl from './PropControl.vue'
import TrendSection from './TrendSection.vue'
import type { MetricKey } from '../lib/env'

const props = defineProps<{
  device: Device
  spec: Spec | null
  values: Record<string, unknown>
  busyProp: string
  groups: SpecGroup[]
}>()
const emit = defineEmits<{
  (e: 'set-prop', def: PropDef, value: unknown): void
  (e: 'run-action', siid: number, aiid: number, name: string): void
  (e: 'refresh'): void
}>()

const online = computed(() => props.device.online)
const core = computed(() => airProps(props.spec))

/** 这台设备能画出哪些曲线（按实际存在的属性决定） */
const trendMetrics = computed<MetricKey[]>(() => {
  const list: MetricKey[] = []
  if (core.value.hcho) list.push('hcho')
  if (core.value.pm25) list.push('pm25')
  if (core.value.pm10) list.push('pm10')
  if (core.value.tvoc) list.push('tvoc')
  if (core.value.temp) list.push('t')
  if (core.value.humidity) list.push('h')
  return list
})

function numOf(def: PropDef | null): number | undefined {
  if (!def) return undefined
  const v = props.values[def.name]
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

/** 小数位数跟着规格步长走：step 0.001 显示三位（甲醛），step 1 显示整数（湿度） */
function digitsOf(def: PropDef | null): number {
  const step = Number(def?.range?.[2] ?? 1)
  if (!(step > 0) || step >= 1) return 0
  return Math.min(3, Math.round(-Math.log10(step)))
}

function fmt(v: number | undefined, digits: number): string {
  return v === undefined ? '—' : v.toFixed(digits)
}

// ---------------------------------------------------------------- 污染物口径
type Tone = 'good' | 'ok' | 'warn' | 'bad'

interface Grade {
  up: number
  label: string
  note: string
  tone: Tone
}

interface Meter {
  key: string
  def: PropDef
  label: string
  sub: string
  unit: string
  digits: number
  /** 标尺展示上限。规格写 0–5 的话，0.08 的国标线只有 1.6% 宽，等于看不见 */
  max: number
  /** 国标限值（决定标尺上的参考线与等级分界） */
  limit: number
  /** 等级从优到差，按 up 升序，最后一条兜底 */
  grades: Grade[]
  /** 色带锚点：位置按 0..max 归一，颜色用于渐变条 */
  stops: [number, [number, number, number]][]
  ticks: number[]
}

/**
 * 甲醛：GB/T 18883-2022 室内空气质量标准限值 0.08 mg/m³（1 小时均值）。
 * 分档不给"安全/有毒"这种绝对结论——低于限值也可能有气味，
 * 只在接近和超过限值时明确提示。
 */
const HCHO: Omit<Meter, 'def'> = {
  key: 'hcho',
  label: '甲醛浓度',
  sub: 'HCHO',
  unit: 'mg/m³',
  digits: 3,
  max: 0.2,
  limit: 0.08,
  grades: [
    { up: 0.03, label: '优', note: '远低于国标限值', tone: 'good' },
    { up: 0.06, label: '良好', note: '正常水平', tone: 'ok' },
    { up: 0.08, label: '接近限值', note: '建议开窗通风', tone: 'warn' },
    { up: Infinity, label: '超标', note: '已超过国标限值', tone: 'bad' },
  ],
  stops: [
    [0, [23, 178, 106]],
    [0.03, [110, 190, 84]],
    [0.055, [236, 186, 52]],
    [0.08, [240, 132, 44]],
    [0.14, [226, 84, 60]],
    [0.2, [198, 54, 58]],
  ],
  ticks: [0, 0.04, 0.08, 0.12, 0.2],
}

/** PM2.5：GB/T 18883-2022 一级标准 0.035 mg/m³（24 小时均值），这里按 μg/m³ 展示 */
const PM25: Omit<Meter, 'def'> = {
  key: 'pm25',
  label: 'PM2.5',
  sub: '细颗粒物',
  unit: 'μg/m³',
  digits: 0,
  max: 150,
  limit: 35,
  grades: [
    { up: 15, label: '优', note: '空气很干净', tone: 'good' },
    { up: 35, label: '良好', note: '符合一级标准', tone: 'ok' },
    { up: 75, label: '轻度污染', note: '建议关窗净化', tone: 'warn' },
    { up: Infinity, label: '污染', note: '已超过一级标准', tone: 'bad' },
  ],
  stops: [
    [0, [23, 178, 106]],
    [15, [110, 190, 84]],
    [35, [236, 186, 52]],
    [75, [240, 132, 44]],
    [150, [222, 74, 62]],
  ],
  ticks: [0, 35, 75, 115, 150],
}

/** TVOC：GB/T 18883-2022 限值 0.6 mg/m³（8 小时均值） */
const TVOC: Omit<Meter, 'def'> = {
  key: 'tvoc',
  label: 'TVOC',
  sub: '总挥发性有机物',
  unit: 'mg/m³',
  digits: 2,
  max: 1.2,
  limit: 0.6,
  grades: [
    { up: 0.2, label: '优', note: '远低于国标限值', tone: 'good' },
    { up: 0.45, label: '良好', note: '正常水平', tone: 'ok' },
    { up: 0.6, label: '接近限值', note: '建议通风', tone: 'warn' },
    { up: Infinity, label: '超标', note: '已超过国标限值', tone: 'bad' },
  ],
  stops: [
    [0, [23, 178, 106]],
    [0.2, [110, 190, 84]],
    [0.45, [236, 186, 52]],
    [0.6, [240, 132, 44]],
    [1.2, [222, 74, 62]],
  ],
  ticks: [0, 0.3, 0.6, 0.9, 1.2],
}

/**
 * 主污染物：哪台机器测什么就显示什么。
 * 优先级（甲醛 > PM2.5 > TVOC）由 spec.ts 的 mainPollutant 统一决定，
 * 和卡片摘要共用一套口径——这台是甲醛仪，
 * 但同一套面板也该能扛住只测颗粒物的型号。
 */
const MAIN_CFG: Record<'hcho' | 'pm25' | 'tvoc', Omit<Meter, 'def'>> = {
  hcho: HCHO,
  pm25: PM25,
  tvoc: TVOC,
}

const meter = computed<Meter | null>(() => {
  const p = mainPollutant(props.spec)
  return p ? { ...MAIN_CFG[p.key], def: p.def } : null
})

const mainValue = computed(() => numOf(meter.value?.def ?? null))
const mainText = computed(() =>
  meter.value ? fmt(mainValue.value, meter.value.digits) : '—'
)

function pct(v: number, lo: number, hi: number): number {
  const x = ((v - lo) / (hi - lo)) * 100
  return Math.min(100, Math.max(0, x))
}

const mainPct = computed(() => {
  const m = meter.value
  if (!m || mainValue.value === undefined) return 0
  return pct(mainValue.value, 0, m.max)
})

const limitPct = computed(() => (meter.value ? pct(meter.value.limit, 0, meter.value.max) : 0))

/** 标尺刻度：按真实比例定位，不能等分（0.08 在 0–0.2 里只占 40%） */
const ticks = computed(() => {
  const m = meter.value
  if (!m) return []
  const last = m.ticks.length - 1
  return m.ticks.map((v, i) => ({
    key: v,
    text: m.digits ? v.toFixed(m.digits === 3 ? 2 : m.digits) : String(v),
    left: pct(v, 0, m.max),
    align: i === 0 ? 'start' : i === last ? 'end' : 'mid',
  }))
})

/** 色带：把锚点换算成百分比，拼成一条 linear-gradient */
const scaleBg = computed(() => {
  const m = meter.value
  if (!m) return ''
  const parts = m.stops.map(
    ([v, c]) => `rgb(${c.join(', ')}) ${pct(v, 0, m.max).toFixed(1)}%`
  )
  return `linear-gradient(90deg, ${parts.join(', ')})`
})

const grade = computed<Grade>(() => {
  const m = meter.value
  if (!m) return { up: 0, label: '—', note: '', tone: 'ok' }
  const v = mainValue.value
  if (v === undefined) return { up: 0, label: '暂无读数', note: '', tone: 'ok' }
  return m.grades.find((g) => v < g.up) ?? m.grades[m.grades.length - 1]
})

/**
 * 等级参考表：光有一个数字加一条色带，用户并不知道 0.05 算好还是差。
 * 这里把四档区间直接列出来，当前所处档位高亮。
 */
const gradeRows = computed(() => {
  const m = meter.value
  if (!m) return []
  const nf = (v: number) => String(Number(v.toFixed(2)))
  let from = 0
  return m.grades.map((g) => {
    let range: string
    if (!Number.isFinite(g.up)) range = `≥ ${nf(from)}`
    else if (from === 0) range = `< ${nf(g.up)}`
    else range = `${nf(from)} – ${nf(g.up)}`
    from = g.up
    return { tone: g.tone, label: g.label, range }
  })
})

const TONE_RGB: Record<Tone, [string, string, string]> = {
  // 图形色、浅色主题下的文字色、暗色主题下的文字色
  good: ['23, 178, 106', '18, 148, 90', '74, 222, 128'],
  ok: ['236, 186, 52', '163, 106, 8', '251, 191, 36'],
  warn: ['240, 132, 44', '192, 88, 16', '251, 146, 60'],
  bad: ['222, 74, 62', '197, 54, 58', '248, 113, 113'],
}

const toneStyle = computed(() => {
  const [graphic, ink, inkDark] = TONE_RGB[grade.value.tone]
  return { '--cr': graphic, '--ci': ink, '--cd': inkDark }
})

// ---------------------------------------------------------------- 温湿度
const temp = computed(() => numOf(core.value.temp))
const humidity = computed(() => numOf(core.value.humidity))
const tempDigits = computed(() => digitsOf(core.value.temp))
const humDigits = computed(() => digitsOf(core.value.humidity))

const batt = computed(() => numOf(core.value.battery))
const battLow = computed(() => batt.value !== undefined && batt.value <= 20)
const battRgb = computed(() => {
  const v = batt.value
  if (v === undefined) return '143, 162, 186'
  if (v <= 20) return '229, 72, 77'
  if (v <= 50) return '236, 168, 44'
  return '23, 178, 106'
})

// ---------------------------------------------------------------- 状态
const statusText = computed(() => {
  if (!online.value) return '设备离线'
  if (mainValue.value === undefined) return '读取中'
  return props.device.room_name || '在线'
})

const refreshing = ref(false)
function doRefresh() {
  if (refreshing.value) return
  refreshing.value = true
  emit('refresh')
  window.setTimeout(() => {
    refreshing.value = false
  }, 700)
}

// ---------------------------------------------------------------- 收纳其余参数
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const c = core.value
  for (const d of [c.hcho, c.pm25, c.tvoc, c.temp, c.humidity, c.battery]) {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  return keys
})

/**
 * 其余可写属性 / 动作。
 *
 * 杜克 HF1 的规格里除了 4 个读数就什么都没有：没有 on、没有动作，
 * 所以这块会整个不出现，属于正常。设备信息（厂商 / 型号 / SN / 固件）
 * 在协议层就被 `flatten_spec` 跳过了，前端拿不到，不要在这里留一个
 * 永远为空的「显示设备信息」开关。
 */
const restGroups = computed<SpecGroup[]>(() => {
  const keys = consumedKeys.value
  return props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      actions: g.actions,
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})
</script>

<template>
  <div v-if="spec" class="amp">
    <!-- 主污染物：这台设备存在的意义就是这一个数字 -->
    <section v-if="meter" class="hero" :class="{ off: !online }" :style="toneStyle">
      <header class="hero-head">
        <div class="badge" :class="grade.tone">
          <span class="dot" />
          <span class="badge-label">{{ grade.label }}</span>
          <span v-if="grade.note" class="badge-note">{{ grade.note }}</span>
        </div>
        <div class="head-right">
          <span class="where">{{ statusText }}</span>
          <button
            class="refresh"
            :class="{ spin: refreshing }"
            :disabled="!online || refreshing"
            title="重新读取"
            @click="doRefresh"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">
              <path d="M20 11.5A8 8 0 0 0 6.3 6.6L4 9" />
              <path d="M4 4.6V9h4.4" />
              <path d="M4 12.5A8 8 0 0 0 17.7 17.4L20 15" />
              <path d="M20 19.4V15h-4.4" />
            </svg>
          </button>
        </div>
      </header>

      <div class="reading">
        <span class="r-label">{{ meter.label }}</span>
        <div class="r-num">
          {{ mainText }}<em>{{ meter.unit }}</em>
        </div>
        <span class="r-sub">{{ meter.sub }}</span>
      </div>

      <div class="scale">
        <div class="track" :style="{ background: scaleBg }">
          <span class="limit" :style="{ left: `${limitPct}%` }" />
          <span v-if="mainValue !== undefined" class="marker" :style="{ left: `${mainPct}%` }" />
        </div>
        <div class="ticks">
          <span
            v-for="t in ticks"
            :key="t.key"
            :class="['tick', t.align]"
            :style="{ left: `${t.left}%` }"
          >{{ t.text }}</span>
        </div>
        <p class="limit-note">
          国标限值 <b>{{ meter.limit }}</b> {{ meter.unit }}<span class="mn">· GB/T 18883</span>
        </p>
      </div>
    </section>

    <!-- 温湿度：污染物浓度脱离温湿度没意义，放同一屏里对照着看 -->
    <section v-if="core.temp || core.humidity" class="pair" :class="{ off: !online }">
      <div v-if="core.temp" class="mini">
        <span class="mini-label">温度</span>
        <div class="mini-num temp">
          {{ fmt(temp, tempDigits) }}<em v-if="temp !== undefined">°C</em>
        </div>
      </div>
      <span v-if="core.temp && core.humidity" class="mini-line" />
      <div v-if="core.humidity" class="mini">
        <span class="mini-label">相对湿度</span>
        <div class="mini-num hum">
          {{ fmt(humidity, humDigits) }}<em v-if="humidity !== undefined">%</em>
        </div>
      </div>
    </section>

    <!-- 等级参考：把标尺每一段对应的数值摆出来 -->
    <section v-if="gradeRows.length" class="legend" :class="{ off: !online }">
      <h3 class="legend-title">等级参考</h3>
      <div class="legend-rows">
        <div
          v-for="r in gradeRows"
          :key="r.label"
          class="legend-row"
          :class="[r.tone, { on: r.tone === grade.tone }]"
        >
          <span class="lg-dot" />
          <span class="lg-label">{{ r.label }}</span>
          <span class="lg-range">{{ r.range }}</span>
        </div>
      </div>
    </section>

    <!-- 历史曲线：本机采样记录，甲醛/温湿度走势 -->
    <TrendSection :did="device.did" :metrics="trendMetrics" title="历史曲线" />

    <!-- 电池 -->
    <section v-if="core.battery" class="batt" :class="{ low: battLow, off: !online }" :style="{ '--br': battRgb }">
      <div class="batt-left">
        <span class="batt-ico">
          <i :style="{ width: `${Math.min(100, Math.max(4, batt ?? 0))}%` }" />
        </span>
        <span class="batt-label">电池电量</span>
      </div>
      <b class="batt-num">{{ batt === undefined ? '—' : Math.round(batt) }}<em>%</em></b>
    </section>
    <p v-if="battLow" class="batt-warn">电量偏低，建议尽快更换电池，否则读数会停止上报</p>

    <!-- 其余可写参数 / 动作。杜克 HF1 除读数外没有可配置项，这块不会出现 -->
    <details v-if="restGroups.length" class="adv">
      <summary>
        <span>更多设置</span>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M8 10l4 4 4-4" /></svg>
      </summary>
      <div class="adv-body">
        <section v-for="g in restGroups" :key="g.key">
          <h4>{{ g.title }}</h4>
          <PropControl
            v-for="p in g.writable"
            :key="`${p.siid}-${p.piid}`"
            :def="p"
            :value="values[p.name]"
            :disabled="!online || busyProp === p.name"
            @change="(v) => emit('set-prop', p, v)"
          />
          <div v-if="g.readable.length" class="readable">
            <PropControl
              v-for="p in g.readable"
              :key="`${p.siid}-${p.piid}`"
              :def="p"
              :value="values[p.name]"
              disabled
            />
          </div>
          <div v-if="g.actions.length" class="acts">
            <button
              v-for="a in g.actions"
              :key="`${a.siid}-${a.aiid}`"
              class="btn btn-quiet"
              :disabled="!online"
              @click="emit('run-action', a.siid, a.aiid, a.name)"
            >
              {{ a.name }}
            </button>
          </div>
        </section>
      </div>
    </details>

    <p class="hint">点右上角刷新可重新读取</p>
  </div>
</template>

<style scoped>
.amp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.amp > section,
.amp > details {
  animation: fade-up 0.32s var(--ease) both;
}

.amp > section:nth-of-type(2) {
  animation-delay: 0.06s;
}

.amp > section:nth-of-type(3) {
  animation-delay: 0.11s;
}

.amp > section:nth-of-type(4) {
  animation-delay: 0.15s;
}

/* ------------------------------------------------ 主污染物 */
.hero {
  margin-top: 12px;
  padding: 15px 16px 14px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  position: relative;
  overflow: hidden;
}

/* 左上角一层等级色光晕，颜色跟着污染程度变 */
.hero::before {
  content: '';
  position: absolute;
  inset: -70% 45% auto -25%;
  height: 240px;
  background: radial-gradient(circle, rgba(var(--cr), 0.17), transparent 66%);
  transition: background 0.4s ease;
  pointer-events: none;
}

.hero.off {
  opacity: 0.55;
  filter: saturate(0.45);
}

.hero-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 11px 0 9px;
  border-radius: 999px;
  font-size: 11.5px;
  font-weight: 600;
  max-width: 100%;
  overflow: hidden;
  color: rgb(var(--ci));
  background: rgba(var(--cr), 0.13);
  border: 1px solid rgba(var(--cr), 0.34);
}

[data-theme='dark'] .badge {
  color: rgb(var(--cd));
}

.badge .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex: none;
  background: currentColor;
}

.badge.good .dot {
  animation: breathe 2.6s ease-in-out infinite;
}

.badge.bad {
  animation: breathe 3s ease-in-out infinite;
}

.badge-note {
  font-weight: 500;
  opacity: 0.82;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.where {
  font-size: 11px;
  color: var(--text-3);
  flex: none;
}

.head-right {
  display: flex;
  align-items: center;
  gap: 6px;
  flex: none;
}

.refresh {
  width: 26px;
  height: 26px;
  display: grid;
  place-items: center;
  border-radius: 8px;
  color: var(--text-3);
  background: transparent;
  transition: color 0.16s ease, background-color 0.16s ease;
}

.refresh svg {
  width: 14px;
  height: 14px;
}

.refresh:hover:not(:disabled) {
  color: var(--brand);
  background: var(--brand-soft);
}

.refresh:disabled {
  opacity: 0.45;
}

.refresh.spin svg {
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

/* 读数：数字占满宽度，单位跟在后面 */
.reading {
  position: relative;
  z-index: 1;
  margin-top: 13px;
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.r-label {
  font-size: 11px;
  color: var(--text-3);
  letter-spacing: 0.3px;
}

.r-num {
  display: flex;
  align-items: baseline;
  gap: 5px;
  font-size: 42px;
  line-height: 1.04;
  font-weight: 700;
  letter-spacing: -1.2px;
  font-variant-numeric: tabular-nums;
  color: rgb(var(--ci));
  transition: color 0.4s ease;
}

[data-theme='dark'] .r-num {
  color: rgb(var(--cd));
}

.r-num em {
  font-style: normal;
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0;
  opacity: 0.72;
}

.r-sub {
  font-size: 10px;
  color: var(--text-3);
  letter-spacing: 0.6px;
}

/* 标尺 */
.scale {
  position: relative;
  z-index: 1;
  margin-top: 14px;
}

.track {
  position: relative;
  height: 9px;
  border-radius: 999px;
  opacity: 0.92;
  box-shadow: inset 0 0 0 1px rgba(22, 35, 58, 0.06);
}

/* 国标限值：一条竖向白线，比刻度数字更直观 */
.limit {
  position: absolute;
  top: -4px;
  bottom: -4px;
  width: 2px;
  margin-left: -1px;
  border-radius: 1px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 0 0 1px rgba(22, 35, 58, 0.16);
}

[data-theme='dark'] .limit {
  background: rgba(12, 20, 34, 0.9);
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.22);
}

.marker {
  position: absolute;
  top: 50%;
  width: 13px;
  height: 13px;
  margin: -6.5px 0 0 -6.5px;
  border-radius: 50%;
  background: #fff;
  border: 2px solid rgba(10, 17, 29, 0.4);
  box-shadow: 0 1px 6px rgba(10, 17, 29, 0.45);
  transition: left 0.45s var(--ease);
}

[data-theme='dark'] .marker {
  background: #eef4ff;
}

/* 刻度按真实比例绝对定位，等分会让 0.08 落在错误位置 */
.ticks {
  position: relative;
  height: 13px;
  margin-top: 5px;
  font-size: 9px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.tick {
  position: absolute;
  top: 0;
  white-space: nowrap;
}

.tick.start {
  transform: none;
}

.tick.mid {
  transform: translateX(-50%);
}

.tick.end {
  transform: translateX(-100%);
}

.limit-note {
  margin: 6px 0 0;
  font-size: 10px;
  color: var(--text-3);
}

.limit-note b {
  font-weight: 700;
  color: var(--text-2);
  font-variant-numeric: tabular-nums;
}

.limit-note .mn {
  margin-left: 6px;
  opacity: 0.7;
}

/* ------------------------------------------------ 温湿度 */
.pair {
  margin-top: 12px;
  display: flex;
  align-items: stretch;
  padding: 12px 15px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.pair.off {
  opacity: 0.55;
}

.mini {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.mini-label {
  font-size: 10.5px;
  color: var(--text-3);
}

.mini-num {
  display: flex;
  align-items: baseline;
  gap: 2px;
  font-size: 22px;
  font-weight: 700;
  line-height: 1.15;
  letter-spacing: -0.4px;
  font-variant-numeric: tabular-nums;
}

.mini-num.temp {
  color: var(--text);
}

.mini-num.hum {
  color: rgb(56, 160, 214);
}

.mini-num em {
  font-style: normal;
  font-size: 11px;
  font-weight: 600;
  opacity: 0.7;
}

.mini-line {
  width: 1px;
  flex: none;
  margin: 2px 12px;
  background: linear-gradient(180deg, transparent, var(--border-strong), transparent);
}

/* ------------------------------------------------ 等级参考 */
.legend {
  margin-top: 12px;
  padding: 12px 14px 10px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--grad-surface);
}

.legend.off {
  opacity: 0.55;
}

.legend-title {
  margin: 0 0 7px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.3px;
  color: var(--text-3);
}

.legend-rows {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.legend-row {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 27px;
  padding: 0 9px;
  border-radius: 7px;
  border: 1px solid transparent;
  font-size: 11.5px;
  color: var(--text-2);
  transition: background-color 0.2s ease, border-color 0.2s ease;
}

/* 当前所处档位：不是靠变色，而是整行浮起，扫一眼就能定位 */
.legend-row.on {
  background: var(--surface-2);
  border-color: var(--border);
  color: var(--text);
  font-weight: 600;
}

.lg-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  flex: none;
}

.legend-row.good .lg-dot {
  background: #17b26a;
}

.legend-row.ok .lg-dot {
  background: #e2b52f;
}

.legend-row.warn .lg-dot {
  background: #ef7f28;
}

.legend-row.bad .lg-dot {
  background: #de4a3e;
}

.lg-label {
  flex: none;
}

.lg-range {
  margin-left: auto;
  font-size: 10.5px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.legend-row.on .lg-range {
  color: var(--text-2);
}

/* ------------------------------------------------ 电池 */
.batt {
  margin-top: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 14px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.batt.off {
  opacity: 0.55;
}

.batt-left {
  display: flex;
  align-items: center;
  gap: 9px;
  min-width: 0;
}

.batt-ico {
  position: relative;
  width: 22px;
  height: 11px;
  border-radius: 3px;
  flex: none;
  background: var(--surface-3);
  box-shadow: inset 0 0 0 1.5px rgba(var(--br), 0.45);
  overflow: hidden;
}

.batt-ico::after {
  content: '';
  position: absolute;
  right: -4px;
  top: 3px;
  width: 2px;
  height: 5px;
  border-radius: 0 1px 1px 0;
  background: rgba(var(--br), 0.45);
}

.batt-ico i {
  position: absolute;
  left: 1.5px;
  top: 1.5px;
  bottom: 1.5px;
  border-radius: 1.5px;
  background: rgb(var(--br));
  transition: width 0.4s var(--ease), background-color 0.3s ease;
}

.batt-label {
  font-size: 11.5px;
  color: var(--text-2);
}

.batt-num {
  font-size: 14px;
  font-weight: 700;
  color: rgb(var(--br));
  font-variant-numeric: tabular-nums;
  flex: none;
}

.batt-num em {
  font-style: normal;
  font-size: 10px;
  margin-left: 1px;
  opacity: 0.8;
}

.batt.low {
  border-color: rgba(229, 72, 77, 0.4);
  background: rgba(229, 72, 77, 0.06);
  animation: breathe 2.8s ease-in-out infinite;
}

.batt-warn {
  margin: 7px 2px 0;
  font-size: 10.5px;
  line-height: 1.5;
  color: var(--danger);
}

.hint {
  margin: 14px 0 0;
  font-size: 10.5px;
  color: var(--text-3);
  text-align: center;
}

/* ------------------------------------------------ 更多设置 */
.adv {
  margin-top: 16px;
  border-top: 1px solid var(--border);
  padding-top: 4px;
}

.adv summary {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 0;
  font-size: 12.5px;
  color: var(--text-2);
  cursor: pointer;
  list-style: none;
}

.adv summary::-webkit-details-marker {
  display: none;
}

.adv summary:hover {
  color: var(--text);
}

.adv summary svg {
  width: 15px;
  height: 15px;
  transition: transform 0.2s var(--ease);
}

.adv[open] summary svg {
  transform: rotate(180deg);
}

.adv-body section {
  margin-bottom: 6px;
}

h4 {
  margin: 10px 0 2px;
  font-size: 11.5px;
  font-weight: 600;
  color: var(--text-3);
}

.readable {
  padding: 2px 10px 4px;
  border-radius: 10px;
  background: var(--surface-2);
}

.readable :deep(.prop) {
  border-bottom: none;
  padding: 6px 0;
}

.acts {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  padding-top: 8px;
}
</style>
