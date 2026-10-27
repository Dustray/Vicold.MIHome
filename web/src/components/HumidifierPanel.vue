<script setup lang="ts">
/**
 * 加湿器专属面板。
 *
 * 主视觉是「当前湿度 + 目标湿度」的双环：外环是当前湿度，内环是目标值，
 * 中间显示当前湿度数字。下面是模式选择（恒湿/睡眠/强力）与目标湿度滑杆。
 */
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { humidifierProps } from '../lib/spec'
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
  drop: '<path d="M12 3.9s5.2 5.6 5.2 9.1A5.2 5.2 0 0 1 12 18.2a5.2 5.2 0 0 1-5.2-5.2C6.8 9.5 12 3.9 12 3.9z"/>',
  constant: '<path d="M3.6 12h3.2l2-5.6 3.2 11.2 2.6-8.4 1.8 2.8h4.2"/>',
  sleep: '<path d="M20 14.4A8.6 8.6 0 0 1 9.6 4a8.6 8.6 0 1 0 10.4 10.4z"/>',
  strong: '<path d="M12 3.4c2.6 3 4.4 5.4 4.4 8a4.4 4.4 0 0 1-8.8 0c0-1.5.6-2.9 1.5-3.9.1 1.4.9 2.3 1.7 2.3.9 0 1.4-.8 1.1-2a9 9 0 0 1 .1-4.4z"/>',
  water: '<path d="M4 15.4c1.8-1.6 3.2 1.6 5 0s3.2 1.6 5 0 3.2 1.6 5 0"/><path d="M4 19.4c1.8-1.6 3.2 1.6 5 0s3.2 1.6 5 0 3.2 1.6 5 0"/>',
  filter: '<path d="M4.8 4.8h14.4L13.7 11v7.6l-3.4-1.8V11z"/>',
  thermo: '<path d="M14 14.7V5.2a2 2 0 1 0-4 0v9.5a4 4 0 1 0 4 0z"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
  timer: '<circle cx="12" cy="13" r="7.4"/><path d="M12 9.4V13l2.4 1.6M9.4 2.6h5.2"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.drop
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const hp = computed(() => humidifierProps(props.spec))

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

const isOn = computed(() => {
  const p = hp.value.on
  return p ? !!raw(p) : false
})

/** 当前湿度 */
const humidity = computed(() => numOf(hp.value.humidity))
/** 目标湿度 */
const targetHum = computed(() => numOf(hp.value.target))
/** 温度 */
const temperature = computed(() => numOf(hp.value.temperature))

/** 故障 */
const faultText = computed(() => {
  const p = hp.value.fault
  const v = raw(p)
  if (v === null || v === undefined) return ''
  const code = Number(v)
  if (!code) return ''
  const hit = p?.value_list?.find((i) => String(i.value) === String(code))
  return hit ? valueLabel(hit) : `故障码 ${code}`
})

/** 缺水提醒：只认设备自己的 water-status 位 */
const lowWater = computed(() => {
  const v = raw(hp.value.waterStatus)
  if (v === null || v === undefined) return false
  const code = Number(v)
  if (!code) return false
  const hit = hp.value.waterStatus?.value_list?.find((i) => String(i.value) === String(code))
  const zh = hit ? valueLabel(hit) : ''
  return /低|缺|少/.test(zh) || code === 1
})

/**
 * 水箱水位刻度（%）。
 *
 * ⚠️ 设备待机时液位计不工作，会上报 0，而 `water-status` 同时报「正常」——
 * 两者矛盾时说明刻度不可信（实测本机加湿器就是这种情况），此时不显示 0%，
 * 免得让人以为水箱空了。缺水与否一律以 `water-status` 为准。
 */
const waterLevel = computed(() => {
  const p = hp.value.waterLevel
  const v = numOf(p)
  if (v === undefined) return undefined
  if (!p?.value_list?.length) return v
  const nums = p.value_list
    .map((i) => Number(i.value))
    .filter((n) => Number.isFinite(n))
  const max = nums.length ? Math.max(...nums) : 100
  if (!max) return undefined
  // 刻度为 0 且设备没报缺水 → 液位读数不可信
  if (v === 0 && !lowWater.value) return undefined
  return Math.min(100, Math.max(0, (v / max) * 100))
})

