<script setup lang="ts">
/**
 * 传感器钉子板上的一个读数。
 *
 * 同一个读数有两种看法，用户可以逐条切：
 *   gauge —— 弧形表盘 + 指针，适合看「在量程里的位置」；
 *   bar   —— 大数字 + 分段进度条 + 迷你趋势，适合看「离阈值还有多远」。
 *
 * 两种样式共用同一套量程（METRIC_RANGE）与配色（metricState），
 * 切换样式不会让读数看起来变了。
 */
import { computed } from 'vue'
import ArcGauge from './ArcGauge.vue'
import type { Pt } from '../lib/history'
import type { PinStyle } from '../lib/sensorPins'

const props = withDefaults(
  defineProps<{
    label: string
    /** 设备名（钉的是哪个探头） */
    device: string
    value?: number
    min: number
    max: number
    decimals?: number
    unit?: string
    /** 主题色 "r,g,b" */
    color?: string
    /** 评价词：舒适 / 优 / 轻度污染 … */
    note?: string
    /** 温湿度的舒适区间（表盘弧上标绿） */
    band?: [number, number] | null
    /** 进度条的分段阈值 */
    good?: number
    ok?: number
    bad?: number
    /** 展示样式（不能叫 style：那是 Vue 的样式绑定，同名 prop 会被吃掉） */
    mode: PinStyle
    online?: boolean
    points?: Pt[]
  }>(),
  {
    value: undefined,
    decimals: 0,
    unit: '',
    color: '47,109,246',
    note: '',
    band: null,
    good: 0,
    ok: 0,
    bad: 0,
    online: true,
    points: () => []
  }
)

const emit = defineEmits<{
  (e: 'toggle-style', style: PinStyle): void
  (e: 'remove'): void
}>()

const hasValue = computed(() => props.value !== undefined && Number.isFinite(props.value as number))

const display = computed(() => {
  if (!hasValue.value) return '—'
  const v = props.value as number
  return props.decimals > 0 ? v.toFixed(props.decimals) : String(Math.round(v))
})

/** 表盘中间刻度：0° 弧顶就是量程中点，所以取量程中点（不能取舒适区中点，否则数字与指针位置不符） */
const mid = computed(() => (props.min + props.max) / 2)

function pct(v: number) {
  const span = props.max - props.min
  if (!span) return 0
  return Math.min(100, Math.max(0, ((v - props.min) / span) * 100))
}

const valuePct = computed(() => (hasValue.value ? pct(props.value as number) : 0))

/** 进度条轨道分段：优 / 良 / 差 / 更差 */
const segs = computed(() => {
  const g = pct(props.good)
  const o = pct(props.ok)
  const b = pct(props.bad)
  return [
    { cls: 'good', left: 0, width: g },
    { cls: 'ok', left: g, width: Math.max(0, o - g) },
    { cls: 'bad', left: o, width: Math.max(0, b - o) },
    { cls: 'worst', left: b, width: Math.max(0, 100 - b) }
  ]
})

/** 刻度数字：两端必留，中间阈值挨太近就丢掉（否则会叠字） */
function fmtNum(v: number) {
  if (!Number.isFinite(v)) return '—'
  if (props.decimals > 0) {
    const s = v.toFixed(props.decimals)
    return s.includes('.') ? s.replace(/\.?0+$/, '') : s
  }
  return String(Math.round(v))
}

const scaleMarks = computed(() => {
  const all = [props.min, props.good, props.max]
  const out = [all[0]]
  for (const v of all.slice(1, -1)) {
    const prev = out[out.length - 1]
    if (prev !== undefined && Math.abs(pct(v) - pct(prev)) < 14) continue
    out.push(v)
  }
  out.push(all[all.length - 1])
  return out.map((v) => ({ v, text: fmtNum(v) }))
})

