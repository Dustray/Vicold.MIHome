<script setup lang="ts">
/**
 * 大号自适应弧形仪表盘（温湿度用）。
 *
 * 与旧的 GaugeDial（270° 指针表盘、固定像素宽）相比：
 *  - **自适应宽度**：SVG 用 viewBox + width:100%，表盘随容器拉伸填满，
 *    窗口放大时不会在格子两侧留下大片空白；
 *  - **240° 弧 + 底部缺口**：读数落在缺口正中，指针不压字；
 *  - **刻度带数字**（最小 / 中间 / 最大），不用猜量程；
 *  - 数值弧走渐变 + 发光，指针有拖影，中心带辉光。
 */
import { computed, useId } from 'vue'

const props = withDefaults(
  defineProps<{
    /** 当前值；undefined/null 表示无数据 */
    value?: number | null
    min?: number
    max?: number
    unit?: string
    decimals?: number
    label?: string
    /** 主题色 "r,g,b" */
    color?: string
    /** 安全区间（弧上用浅绿标出） */
    band?: [number, number] | null
    /** 指针下方的小字说明 */
    note?: string
    /** 中间刻度值，默认 (min+max)/2 */
    mid?: number
  }>(),
  {
    min: 0,
    max: 100,
    unit: '',
    decimals: 0,
    label: '',
    color: '47,109,246',
    band: null,
    note: '',
    mid: undefined
  }
)

const CX = 100
const CY = 96
const R = 72
const CIRC = 2 * Math.PI * R
/** 弧占 240°，缺口（120°）朝下 */
const SWEEP = 240
const SPAN = SWEEP / 360
/** 读数盘半径：指针从盘缘往外伸，读数落在盘里，两者不打架 */
const FACE_R = 33
/** 指针外端到圆心的距离 */
const NEEDLE_OUT = R - 4
const NEEDLE_IN = FACE_R + 1

const uid = useId()
const gradId = `ag-${uid}`
const glowId = `ag-glow-${uid}`

const hasValue = computed(
  () =>
    props.value !== null &&
    props.value !== undefined &&
    Number.isFinite(props.value as number)
)

const frac = computed(() => {
  if (!hasValue.value) return 0
  const span = props.max - props.min
  if (!span) return 0
  return Math.min(1, Math.max(0, ((props.value as number) - props.min) / span))
})

/**
 * 指针角度：-120°（左下）→ +120°（右下），途经正上方。
 *
 * 与 polar() 的数学约定一致（0°朝上、顺时针为正），
 * 所以指针线段本身画成「朝上」的固定形状，再靠 CSS rotate 转到目标角度 ——
 * 端点也用 polar() 算的话会被转两次，指针会指错方向。
 */
const angle = computed(() => -120 + SWEEP * frac.value)

function polar(deg: number, r: number) {
  const rad = (deg * Math.PI) / 180
  return { x: CX + r * Math.sin(rad), y: CY - r * Math.cos(rad) }
}

/** 刻度线：每 2 格一根，整 4 格为长刻度 */
const tickMarks = computed(() =>
  Array.from({ length: 25 }, (_, i) => {
    const g = i / 24
    const deg = -120 + SWEEP * g
    const major = i % 4 === 0
    const mid = i % 2 === 0
    const outer = polar(deg, R + 3)
    const inner = polar(deg, R - (major ? 10 : mid ? 7 : 4.5))
    return { x1: inner.x, y1: inner.y, x2: outer.x, y2: outer.y, major, mid }
  })
)

function fmt(v: number) {
  if (!Number.isFinite(v)) return '—'
  return props.decimals > 0 ? v.toFixed(props.decimals) : String(Math.round(v))
}

/** 刻度上的三个数字：最小 / 中间 / 最大 */
const scaleLabels = computed(() => {
  const mid = props.mid !== undefined ? props.mid : (props.min + props.max) / 2
  return [
    { x: polar(-120, R + 17).x, y: polar(-120, R + 17).y, text: fmt(props.min) },
    { x: polar(0, R + 17).x, y: polar(0, R + 17).y, text: fmt(mid) },
    { x: polar(120, R + 17).x, y: polar(120, R + 17).y, text: fmt(props.max) }
  ]
})

/** 安全区间弧 */
const bandArc = computed(() => {
  if (!props.band) return null
  const span = props.max - props.min
  if (!span) return null
  const g0 = Math.min(1, Math.max(0, (props.band[0] - props.min) / span))
  const g1 = Math.min(1, Math.max(0, (props.band[1] - props.min) / span))
  if (g1 <= g0) return null
  const len = (g1 - g0) * SPAN * CIRC
  return { dash: `${len.toFixed(2)} ${CIRC.toFixed(2)}`, offset: (-g0 * SPAN * CIRC).toFixed(2) }
})

const trackDash = `${(SPAN * CIRC).toFixed(2)} ${CIRC.toFixed(2)}`
const valueDash = computed(() => `${(frac.value * SPAN * CIRC).toFixed(2)} ${CIRC.toFixed(2)}`)

const text = computed(() => (hasValue.value ? fmt(props.value as number) : '—'))
</script>

