<script setup lang="ts">
/**
 * 历史趋势折线图（纯 SVG，不引第三方图表库）。
 *
 * 设计要点：
 *  - 坐标按容器实际像素算（ResizeObserver），不做 preserveAspectRatio 拉伸，
 *    否则描边和圆点会被压扁；
 *  - **X 轴是真实时间轴**：给定 `hours` 时固定按「现在往前 N 小时」绘制，
 *    刻度是真实钟点（HH:mm / MM-DD），没有数据的时间段留白，
 *    不把仅有的一小段数据拉伸铺满整张图（那样会让人误以为是完整曲线）；
 *  - 采样中断超过阈值就**断开曲线**，不把两段数据用直线连起来；
 *  - 支持「安全区间」底色（温度 18~26°C、CO₂ 400~1000ppm 这种）；
 *  - 悬停出十字线 + 气泡，标注时间与数值；
 *  - 只有一两个点时也画得出来（画成小折线 + 圆点）。
 */
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { nowTs, thinToWidth, type Pt } from '../lib/history'

const props = withDefaults(
  defineProps<{
    points: Pt[]
    unit?: string
    /** 颜色用 "r,g,b" 形式，方便整体调透明度 */
    color?: string
    height?: number
    decimals?: number
    /** 安全区间（浅色底），越界读数一眼能看出来 */
    band?: [number, number] | null
    /** 强制 Y 轴范围 */
    domain?: [number, number] | null
    /** Y 轴最小跨度：点数很少、值很接近时避免曲线被拉成一根竖线 */
    minSpan?: number
    /** 时间范围标签 */
    rangeLabel?: string
    /** 时间窗（小时）。> 0 时 X 轴固定按「现在往前 N 小时」绘制 */
    hours?: number
    /** 采样间隔超过该秒数就断开曲线；0 = 按实际采样间隔自动推断 */
    gapSec?: number
  }>(),
  {
    unit: '',
    color: '47,109,246',
    height: 92,
    decimals: 1,
    band: null,
    domain: null,
    minSpan: 1,
    rangeLabel: '',
    hours: 0,
    gapSec: 0
  }
)

const wrap = ref<HTMLElement | null>(null)
const width = ref(320)
const hoverIdx = ref(-1)

let ro: ResizeObserver | null = null
onMounted(() => {
  if (!wrap.value) return
  ro = new ResizeObserver(() => {
    if (wrap.value) width.value = Math.max(120, wrap.value.clientWidth)
  })
  ro.observe(wrap.value)
  width.value = Math.max(120, wrap.value.clientWidth)
})
onBeforeUnmount(() => ro?.disconnect())

const PAD = { t: 10, b: 24, l: 4, r: 40 }

const rawPts = computed(() => props.points.filter((p) => Number.isFinite(p.v)))

/**
 * 时间域。
 *
 * 给定 hours（默认 24 小时）时用「现在往前 N 小时」这个**固定窗**，
 * 这样 X 轴刻度落在真实钟点上，缺数据的时段自然留白；
 * 不给 hours 时退化成「按数据首尾铺满」，用于临时预览。
 */
const timeDomain = computed<[number, number]>(() => {
  if (props.hours > 0) {
    const to = nowTs.value
    return [to - props.hours * 3600, to]
  }
  const ts = rawPts.value.map((p) => p.t)
  if (!ts.length) {
    const to = nowTs.value
    return [to - 3600, to]
  }
  return [Math.min(...ts), Math.max(...ts)]
})

/** 落在时间域内的点（域外的裁掉） */
const pts = computed(() => {
  const [a, b] = timeDomain.value
  return rawPts.value.filter((p) => p.t >= a && p.t <= b)
})