/** 滤芯寿命 */
const filterPct = computed(() => {
  const v = numOf(hp.value.filter)
  if (v === undefined) return undefined
  return Math.min(100, Math.max(0, v))
})

// ---------------------------------------------------------------- 环
const R = 46
const CIRC = 2 * Math.PI * R
const SPAN = 0.72 // 260°

function arcStyle(pct: number) {
  const len = CIRC * SPAN
  const on = Math.max(0, Math.min(1, pct / 100)) * len
  return {
    strokeDasharray: `${on} ${len}`,
    transform: 'rotate(135deg)',
    transformOrigin: '60px 60px',
  }
}

/** 当前湿度占 0~100 的比例 */
const humPct = computed(() => (humidity.value === undefined ? 0 : Math.min(100, Math.max(0, humidity.value))))
/** 目标湿度 */
const targetPct = computed(() => (targetHum.value === undefined ? 0 : Math.min(100, Math.max(0, targetHum.value))))

/** 舒适度：当前与目标差得越多越「缺水」 */
const gap = computed(() => {
  if (humidity.value === undefined || targetHum.value === undefined) return undefined
  return humidity.value - targetHum.value
})

const statusText = computed(() => {
  if (!isOn.value) return '已关闭'
  const g = gap.value
  if (g === undefined) return '运行中'
  if (g < -3) return '正在加湿'
  if (g > 3) return '正在除湿'
  return '已达目标'
})

/** 颜色：达标绿、加湿蓝、缺水橙 */
const toneColor = computed(() => {
  if (!isOn.value) return '148, 163, 184'
  const g = gap.value
  if (g === undefined) return '47, 109, 246'
  if (Math.abs(g) <= 3) return '23, 178, 106'
  if (g < 0) return '47, 109, 246'
  return '224, 160, 0'
})

const ringStyle = computed(() => ({ '--tone': toneColor.value }))

// ---------------------------------------------------------------- 模式
const modeProp = computed(() => hp.value.mode)
const modes = computed(() => {
  const p = modeProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => {
    const label = valueLabel(it)
    return { value: it.value, label, icon: modeIcon(label) }
  })
})

function modeIcon(label: string): string {
  if (/睡眠|sleep/i.test(label)) return 'sleep'
  if (/强力|强档|max|strong/i.test(label)) return 'strong'
  return 'constant'
}

const modeValue = computed(() => raw(modeProp.value))

function pickMode(value: unknown) {
  const p = modeProp.value
  if (!p || !online.value) return
  if (String(value) === String(modeValue.value)) return
  emit('set-prop', p, value)
}

// ---------------------------------------------------------------- 目标湿度滑杆
const humRange = computed(() => {
  const r = hp.value.target?.range
  if (r && r.length >= 2) return { min: Number(r[0]), max: Number(r[1]) }
  return { min: 40, max: 70 }
})

const humFill = computed(() => {
  const { min, max } = humRange.value
  const span = max - min
  if (!span) return '0%'
  const v = targetHum.value ?? min
  return `${Math.min(100, Math.max(0, ((v - min) / span) * 100))}%`
})

function onTarget(e: Event) {
  const p = hp.value.target
  if (!p) return
  const v = Number((e.target as HTMLInputElement).value)
  emit('set-prop', p, p.type === 'float' ? v : Math.round(v))
}

// ---------------------------------------------------------------- 延时关机
const delayProp = computed(() => {
  const s = props.spec
  if (!s) return null
  return s.properties.find((p) => p.base_name === 'off-delay-time') || null
})

const delayValue = computed(() => numOf(delayProp.value))

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(hp.value.on)
  add(hp.value.mode)
  add(hp.value.target)
  add(hp.value.humidity)
  add(hp.value.temperature)
  add(hp.value.waterLevel)
  add(hp.value.waterStatus)
  add(hp.value.filter)
  add(hp.value.fault)
  add(delayProp.value)
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
</script>

