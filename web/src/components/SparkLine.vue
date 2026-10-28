<script setup lang="ts">
/**
 * 卡片上的迷你趋势线。
 *
 * 和抽屉里的「历史曲线」是**同一份数据、同一个时间窗**（最近 N 小时），
 * 只是没有轴、没有色带：
 *  - 横向按**真实时间**铺开，不是按点的序号 —— 否则采样稀疏的时段被压扁、
 *    密集的时段被拉宽，形状跟点开看到的完全对不上；
 *  - 点数压到和卡片像素宽度差不多（`thinToWidth`）：云端回填 37 秒一个点，
 *    24 小时 2000+ 个点全画进 88px 既看不出东西又白费，
 *    不过抽稀会留住峰谷和拐点；
 *  - 断档处**断开不连线**，别用一条直线把几小时的空档抹平。
 *
 * 唯一保留的差别是纵向：卡片只有 22px 高，必须把 min~max 拉满才看得见起伏，
 * 抽屉里那条会留 18% 余量并画出舒适区色带。
 */
import { computed } from 'vue'
import { nowTs, thinToWidth, type Pt } from '../lib/history'

const props = withDefaults(
  defineProps<{
    points: Pt[]
    color?: string
    width?: number
    height?: number
    /** 时间窗（小时）：横向按 [now - hours, now] 铺开；0 = 按数据首尾铺满 */
    hours?: number
  }>(),
  { color: '47,109,246', width: 88, height: 22, hours: 24 }
)

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

  const w = props.width
  const h = props.height
  const pad = 2.5
  const dt = t1 - t0 || 1
  const x = (t: number) => pad + ((t - t0) / dt) * (w - pad * 2)
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
  return { paths, dot: { x: x(last.t), y: y(last.v) } }
})
</script>

<template>
  <svg
    v-if="draw"
    class="spark"
    :width="width"
    :height="height"
    :viewBox="`0 0 ${width} ${height}`"
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
</template>

<style scoped>
.spark {
  display: block;
  overflow: visible;
}
</style>
