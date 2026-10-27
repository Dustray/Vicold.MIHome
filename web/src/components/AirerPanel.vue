<script setup lang="ts">
/**
 * 智能晾衣架专属面板。
 *
 * 主视觉是一根晾衣杆：杆子按「升起高度」上下移动，衣物挂在杆上。
 * 下面是「升起 / 暂停 / 下降」三个按钮 + 干衣模式 + UV 杀菌 + 照明。
 *
 * ⚠️ **两个型号的 motor-control 枚举含义不同**，必须按中文标签匹配：
 *  - xiaomi.airer.1sfun：0=暂停 1=升高 2=降低
 *  - ouchen.airer.m01：0=升高 1=暂停 2=降低
 *
 * ⚠️ `target-position` 方向是 **0=最高、100=最低**，跟窗帘相反，
 * 展示时要翻转成「升起高度百分比」（100 - value）。
 */
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { airerProps } from '../lib/spec'
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
  up: '<path d="M12 20V6"/><path d="M6.5 11.5L12 6l5.5 5.5"/>',
  down: '<path d="M12 4v14"/><path d="M6.5 12.5L12 18l5.5-5.5"/>',
  stop: '<rect x="7" y="7" width="10" height="10" rx="1.6"/>',
  dry: '<path d="M4 15c2.4-3.2 4.4 1.6 6.8-1.4S15.6 9.4 20 12"/><path d="M4 20c2.4-3.2 4.4 1.6 6.8-1.4"/>',
  wind: '<path d="M3 8.5h10.5a3 3 0 1 0-3-3"/><path d="M3 15.5h7.5a2.6 2.6 0 1 1-2.6 2.6"/>',
  uv: '<circle cx="12" cy="12" r="3.6"/><path d="M12 2.8v2.2M12 19v2.2M2.8 12H5M19 12h2.2M5.5 5.5l1.5 1.5M17 17l1.5 1.5M18.5 5.5L17 7M7 17l-1.5 1.5"/>',
  lamp: '<path d="M12 3a6 6 0 0 0-3.5 10.9c.6.5.9 1.1 1 1.8l.1.8h4.8l.1-.8c.1-.7.4-1.3 1-1.8A6 6 0 0 0 12 3Z"/><path d="M9.6 19h4.8"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
  shirt: '<path d="M8.6 3.6L4 6.2l1.8 3.6 2-.9v9.5h8.4v-9.5l2 .9L20 6.2l-4.6-2.6a3.4 3.4 0 0 1-6.8 0Z"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.shirt
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const ap = computed(() => airerProps(props.spec))

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
 * 升起高度百分比（0=最低，100=最高）。
 *
 * ⚠️ 设备的 target-position / current-position 是 **0=最高、100=最低**，
 * 直接显示会让「升到顶」显示成 0%。这里统一翻转。
 */
const heightPct = computed(() => {
  const v = numOf(ap.value.current) ?? numOf(ap.value.target)
  if (v === undefined) return 0
  return Math.min(100, Math.max(0, 100 - v))
})

/** 目标高度（滑杆用，也是翻转后的） */
const targetHeight = computed(() => {
  const v = numOf(ap.value.target)
  if (v === undefined) return heightPct.value
  return Math.min(100, Math.max(0, 100 - v))
})

/** 工作状态 */
const statusText = computed(() => {
  const p = ap.value.status
  const v = raw(p)
  if (p?.value_list && v !== null && v !== undefined) {
    const hit = p.value_list.find((i) => String(i.value) === String(v))
    if (hit) return valueLabel(hit)
  }
  return ''
})

const running = computed(() => /上升中|下降中|运行/.test(statusText.value))

const faultText = computed(() => {
  const p = ap.value.fault
  const v = raw(p)
  if (v === null || v === undefined) return ''
  const code = Number(v)
  if (!code) return ''
  const hit = p?.value_list?.find((i) => String(i.value) === String(code))
  return hit ? valueLabel(hit) : `故障码 ${code}`
})

