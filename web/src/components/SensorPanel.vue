<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { sensorProps } from '../lib/spec'
import { propLabel } from '../lib/zh'
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
const core = computed(() => sensorProps(props.spec))

// ---------------------------------------------------------------- 读数
function numOf(def: PropDef | null): number | undefined {
  if (!def) return undefined
  const v = props.values[def.name]
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

const temp = computed(() => numOf(core.value.temp))
const humidity = computed(() => numOf(core.value.humidity))
const co2 = computed(() => numOf(core.value.co2))
const battery = computed(() => numOf(core.value.battery))

/** 小数位数跟着规格步长走：step 0.1 显示一位，step 1 显示整数 */
function digitsOf(def: PropDef | null): number {
  const step = Number(def?.range?.[2] ?? 1)
  return step > 0 && step < 1 ? 1 : 0
}

const tempDigits = computed(() => digitsOf(core.value.temp))
const humDigits = computed(() => digitsOf(core.value.humidity))

/** 这台设备能画出哪些曲线（按实际存在的属性决定） */
const trendMetrics = computed<MetricKey[]>(() => {
  const list: MetricKey[] = []
  if (core.value.temp) list.push('t')
  if (core.value.humidity) list.push('h')
  if (core.value.co2) list.push('co2')
  return list
})

function fmt(v: number | undefined, digits: number): string {
  return v === undefined ? '—' : v.toFixed(digits)
}

const tempText = computed(() => fmt(temp.value, tempDigits.value))
const humText = computed(() => fmt(humidity.value, humDigits.value))

// ---------------------------------------------------------------- 温度色阶
/**
 * 温度 → 颜色：偏冷走蓝、舒适区走绿、偏热走橙红。
 *
 * 用感知色阶而不是线性插值 RGB —— 蓝色与绿色直接线性插值会经过一段发灰的
 * 青灰，看起来像"故障色"。这里给足中间锚点，让 21–26°C 稳定落在绿色系。
 */
const TEMP_STOPS: [number, [number, number, number]][] = [
  [5, [64, 124, 240]],
  [13, [56, 158, 232]],
  [18, [40, 178, 148]],
  [22, [23, 178, 106]],
  [26, [110, 190, 84]],
  [30, [243, 154, 46]],
  [35, [228, 88, 56]],
]

function lerpRgb(stops: [number, [number, number, number]][], v: number): [number, number, number] {
  const first = stops[0]
  const last = stops[stops.length - 1]
  if (v <= first[0]) return first[1]
  if (v >= last[0]) return last[1]
  for (let i = 0; i < stops.length - 1; i += 1) {
    const [x0, c0] = stops[i]
    const [x1, c1] = stops[i + 1]
    if (v >= x0 && v <= x1) {
      const t = (v - x0) / (x1 - x0)
      return [
        Math.round(c0[0] + (c1[0] - c0[0]) * t),
        Math.round(c0[1] + (c1[1] - c0[1]) * t),
        Math.round(c0[2] + (c1[2] - c0[2]) * t),
      ]
    }
  }
  return last[1]
}

const tempRgb = computed(() => lerpRgb(TEMP_STOPS, temp.value ?? 22))

/** 湿度固定青色：温湿度同屏时颜色已经够多，湿度不再跟着数值变色 */
const HUM_RGB = '56, 160, 214'

// ---------------------------------------------------------------- CO₂ 色阶
const CO2_STOPS: [number, [number, number, number]][] = [
  [400, [23, 178, 106]],
  [800, [124, 190, 74]],
  [1000, [236, 186, 52]],
  [1500, [240, 132, 44]],
  [2200, [222, 74, 62]],
]

const co2Rgb = computed(() => lerpRgb(CO2_STOPS, co2.value ?? 400))

interface Level {
  label: string
  note: string
}

/** CO₂ 等级：按室内空气质量通行口径 */
const co2Level = computed<Level>(() => {
  const v = co2.value
  if (v === undefined) return { label: '—', note: '暂无读数' }
  if (v < 800) return { label: '优', note: '空气清新' }
  if (v < 1000) return { label: '良', note: '可以接受' }
  if (v < 1500) return { label: '一般', note: '建议开窗通风' }
  if (v < 2200) return { label: '较差', note: '尽快通风' }
  return { label: '差', note: '需要立即通风' }
})

// ---------------------------------------------------------------- 舒适度
/**
 * 舒适度不是简单平均温湿度，而是「哪一项最不舒适就按哪一项算」。
 * 22°C / 25% 湿度这种组合平均下来很舒服，实际体感很干。
 */
const comfort = computed<{ label: string; note: string; tone: 'good' | 'ok' | 'bad' }>(() => {
  const t = temp.value
  const h = humidity.value
  if (t === undefined && h === undefined) return { label: '暂无读数', note: '', tone: 'ok' }

  const tScore = t === undefined ? 0 : t < 16 || t > 28 ? 2 : t < 18 || t > 26 ? 1 : 0
  const hScore = h === undefined ? 0 : h < 30 || h > 70 ? 2 : h < 40 || h > 60 ? 1 : 0
  const worst = Math.max(tScore, hScore)

  const issues: string[] = []
  if (t !== undefined) {
    if (t < 16) issues.push('偏冷')
    else if (t < 18) issues.push('微凉')
    else if (t > 28) issues.push('偏热')
    else if (t > 26) issues.push('微热')
  }
  if (h !== undefined) {
    if (h < 30) issues.push('很干')
    else if (h < 40) issues.push('偏干')
    else if (h > 70) issues.push('很潮')
    else if (h > 60) issues.push('偏潮')
  }

  if (worst === 2) return { label: '不太舒适', note: issues.join(' · '), tone: 'bad' }
  if (worst === 1) return { label: '基本舒适', note: issues.join(' · '), tone: 'ok' }
  return { label: '舒适', note: '温湿度都在理想区间', tone: 'good' }
})

// ---------------------------------------------------------------- 刻度条
/** 展示区间固定，不用规格的 -30~100 —— 那个跨度下 18–26 的舒适带只有 6% 宽，看不出东西 */
const T_MIN = -10
const T_MAX = 40

function pct(v: number, lo: number, hi: number): number {
  const x = ((v - lo) / (hi - lo)) * 100
  return Math.min(100, Math.max(0, x))
}

const tempPct = computed(() => pct(temp.value ?? T_MIN, T_MIN, T_MAX))
const humPct = computed(() => pct(humidity.value ?? 0, 0, 100))

/** 理想区间带：温度 18–26°C、湿度 40–60% */
const tempBand = computed(() => {
  const l = pct(18, T_MIN, T_MAX)
  return { left: `${l}%`, width: `${pct(26, T_MIN, T_MAX) - l}%` }
})
const humBand = computed(() => {
  const l = pct(40, 0, 100)
  return { left: `${l}%`, width: `${pct(60, 0, 100) - l}%` }
})

const heroStyle = computed(() => ({
  '--tr': tempRgb.value.join(', '),
  '--hr': HUM_RGB,
}))

/** 只读设备没有开关，顶部徽章改成展示"在线状态 / 最近一次读数是否正常" */
const statusText = computed(() => {
  if (!online.value) return '设备离线'
  if (temp.value === undefined && humidity.value === undefined) return '读取中'
  return props.device.room_name || '在线'
})

/** 手动刷新：读数由设备主动上报，不会自己变，给个显式入口 */
const refreshing = ref(false)
function doRefresh() {
  if (refreshing.value) return
  refreshing.value = true
  emit('refresh')
  window.setTimeout(() => {
    refreshing.value = false
  }, 700)
}

// ---------------------------------------------------------------- 电池
const battLow = computed(() => battery.value !== undefined && battery.value <= 20)
const battRgb = computed(() => {
  const v = battery.value
  if (v === undefined) return '143, 162, 186'
  if (v <= 20) return '229, 72, 77'
  if (v <= 50) return '236, 168, 44'
  return '23, 178, 106'
})

// ---------------------------------------------------------------- 收纳其余参数
/**
 * 面板已消费的读数：温 / 湿 / CO₂ / 电池。
 * 其余属性（温度显示单位、时区、CO₂ 采集间隔等）规格里 access 为空，
 * 前后端都不会取到值，只能靠「更多设置」里的动作（校时/校准）间接操作。
 */
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  for (const d of [core.value.temp, core.value.humidity, core.value.co2, core.value.battery]) {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  return keys
})