/**
 * 底部趋势曲线。
 *
 * 两种样式共用同一条曲线（原来只有进度条样式有迷你线，仪表盘样式看不到走势）。
 * 四个约定：
 *  - **横轴按真实时间比例**，不按数组下标：采样间隔不均时下标会把密集段压扁，
 *    曲线形状失真；
 *  - **只画线不铺面积**：与设备卡片 / 「全屋空气」指标块的迷你趋势保持同一种语言，
 *    22px 的高度里铺面积只会变成几块看不懂的色斑；
 *  - **长时间没采样才断线**：门限取中位间隔的 5 倍、且不少于 30 分钟。太敏感会把
 *    曲线碎成一堆短线（采样只在程序运行时进行，重启就断一次）；
 *  - **窄碎片并回相邻段**：断线后只剩两三个点时，那一段在 22px 上就是个看不出含义
 *    的小色块（还会显得像「线断了又冒出个点」），宽度不足一成的一律接回去。
 * viewBox 固定 + preserveAspectRatio=none 铺满宽度，描边用 non-scaling-stroke
 * 保持线宽不被横向拉伸变形。
 */
const SPARK_H = 22
const SPARK_PAD = 3
/** 横向留边：给末端的圆点留出半个直径，不然会贴着卡片边框 */
const SPARK_INSET = 1.5
/** 曲线跨度（小时）：卡片只有一两百像素宽，看的是「一天里怎么变」 */
const SPARK_HOURS = 24
/**
 * 曲线上限点数。
 *
 * 卡片宽度约 160px，超过这个数一个像素里就挤了好几个点，
 * 云端回填（约 37 秒一个点）24 小时能给 2000+ 点，画出来全是竖毛刺。
 */
const SPARK_MAX_POINTS = 180

/** 按时间桶取平均抽稀：保留首尾，桶内做均值，空洞桶自然跳过 */
function bucketAverage(pts: Pt[], target: number): Pt[] {
  const t0 = pts[0].t
  const step = Math.max((pts[pts.length - 1].t - t0) / target, 1)
  const buckets = new Map<number, { ts: number; sum: number; n: number }>()
  for (const p of pts) {
    const b = Math.floor((p.t - t0) / step)
    const cur = buckets.get(b) || { ts: 0, sum: 0, n: 0 }
    cur.ts += p.t
    cur.sum += p.v
    cur.n += 1
    buckets.set(b, cur)
  }
  return [...buckets.entries()]
    .sort((a, b) => a[0] - b[0])
    .map(([, c]) => ({ t: Math.round(c.ts / c.n), v: c.sum / c.n }))
}

/**
 * 时间刻度文案。
 *
 * 卡片窄，三个刻度挤在一行，所以只留 `HH:MM`；跨天的那个加「昨」区分 ——
 * 24 小时窗口的左端一般落在昨天同一时刻，不标出来会让人以为是今天的数据。
 */
function fmtTick(ts: number): string {
  const d = new Date(ts * 1000)
  const hm = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
  const today = new Date()
  if (d.toDateString() === today.toDateString()) return hm
  const yst = new Date(today)
  yst.setDate(yst.getDate() - 1)
  if (d.toDateString() === yst.toDateString()) return `昨 ${hm}`
  return `${d.getMonth() + 1}/${d.getDate()} ${hm}`
}

