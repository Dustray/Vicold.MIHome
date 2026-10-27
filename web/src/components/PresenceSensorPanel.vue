<script setup lang="ts">
/**
 * 存在类传感器专属面板（人在传感器 / 人体传感器）。
 *
 * 主视觉是一颗「雷达波」指示：有人时绿色扩散波纹，无人时灰色静止。
 * 下面是光照度、人体距离（若有）、电池、持续时长与灵敏度设置。
 *
 * 这两个品类的属性差异极大（hb01 有 14 个可写属性，es2/hs1bb2 一个都没有），
 * 面板按属性存在与否渲染，缺什么就不显示什么。
 */
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { presenceProps } from '../lib/spec'
import { valueLabel } from '../lib/zh'
import PropControl from './PropControl.vue'

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
}>()

// ---------------------------------------------------------------- 图标
const ICONS: Record<string, string> = {
  person: '<circle cx="12" cy="7.4" r="3.4"/><path d="M4.8 20.2c0-3.8 3.2-6.6 7.2-6.6s7.2 2.8 7.2 6.6"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2.6v2.2M12 19.2v2.2M2.6 12h2.2M19.2 12h2.2M5.2 5.2l1.6 1.6M17.2 17.2l1.6 1.6M18.8 5.2l-1.6 1.6M6.8 17.2l-1.6 1.6"/>',
  ruler: '<path d="M3.4 15.2L15.2 3.4l5.4 5.4L8.8 20.6z"/><path d="M7.6 11l1.8 1.8M10.4 8.2l1.8 1.8M13.2 5.4l1.8 1.8"/>',
  battery: '<rect x="2.6" y="8" width="16" height="8" rx="2.2"/><path d="M21 11.4v1.2"/>',
  timer: '<circle cx="12" cy="13" r="7.4"/><path d="M12 9.4V13l2.4 1.6M9.4 2.6h5.2"/>',
  tune: '<path d="M4 8h10M18 8h2M4 16h4M12 16h8"/><circle cx="16" cy="8" r="2"/><circle cx="10" cy="16" r="2"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.person
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const pp = computed(() => presenceProps(props.spec))

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

/**
 * 有人 / 无人。
 *
 * ⚠️ 两款设备的枚举含义不同：
 *  - hb01：0=无人 1=有人 2=none 3=none
 *  - es2 ：0=无人 1=有人 2..7=有人（极客版，非 0 都算有人）
 * 所以判定用「值是否非 0」，不能死等 1。
 */
const occupancy = computed(() => {
  const v = numOf(pp.value.occupancy)
  if (v === undefined) return undefined
  return v !== 0
})

/** 人体传感器（linp.motion）没有 occupancy-status，用「无移动时长」代替 */
const motionState = computed(() => {
  if (occupancy.value !== undefined) return undefined
  const d = numOf(pp.value.noMotion)
  if (d === undefined) return undefined
  // 无移动时长为 0 表示刚刚检测到动静
  return d === 0
})

const present = computed(() => occupancy.value ?? motionState.value)

const stateText = computed(() => {
  if (present.value === undefined) return '状态未知'
  return present.value ? '检测到有人' : '当前无人'
})

/** 无人传感器（linp.motion）叫「无人体活动」 */
const isMotionOnly = computed(() => occupancy.value === undefined && !!pp.value.noMotion)

const stateLabel = computed(() => {
  if (occupancy.value !== undefined) return '有人 / 无人'
  if (isMotionOnly.value) return '人体活动'
  return '状态'
})

/** 光照度（lux） */
const illumination = computed(() => numOf(pp.value.illumination))

/** 人体距离（米） */
const distance = computed(() => numOf(pp.value.distance))

/** 电池 */
const battery = computed(() => numOf(pp.value.battery))

/** 有人持续时长（分钟） */
const hasDuration = computed(() => numOf(pp.value.hasDuration))
/** 无人持续时长（分钟） */
const noDuration = computed(() => numOf(pp.value.noDuration))

const toneColor = computed(() => {
  if (present.value === undefined) return '148, 163, 184'
  return present.value ? '23, 178, 106' : '148, 163, 184'
})

const heroStyle = computed(() => ({ '--tone': toneColor.value }))

// ---------------------------------------------------------------- 灵敏度
const sensProp = computed(() => pp.value.sensitivity)
const sensOptions = computed(() => {
  const p = sensProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: sensLabel(it) }))
})
const sensValue = computed(() => raw(sensProp.value))

function sensLabel(v: { value: number | string; description: string; desc_zh?: string }): string {
  const raw2 = valueLabel(v)
  if (/Low/i.test(raw2)) return '低'
  if (/Middle|Mid/i.test(raw2)) return '中'
  if (/High/i.test(raw2)) return '高'
  if (/Custom|User/i.test(raw2)) return '自定义'
  return raw2
}