const showRaw = ref(false)

/** 设备信息（厂商/型号/序列号/固件）对日常使用没意义，默认折叠 */
const DEBUG_SERVICES = new Set(['device-information', 'historycal-sync'])

const svcNameOf = computed(() => {
  const map = new Map<string, string>()
  for (const s of props.spec?.services || []) map.set(String(s.siid), s.name)
  return map
})

const debugSiids = computed(() => {
  const set = new Set<string>()
  for (const [siid, name] of svcNameOf.value) if (DEBUG_SERVICES.has(name)) set.add(siid)
  return set
})

const restGroups = computed<SpecGroup[]>(() => {
  const keys = consumedKeys.value
  const hidden = showRaw.value ? new Set<string>() : debugSiids.value
  return props.groups
    .filter((g) => !hidden.has(String(g.key)))
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      actions: g.actions,
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})

/** 动作中文名兜底：规格里校时类动作没有官方中文 */
function actionName(a: { name: string; siid: number; aiid: number }): string {
  return a.name
}

/** 温度标签：官方有中文就用官方，没有就落到兜底字典的「温度」 */
const tempLabel = computed(() =>
  core.value.temp ? propLabel(core.value.temp) : '温度'
)
</script>

<template>
  <div v-if="spec" class="sp">
    <!-- 主仪表盘：温湿度双读数 + 理想区间带 -->
    <section class="hero" :class="{ off: !online }" :style="heroStyle">
      <header class="hero-head">
        <div class="badge" :class="comfort.tone">
          <span class="dot" />
          <span class="badge-label">{{ comfort.label }}</span>
          <span v-if="comfort.note" class="badge-note">{{ comfort.note }}</span>
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
      <div class="dual">
        <div class="metric">
          <span class="m-label">{{ tempLabel }}</span>
          <div class="m-num temp">
            {{ tempText }}<em v-if="temp !== undefined">°C</em>
          </div>
          <div class="m-sub">
            <span class="band-tag">理想 18–26</span>
          </div>
        </div>

        <div class="m-line" />

        <div class="metric">
          <span class="m-label">湿度</span>
          <div class="m-num hum">
            {{ humText }}<em v-if="humidity !== undefined">%</em>
          </div>
          <div class="m-sub">
            <span class="band-tag">理想 40–60</span>
          </div>
        </div>
      </div>

      <div class="bars">
        <div class="bar-row">
          <div class="bar">
            <span class="band" :style="tempBand" />
            <span
              v-if="temp !== undefined"
              class="marker"
              :style="{ left: `${tempPct}%`, background: `rgb(${tempRgb})` }"
            />
          </div>
          <div class="ticks">
            <span>-10</span><span>15</span><span>25</span><span>40 °C</span>
          </div>
        </div>

        <div class="bar-row">
          <div class="bar">
            <span class="band" :style="humBand" />
            <span
              v-if="humidity !== undefined"
              class="marker"
              :style="{ left: `${humPct}%`, background: `rgb(${HUM_RGB})` }"
            />
          </div>
          <div class="ticks">
            <span>0</span><span>30</span><span>60</span><span>100 %</span>
          </div>
        </div>
      </div>
    </section>

    <!-- CO₂：只有 3 Pro 这类带环境模块的机型才有 -->
    <section v-if="core.co2" class="co2" :class="{ off: !online }" :style="{ '--cr': co2Rgb.join(', ') }">
      <header class="co2-head">
        <div class="co2-title">
          <h3>CO₂ 浓度</h3>
          <span class="co2-sub">二氧化碳</span>
        </div>
        <span class="co2-level">{{ co2Level.label }}</span>
      </header>

      <div class="co2-val">
        <b>{{ co2 === undefined ? '—' : Math.round(co2) }}</b>
        <em>ppm</em>
        <span class="co2-note">{{ co2Level.note }}</span>
      </div>

      <div class="co2-bar">
        <span
          v-if="co2 !== undefined"
          class="co2-marker"
          :style="{ left: `${pct(co2, 400, 2200)}%` }"
        />
      </div>
      <div class="co2-ticks">
        <span>400 优</span><span>1000 良</span><span>1500 一般</span><span>2200+</span>
      </div>
    </section>

    <!-- 历史曲线：本机采样记录，温度/湿度/CO₂ 都能看走势 -->
    <TrendSection
      :did="device.did"
      :metrics="trendMetrics"
      title="历史曲线"
    />

    <!-- 电池 -->
    <section v-if="core.battery" class="batt" :class="{ low: battLow, off: !online }" :style="{ '--br': battRgb }">
      <div class="batt-left">
        <span class="batt-ico">
          <i :style="{ width: `${Math.min(100, Math.max(4, battery ?? 0))}%` }" />
        </span>
        <span class="batt-label">电池电量</span>
      </div>
      <b class="batt-num">{{ battery === undefined ? '—' : Math.round(battery) }}<em>%</em></b>
    </section>
    <p v-if="battLow" class="batt-warn">电量偏低，建议尽快更换电池，否则读数会停止上报</p>

    <!-- 其余参数 / 动作 -->
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
              @click="emit('run-action', a.siid, a.aiid, actionName(a))"
            >
              {{ a.name }}
            </button>
          </div>
        </section>

        <label v-if="debugSiids.size" class="raw-toggle">
          <input v-model="showRaw" type="checkbox" />
          <span>显示设备信息</span>
        </label>
      </div>
    </details>

    <p class="hint">读数由设备主动上报，可点右上角刷新重新读取</p>
  </div>