// ---------------------------------------------------------------- 电机按钮
/** 按中文标签找枚举值（两个型号含义不同，不能写死 value） */
function motorByLabel(re: RegExp): unknown {
  const p = ap.value.motor
  if (!p?.value_list) return undefined
  const hit = p.value_list.find((it) => re.test(it.desc_zh || it.description || ''))
  return hit ? hit.value : undefined
}

const upValue = computed(() => motorByLabel(/升高|上升|升到顶/))
const downValue = computed(() => motorByLabel(/降低|下降|降到底/))
const pauseValue = computed(() => motorByLabel(/暂停|停止/))

function fire(def: PropDef | null, value: unknown) {
  if (!def || value === undefined || !online.value) return
  emit('set-prop', def, value)
}

// ---------------------------------------------------------------- 高度滑杆
const heightRange = computed(() => {
  const r = ap.value.target?.range
  if (r && r.length >= 2) return { min: Number(r[0]), max: Number(r[1]) }
  return { min: 0, max: 100 }
})

const heightFill = computed(() => {
  const { min, max } = heightRange.value
  const span = max - min
  if (!span) return '0%'
  return `${Math.min(100, Math.max(0, ((targetHeight.value - min) / span) * 100))}%`
})

function onHeight(e: Event) {
  const def = ap.value.target
  if (!def) return
  // 界面是「升起高度」，设备是「位置（0最高）」，下发前要翻回去
  const shown = Number((e.target as HTMLInputElement).value)
  const { min, max } = heightRange.value
  const deviceVal = Math.round(max - (shown - min))
  emit('set-prop', def, deviceVal)
}

// ---------------------------------------------------------------- 干衣模式
const dryerProp = computed(() => ap.value.dryer)
const dryerOptions = computed(() => {
  const p = dryerProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: valueLabel(it) }))
})
const dryerValue = computed(() => raw(dryerProp.value))

function dryerIcon(label: string): string {
  if (/烘/.test(label)) return 'dry'
  if (/风/.test(label)) return 'wind'
  return 'dry'
}

/** 干衣时长（只有 1sfun 有） */
const dryingTimeProp = computed(() => {
  const s = props.spec
  if (!s) return null
  return s.properties.find((p) => p.base_name === 'drying-time') || null
})

// ---------------------------------------------------------------- UV / 照明
const uvProp = computed(() => ap.value.uv)
const uvOn = computed(() => !!raw(uvProp.value))

const lightProp = computed(() => ap.value.light)
const lightOn = computed(() => !!raw(lightProp.value))
const brightProp = computed(() => ap.value.brightness)
const brightValue = computed(() => numOf(brightProp.value))

const brightFill = computed(() => {
  const p = brightProp.value
  const min = Number(p?.range?.[0] ?? 1)
  const max = Number(p?.range?.[1] ?? 100)
  const v = brightValue.value ?? min
  const span = max - min
  if (!span) return '0%'
  return `${Math.min(100, Math.max(0, ((v - min) / span) * 100))}%`
})