// ---------------------------------------------------------------- 其它可写项
/** 无人判定时长（hb01 可写，单位分钟） */
const judgeTimeProp = computed(() => {
  const s = props.spec
  if (!s) return null
  return s.properties.find((p) => p.base_name === 'no-one-determine-time') || null
})

const judgeTimeValue = computed(() => numOf(judgeTimeProp.value))

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(pp.value.occupancy)
  add(pp.value.illumination)
  add(pp.value.battery)
  add(pp.value.distance)
  add(pp.value.hasDuration)
  add(pp.value.noDuration)
  add(pp.value.noMotion)
  add(pp.value.sensitivity)
  add(judgeTimeProp.value)
  return keys
})

const restGroups = computed<SpecGroup[]>(() =>
  props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      actions: g.actions,
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
)

/** 时长显示：超过 90 分钟按小时算 */
function durationText(v: number | undefined): string {
  if (v === undefined) return '—'
  if (v < 60) return `${Math.round(v)} 分钟`
  const h = v / 60
  if (h < 24) return `${h.toFixed(1)} 小时`
  return `${(h / 24).toFixed(1)} 天`
}
</script>

<template>
  <div v-if="spec" class="ps">
    <!-- 主视觉：雷达波 -->
    <section class="hero" :class="{ present: present === true, away: present === false }" :style="heroStyle">
      <div class="radar">
        <span class="wave w1" />
        <span class="wave w2" />
        <span class="wave w3" />
        <span class="core">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('person')" />
        </span>
      </div>

      <div class="hero-side">
        <div class="state-text">{{ stateText }}</div>
        <div class="state-sub">{{ stateLabel }}</div>

        <div class="metrics">
          <div v-if="illumination !== undefined" class="metric">
            <span class="m-label">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('sun')" />
              光照度
            </span>
            <b class="m-val">{{ Math.round(illumination) }}<em>lx</em></b>
          </div>
          <div v-if="distance !== undefined" class="metric">
            <span class="m-label">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('ruler')" />
              人体距离
            </span>
            <b class="m-val">{{ distance.toFixed(2) }}<em>m</em></b>
          </div>
          <div v-if="battery !== undefined" class="metric">
            <span class="m-label">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('battery')" />
              电量
            </span>
            <b class="m-val" :class="{ low: battery < 20 }">{{ Math.round(battery) }}<em>%</em></b>
          </div>
        </div>
      </div>
    </section>

    <!-- 持续时长 -->
    <section v-if="hasDuration !== undefined || noDuration !== undefined" class="blk">
      <h3>持续时长</h3>
      <div class="dur-row">
        <div v-if="hasDuration !== undefined" class="dur">
          <span class="d-cap">有人</span>
          <b class="d-val">{{ durationText(hasDuration) }}</b>
        </div>
        <div v-if="noDuration !== undefined" class="dur">
          <span class="d-cap">无人</span>
          <b class="d-val">{{ durationText(noDuration) }}</b>
        </div>
      </div>
    </section>

    <!-- 灵敏度 -->
    <section v-if="sensOptions.length" class="blk">
      <h3>灵敏度</h3>
      <div class="chip-row">
        <button
          v-for="o in sensOptions"
          :key="String(o.value)"
          class="chip"
          :class="{ active: String(o.value) === String(sensValue) }"
          :disabled="!online"
          @click="emit('set-prop', sensProp!, o.value)"
        >
          {{ o.label }}
        </button>
      </div>
    </section>

    <!-- 无人判定时长 -->
    <section v-if="judgeTimeProp" class="blk">
      <h3>无人判定时长</h3>
      <div class="pos-row">
        <span class="pos-cap">即时</span>
        <input
          class="slider"
          type="range"
          :min="Number(judgeTimeProp.range?.[0] ?? 0)"
          :max="Number(judgeTimeProp.range?.[1] ?? 10000)"
          :step="Number(judgeTimeProp.range?.[2] ?? 1) || 1"
          :value="judgeTimeValue ?? 0"
          :disabled="!online"
          @change="(e) => emit('set-prop', judgeTimeProp!, Number((e.target as HTMLInputElement).value))"
        />
        <span class="pos-cap">{{ judgeTimeValue ? durationText(judgeTimeValue) : '即时' }}</span>
      </div>
    </section>

    <!-- 其余参数 -->
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
  </div>
</template>

