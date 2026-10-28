<script setup lang="ts">
/**
 * 设备面板里的「历史曲线」区块。
 *
 * 曲线数据来自本机采样落盘的历史库（lib/history.ts），
 * 设备自带的 read-history-data / record-data 云端读不到，只能自己攒。
 */
import { computed, ref, watch } from 'vue'
import TrendChart from './TrendChart.vue'
import { AQ_LABEL, METRICS, trendOf, type MetricKey } from '../lib/env'

const props = defineProps<{
  did: string
  metrics: MetricKey[]
  title?: string
}>()

const active = ref<MetricKey>(props.metrics[0] ?? 't')
const hours = ref(24)

watch(
  () => props.metrics,
  (list) => {
    if (!list.includes(active.value)) active.value = list[0] ?? 't'
  }
)

const HOURS = [
  { v: 1, label: '1 时' },
  { v: 6, label: '6 时' },
  { v: 24, label: '24 时' },
  { v: 168, label: '7 天' }
]

const def = computed(() => METRICS[active.value])
const points = computed(() => trendOf(props.did, active.value, hours.value))
const band = computed(() => def.value.band || null)

function labelOf(k: MetricKey) {
  return k === 'aq' ? '空气质量' : METRICS[k].label
}

function unitOf(k: MetricKey) {
  return k === 'aq' ? '' : METRICS[k].unit
}
</script>

<template>
  <section v-if="metrics.length" class="trend-sec">
    <header class="ts-head">
      <div class="ts-title">
        <h3>{{ title || '历史曲线' }}</h3>
        <span class="ts-sub">仅保存在本机</span>
      </div>
      <div class="ts-hours">
        <button
          v-for="h in HOURS"
          :key="h.v"
          class="hb"
          :class="{ active: hours === h.v }"
          @click="hours = h.v"
        >
          {{ h.label }}
        </button>
      </div>
    </header>

    <div class="ts-tabs">
      <button
        v-for="k in metrics"
        :key="k"
        class="tab"
        :class="{ active: active === k }"
        @click="active = k"
      >
        {{ labelOf(k) }}
      </button>
    </div>

    <TrendChart
      :points="points"
      :unit="unitOf(active)"
      :color="def.color"
      :decimals="active === 'aq' ? 0 : def.decimals"
      :min-span="active === 'aq' ? 1 : def.scaleMax * 0.06"
      :band="band"
      :height="128"
      :hours="hours"
      :range-label="`最近 ${hours >= 168 ? '7 天' : hours + ' 小时'}`"
    />

    <p v-if="active === 'aq'" class="ts-tip">
      空气质量是档位值：0 优 · 1 良 · 2 轻度污染 · 3 中度污染 · 4 重度污染 · 5 严重污染
      （{{ AQ_LABEL.join(' / ') }}）
    </p>
    <p v-else class="ts-tip">
      历史数据最多保留 7 天
    </p>
  </section>
</template>

<style scoped>
.trend-sec {
  margin-top: 14px;
  padding: 14px 16px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--surface-2);
}

.ts-head {
  display: flex;
  align-items: center;
  gap: 10px;
}

.ts-title h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 650;
  color: var(--text);
}

.ts-sub {
  font-size: 10.5px;
  color: var(--text-3);
  margin-left: 6px;
}

.ts-hours {
  margin-left: auto;
  display: flex;
  gap: 2px;
}

.hb {
  font-size: 10.5px;
  color: var(--text-3);
  background: none;
  border: 0;
  border-radius: 6px;
  padding: 2px 6px;
  cursor: pointer;
  font-family: inherit;
}

.hb.active {
  color: var(--brand);
  background: var(--brand-soft);
}

.ts-tabs {
  display: flex;
  gap: 5px;
  margin: 10px 0 4px;
  flex-wrap: wrap;
}

.tab {
  font-size: 11.5px;
  color: var(--text-2);
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 3px 11px;
  cursor: pointer;
  font-family: inherit;
}

.tab.active {
  color: #fff;
  background: var(--brand);
  border-color: var(--brand);
}

.ts-tip {
  margin: 6px 0 0;
  font-size: 10.5px;
  color: var(--text-3);
  line-height: 1.5;
}
</style>