const scale = computed(() => {
  const w = width.value
  const h = props.height
  const innerW = Math.max(10, w - PAD.l - PAD.r)
  const innerH = Math.max(10, h - PAD.t - PAD.b)
  const vs = pts.value.map((p) => p.v)
  let min = vs.length ? Math.min(...vs) : 0
  let max = vs.length ? Math.max(...vs) : 1
  if (props.domain) {
    min = props.domain[0]
    max = props.domain[1]
  } else {
    const span = max - min
    const floor = Math.max(1e-6, props.minSpan)
    if (span < floor) {
      // 值几乎没变（刚开始采样、或设备读数稳定）：给一个最小跨度，
      // 否则曲线会被拉成贴着顶/底边的一根线，看不出任何变化
      const mid = (max + min) / 2
      min = mid - floor / 2
      max = mid + floor / 2
    } else {
      const pad = span * 0.18
      min -= pad
      max += pad
    }
  }
  const [t0, t1] = timeDomain.value
  const tSpan = Math.max(1, t1 - t0)
  const x = (t: number) => PAD.l + ((t - t0) / tSpan) * innerW
  const y = (v: number) => PAD.t + innerH - ((v - min) / (max - min)) * innerH
  return { x, y, min, max, innerH, innerW, t0, t1 }
})

// ------------------------------------------------------------ 断点分段

/** 自动推断「多久算一次采样中断」：取实际间隔中位数 × 3，限制在 10~90 分钟 */
function autoGap(p: Pt[]): number {
  if (p.length < 3) return 20 * 60
  const ds: number[] = []
  for (let i = 1; i < p.length; i++) ds.push(p[i].t - p[i - 1].t)
  ds.sort((a, b) => a - b)
  const med = ds[Math.floor(ds.length / 2)] || 300
  return Math.max(10 * 60, Math.min(90 * 60, med * 3))
}

/** 按采样间隔把点切成若干段，段与段之间不连线 */
const segments = computed(() => {
  const p = pts.value
  if (!p.length) return [] as Pt[][]
  const gap = props.gapSec > 0 ? props.gapSec : autoGap(p)
  const out: Pt[][] = []
  let cur: Pt[] = [p[0]]
  for (let i = 1; i < p.length; i++) {
    if (p[i].t - p[i - 1].t > gap) {
      out.push(cur)
      cur = [p[i]]
    } else cur.push(p[i])
  }
  out.push(cur)
  return out
})

/**
 * 画之前先按像素宽度抽稀。
 *
 * 一条 24 小时的曲线能有 2000+ 个采样点，而这张图横向也就三四百像素，
 * 多出来的点根本落不进不同的像素列，只会让 path 字符串变成几十 KB，
 * 窗口一缩放、每分钟刷新都要重新拼一遍。
 *
 * 每段的预算取**它自己的横向跨度**（不是整图宽度），这样几段短数据
 * 不会被抽得只剩折线；段的起点和终点一定会保留，切口不会移位。
 */
const drawSegs = computed(() => {
  const s = scale.value
  return segments.value.map((sg) => {
    if (sg.length < 3) return sg
    const px = s.x(sg[sg.length - 1].t) - s.x(sg[0].t)
    return thinToWidth(sg, px)
  })
})

const linePath = computed(() => {
  const s = scale.value
  return drawSegs.value
    .filter((sg) => sg.length >= 2)
    .map((sg) =>
      sg
        .map((p, i) => `${i === 0 ? 'M' : 'L'}${s.x(p.t).toFixed(1)},${s.y(p.v).toFixed(1)}`)
        .join(' ')
    )
    .join(' ')
})

const areaPath = computed(() => {
  const s = scale.value
  const base = s.y(s.min)
  return drawSegs.value
    .filter((sg) => sg.length >= 2)
    .map((sg) => {
      const first = sg[0]
      const last = sg[sg.length - 1]
      const d = sg
        .map((p, i) => `${i === 0 ? 'M' : 'L'}${s.x(p.t).toFixed(1)},${s.y(p.v).toFixed(1)}`)
        .join(' ')
      return `${d} L${s.x(last.t).toFixed(1)},${base.toFixed(1)} L${s.x(first.t).toFixed(1)},${base.toFixed(1)} Z`
    })
    .join(' ')
})

/** 只有一个点时也画一小段横线，让「有数据」和「没数据」能区分开 */
const soloPath = computed(() => {
  if (pts.value.length !== 1) return ''
  const s = scale.value
  const p = pts.value[0]
  const x = s.x(p.t)
  const y = s.y(p.v)
  return `M${(x - 6).toFixed(1)},${y.toFixed(1)} L${(x + 6).toFixed(1)},${y.toFixed(1)}`
})