</template>

<style scoped>
.sp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.sp > section,
.sp > details {
  animation: fade-up 0.32s var(--ease) both;
}

.sp > section:nth-of-type(2) {
  animation-delay: 0.06s;
}

.sp > section:nth-of-type(3) {
  animation-delay: 0.11s;
}

/* ------------------------------------------------ 主仪表盘 */
.hero {
  margin-top: 12px;
  padding: 15px 16px 13px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  position: relative;
  overflow: hidden;
}

/* 顶部一层极淡的双色光，温度和湿度的颜色各占一半 */
.hero::before {
  content: '';
  position: absolute;
  inset: -60% 40% auto -30%;
  height: 220px;
  background: radial-gradient(circle, rgba(var(--tr), 0.16), transparent 66%);
  pointer-events: none;
}

.hero::after {
  content: '';
  position: absolute;
  inset: -60% -30% auto 42%;
  height: 220px;
  background: radial-gradient(circle, rgba(var(--hr), 0.14), transparent 66%);
  pointer-events: none;
}

.hero.off {
  opacity: 0.55;
  filter: saturate(0.5);
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
}

.badge.good {
  color: #12945a;
  background: rgba(23, 178, 106, 0.12);
  border: 1px solid rgba(23, 178, 106, 0.3);
}

.badge.ok {
  color: #b47708;
  background: rgba(236, 168, 44, 0.14);
  border: 1px solid rgba(236, 168, 44, 0.34);
}

