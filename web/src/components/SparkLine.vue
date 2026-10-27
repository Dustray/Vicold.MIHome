<script setup lang="ts">
/**
 * 卡片上的迷你趋势线。
 *
 * 只画线，不画轴不画点 —— 卡片空间小，目的是一眼看出「最近在涨还是在跌」。
 */
import { computed } from 'vue'
import type { Pt } from '../lib/history'

const props = withDefaults(
  defineProps<{
    points: Pt[]
    color?: string
    width?: number
    height?: number
  }>(),
  { color: '47,109,246', width: 88, height: 22 }
)

const d = computed(() => {
  const pts = props.points.filter((p) => Number.isFinite(p.v))
  if (pts.length < 2) return ''
  const vs = pts.map((p) => p.v)
  const min = Math.min(...vs)
  const max = Math.max(...vs)
  const span = max - min || 1
  const w = props.width
  const h = props.height
  const pad = 2.5
  return pts
    .map((p, i) => {
      const x = (i / (pts.length - 1)) * (w - pad * 2) + pad
      const y = h - pad - ((p.v - min) / span) * (h - pad * 2)
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')
})
</script>

<template>
  <svg v-if="d" class="spark" :width="width" :height="height" :viewBox="`0 0 ${width} ${height}`">
    <path :d="d" fill="none" :stroke="`rgb(${color})`" stroke-width="1.6" stroke-linecap="round"
      stroke-linejoin="round" />
    <circle
      v-if="points.length"
      :cx="width - 2.5"
      :cy="Number(d.split(' ').pop()?.split(',')[1] ?? height / 2)"
      r="2"
      :fill="`rgb(${color})`"
    />
  </svg>
</template>

<style scoped>
.spark {
  display: block;
  overflow: visible;
}
</style>
