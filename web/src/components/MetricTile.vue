<script setup lang="ts">
/**
 * 污染物指标块。
 *
 * 与圆形表盘混搭，构成「不同仪表形式」：
 *   - 大数值 —— 扫一眼就知道当前读数；
 *   - 分段刻度条 —— 轨道按「优 / 良 / 轻度污染 / 更差」分成四段并各带底色，
 *     当前值位置有发光游标 + 白色刻线，一眼看出离阈值还有多远；
 *   - 迷你趋势线 —— 只画线不画轴，看最近在涨还是在跌。
 *
 * 宽度全部走百分比，天然填满所在栅格，不会像固定像素表盘那样留空白。
 */
import { computed } from 'vue'
import type { Pt } from '../lib/history'

const props = withDefaults(
  defineProps<{
    label: string
    value?: number
    min?: number
    max?: number
    unit?: string
    decimals?: number
    /** 主题色 "r,g,b" */
    color?: string
    /** 状态词（如「优」「轻度污染」） */
    note?: string
    /** 阈值：优 / 良 / 差 */
    good?: number
    ok?: number
    bad?: number
    /** 迷你趋势数据 */
    points?: Pt[]
    /** 无数据时的占位 */
    empty?: boolean
  }>(),
  {
    min: 0,
    max: 100,
    unit: '',
    decimals: 0,
    color: '47,109,246',
    note: '',
    good: 0,
    ok: 0,
    bad: 0,
    points: () => [],
    empty: false
  }
)

const hasValue = computed(
  () => props.value !== undefined && Number.isFinite(props.value as number)
)

/** 百分比位置 */
function pct(v: number) {
  const span = props.max - props.min
  if (!span) return 0
  return Math.min(100, Math.max(0, ((v - props.min) / span) * 100))
}

const valuePct = computed(() => (hasValue.value ? pct(props.value as number) : 0))

/** 轨道分段：优 / 良 / 轻度污染 / 其余 */
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

/** 刻度数字：起点 / 优 / 良 / 上限 */
const scaleMarks = computed(() => {
  const all = [
    { v: props.min, text: fmtNum(props.min) },
    { v: props.good, text: fmtNum(props.good) },
    { v: props.ok, text: fmtNum(props.ok) },
    { v: props.max, text: fmtNum(props.max) }
  ]
  // 量程两端必留；中间阈值挨太近就只留前一个，否则两组数字会叠在一起
  // （甲醛的 0.08 / 0.1 只差 10%，叠在一块根本看不清）
  const out = [all[0]]
  for (const m of all.slice(1, -1)) {
    const prev = out[out.length - 1]
    if (prev && Math.abs(pct(m.v) - pct(prev.v)) < 12) continue
    out.push(m)
  }
  out.push(all[all.length - 1])
  return out
})

/** 数字格式化：小数位多时去掉多余的 0（0.300 → 0.3，2.000 → 2） */
function fmtNum(v: number) {
  if (!Number.isFinite(v)) return '—'
  if (props.decimals > 0) {
    const s = v.toFixed(props.decimals)
    return s.includes('.') ? s.replace(/\.?0+$/, '') : s
  }
  return String(Math.round(v))
}

const display = computed(() => {
  if (!hasValue.value) return '—'
  const v = props.value as number
  return props.decimals > 0 ? v.toFixed(props.decimals) : String(Math.round(v))
})

/** 迷你趋势线：viewBox 固定比例 + preserveAspectRatio=none 铺满宽度，
 *  描边用 non-scaling-stroke 保持线宽不被拉变形。 */
const spark = computed(() => {
  const pts = props.points.filter((p) => Number.isFinite(p.v))
  if (pts.length < 2) return { d: '', last: false }
  const vs = pts.map((p) => p.v)
  const min = Math.min(...vs)
  const max = Math.max(...vs)
  const span = max - min || 1
  const W = 100
  const H = 24
  const pad = 3
  const d = pts
    .map((p, i) => {
      const x = (i / (pts.length - 1)) * W
      const y = H - pad - ((p.v - min) / span) * (H - pad * 2)
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
    })
    .join(' ')
  return { d, last: true }
})
</script>