function onBright(e: Event) {
  const p = brightProp.value
  if (!p) return
  const raw2 = Number((e.target as HTMLInputElement).value)
  emit('set-prop', p, p.type === 'float' ? raw2 : Math.round(raw2))
}

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(ap.value.motor)
  add(ap.value.status)
  add(ap.value.target)
  add(ap.value.current)
  add(ap.value.dryer)
  add(ap.value.uv)
  add(ap.value.light)
  add(ap.value.brightness)
  add(ap.value.fault)
  add(dryingTimeProp.value)
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
  <div v-if="spec" class="air">
    <!-- 故障提示 -->
    <div v-if="faultText" class="fault-row">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ faultText }}</span>
    </div>

    <!-- 主视觉：晾衣杆 -->
    <section class="hero" :class="{ running }" :style="{ '--h': String(heightPct) }">
      <div class="scene">
        <div class="ceiling" />
        <!-- 杆子：按升起高度上下移动 -->
        <div class="rod-wrap" :style="{ bottom: `calc(${heightPct}% )` }">
          <div class="rod">
            <span v-for="n in 4" :key="n" class="cloth">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" v-html="icon('shirt')" />
            </span>
          </div>
        </div>
        <div class="floor" />
      </div>

      <div class="hero-side">
        <div class="pct-big">
          <b>{{ Math.round(heightPct) }}</b><span>%</span>
        </div>
        <div class="pct-cap">升起高度</div>
        <div class="status-chip" :class="{ on: running }">
          <i class="dot" />
          {{ statusText || '已停止' }}
        </div>
      </div>
    </section>

    <!-- 升降控制 -->
    <section class="blk">
      <h3>升降</h3>
      <div class="motor-row">
        <button class="m-btn up" :disabled="!online || upValue === undefined" @click="fire(ap.motor, upValue)">
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('up')" />
          </span>
          <span class="m-name">升起</span>
        </button>
        <button class="m-btn stop" :disabled="!online || pauseValue === undefined" @click="fire(ap.motor, pauseValue)">
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('stop')" />
          </span>
          <span class="m-name">暂停</span>
        </button>
        <button class="m-btn down" :disabled="!online || downValue === undefined" @click="fire(ap.motor, downValue)">
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('down')" />
          </span>
          <span class="m-name">下降</span>
        </button>
      </div>
    </section>

    <!-- 高度滑杆 -->
    <section v-if="ap.target" class="blk">
      <h3>
        目标高度
        <em class="h3-hint">{{ Math.round(targetHeight) }}%</em>
      </h3>
      <div class="pos-row">
        <span class="pos-cap">最低</span>
        <input
          class="slider"
          type="range"
          :min="heightRange.min"
          :max="heightRange.max"
          :step="1"
          :value="targetHeight"
          :disabled="!online"
          :style="{ '--fill': heightFill }"
          @change="onHeight"
        />
        <span class="pos-cap">最高</span>
      </div>
    </section>

    <!-- 干衣模式 -->
    <section v-if="dryerOptions.length" class="blk">
      <h3>干衣</h3>
      <div class="dry-row">
        <button
          v-for="o in dryerOptions"
          :key="String(o.value)"
          class="dry-btn"
          :class="{ active: String(o.value) === String(dryerValue) }"
          :disabled="!online"
          @click="fire(dryerProp, o.value)"
        >
          <span class="d-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(dryerIcon(o.label))" />
          </span>
          <span class="d-name">{{ o.label }}</span>
        </button>
      </div>
      <div v-if="dryingTimeProp" class="time-row">
        <span class="t-cap">干衣时长</span>
        <input
          class="slider"
          type="range"
          :min="Number(dryingTimeProp.range?.[0] ?? 0)"
          :max="Number(dryingTimeProp.range?.[1] ?? 240)"
          :step="Number(dryingTimeProp.range?.[2] ?? 1) || 1"
          :value="Number(raw(dryingTimeProp) ?? 0)"
          :disabled="!online"
          @change="(e) => emit('set-prop', dryingTimeProp!, Number((e.target as HTMLInputElement).value))"
        />
        <span class="t-val">{{ Math.round(Number(raw(dryingTimeProp) ?? 0)) }} 分钟</span>
      </div>
    </section>

    <!-- 辅助开关：UV / 照明 -->
    <section v-if="uvProp || lightProp" class="blk">
      <h3>辅助</h3>
      <div class="tile-grid">
        <button
          v-if="uvProp"
          class="tile"
          :class="{ on: uvOn }"
          :disabled="!online || busyProp === uvProp.name"
          @click="emit('set-prop', uvProp, !uvOn)"
        >
          <span class="t-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('uv')" />
          </span>
          <span class="t-body">
            <span class="t-name">UV 杀菌</span>
            <span class="t-state">{{ uvOn ? '工作中' : '已关闭' }}</span>
          </span>
          <span class="t-sw"><i /></span>
        </button>

        <button
          v-if="lightProp"
          class="tile"
          :class="{ on: lightOn }"
          :disabled="!online || busyProp === lightProp.name"
          @click="emit('set-prop', lightProp, !lightOn)"
        >
          <span class="t-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('lamp')" />
          </span>
          <span class="t-body">
            <span class="t-name">照明</span>
            <span class="t-state">{{ lightOn ? '已点亮' : '已关闭' }}</span>
          </span>
          <span class="t-sw"><i /></span>
        </button>
      </div>

      <div v-if="brightProp && lightOn" class="time-row">
        <span class="t-cap">照明亮度</span>
        <input
          class="slider"
          type="range"
          :min="Number(brightProp.range?.[0] ?? 1)"
          :max="Number(brightProp.range?.[1] ?? 100)"
          :step="Number(brightProp.range?.[2] ?? 1) || 1"
          :value="brightValue ?? 1"
          :disabled="!online"
          :style="{ '--fill': brightFill }"
          @change="onBright"
        />
        <span class="t-val">{{ Math.round(brightValue ?? 0) }}%</span>
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
.air {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.air > section,
.air > details {
  animation: fade-up 0.32s var(--ease) both;
}

svg {
  width: 100%;
  height: 100%;
}

/* ------------------------------------------------ 故障 */
.fault-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 12px 0 0;
  padding: 9px 12px;
  border-radius: 10px;
  font-size: 12.5px;
  color: var(--danger);
  background: rgba(229, 72, 77, 0.1);
  border: 1px solid rgba(229, 72, 77, 0.28);
}

