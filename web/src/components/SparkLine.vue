<script setup lang="ts">
/**
 * 卡片上的迷你趋势线 + 一行时间轴。
 *
 * 和抽屉里的「历史曲线」是**同一份数据、同一个时间窗**（最近 N 小时），
 * 只是没有网格和色带：
 *  - 横向按**真实时间**铺开，不是按点的序号 —— 否则采样稀疏的时段被压扁、
 *    密集的时段被拉宽，形状跟点开看到的完全对不上；
 *  - **撑满卡片宽度**：宽度用 ResizeObserver 实测，不再写死 88px
 *    （写死会在曲线右边留一块空白）；
 *  - 点数压到和像素宽度差不多（`thinToWidth`）：云端回填 37 秒一个点，
 *    24 小时 2000+ 个点全画进两百来像素既看不出东西又白费；
 *  - 断档处**断开不连线**，别用一条直线把几小时的空档抹平。
 *
 * 曲线下面一行小字：左边窗口起点、中间是**什么要素**（温度 / 湿度 / PM2.5…）、
 * 右边窗口终点。光看一条线看不出画的是温度还是湿度，卡片上又没别的地方可写。
 *
 * 唯一保留的差别是纵向：卡片只有 22px 高，必须把 min~max 拉满才看得见起伏，
 * 抽屉里那条会留 18% 余量并画出舒适区色带。
 */
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { nowTs, thinToWidth, type Pt } from '../lib/history'

const props = withDefaults(
  defineProps<{
    points: Pt[]
    color?: string
    /** 要素名（温度 / 湿度 / PM2.5 …），写在两个时间的中间 */
    label?: string
    height?: number
    /** 时间窗（小时）：横向按 [now - hours, now] 铺开；0 = 按数据首尾铺满 */
    hours?: number
    /** 宽度兜底值：实测出来之前（首帧 / SSR）先用它 */
    width?: number
  }>(),
  { color: '47,109,246', label: '', height: 22, hours: 24, width: 88 }
)

// ---------------------------------------------------------------- 宽度实测

const wrap = ref<HTMLElement | null>(null)
const boxW = ref(0)
let ro: ResizeObserver | null = null

onMounted(() => {
  if (!wrap.value) return
  ro = new ResizeObserver(() => {
    if (wrap.value) boxW.value = wrap.value.clientWidth
  })
  ro.observe(wrap.value)
  boxW.value = wrap.value.clientWidth
})
onBeforeUnmount(() => ro?.disconnect())

/** 实测宽度；量不到时退回 prop（别用 0，会把路径算成一堆 NaN） */
const w = computed(() => Math.max(60, Math.round(boxW.value || props.width)))

// ---------------------------------------------------------------- 曲线