/** 落单的点（被采样中断隔出来的孤立点），画成小圆点 */
const isoDots = computed(() => {
  const s = scale.value
  return segments.value
    .filter((sg) => sg.length === 1)
    .map((sg) => ({ x: s.x(sg[0].t), y: s.y(sg[0].v) }))
})

const lastPoint = computed(() => (pts.value.length ? pts.value[pts.value.length - 1] : null))

const bandRect = computed(() => {
  if (!props.band) return null
  const s = scale.value
  const yTop = s.y(Math.max(props.band[1], s.max))
  const yBot = s.y(Math.min(props.band[0], s.min))
  const h = yBot - yTop
  // 安全区几乎铺满整张图时反而成了背景色块，不画
  if (h < s.innerH * 0.12 || h > s.innerH * 0.92) return null
  return { y: yTop, h }
})

const gridLines = computed(() => {
  const s = scale.value
  return [0, 0.5, 1].map((r) => ({
    y: s.y(s.min + (s.max - s.min) * r),
    label: fmt(s.max - (s.max - s.min) * r)
  }))
})

// ------------------------------------------------------------ X 轴时间刻度

/** 可选的时间步长（秒），从小到大 */
const TIME_STEPS = [
  60, 5 * 60, 15 * 60, 30 * 60,
  3600, 2 * 3600, 3 * 3600, 6 * 3600, 12 * 3600, 24 * 3600
]

/** 本地时区偏移（秒，西为正）。用来把刻度对齐到本地整点/整日 */
function tzOffset(ts: number): number {
  return new Date(ts * 1000).getTimezoneOffset() * 60
}

function fmtClock(ts: number): string {
  const d = new Date(ts * 1000)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${p(d.getHours())}:${p(d.getMinutes())}`
}

function fmtDay(ts: number): string {
  const d = new Date(ts * 1000)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${p(d.getMonth() + 1)}-${p(d.getDate())}`
}

/** 在 [t0,t1] 内按 step 生成对齐到本地时间的刻度 */
function ticksIn(t0: number, t1: number, step: number): number[] {
  const out: number[] = []
  const off = tzOffset(t0)
  let k = Math.ceil((t0 - off) / step - 1e-9)
  for (let guard = 0; guard < 200; guard++, k++) {
    const t = k * step + off
    if (t > t1 + 1e-9) break
    if (t >= t0 - 1e-9) out.push(t)
  }
  return out
}

/** 标签格式：跨天用「月-日」，1 天以上但步长小于 1 天用「月-日 时:分」，否则「时:分」 */
function tickLabel(t: number, step: number, span: number): string {
  if (step >= 86400) return fmtDay(t)
  if (span > 48 * 3600) return `${fmtDay(t)} ${fmtClock(t)}`
  return fmtClock(t)
}

const xTicks = computed(() => {
  const s = scale.value
  const span = s.t1 - s.t0
  if (span <= 0) return []
  // 按容器宽度决定最多能放几个标签，避免挤成一团
  const maxTicks = Math.max(2, Math.min(6, Math.floor(s.innerW / 58)))
  let chosen = TIME_STEPS[TIME_STEPS.length - 1]
  let list: number[] = []
  for (const st of TIME_STEPS) {
    const l = ticksIn(s.t0, s.t1, st)
    if (l.length >= 2 && l.length <= maxTicks) {
      chosen = st
      list = l
      break
    }
    if (l.length < 2) continue
  }
  if (!list.length) list = ticksIn(s.t0, s.t1, chosen)
  return list.map((t) => ({ t, x: s.x(t), label: tickLabel(t, chosen, span) }))
})

/**
 * X 轴最终标签集合。
 * 固定时间窗下最右端就是「现在」，单独标出来；
 * 若它和最后一个整点刻度挨得太近，就让出整点刻度，避免文字重叠。
 */
const axis = computed(() => {
  const s = scale.value
  const right = PAD.l + s.innerW
  let ticks = xTicks.value
  let nowLabel = ''
  if (props.hours > 0) {
    nowLabel = fmtClock(s.t1)
    if (ticks.length && right - ticks[ticks.length - 1].x < 48) ticks = ticks.slice(0, -1)
  }
  return { ticks, nowLabel, nowX: right }
})

// ------------------------------------------------------------ 覆盖度提示