const spark = computed<{ d: string[]; lastX: number; lastY: number; ticks: string[] } | null>(() => {
  const raw = props.points.filter((p) => Number.isFinite(p.v) && Number.isFinite(p.t))
  if (raw.length < 2) return null
  const t0 = raw[0].t
  const t1 = raw[raw.length - 1].t
  const dt = t1 - t0 || 1

  // 云端回填后一条曲线能有 2000+ 点，挤进 100 单位的 viewBox 就是一坨毛刺
  // （整数上报的温度会变成楼梯块）。按时间桶取平均压到 ~180 点，
  // 形状不变、毛刺消失。
  const pts = raw.length > SPARK_MAX_POINTS ? bucketAverage(raw, SPARK_MAX_POINTS) : raw
  const vs = pts.map((p) => p.v)
  const lo = Math.min(...vs)
  const hi = Math.max(...vs)
  const span = hi - lo || 1
  const tx = (t: number) => SPARK_INSET + ((t - t0) / dt) * (100 - SPARK_INSET * 2)
  const vy = (v: number) => SPARK_H - SPARK_PAD - ((v - lo) / span) * (SPARK_H - SPARK_PAD * 2)

  // ⚠️ 断线阈值要用「原始」采样间隔算：抽样之后间隔被桶宽拉大，
  // 再用它算 cut 会把真断档也连起来。
  const gaps = raw.slice(1).map((p, i) => p.t - raw[i].t).sort((a, b) => a - b)
  const middle = gaps.length ? gaps[Math.floor(gaps.length / 2)] : 0
  const cut = Math.max(middle * 5, 30 * 60)

  // 先按时间连续切成若干段
  const runs: number[][] = []
  let cur: number[] = [0]
  for (let i = 1; i < pts.length; i += 1) {
    if (pts[i].t - pts[i - 1].t > cut) {
      runs.push(cur)
      cur = [i]
    } else {
      cur.push(i)
    }
  }
  runs.push(cur)

  /**
   * 碎片 = 只有一两个采样点、或者只覆盖十几分钟的段。
   *
   * ⚠️ 判据不能看「这一段占多少宽度」：24 小时窗口里一段 2 小时的数据也就一成宽，
   * 按宽度判会把它当成碎片接回上一段，于是跨夜的 17 小时空档被一条直线抹平。
   */
  const isFrag = (r: number[]) =>
    r.length < 3 || pts[r[r.length - 1]].t - pts[r[0]].t < 20 * 60

  const merged: number[][] = []
  for (const r of runs) {
    // 末尾的碎片接回前一段
    if (merged.length && isFrag(r)) merged[merged.length - 1].push(...r)
    else merged.push(r.slice())
  }
  // 开头的碎片接到下一段前面
  if (merged.length > 1 && isFrag(merged[0])) {
    merged[1] = [...merged[0], ...merged[1]]
    merged.shift()
  }

  const d = merged
    .map((r) =>
      r
        .map((i, k) => {
          const x = tx(pts[i].t).toFixed(2)
          const y = vy(pts[i].v).toFixed(2)
          return `${k === 0 ? 'M' : 'L'}${x},${y}`
        })
        .join(' ')
    )
    .filter((s) => s.includes('L'))

  if (!d.length) return null
  const last = pts[pts.length - 1]
  return {
    d,
    lastX: tx(last.t),
    lastY: vy(last.v),
    // 只打三个刻度：起点、中点、终点；中点按时间算，和曲线的横轴口径一致
    ticks: [fmtTick(t0), fmtTick(Math.round(t0 + (t1 - t0) / 2)), fmtTick(t1)]
  }
})
</script>