<style scoped>
.ps {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.ps > section,
.ps > details {
  animation: fade-up 0.32s var(--ease) both;
}

svg {
  width: 100%;
  height: 100%;
}

/* ------------------------------------------------ 主视觉 */
.hero {
  position: relative;
  margin-top: 12px;
  padding: 16px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  display: grid;
  grid-template-columns: auto 1fr;
  align-items: center;
  gap: 20px;
  overflow: hidden;
}

.hero::before {
  content: '';
  position: absolute;
  top: -70px;
  left: -50px;
  width: 210px;
  height: 210px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--tone), 0.24), transparent 68%);
  pointer-events: none;
  transition: background 0.4s ease;
}

/* 雷达 */
.radar {
  position: relative;
  width: 104px;
  height: 104px;
  flex: none;
  display: grid;
  place-items: center;
}

.wave {
  position: absolute;
  border-radius: 50%;
  border: 1.5px solid rgb(var(--tone));
  opacity: 0;
}

.hero.present .wave {
  animation: ripple 2.4s ease-out infinite;
}

.hero.present .wave.w2 { animation-delay: 0.8s; }
.hero.present .wave.w3 { animation-delay: 1.6s; }

@keyframes ripple {
  0% { width: 34px; height: 34px; opacity: 0.55; }
  100% { width: 100px; height: 100px; opacity: 0; }
}

.core {
  position: relative;
  z-index: 1;
  width: 42px;
  height: 42px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  color: rgb(var(--tone));
  background: rgba(var(--tone), 0.14);
  border: 1px solid rgba(var(--tone), 0.35);
  transition: all 0.35s var(--ease);
}

.core svg {
  width: 22px;
  height: 22px;
}

.hero.present .core {
  box-shadow: 0 0 10px rgba(var(--tone), 0.26);
}

/* 无人时灰色静止 */
.hero.away .core {
  color: var(--text-3);
  background: var(--surface-2);
  border-color: var(--border);
  box-shadow: none;
}

/* 右侧 */
.hero-side {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 3px;
  min-width: 0;
}

.state-text {
  font-size: 19px;
  font-weight: 650;
  color: rgb(var(--tone));
  letter-spacing: 0.2px;
  transition: color 0.35s ease;
}

.hero.away .state-text {
  color: var(--text-2);
}

.state-sub {
  font-size: 11px;
  color: var(--text-3);
}

.metrics {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  margin-top: 12px;
  padding-top: 12px;
  border-top: 1px dashed var(--border);
  width: 100%;
}

.metric {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.m-label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 10.5px;
  color: var(--text-3);
  white-space: nowrap;
}

.m-label svg {
  width: 12px;
  height: 12px;
  flex: none;
}

.m-val {
  font-size: 15px;
  font-weight: 600;
  color: var(--text);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.m-val em {
  font-style: normal;
  font-size: 10px;
  font-weight: 400;
  color: var(--text-3);
  margin-left: 2px;
}

.m-val.low {
  color: var(--danger);
}

/* ------------------------------------------------ 分区 */
.blk {
  margin-top: 18px;
}

h3 {
  display: flex;
  align-items: center;
  gap: 7px;
  margin: 0 0 9px;
  font-size: 12.5px;
  font-weight: 600;
  color: var(--text-2);
}

h3::before {
  content: '';
  width: 3px;
  height: 12px;
  border-radius: 2px;
  background: var(--grad-accent);
}

/* ------------------------------------------------ 持续时长 */
.dur-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 9px;
}

.dur {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 11px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.d-cap {
  font-size: 10.5px;
  color: var(--text-3);
}

.d-val {
  font-size: 15px;
  font-weight: 600;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

/* ------------------------------------------------ 滑杆 */
.pos-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.pos-cap {
  font-size: 10.5px;
  color: var(--text-3);
  flex: none;
  width: 42px;
}

.pos-cap:last-child {
  text-align: right;
}

.slider {
  flex: 1;
  min-width: 0;
  appearance: none;
  height: 5px;
  border-radius: 5px;
  background: linear-gradient(90deg, var(--brand) 0%, var(--brand) var(--fill, 0%), var(--border-strong) var(--fill, 0%));
  outline: none;
}

.slider::-webkit-slider-thumb {
  appearance: none;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--brand);
  box-shadow: 0 0 6px rgba(47, 109, 246, 0.26);
  cursor: pointer;
}

.slider:disabled {
  opacity: 0.5;
}

/* ------------------------------------------------ 枚举按钮 */
.chip-row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.chip {
  font-size: 11.5px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 4px 15px;
  transition: all 0.16s var(--ease);
}

.chip:hover:not(:disabled) {
  border-color: var(--brand);
  color: var(--brand);
}

.chip.active {
  color: #fff;
  background: var(--grad-brand);
  border-color: transparent;
  box-shadow: var(--glow-brand);
}

.chip:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

/* ------------------------------------------------ 更多设置 */
.adv {
  margin-top: 18px;
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