<template>
  <div class="mt" :style="{ '--c': color }">
    <header class="mt-head">
      <span class="mt-label">{{ label }}</span>
      <span v-if="note" class="mt-note">{{ note }}</span>
    </header>

    <div class="mt-value">
      <b>{{ display }}</b>
      <em v-if="unit">{{ unit }}</em>
    </div>

    <!-- 分段刻度条 -->
    <div class="mt-track">
      <span
        v-for="(s, i) in segs"
        :key="i"
        class="seg"
        :class="s.cls"
        :style="{ left: `${s.left}%`, width: `${s.width}%` }"
      />
      <span class="fill" :style="{ width: `${valuePct}%` }" />
      <span
        v-if="hasValue"
        class="cursor"
        :style="{ left: `${valuePct}%` }"
      />
    </div>

    <div class="mt-scale">
      <span v-for="(m, i) in scaleMarks" :key="i" :style="{ left: `${pct(m.v)}%` }">{{ m.text }}</span>
    </div>

    <!-- 迷你趋势 -->
    <div class="mt-spark">
      <svg v-if="spark.d" viewBox="0 0 100 24" preserveAspectRatio="none">
        <path :d="spark.d" fill="none" :stroke="`rgb(${color})`" stroke-width="1.8"
          stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke" />
        <line x1="99" y1="0" x2="99" y2="24" :stroke="`rgb(${color})`" stroke-width="1.6"
          vector-effect="non-scaling-stroke" opacity="0.75" />
      </svg>
      <span v-else class="mt-nodata">暂无趋势</span>
    </div>
  </div>
</template>

<style scoped>
.mt {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 11px 12px 10px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface);
  box-shadow: var(--shadow-sm);
  transition: box-shadow 0.2s var(--ease), transform 0.2s var(--ease), border-color 0.2s var(--ease);
  min-width: 0;
  overflow: hidden;
}

/* 顶部一道指标色，扫一眼就能分清是哪个指标 */
.mt::before {
  content: '';
  position: absolute;
  inset: 0 0 auto 0;
  height: 2px;
  background: linear-gradient(90deg, rgb(var(--c)), transparent 78%);
  opacity: 0.75;
}

.mt:hover {
  box-shadow: var(--shadow-md);
  transform: translateY(-1px);
  border-color: var(--border-strong);
}

.mt-head {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.mt-label {
  font-size: 11.5px;
  font-weight: 650;
  color: var(--text-2);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mt-note {
  margin-left: auto;
  flex: none;
  font-size: 10px;
  font-weight: 600;
  color: rgb(var(--c));
  background: rgba(var(--c), 0.12);
  border-radius: 999px;
  padding: 1px 7px;
}

.mt-value {
  display: flex;
  align-items: baseline;
  gap: 3px;
  line-height: 1;
}

.mt-value b {
  font-size: 25px;
  font-weight: 700;
  letter-spacing: -0.8px;
  color: rgb(var(--c));
  font-variant-numeric: tabular-nums;
}

.mt-value em {
  font-style: normal;
  font-size: 10.5px;
  font-weight: 600;
  color: var(--text-3);
}

/* ---- 分段刻度条 ---- */
.mt-track {
  position: relative;
  height: 7px;
  border-radius: 4px;
  background: var(--surface-3);
  overflow: hidden;
  margin-top: 3px;
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

/* ---- 刻度数字 ---- */
.mt-scale {
  position: relative;
  height: 11px;
  margin-top: -1px;
}

.mt-scale span {
  position: absolute;
  top: 0;
  transform: translateX(-50%);
  font-size: 9px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.mt-scale span:first-child {
  transform: none;
}

.mt-scale span:last-child {
  transform: translateX(-100%);
}

/* ---- 迷你趋势 ---- */
.mt-spark {
  height: 24px;
  margin-top: 1px;
}

.mt-spark svg {
  display: block;
  width: 100%;
  height: 24px;
  overflow: visible;
}

.mt-nodata {
  font-size: 9.5px;
  color: var(--text-3);
  opacity: 0.6;
}
</style>