<template>
  <article
    class="pin"
    :class="[`s-${mode}`, { off: !online }]"
    draggable="true"
    :title="online ? '拖动可调整顺序' : '设备离线，显示最后一次读数'"
  >
    <header class="pin-head">
      <span class="pin-dev" :title="device">{{ device }}</span>
      <span class="pin-dot" :class="{ live: online }" :title="online ? '在线' : '离线'" />
    </header>

    <!-- 仪表指针 -->
    <div v-if="mode === 'gauge'" class="pin-gauge">
      <ArcGauge
        :value="value"
        :min="min"
        :max="max"
        :mid="mid"
        :decimals="decimals"
        :unit="unit"
        :label="label"
        :color="color"
        :band="band"
      />
    </div>

    <!-- 进度条 -->
    <div v-else class="pin-bar">
      <div class="pb-value">
        <b>{{ display }}</b>
        <em v-if="unit">{{ unit }}</em>
      </div>
      <span class="pb-label">{{ label }}</span>
      <div class="pb-track">
        <span
          v-for="(s, i) in segs"
          :key="i"
          class="seg"
          :class="s.cls"
          :style="{ left: `${s.left}%`, width: `${s.width}%` }"
        />
        <span class="fill" :style="{ width: `${valuePct}%` }" />
        <span v-if="hasValue" class="cursor" :style="{ left: `${valuePct}%` }" />
      </div>
      <div class="pb-scale">
        <span v-for="(m, i) in scaleMarks" :key="i" :style="{ left: `${pct(m.v)}%` }">{{ m.text }}</span>
      </div>
    </div>

    <!-- 底部趋势曲线：两种样式都有，没有历史就不占位 -->
    <div v-if="spark" class="pin-spark" :title="`最近 ${SPARK_HOURS} 小时走势`">
      <svg class="ps-curve" :viewBox="`0 0 100 ${SPARK_H}`" preserveAspectRatio="none" aria-hidden="true">
        <path
          v-for="(d, i) in spark.d"
          :key="i"
          :d="d"
          fill="none"
          :stroke="`rgb(${color})`"
          stroke-width="1.6"
          stroke-linecap="round"
          stroke-linejoin="round"
          vector-effect="non-scaling-stroke"
        />
      </svg>
      <i
        class="ps-dot"
        :style="{
          left: `${spark.lastX}%`,
          top: `${spark.lastY}px`,
          background: `rgb(${color})`,
          boxShadow: `0 0 0 2px rgba(${color}, 0.16)`
        }"
      />
      <!-- 只打三个刻度：起点 / 中点 / 终点 -->
      <div class="ps-axis">
        <span v-for="(t, i) in spark.ticks" :key="i" :class="`t${i}`">{{ t }}</span>
      </div>
    </div>
    <span v-else class="ps-empty">近 {{ SPARK_HOURS }} 小时暂无数据</span>

    <footer class="pin-foot">
      <span class="pin-note" :style="{ '--c': color }">{{ note }}</span>
      <div class="pin-tools">
        <button
          class="ptb"
          :class="{ active: mode === 'gauge' }"
          title="仪表指针"
          aria-label="切换为仪表指针"
          @click.stop="emit('toggle-style', 'gauge')"
        >
          <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
            <path d="M3.2 12.2a5.6 5.6 0 1 1 9.6 0" stroke="currentColor" stroke-width="1.4"
              stroke-linecap="round" />
            <path d="M8 8.6l2.6-2.2" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" />
            <circle cx="8" cy="8.6" r="0.9" fill="currentColor" />
          </svg>
        </button>
        <button
          class="ptb"
          :class="{ active: mode === 'bar' }"
          title="进度条"
          aria-label="切换为进度条"
          @click.stop="emit('toggle-style', 'bar')"
        >
          <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
            <rect x="2" y="4" width="12" height="2.4" rx="1.2" fill="currentColor" opacity="0.9" />
            <rect x="2" y="9.6" width="12" height="2.4" rx="1.2" fill="currentColor" opacity="0.35" />
          </svg>
        </button>
        <button class="ptb rm" title="从钉子板移除" aria-label="移除" @click.stop="emit('remove')">
          <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
            <path d="M5 5l6 6M11 5l-6 6" stroke="currentColor" stroke-width="1.5"
              stroke-linecap="round" />
          </svg>
        </button>
      </div>
    </footer>
  </article>
</template>

<style scoped>
.pin {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 0;
  padding: 9px 11px 7px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface);
  box-shadow: var(--shadow-sm);
  cursor: grab;
  transition: box-shadow 0.2s var(--ease), transform 0.2s var(--ease), border-color 0.2s var(--ease);
}

.pin:hover {
  box-shadow: var(--shadow-md);
  transform: translateY(-1px);
  border-color: var(--border-strong);
}

.pin.dragging {
  opacity: 0.4;
}

.pin.drop-target {
  border-color: var(--brand);
  box-shadow: 0 0 0 2px var(--brand-soft), var(--shadow-md);
}

.pin.off {
  opacity: 0.72;
}

.pin-head {
  display: flex;
  align-items: center;
  gap: 5px;
  min-width: 0;
}

.pin-dev {
  font-size: 10.5px;
  color: var(--text-3);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.pin-dot {
  margin-left: auto;
  flex: none;
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--border-strong);
}

.pin-dot.live {
  background: var(--accent);
  box-shadow: 0 0 0 3px rgba(23, 178, 106, 0.14);
}

/* ---- 仪表指针 ---- */
.pin-gauge {
  flex: 1;
  display: grid;
  place-items: center;
  min-width: 0;
}

/* ---- 进度条 ---- */
.pin-bar {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 3px;
  min-width: 0;
  padding: 2px 0;
}

.pb-value {
  display: flex;
  align-items: baseline;
  gap: 3px;
  line-height: 1;
}

.pb-value b {
  font-size: 27px;
  font-weight: 700;
  letter-spacing: -1px;
  color: rgb(var(--c));
  font-variant-numeric: tabular-nums;
}