const draw = computed(() => {
  let raw = props.points.filter((p) => Number.isFinite(p.v) && Number.isFinite(p.t))
  if (raw.length < 2) return null

  // 时间窗：给 hours 就用「现在往前 N 小时」的固定窗，和抽屉里那条完全对齐
  let t0 = raw[0].t
  let t1 = raw[raw.length - 1].t
  if (props.hours > 0) {
    t1 = nowTs.value
    t0 = t1 - props.hours * 3600
    // 窗口外的点（历史库里 24 小时前的老数据）不画，免得溢出到卡片外面
    raw = raw.filter((p) => p.t >= t0 && p.t <= t1)
    if (raw.length < 2) return null
  }

  // 断档阈值用**原始**采样间隔算：抽稀之后间隔被拉大，再拿它判就会把真断档接起来
  const ds = raw.slice(1).map((p, i) => p.t - raw[i].t).sort((a, b) => a - b)
  const median = ds.length ? ds[Math.floor(ds.length / 2)] : 0
  const cut = Math.max(median * 5, 30 * 60)

  // 纵轴范围看全部原始点，别让抽稀影响曲线高低
  const vs = raw.map((p) => p.v)
  const lo = Math.min(...vs)
  const hi = Math.max(...vs)
  const span = hi - lo || 1

  const width = w.value
  const h = props.height
  const pad = 2.5
  const dt = t1 - t0 || 1
  const x = (t: number) => pad + ((t - t0) / dt) * (width - pad * 2)
  const y = (v: number) => h - pad - ((v - lo) / span) * (h - pad * 2)

  // 先在原始点上切段，再按段各自的像素宽度抽稀：
  // 反过来的话断档两侧的点会被抽掉，切口就歪了
  const runs: Pt[][] = []
  let cur: Pt[] = [raw[0]]
  for (let i = 1; i < raw.length; i++) {
    if (raw[i].t - raw[i - 1].t > cut) {
      runs.push(cur)
      cur = [raw[i]]
    } else {
      cur.push(raw[i])
    }
  }
  runs.push(cur)

  const paths = runs
    .filter((r) => r.length >= 2)
    .map((r) =>
      thinToWidth(r, x(r[r.length - 1].t) - x(r[0].t))
        .map((p, i) => `${i === 0 ? 'M' : 'L'}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`)
        .join(' ')
    )
  if (!paths.length) return null

  const last = raw[raw.length - 1]
  return { paths, dot: { x: x(last.t), y: y(last.v) }, t0, t1 }
})

// ---------------------------------------------------------------- 时间轴

function clock(ts: number): string {
  const d = new Date(ts * 1000)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}

/**
 * 端点文案。
 *
 * 24 小时窗口的左端必然落在昨天同一时刻，光写 `08:30` 会和右端的 `08:30`
 * 一模一样（一天里正好差 24 小时），看着像写错了，所以跨天要带上「昨」或日期。
 */
function edgeText(ts: number, now: number): string {
  const d = new Date(ts * 1000)
  const today = new Date(now * 1000)
  if (d.toDateString() === today.toDateString()) return clock(ts)
  const y = new Date(today)
  y.setDate(y.getDate() - 1)
  if (d.toDateString() === y.toDateString()) return `昨 ${clock(ts)}`
  return `${d.getMonth() + 1}/${d.getDate()} ${clock(ts)}`
}

const axis = computed<{ left: string; right: string } | null>(() => {
  if (!draw.value) return null
  const now = nowTs.value
  const { t0, t1 } = draw.value
  return { left: edgeText(t0, now), right: edgeText(t1, now) }
})
</script>

<template>
  <div ref="wrap" class="spark">
    <svg
      v-if="draw"
      :width="w"
      :height="height"
      :viewBox="`0 0 ${w} ${height}`"
      aria-hidden="true"
    >
      <path
        v-for="(d, i) in draw.paths"
        :key="i"
        :d="d"
        fill="none"
        :stroke="`rgb(${color})`"
        stroke-width="1.6"
        stroke-linecap="round"
        stroke-linejoin="round"
      />
      <circle :cx="draw.dot.x" :cy="draw.dot.y" r="2" :fill="`rgb(${color})`" />
    </svg>
    <!-- 左端起点 · 中间要素名 · 右端终点 -->
    <div v-if="axis" class="spark-axis">
      <span class="t">{{ axis.left }}</span>
      <span class="lbl">{{ label }}</span>
      <span class="t right">{{ axis.right }}</span>
    </div>
  </div>
</template>

<style scoped>
.spark {
  display: block;
  width: 100%;
}

.spark svg {
  display: block;
  overflow: visible;
}

/* 三列 1fr auto 1fr：左右两列等宽，中间那个才是真正居中的 */
.spark-axis {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto minmax(0, 1fr);
  align-items: center;
  gap: 4px;
  margin-top: 1px;
  font-size: 10px;
  line-height: 1.3;
  font-variant-numeric: tabular-nums;
  color: var(--text-3);
  opacity: 0.85;
}

.spark-axis .t {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.spark-axis .right {
  text-align: right;
}

.spark-axis .lbl {
  color: var(--text-2);
  white-space: nowrap;
}
</style>