<template>
  <div v-if="spec" class="hum">
    <!-- 故障 / 缺水 -->
    <div v-if="faultText" class="alert-row danger">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ faultText }}</span>
    </div>
    <div v-else-if="lowWater" class="alert-row warn">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('water')" />
      <span>水箱水位偏低，请及时加水</span>
    </div>

    <!-- 主视觉：双环湿度表 -->
    <section class="hero" :class="{ off: !isOn }" :style="ringStyle">
      <div class="ring-wrap">
        <svg class="ring" viewBox="0 0 120 120">
          <!-- 底槽 -->
          <circle class="track" cx="60" cy="60" :r="R" :stroke-dasharray="`${CIRC * SPAN} ${CIRC}`" />
          <!-- 当前湿度 -->
          <circle class="cur" cx="60" cy="60" :r="R" :style="arcStyle(humPct)" />
          <!-- 目标刻度（内圈小环） -->
          <circle class="tgt" cx="60" cy="60" r="34" :stroke-dasharray="`${CIRC * 0.72 * (targetPct / 100)} ${CIRC}`" />
        </svg>
        <div class="face">
          <b>{{ humidity === undefined ? '—' : Math.round(humidity) }}</b>
          <span>%</span>
          <em>当前湿度</em>
        </div>
      </div>

      <div class="hero-side">
        <div class="status-badge">{{ statusText }}</div>
        <div class="kv">
          <span class="k">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('drop')" />
            目标
          </span>
          <b>{{ targetHum === undefined ? '—' : Math.round(targetHum) }}%</b>
        </div>
        <div class="kv">
          <span class="k">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('thermo')" />
            温度
          </span>
          <b>{{ temperature === undefined ? '—' : temperature.toFixed(1) }}°C</b>
        </div>
        <button
          class="power-chip"
          :class="{ on: isOn }"
          :disabled="!online || !hp.on || busyProp === hp.on?.name"
          @click="hp.on && emit('set-prop', hp.on, !isOn)"
        >
          {{ isOn ? '已开启' : '已关闭' }}
        </button>
      </div>
    </section>

    <!-- 模式 -->
    <section v-if="modes.length" class="blk">
      <h3>
        模式
        <em v-if="!isOn" class="h3-hint">设备已关闭，当前为设定模式</em>
      </h3>
      <div class="mode-grid" :style="{ '--cols': String(modes.length) }">
        <button
          v-for="m in modes"
          :key="String(m.value)"
          class="mode-item"
          :class="{ active: String(m.value) === String(modeValue), muted: !isOn, dim: !online }"
          :disabled="!online"
          @click="pickMode(m.value)"
        >
          <span class="mode-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(m.icon)" />
          </span>
          <span class="mode-name">{{ m.label }}</span>
          <i class="mode-check" />
        </button>
      </div>
    </section>

    <!-- 目标湿度 -->
    <section v-if="hp.target" class="blk">
      <h3>
        目标湿度
        <em class="h3-hint">{{ targetHum === undefined ? '—' : Math.round(targetHum) + '%' }}</em>
      </h3>
      <div class="pos-row">
        <span class="pos-cap">40%</span>
        <input
          class="slider"
          type="range"
          :min="humRange.min"
          :max="humRange.max"
          :step="1"
          :value="targetHum ?? humRange.min"
          :disabled="!online"
          :style="{ '--fill': humFill }"
          @change="onTarget"
        />
        <span class="pos-cap">70%</span>
      </div>
    </section>

    <!-- 水箱 / 滤芯 -->
    <section v-if="waterLevel !== undefined || filterPct !== undefined" class="blk">
      <h3>水箱与滤芯</h3>
      <div class="bars">
        <div v-if="waterLevel !== undefined" class="bar-card">
          <div class="b-head">
            <span class="b-name">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('water')" />
              水箱水位
            </span>
            <b class="b-pct" :class="{ low: waterLevel < 25 }">{{ Math.round(waterLevel) }}%</b>
          </div>
          <div class="b-bar water"><i :style="{ width: waterLevel + '%' }" /></div>
        </div>
        <div v-if="filterPct !== undefined" class="bar-card">
          <div class="b-head">
            <span class="b-name">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('filter')" />
              滤芯寿命
            </span>
            <b class="b-pct" :class="{ low: filterPct < 30 }">{{ Math.round(filterPct) }}%</b>
          </div>
          <div class="b-bar"><i :style="{ width: filterPct + '%' }" /></div>
        </div>
      </div>
    </section>

    <!-- 延时关机 -->
    <section v-if="delayProp" class="blk">
      <h3>延时关机</h3>
      <div class="pos-row">
        <span class="pos-cap">立即</span>
        <input
          class="slider"
          type="range"
          :min="Number(delayProp.range?.[0] ?? 0)"
          :max="Number(delayProp.range?.[1] ?? 480)"
          :step="Number(delayProp.range?.[2] ?? 1) || 1"
          :value="delayValue ?? 0"
          :disabled="!online"
          @change="(e) => emit('set-prop', delayProp!, Number((e.target as HTMLInputElement).value))"
        />
        <span class="pos-cap">{{ delayValue ? Math.round(delayValue) + ' 分钟' : '不延时' }}</span>
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
.hum {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.hum > section,
.hum > details {
  animation: fade-up 0.32s var(--ease) both;
}

svg {
  width: 100%;
  height: 100%;
}

/* ------------------------------------------------ 提示条 */
.alert-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 12px 0 0;
  padding: 9px 12px;
  border-radius: 10px;
  font-size: 12.5px;
}