.pb-value em {
  font-style: normal;
  font-size: 10.5px;
  font-weight: 600;
  color: var(--text-3);
}

.pb-label {
  font-size: 10.5px;
  color: var(--text-3);
}

.pb-track {
  position: relative;
  height: 7px;
  margin-top: 2px;
  border-radius: 4px;
  background: var(--surface-3);
  overflow: hidden;
}

.seg {
  position: absolute;
  top: 0;
  bottom: 0;
}

.seg.good {
  background: rgba(23, 178, 106, 0.16);
}

.seg.ok {
  background: rgba(224, 160, 0, 0.16);
}

.seg.bad {
  background: rgba(242, 98, 15, 0.18);
}

.seg.worst {
  background: rgba(229, 72, 77, 0.2);
}

.fill {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  border-radius: 4px;
  background: linear-gradient(90deg, rgba(var(--c), 0.45), rgb(var(--c)));
  box-shadow: 0 0 6px rgba(var(--c), 0.22);
  transition: width 0.55s var(--ease);
}

.cursor {
  position: absolute;
  top: -2px;
  bottom: -2px;
  width: 2px;
  margin-left: -1px;
  border-radius: 1px;
  background: #fff;
  box-shadow: 0 0 4px rgba(var(--c), 0.5);
  transition: left 0.55s var(--ease);
}

.pb-scale {
  position: relative;
  height: 10px;
}

.pb-scale span {
  position: absolute;
  top: 0;
  transform: translateX(-50%);
  font-size: 9px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.pb-scale span:first-child {
  transform: none;
}

.pb-scale span:last-child {
  transform: translateX(-100%);
}

/* ---- 底部趋势曲线（两种样式共用） ---- */
.pin-spark {
  position: relative;
  margin-top: -2px;
}

.ps-curve {
  display: block;
  width: 100%;
  height: 22px;
  overflow: visible;
}

/* 最新采样点：圆点跟着曲线走 ——
 * 不用 SVG <circle> 是因为 preserveAspectRatio=none 会把圆拉成椭圆 */
.ps-dot {
  position: absolute;
  width: 4px;
  height: 4px;
  margin: -2px 0 0 -2px;
  border-radius: 50%;
  pointer-events: none;
}

/* 三个时间刻度：起点贴左、中点居中、终点贴右 */
.ps-axis {
  position: relative;
  height: 12px;
  margin-top: 1px;
  font-size: 9px;
  line-height: 12px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.ps-axis span {
  position: absolute;
  top: 0;
  white-space: nowrap;
}

.ps-axis .t0 {
  left: 0;
}

.ps-axis .t1 {
  left: 50%;
  transform: translateX(-50%);
}

.ps-axis .t2 {
  right: 0;
}

.ps-empty {
  font-size: 9px;
  line-height: 1.2;
  color: var(--text-3);
  opacity: 0.55;
}

/* ---- 底栏：评价词 + 工具 ---- */
.pin-foot {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.pin-note {
  font-size: 10px;
  font-weight: 600;
  color: rgb(var(--c));
  background: rgba(var(--c), 0.1);
  border-radius: 999px;
  padding: 1px 7px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

.pin-tools {
  margin-left: auto;
  flex: none;
  display: flex;
  gap: 1px;
  opacity: 0;
  transition: opacity 0.15s ease;
}

.pin:hover .pin-tools {
  opacity: 1;
}

.ptb {
  width: 18px;
  height: 18px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 0;
  border-radius: 5px;
  background: transparent;
  color: var(--text-3);
  cursor: pointer;
  transition: color 0.15s ease, background-color 0.15s ease;
}

.ptb svg {
  width: 13px;
  height: 13px;
}

.ptb:hover {
  color: var(--brand);
  background: var(--brand-soft);
}

.ptb.active {
  color: var(--brand);
  background: var(--brand-soft);
}

.ptb.rm:hover {
  color: var(--danger);
  background: rgba(229, 72, 77, 0.12);
}

/* 触摸 / 键盘用户看不到 hover，工具条常显 */
@media (hover: none) {
  .pin-tools {
    opacity: 1;
  }
}
</style>