.badge.bad {
  color: #c93a3f;
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.3);
}

[data-theme='dark'] .badge.good {
  color: #4ade80;
}
[data-theme='dark'] .badge.ok {
  color: #fbbf24;
}
[data-theme='dark'] .badge.bad {
  color: #f87171;
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

/* 双读数 */
.dual {
  display: flex;
  align-items: stretch;
  gap: 4px;
  margin-top: 14px;
  position: relative;
  z-index: 1;
}

.metric {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.m-line {
  width: 1px;
  flex: none;
  margin: 6px 8px;
  background: linear-gradient(180deg, transparent, var(--border-strong), transparent);
}

.m-label {
  font-size: 11px;
  color: var(--text-3);
  letter-spacing: 0.3px;
}

.m-num {
  font-size: 38px;
  line-height: 1.06;
  font-weight: 700;
  letter-spacing: -1px;
  font-variant-numeric: tabular-nums;
  display: flex;
  align-items: baseline;
  gap: 2px;
  transition: color 0.35s ease;
}

.m-num.temp {
  color: rgb(var(--tr));
}

.m-num.hum {
  color: rgb(var(--hr));
}

.m-num em {
  font-style: normal;
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0;
  opacity: 0.72;
}

.m-sub {
  display: flex;
  align-items: center;
  gap: 6px;
  min-height: 16px;
}

.band-tag {
  font-size: 9.5px;
  color: var(--text-3);
  padding: 1px 6px;
  border-radius: 999px;
  background: var(--surface-3);
  white-space: nowrap;
}

/* 刻度条 */
.bars {
  margin-top: 13px;
  display: flex;
  flex-direction: column;
  gap: 9px;
  position: relative;
  z-index: 1;
}

.bar-row {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.bar {
  position: relative;
  height: 7px;
  border-radius: 999px;
  background: var(--surface-3);
  box-shadow: inset 0 1px 2px rgba(22, 35, 58, 0.07);
}

[data-theme='dark'] .bar {
  background: #0b1320;
  box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.5);
}

/* 理想区间：一条淡绿底带，直接说明"落在这里就舒服" */
.band {
  position: absolute;
  top: 0;
  bottom: 0;
  border-radius: 999px;
  background: rgba(23, 178, 106, 0.26);
  box-shadow: inset 0 0 0 1px rgba(23, 178, 106, 0.34);
}

.marker {
  position: absolute;
  top: 50%;
  width: 11px;
  height: 11px;
  margin: -5.5px 0 0 -5.5px;
  border-radius: 50%;
  border: 2px solid var(--surface);
  box-shadow: 0 1px 5px rgba(22, 35, 58, 0.32);
  transition: left 0.4s var(--ease), background-color 0.35s ease;
}

[data-theme='dark'] .marker {
  border-color: #0d1626;
}

.ticks {
  display: flex;
  justify-content: space-between;
  font-size: 9px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  padding: 0 1px;
}

/* ------------------------------------------------ CO₂ */
.co2 {
  margin-top: 14px;
  padding: 13px 15px 12px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  position: relative;
  overflow: hidden;
}

.co2::before {
  content: '';
  position: absolute;
  inset: 0 auto 0 0;
  width: 3px;
  background: rgb(var(--cr));
  opacity: 0.9;
}

.co2.off {
  opacity: 0.55;
  filter: saturate(0.5);
}

.co2-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 10px;
}

.co2-title {
  display: flex;
  align-items: baseline;
  gap: 7px;
  min-width: 0;
}

h3 {
  margin: 0;
  font-size: 12.5px;
  font-weight: 600;
  color: var(--text-2);
}

.co2-sub {
  font-size: 10px;
  color: var(--text-3);
}

.co2-level {
  flex: none;
  font-size: 11px;
  font-weight: 700;
  padding: 2px 9px;
  border-radius: 999px;
  color: rgb(var(--cr));
  background: rgba(var(--cr), 0.13);
  border: 1px solid rgba(var(--cr), 0.3);
  /* 浅色主题下 800ppm 附近的黄绿太亮，压暗一档才看得清 */
  filter: brightness(0.8) saturate(1.3);
}

[data-theme='dark'] .co2-level {
  filter: none;
}

.co2-val {
  display: flex;
  align-items: baseline;
  gap: 5px;
  margin-top: 7px;
}

.co2-val b {
  font-size: 27px;
  font-weight: 700;
  line-height: 1.1;
  letter-spacing: -0.5px;
  font-variant-numeric: tabular-nums;
  color: rgb(var(--cr));
  transition: color 0.35s ease;
}

.co2-val em {
  font-style: normal;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-3);
  margin-right: 6px;
}