/**
 * 数据覆盖度提示。
 * 只有在「这个时间窗里的记录没能从头到尾都有」时才说一句，免得用户把半截曲线
 * 当成一整天的真实走势；数据齐全就不打扰，也不要在界面上摆采样率之类的内部数字。
 */
const coverage = computed(() => {
  if (props.hours <= 0) return ''
  if (!pts.value.length) return '暂无数据'
  const first = Math.min(...pts.value.map((p) => p.t))
  const b = timeDomain.value[1]
  if ((b - first) / 3600 >= props.hours - 0.1) return ''
  return `从 ${fmtClock(first)} 开始有记录`
})

function fmt(v: number) {
  if (!Number.isFinite(v)) return '—'
  const d = props.decimals
  return Number.isInteger(v) && d === 1 ? String(v) : v.toFixed(d)
}

function fmtTime(ts: number) {
  const d = new Date(ts * 1000)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

function onMove(ev: MouseEvent) {
  if (pts.value.length < 1) return
  const rect = (ev.currentTarget as SVGElement).getBoundingClientRect()
  const x = ev.clientX - rect.left
  const s = scale.value
  let best = 0
  let bestD = Infinity
  pts.value.forEach((p, i) => {
    const d = Math.abs(s.x(p.t) - x)
    if (d < bestD) {
      bestD = d
      best = i
    }
  })
  hoverIdx.value = best
}

const hover = computed(() => {
  const i = hoverIdx.value
  if (i < 0 || i >= pts.value.length) return null
  const s = scale.value
  const p = pts.value[i]
  return { p, x: s.x(p.t), y: s.y(p.v) }
})

watch(
  () => [props.points.length, props.hours],
  () => {
    hoverIdx.value = -1
  }
)
</script>

<template>
  <div ref="wrap" class="trend" :style="{ height: `${height}px` }">
    <svg
      :width="width"
      :height="height"
      :viewBox="`0 0 ${width} ${height}`"
      @mousemove="onMove"
      @mouseleave="hoverIdx = -1"
    >
      <defs>
        <linearGradient :id="`tg-${color.replace(/[^0-9]/g, '')}`" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" :stop-color="`rgba(${color},0.24)`" />
          <stop offset="100%" :stop-color="`rgba(${color},0.01)`" />
        </linearGradient>
      </defs>

      <!-- 横向网格 + Y 轴刻度 -->
      <g class="grid">
        <line
          v-for="(g, i) in gridLines"
          :key="i"
          :x1="PAD.l"
          :x2="width - PAD.r"
          :y1="g.y"
          :y2="g.y"
        />
      </g>

      <!-- 纵向网格（对应 X 轴时间刻度） -->
      <g class="grid v">
        <line
          v-for="(t, i) in axis.ticks"
          :key="'v' + i"
          :x1="t.x"
          :x2="t.x"
          :y1="PAD.t"
          :y2="height - PAD.b"
        />
      </g>

      <!-- 安全区间底色 -->
      <rect
        v-if="bandRect"
        class="band"
        :x="PAD.l"
        :y="bandRect.y"
        :width="width - PAD.l - PAD.r"
        :height="bandRect.h"
      />

      <!-- 曲线 -->
      <path v-if="areaPath" class="area" :d="areaPath" :fill="`url(#tg-${color.replace(/[^0-9]/g, '')})`" />
      <path v-if="linePath" class="line" :d="linePath" :stroke="`rgb(${color})`" />
      <path v-if="soloPath" class="line" :d="soloPath" :stroke="`rgb(${color})`" />
      <circle
        v-for="(d, i) in isoDots"
        :key="'iso' + i"
        class="dot"
        :cx="d.x"
        :cy="d.y"
        r="2.8"
        :fill="`rgb(${color})`"
      />

      <!-- 末点 -->
      <g v-if="lastPoint">
        <circle
          class="pulse"
          :cx="scale.x(lastPoint.t)"
          :cy="scale.y(lastPoint.v)"
          r="6"
          :fill="`rgba(${color},0.20)`"
        />
        <circle
          class="dot"
          :cx="scale.x(lastPoint.t)"
          :cy="scale.y(lastPoint.v)"
          r="3.4"
          :fill="`rgb(${color})`"
        />
      </g>

      <!-- 悬停十字线 -->
      <g v-if="hover">
        <line
          class="cross"
          :x1="hover.x"
          :x2="hover.x"
          :y1="PAD.t"
          :y2="height - PAD.b"
        />
        <circle :cx="hover.x" :cy="hover.y" r="4" :fill="`rgb(${color})`" class="hover-dot" />
      </g>

      <!-- Y 轴刻度 -->
      <g class="axis">
        <text v-for="(g, i) in gridLines" :key="i" :x="width - PAD.r + 6" :y="g.y + 3.5">
          {{ g.label }}
        </text>
      </g>

      <!-- X 轴时间刻度 -->
      <g class="axis x-axis">
        <text
          v-for="(t, i) in axis.ticks"
          :key="'xt' + i"
          :x="t.x"
          :y="height - PAD.b + 13"
          text-anchor="middle"
        >
          {{ t.label }}
        </text>
        <text v-if="axis.nowLabel" class="now" :x="axis.nowX" :y="height - PAD.b + 13" text-anchor="end">
          {{ axis.nowLabel }}
        </text>
      </g>
    </svg>

    <div v-if="hover" class="tip" :style="{ left: `${hover.x}px`, top: `${Math.max(0, hover.y - 34)}px` }">
      <b>{{ fmt(hover.p.v) }}{{ unit }}</b>
      <span>{{ fmtTime(hover.p.t) }}</span>
    </div>

    <div v-if="!pts.length" class="empty">
      <svg width="26" height="14" viewBox="0 0 26 14">
        <path d="M1 11 L8 8 L13 10 L19 4 L25 6" fill="none" stroke="currentColor" stroke-width="1.6"
          stroke-dasharray="3 3" stroke-linecap="round" />
      </svg>
      <span>暂无历史数据</span>
    </div>

    <div v-else-if="rangeLabel || coverage" class="range">
      {{ rangeLabel }}<span v-if="coverage"> · {{ coverage }}</span>
    </div>
  </div>
</template>

<style scoped>
.trend {
  position: relative;
  width: 100%;
}

svg {
  display: block;
  overflow: visible;
}

.grid line {
  stroke: var(--border);
  stroke-width: 1;
  stroke-dasharray: 3 4;
  opacity: 0.75;
}

.grid.v line {
  opacity: 0.45;
}

.band {
  fill: rgba(23, 178, 106, 0.07);
}

.line {
  fill: none;
  stroke-width: 2;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.area {
  stroke: none;
}

.dot {
  stroke: var(--surface);
  stroke-width: 1.6;
}

.pulse {
  animation: tg-pulse 2.4s ease-out infinite;
  transform-box: fill-box;
  transform-origin: center;
}

@keyframes tg-pulse {
  0% {
    opacity: 0.75;
    transform: scale(0.7);
  }
  70% {
    opacity: 0;
    transform: scale(1.9);
  }
  100% {
    opacity: 0;
    transform: scale(1.9);
  }
}

.cross {
  stroke: var(--text-3);
  stroke-width: 1;
  stroke-dasharray: 3 3;
}

.hover-dot {
  stroke: var(--surface);
  stroke-width: 1.6;
}

.axis text {
  fill: var(--text-3);
  font-size: 10px;
  font-variant-numeric: tabular-nums;
}

.x-axis text {
  fill: var(--text-3);
  opacity: 0.9;
}

.x-axis .now {
  fill: var(--brand);
  opacity: 1;
}

.tip {
  position: absolute;
  transform: translate(-50%, 0);
  background: var(--surface);
  border: 1px solid var(--border-strong);
  border-radius: 8px;
  box-shadow: var(--shadow-md);
  padding: 4px 8px;
  font-size: 11px;
  line-height: 1.35;
  white-space: nowrap;
  pointer-events: none;
  z-index: 3;
}

.tip b {
  display: block;
  font-size: 12px;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.tip span {
  color: var(--text-3);
}

.empty {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  color: var(--text-3);
  font-size: 11px;
  pointer-events: none;
}

.range {
  position: absolute;
  left: 0;
  top: 0;
  font-size: 10px;
  color: var(--text-3);
  pointer-events: none;
}
</style>