.alert-row svg {
  width: 15px;
  height: 15px;
  flex: none;
}

.alert-row.danger {
  color: var(--danger);
  background: rgba(229, 72, 77, 0.1);
  border: 1px solid rgba(229, 72, 77, 0.28);
}

.alert-row.warn {
  color: #b07800;
  background: rgba(224, 160, 0, 0.12);
  border: 1px solid rgba(224, 160, 0, 0.3);
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
  gap: 18px;
  overflow: hidden;
}

.hero::before {
  content: '';
  position: absolute;
  top: -80px;
  left: -40px;
  width: 200px;
  height: 200px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--tone), 0.22), transparent 68%);
  pointer-events: none;
  animation: breathe 4.5s ease-in-out infinite;
}

.hero.off {
  opacity: 0.75;
}

.hero.off::before {
  animation: none;
  opacity: 0.3;
}

.ring-wrap {
  position: relative;
  width: 120px;
  height: 120px;
  flex: none;
}

.ring {
  position: absolute;
  inset: 0;
}

.ring .track {
  fill: none;
  stroke: var(--border-strong);
  stroke-width: 9;
  stroke-linecap: round;
  transform: rotate(135deg);
  transform-origin: 60px 60px;
}

.ring .cur {
  fill: none;
  stroke: rgb(var(--tone));
  stroke-width: 9;
  stroke-linecap: round;
  filter: drop-shadow(0 0 6px rgba(var(--tone), 0.6));
  transition: stroke-dasharray 0.6s var(--ease), stroke 0.4s ease;
}

.ring .tgt {
  fill: none;
  stroke: var(--accent);
  stroke-width: 3;
  stroke-linecap: round;
  opacity: 0.75;
  transform: rotate(135deg);
  transform-origin: 60px 60px;
  transition: stroke-dasharray 0.5s var(--ease);
}

.face {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 0;
}

.face b {
  font-size: 30px;
  font-weight: 700;
  line-height: 1;
  color: rgb(var(--tone));
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.5px;
  transition: color 0.4s ease;
}

.face span {
  font-size: 12px;
  color: var(--text-3);
  margin-top: 1px;
}

.face em {
  font-style: normal;
  font-size: 10px;
  color: var(--text-3);
  margin-top: 5px;
}

.hero-side {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 8px;
  min-width: 0;
}

.status-badge {
  display: inline-flex;
  align-items: center;
  padding: 4px 12px;
  border-radius: 999px;
  font-size: 12.5px;
  font-weight: 600;
  color: rgb(var(--tone));
  background: rgba(var(--tone), 0.13);
  border: 1px solid rgba(var(--tone), 0.26);
}