.co2-note {
  font-size: 10.5px;
  color: var(--text-3);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.co2-bar {
  position: relative;
  height: 7px;
  margin-top: 11px;
  border-radius: 999px;
  background: linear-gradient(
    90deg,
    rgb(23, 178, 106) 0%,
    rgb(124, 190, 74) 22%,
    rgb(236, 186, 52) 34%,
    rgb(240, 132, 44) 61%,
    rgb(222, 74, 62) 100%
  );
  opacity: 0.9;
}

.co2-marker {
  position: absolute;
  top: 50%;
  width: 11px;
  height: 11px;
  margin: -5.5px 0 0 -5.5px;
  border-radius: 50%;
  background: #fff;
  border: 2px solid rgba(10, 17, 29, 0.36);
  box-shadow: 0 1px 6px rgba(10, 17, 29, 0.42);
  transition: left 0.45s var(--ease);
}

.co2-ticks {
  display: flex;
  justify-content: space-between;
  margin-top: 4px;
  font-size: 9px;
  color: var(--text-3);
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

/* 电池外形：外框 + 内部按电量填充 */
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

/* 低电量：整个卡片描红一次呼吸，避免"只是小字变色"被忽略 */
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

.raw-toggle {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-top: 10px;
  padding-top: 10px;
  border-top: 1px dashed var(--border);
  font-size: 11.5px;
  color: var(--text-3);
  cursor: pointer;
  user-select: none;
}

.raw-toggle input {
  width: 13px;
  height: 13px;
  accent-color: var(--accent);
  cursor: pointer;
}
</style>