<template>
  <div class="ag" :style="{ '--c': color }">
    <svg viewBox="0 0 200 162" role="img" :aria-label="label">
      <defs>
        <linearGradient :id="gradId" x1="0" y1="1" x2="1" y2="0">
          <stop offset="0%" :stop-color="`rgba(${color},0.55)`" />
          <stop offset="55%" :stop-color="`rgb(${color})`" />
          <stop offset="100%" :stop-color="`rgb(${color})`" />
        </linearGradient>
        <radialGradient :id="glowId" cx="50%" cy="50%" r="50%">
          <stop offset="0%" :stop-color="`rgba(${color},0.32)`" />
          <stop offset="100%" :stop-color="`rgba(${color},0)`" />
        </radialGradient>
      </defs>

      <!-- 刻度 -->
      <g class="ticks">
        <line
          v-for="(t, i) in tickMarks"
          :key="i"
          :x1="t.x1"
          :y1="t.y1"
          :x2="t.x2"
          :y2="t.y2"
          :class="{ major: t.major, mid: t.mid }"
        />
      </g>

      <!-- 轨道 / 安全区 / 数值弧（rotate(150) 让弧起点落在左下 150°） -->
      <g :transform="`rotate(150 ${CX} ${CY})`">
        <circle class="track" :cx="CX" :cy="CY" :r="R" :stroke-dasharray="trackDash" />
        <circle
          v-if="bandArc"
          class="band"
          :cx="CX"
          :cy="CY"
          :r="R"
          :stroke-dasharray="bandArc.dash"
          :stroke-dashoffset="bandArc.offset"
        />
        <circle
          v-if="hasValue"
          class="value"
          :cx="CX"
          :cy="CY"
          :r="R"
          :stroke="`url(#${gradId})`"
          :stroke-dasharray="valueDash"
        />
      </g>

      <!-- 指针：线段固定朝上，整组转到目标角度 -->
      <g
        v-if="hasValue"
        class="needle-g"
        :style="{ transform: `rotate(${angle}deg)`, transformOrigin: `${CX}px ${CY}px` }"
      >
        <line class="needle" :x1="CX" :y1="CY - NEEDLE_IN" :x2="CX" :y2="CY - NEEDLE_OUT" />
        <circle class="tip" :cx="CX" :cy="CY - NEEDLE_OUT" r="3.2" :fill="`rgb(${color})`" />
      </g>

      <!-- 读数盘：指针从盘缘伸出，读数落在盘里，两者不打架 -->
      <circle
        v-if="hasValue"
        class="face-glow"
        :cx="CX"
        :cy="CY"
        :r="FACE_R + 8"
        :fill="`url(#${glowId})`"
      />
      <circle class="face" :cx="CX" :cy="CY" :r="FACE_R" />
      <circle class="face-ring" :cx="CX" :cy="CY" :r="FACE_R" />

      <!-- 刻度数字 -->
      <g class="scale">
        <text v-for="(s, i) in scaleLabels" :key="i" :x="s.x" :y="s.y" text-anchor="middle">
          {{ s.text }}
        </text>
      </g>

      <!--
        读数坐标是 SVG 的**基线**，不是盒子中心：25px 的字形全部画在基线上方，
        所以 `CY - 2` 会让整块数字压在圆盘上半边。基线取 CY + 5（数字）/
        CY + 21（单位），两者合起来的视觉中心才落在圆心上，行距也保持 16px。
      -->
      <text class="read" :x="CX" :y="CY + 5" text-anchor="middle">{{ text }}</text>
      <text v-if="unit" class="unit" :x="CX" :y="CY + 21" text-anchor="middle">{{ unit }}</text>
    </svg>

    <div class="ag-cap">
      <span class="nm">{{ label }}</span>
      <span v-if="note" class="nt">{{ note }}</span>
    </div>
  </div>
</template>

<style scoped>
.ag {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  min-width: 0;
}

svg {
  display: block;
  width: 100%;
  height: auto;
  overflow: visible;
}

.ticks line {
  stroke: var(--border-strong);
  stroke-width: 1;
  opacity: 0.45;
}

.ticks line.mid {
  opacity: 0.7;
  stroke-width: 1.1;
}

.ticks line.major {
  opacity: 0.95;
  stroke-width: 1.5;
}

.track {
  fill: none;
  stroke: var(--surface-3);
  stroke-width: 7;
  stroke-linecap: round;
}

.band {
  fill: none;
  stroke: rgba(23, 178, 106, 0.22);
  stroke-width: 7;
  stroke-linecap: butt;
}

.value {
  fill: none;
  stroke-width: 7;
  stroke-linecap: round;
  transition: stroke-dasharray 0.6s var(--ease);
}

.needle-g {
  transition: transform 0.6s var(--ease);
}

.needle {
  stroke: var(--text);
  stroke-width: 2.8;
  stroke-linecap: round;
}

.tip {
  stroke: var(--surface);
  stroke-width: 1.4;
}

.face {
  fill: var(--surface);
  stroke: none;
}

.face-ring {
  fill: none;
  stroke: var(--border);
  stroke-width: 1;
}

.scale text {
  fill: var(--text-3);
  font-size: 9px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.read {
  fill: var(--text);
  font-size: 25px;
  font-weight: 700;
  letter-spacing: -0.8px;
  font-variant-numeric: tabular-nums;
}

.unit {
  fill: var(--text-3);
  font-size: 10px;
  font-weight: 600;
}

.ag-cap {
  display: flex;
  align-items: baseline;
  gap: 6px;
  margin-top: 2px;
}

.nm {
  font-size: 12.5px;
  font-weight: 650;
  color: var(--text-2);
}

/* 评价文字跟着档位变色：灰色的「偏干」看不出严重程度，用户只能靠数字自己判断 */
.nt {
  font-size: 10.5px;
  color: rgb(var(--c));
}
</style>