.kv {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.k {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: var(--text-3);
}

.k svg {
  width: 12px;
  height: 12px;
}

.kv b {
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.power-chip {
  margin-top: 4px;
  height: 30px;
  padding: 0 16px;
  border-radius: 999px;
  font-size: 12.5px;
  font-weight: 600;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: all 0.18s var(--ease);
}

.power-chip.on {
  color: #fff;
  background: var(--grad-accent);
  border-color: transparent;
  box-shadow: var(--glow-accent);
}

.power-chip:hover:not(:disabled) {
  transform: translateY(-1px);
}

.power-chip:disabled {
  opacity: 0.5;
  cursor: not-allowed;
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

.h3-hint {
  font-style: normal;
  font-size: 11px;
  font-weight: 400;
  color: var(--text-3);
}

/* ------------------------------------------------ 模式 */
.mode-grid {
  display: grid;
  grid-template-columns: repeat(var(--cols, 3), minmax(0, 1fr));
  gap: 8px;
}

.mode-item {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 11px 4px 9px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  transition: all 0.18s var(--ease);
}

.mode-ico {
  width: 22px;
  height: 22px;
  color: var(--text-3);
  transition: color 0.18s ease, transform 0.22s var(--ease);
}

.mode-name {
  font-size: 11.5px;
  white-space: nowrap;
}

.mode-check {
  position: absolute;
  top: 6px;
  right: 6px;
  width: 13px;
  height: 13px;
  border-radius: 50%;
  background: var(--accent);
  opacity: 0;
  transform: scale(0.4);
  transition: all 0.2s var(--ease);
}

.mode-check::after {
  content: '';
  position: absolute;
  left: 4px;
  top: 3px;
  width: 4px;
  height: 6px;
  border: solid #fff;
  border-width: 0 1.6px 1.6px 0;
  transform: rotate(42deg);
}

.mode-item:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface);
  color: var(--text);
  transform: translateY(-1px);
}

.mode-item:hover:not(:disabled) .mode-ico {
  color: var(--accent);
}

.mode-item.active {
  color: var(--accent);
  background: var(--accent-soft);
  border-color: rgba(23, 178, 106, 0.42);
  box-shadow: var(--glow-accent);
}

/* 设备关闭时，当前设定不该显示成「正在运行」的绿色 */
.mode-item.active.muted {
  color: var(--text-2);
  background: var(--surface);
  border-color: var(--border-strong);
  box-shadow: none;
}

.mode-item.active.muted .mode-ico {
  color: var(--text-3);
}

.mode-item.active.muted .mode-check {
  background: var(--border-strong);
}

.mode-item.active .mode-ico {
  color: var(--accent);
  transform: scale(1.06);
}

.mode-item.active .mode-check {
  opacity: 1;
  transform: scale(1);
}

.mode-item.dim {
  opacity: 0.5;
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
  box-shadow: 0 0 9px rgba(47, 109, 246, 0.5);
  cursor: pointer;
}

.slider:disabled {
  opacity: 0.5;
}

/* ------------------------------------------------ 水箱 / 滤芯 */
.bars {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 9px;
}

.bar-card {
  padding: 11px 12px 9px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.b-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.b-name {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text);
  white-space: nowrap;
}

.b-name svg {
  width: 13px;
  height: 13px;
  flex: none;
  color: var(--text-3);
}

.b-pct {
  font-size: 13px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
  color: var(--brand);
  flex: none;
}

.b-pct.low {
  color: #e0a000;
}

.b-bar {
  height: 5px;
  margin-top: 8px;
  border-radius: 5px;
  background: var(--border-strong);
  overflow: hidden;
}

.b-bar i {
  display: block;
  height: 100%;
  border-radius: 5px;
  background: var(--grad-brand);
  transition: width 0.5s var(--ease);
}

.b-bar.water i {
  background: linear-gradient(90deg, #7fb2f0, #2f6df6);
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