.fault-row svg {
  width: 15px;
  height: 15px;
  flex: none;
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
  grid-template-columns: 1fr auto;
  align-items: center;
  gap: 18px;
  overflow: hidden;
}

/* 阳台场景 */
.scene {
  position: relative;
  aspect-ratio: 16 / 11;
  border-radius: var(--radius-md);
  overflow: hidden;
  background: linear-gradient(180deg, var(--surface-2), var(--surface-3));
  border: 1px solid var(--border);
}

/* 天花板 */
.ceiling {
  position: absolute;
  left: 0;
  right: 0;
  top: 0;
  height: 7px;
  background: var(--border-strong);
  opacity: 0.75;
}

/* 地板 */
.floor {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  height: 8px;
  background: var(--border-strong);
  opacity: 0.55;
}

/* 晾衣杆：bottom 百分比 = 升起高度 */
.rod-wrap {
  position: absolute;
  left: 8%;
  right: 8%;
  transition: bottom 0.55s var(--ease);
}

/* 杆 */
.rod {
  position: relative;
  height: 4px;
  border-radius: 3px;
  background: linear-gradient(90deg, var(--border-strong), #b6c2d4, var(--border-strong));
}

/* 挂着的衣服 */
.cloth {
  position: absolute;
  top: 3px;
  width: 15%;
  aspect-ratio: 1 / 1.25;
  color: var(--text-3);
  opacity: 0.75;
}

.cloth:nth-child(1) { left: 12%; }
.cloth:nth-child(2) { left: 33%; color: var(--brand); opacity: 0.6; }
.cloth:nth-child(3) { left: 54%; }
.cloth:nth-child(4) { left: 75%; color: var(--accent); opacity: 0.6; }

/* 右侧读数 */
.hero-side {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 4px;
  min-width: 96px;
}

.pct-big {
  display: flex;
  align-items: baseline;
  gap: 2px;
  color: var(--brand);
}

.pct-big b {
  font-size: 34px;
  font-weight: 700;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.5px;
}

.pct-big span {
  font-size: 13px;
  color: var(--text-3);
}

.pct-cap {
  font-size: 11px;
  color: var(--text-3);
}

.status-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin-top: 7px;
  padding: 3px 10px;
  border-radius: 999px;
  font-size: 11.5px;
  color: var(--text-3);
  background: var(--surface-2);
  border: 1px solid var(--border);
  white-space: nowrap;
}

.status-chip .dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--border-strong);
}

.status-chip.on {
  color: var(--accent);
  background: var(--accent-soft);
  border-color: rgba(23, 178, 106, 0.35);
}

.status-chip.on .dot {
  background: var(--accent);
  box-shadow: 0 0 7px rgba(23, 178, 106, 0.85);
  animation: breathe 1.6s ease-in-out infinite;
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

/* ------------------------------------------------ 电机按钮 */
.motor-row {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 9px;
}

.m-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 7px;
  padding: 14px 6px 11px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  transition: all 0.18s var(--ease);
}

.m-ico {
  width: 26px;
  height: 26px;
  color: var(--text-3);
  transition: color 0.18s ease, transform 0.22s var(--ease);
}

.m-name {
  font-size: 12.5px;
}

.m-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  border-color: var(--border-strong);
  background: var(--surface);
  color: var(--text);
}

.m-btn:hover:not(:disabled) .m-ico {
  color: var(--brand);
}

.m-btn.up:hover:not(:disabled) .m-ico {
  color: var(--accent);
}

.m-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

/* ------------------------------------------------ 滑杆 */
.pos-row,
.time-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.time-row {
  margin-top: 10px;
}

.pos-cap,
.t-cap {
  font-size: 10.5px;
  color: var(--text-3);
  flex: none;
  width: 54px;
}

.t-val {
  font-size: 11px;
  color: var(--text);
  font-variant-numeric: tabular-nums;
  width: 54px;
  text-align: right;
  flex: none;
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

/* ------------------------------------------------ 干衣模式 */
.dry-row {
  display: grid;
  grid-auto-flow: column;
  grid-auto-columns: minmax(0, 1fr);
  gap: 9px;
}

.dry-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 12px 6px 10px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  transition: all 0.18s var(--ease);
}

.d-ico {
  width: 24px;
  height: 24px;
  color: var(--text-3);
  transition: color 0.18s ease;
}

.d-name {
  font-size: 12px;
  white-space: nowrap;
}

.dry-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  border-color: var(--border-strong);
  background: var(--surface);
  color: var(--text);
}

.dry-btn:hover:not(:disabled) .d-ico {
  color: var(--brand);
}

.dry-btn.active {
  color: #fff;
  background: var(--grad-brand);
  border-color: transparent;
  box-shadow: var(--glow-brand);
}

.dry-btn.active .d-ico {
  color: #fff;
}

.dry-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

/* ------------------------------------------------ 辅助磁贴 */
.tile-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 9px;
}

.tile {
  display: flex;
  align-items: center;
  gap: 9px;
  padding: 11px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  text-align: left;
  transition: all 0.18s var(--ease);
}

.t-ico {
  width: 18px;
  height: 18px;
  flex: none;
  color: var(--text-3);
}

.t-body {
  display: flex;
  flex-direction: column;
  gap: 1px;
  flex: 1;
  min-width: 0;
}

.t-name {
  font-size: 12px;
  white-space: nowrap;
}

.t-state {
  font-size: 10px;
  color: var(--text-3);
}

.t-sw {
  position: relative;
  width: 28px;
  height: 16px;
  flex: none;
  border-radius: 999px;
  background: var(--border-strong);
  transition: background-color 0.2s ease;
}

.t-sw i {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: #fff;
  transition: transform 0.2s var(--ease);
}

.tile:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface);
}

.tile.on {
  border-color: rgba(23, 178, 106, 0.42);
  background: var(--accent-soft);
  color: var(--accent);
}

.tile.on .t-ico {
  color: var(--accent);
}

.tile.on .t-sw {
  background: var(--accent);
  box-shadow: 0 0 10px rgba(23, 178, 106, 0.55);
}

.tile.on .t-sw i {
  transform: translateX(12px);
}

.tile:disabled {
  opacity: 0.5;
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
