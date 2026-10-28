<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { propBy, powerSiid } from '../lib/spec'
import { valueLabel } from '../lib/zh'
import PropControl from './PropControl.vue'

const props = defineProps<{
  device: Device
  spec: Spec | null
  values: Record<string, unknown>
  busyProp: string
  groups: SpecGroup[]
  /** 设备所在房间的环境读数（环境页聚合），用于兜底展示房间温度 */
  roomEnv?: { name: string; temp?: number; humidity?: number; count: number; stale: boolean } | null
}>()
const emit = defineEmits<{
  (e: 'set-prop', def: PropDef, value: unknown): void
  (e: 'run-action', siid: number, aiid: number, name: string): void
}>()

// ---------------------------------------------------------------- 图标
/** 内联 SVG（仅 path 片段），静态常量 */
const ICONS: Record<string, string> = {
  power: '<path d="M12 3.2v8.4"/><path d="M7.7 6.4a7 7 0 1 0 8.6 0"/>',
  snow: '<path d="M12 2.6v18.8M3.9 7.3l16.2 9.4M20.1 7.3L3.9 16.7"/><path d="M9.2 4.6L12 7.4l2.8-2.8M9.2 19.4L12 16.6l2.8 2.8"/>',
  sun: '<circle cx="12" cy="12" r="4.2"/><path d="M12 2.4v2.4M12 19.2v2.4M2.4 12h2.4M19.2 12h2.4M5.2 5.2l1.7 1.7M17.1 17.1l1.7 1.7M18.8 5.2l-1.7 1.7M6.9 17.1l-1.7 1.7"/>',
  drop: '<path d="M12 3.4s5.4 5.8 5.4 9.4A5.4 5.4 0 0 1 12 18.2a5.4 5.4 0 0 1-5.4-5.4C6.6 9.2 12 3.4 12 3.4z"/><path d="M9.6 13.2a2.5 2.5 0 0 0 2.4 2.4"/>',
  fan: '<circle cx="12" cy="12" r="2.1"/><path d="M12 9.9c0-3.3-1-5.4-2.9-5.8-1.8-.4-2.9 1-2.6 2.6.3 1.7 2.4 3 5.5 3.2z"/><path d="M14.1 12c3.3 0 5.4-1 5.8-2.9.4-1.8-1-2.9-2.6-2.6-1.7.3-3 2.4-3.2 5.5z"/><path d="M12 14.1c0 3.3 1 5.4 2.9 5.8 1.8.4 2.9-1 2.6-2.6-.3-1.7-2.4-3-5.5-3.2z"/><path d="M9.9 12c-3.3 0-5.4 1-5.8 2.9-.4 1.8 1 2.9 2.6 2.6 1.7-.3 3-2.4 3.2-5.5z"/>',
  auto: '<path d="M20.4 12a8.4 8.4 0 1 1-2.6-6.1"/><path d="M20.6 4.6V9h-4.4"/>',
  thermo: '<path d="M14 14.7V5.2a2 2 0 1 0-4 0v9.5a4 4 0 1 0 4 0z"/>',
  wind: '<path d="M3 8.6h11.2a3 3 0 1 0-3-3"/><path d="M3 15.6h8.6a2.8 2.8 0 1 1-2.8 2.8"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  minus: '<path d="M5 12h14"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.fan
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const spec = computed(() => props.spec)

const modeProp = computed(() => propBy(props.spec, 'mode'))
const targetProp = computed(() => propBy(props.spec, 'target-temperature'))
const fanProp = computed(() => propBy(props.spec, 'fan-level'))
const indoorProp = computed(() => propBy(props.spec, 'temperature'))

/** 主开关与模式同属「空调」模块，用 powerSiid 定位，避免取错其它 on */
const powerProp = computed<PropDef | null>(() => {
  const siid = powerSiid(props.spec)
  return siid === null ? null : propBy(props.spec, 'on', siid)
})

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

const isOn = computed(() => !!(powerProp.value && props.values[powerProp.value.name]))

// ---------------------------------------------------------------- 模式配色
interface ModeMeta {
  re: RegExp
  icon: string
  /** 该模式的强调色（rgb 三元组），圆环与选中态都用它 */
  rgb: string
}

const MODE_META: ModeMeta[] = [
  { re: /制冷|冷|cool/i, icon: 'snow', rgb: '52, 140, 255' },
  { re: /制热|加热|暖|heat/i, icon: 'sun', rgb: '255, 138, 61' },
  { re: /除湿|干燥|干|dry/i, icon: 'drop', rgb: '139, 92, 246' },
  { re: /送风|通风|风|fan/i, icon: 'fan', rgb: '20, 178, 196' },
]

const NEUTRAL_RGB = '23, 178, 106'
/** 关机时的中性色：设定值还在，但不再用模式色渲染，一眼能看出没开 */
const OFF_RGB = '148, 163, 184'

function metaOf(label: string): ModeMeta {
  return MODE_META.find((m) => m.re.test(label)) || { re: /./, icon: 'fan', rgb: NEUTRAL_RGB }
}

const modes = computed(() => {
  const p = modeProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => {
    const label = valueLabel(it)
    const meta = metaOf(label)
    return { value: it.value, label, icon: meta.icon, rgb: meta.rgb }
  })
})

const modeValue = computed(() => raw(modeProp.value))

const modeLabel = computed(
  () => modes.value.find((m) => String(m.value) === String(modeValue.value))?.label || ''
)

const modeColor = computed(() => {
  if (!isOn.value) return OFF_RGB
  return modeLabel.value ? metaOf(modeLabel.value).rgb : NEUTRAL_RGB
})

/** 徽章图标：关机时用电源符号，比留一个"制热"雪花更准确 */
const badgeIcon = computed(() => {
  if (!isOn.value) return 'power'
  return modes.value.find((m) => String(m.value) === String(modeValue.value))?.icon || 'fan'
})

function pickMode(value: unknown) {
  const p = modeProp.value
  if (!p || !online.value) return
  if (String(value) === String(modeValue.value)) return
  emit('set-prop', p, value)
}

// ---------------------------------------------------------------- 目标温度
const tMin = computed(() => Number(targetProp.value?.range?.[0] ?? 16))
const tMax = computed(() => Number(targetProp.value?.range?.[1] ?? 32))
const tStep = computed(() => Number(targetProp.value?.range?.[2] ?? 1) || 1)

const target = computed(() => numOf(targetProp.value))

/** 显示值：设备还没返回温度时，先按区间下限占位，避免按钮点了没反应 */
const shownTarget = computed(() => target.value ?? tMin.value)

function clampTemp(v: number): number {
  const lo = tMin.value
  const hi = tMax.value
  return Math.min(hi, Math.max(lo, v))
}

function setTemp(v: number) {
  const p = targetProp.value
  if (!p || !online.value) return
  const next = clampTemp(Math.round(v / tStep.value) * tStep.value)
  if (next === target.value) return
  emit('set-prop', p, next)
}

/** 长按连续调节：按住 380ms 后每 150ms 走一步 */
let holdTimer: number | null = null
let holdDelay: number | null = null

function stopHold() {
  if (holdDelay !== null) {
    clearTimeout(holdDelay)
    holdDelay = null
  }
  if (holdTimer !== null) {
    clearInterval(holdTimer)
    holdTimer = null
  }
}

function startHold(delta: number) {
  stopHold()
  setTemp(shownTarget.value + delta * tStep.value)
  holdDelay = window.setTimeout(() => {
    holdTimer = window.setInterval(() => {
      setTemp(shownTarget.value + delta * tStep.value)
    }, 150)
  }, 380)
}

onBeforeUnmount(stopHold)

/** 刻度标签：16 / 20 / 24 / 28 / 32（按区间五等分） */
const ticks = computed(() => {
  const lo = tMin.value
  const hi = tMax.value
  const span = hi - lo
  return [0, 1, 2, 3, 4].map((i) => Math.round(lo + (span * i) / 4))
})

/** 拖动滑杆时就地显示，松手才下发 */
const dragging = ref(false)
const dragValue = ref(0)

const sliderValue = computed(() => (dragging.value ? dragValue.value : shownTarget.value))

function onSlide(e: Event) {
  dragging.value = true
  dragValue.value = Number((e.target as HTMLInputElement).value)
}

function onSlideEnd(e: Event) {
  const v = Number((e.target as HTMLInputElement).value)
  dragging.value = false
  setTemp(v)
}

const displayTemp = computed(() => (dragging.value ? dragValue.value : shownTarget.value))

/** 把温度换算成 0~100 的填充比例 */
function pctOf(v: number): number {
  const span = tMax.value - tMin.value || 1
  return Math.min(100, Math.max(0, ((v - tMin.value) / span) * 100))
}

/** 环的填充跟着拖动实时走，拖滑杆时圆环也一起变 */
const ringPct = computed(() => pctOf(displayTemp.value))

const heroStyle = computed(() => ({
  '--mc': modeColor.value,
  '--p': `${ringPct.value}%`,
}))

/** 滑杆填充要跟手，用拖动中的值单独算一份 */
const sliderStyle = computed(() => ({
  '--mc': modeColor.value,
  '--p': `${pctOf(sliderValue.value)}%`,
}))

const indoor = computed(() => numOf(indoorProp.value))

// ---------------------------------------------------------------- 房间环境
/**
 * 空调自报的 temperature 挂在「环境参数」服务下，但读出来是回风口温度：
 * 实测主卧空调关机时报 28°C，而房间温湿度计是 24.6°C，差 3 度多。
 * 用户想看的「当前房间温度」显然是后者，所以面板优先用房间探头，
 * 房间没有可读探头时才退回空调自报值，并在提示里说明来源。
 */
const roomEnv = computed(() => props.roomEnv ?? null)

const roomTemp = computed(() => roomEnv.value?.temp)

/** 面板主展示值：房间探头优先，其次空调自报 */
const indoorShown = computed(() => roomTemp.value ?? indoor.value)

const indoorLabel = computed(() => (roomTemp.value !== undefined ? '房间' : '室内'))

const indoorHint = computed(() => {
  const env = roomEnv.value
  if (roomTemp.value !== undefined) {
    const n = env?.count ?? 0
    const stale = env?.stale ? '（传感器离线，显示最后读数）' : ''
    const vent = indoor.value === undefined ? '' : `，空调回风口 ${indoor.value}°C`
    return `${env?.name || '房间'}温湿度传感器读数${stale}，共 ${n} 个${vent}`
  }
  if (indoor.value !== undefined) {
    return '这个房间没有温湿度传感器，这里显示的是空调回风口温度'
  }
  return '空调没有环境温度，这个房间也没有温湿度传感器'
})

/** 湿度只有房间探头能给，空调规格里没有 */
const roomHum = computed(() => roomEnv.value?.humidity)

// ---------------------------------------------------------------- 风速
/** 自动档不参与档位条计数，其余按顺序递增（低/中/高 → 1/2/3 条） */
const FAN_AUTO_RE = /自动|auto/i

const fans = computed(() => {
  const p = fanProp.value
  if (!p?.value_list) return []
  let n = 0
  return p.value_list.map((it) => {
    const label = valueLabel(it)
    const auto = FAN_AUTO_RE.test(label)
    if (!auto) n += 1
    return { value: it.value, label, auto, bars: auto ? 0 : n }
  })
})

/** 档位条最多画几条（有 4 档风的机型画 4 条） */
const maxBars = computed(() => Math.max(3, ...fans.value.map((f) => f.bars)))

const fanValue = computed(() => raw(fanProp.value))

function pickFan(value: unknown) {
  const p = fanProp.value
  if (!p || !online.value) return
  if (String(value) === String(fanValue.value)) return
  emit('set-prop', p, value)
}

const fanLabel = computed(
  () => fans.value.find((f) => String(f.value) === String(fanValue.value))?.label || ''
)

// ---------------------------------------------------------------- 主开关
function togglePower() {
  const p = powerProp.value
  if (!p || !online.value) return
  emit('set-prop', p, !isOn.value)
}

// ---------------------------------------------------------------- 收纳其余参数
/** 面板已消费的属性，剩下的收进「更多设置」，功能一个都不丢 */
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  for (const d of [powerProp.value, modeProp.value, targetProp.value, fanProp.value, indoorProp.value]) {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  return keys
})

const showRaw = ref(false)

/** 工程/调试类模块，默认折叠（空调一般没有，保留通用处理） */
const DEBUG_SERVICES = new Set(['custom-service', 'aqi'])

const svcNameOf = computed(() => {
  const map = new Map<string, string>()
  for (const s of props.spec?.services || []) map.set(String(s.siid), s.name)
  return map
})

const debugSiids = computed(() => {
  const set = new Set<string>()
  for (const [siid, name] of svcNameOf.value) if (DEBUG_SERVICES.has(name)) set.add(siid)
  return set
})

const restGroups = computed<SpecGroup[]>(() => {
  const keys = consumedKeys.value
  const hidden = showRaw.value ? new Set<string>() : debugSiids.value
  return props.groups
    .filter((g) => !hidden.has(String(g.key)))
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      actions: g.actions,
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})
</script>

<template>
  <div v-if="spec" class="ac">
    <!-- 温控主视觉：圆环显示目标温度，随模式换色 -->
    <section class="hero" :class="{ off: !isOn, dim: !online }" :style="heroStyle">
      <div class="hero-top">
        <div class="mode-badge">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(badgeIcon)" />
          <span>{{ isOn ? modeLabel || '运行中' : '已关闭' }}</span>
        </div>
        <button
          class="power-btn"
          :class="{ on: isOn }"
          :disabled="!online || !powerProp || busyProp === powerProp?.name"
          :title="isOn ? '关闭' : '开启'"
          @click="togglePower"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" v-html="icon('power')" />
        </button>
      </div>

      <div class="thermo">
        <button
          class="step"
          :disabled="!online || !targetProp || shownTarget <= tMin"
          aria-label="降低温度"
          @pointerdown.prevent="startHold(-1)"
          @pointerup="stopHold"
          @pointerleave="stopHold"
          @pointercancel="stopHold"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" v-html="icon('minus')" />
        </button>

        <div class="ring-wrap">
          <div class="ring">
            <div class="ring-face">
              <b class="ring-val">{{ displayTemp }}</b>
              <span class="ring-unit">°C</span>
              <em class="ring-cap">目标温度</em>
            </div>
          </div>
        </div>

        <button
          class="step"
          :disabled="!online || !targetProp || shownTarget >= tMax"
          aria-label="升高温度"
          @pointerdown.prevent="startHold(1)"
          @pointerup="stopHold"
          @pointerleave="stopHold"
          @pointercancel="stopHold"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" v-html="icon('plus')" />
        </button>
      </div>

      <div class="scale">
        <input
          type="range"
          :min="tMin"
          :max="tMax"
          :step="tStep"
          :value="sliderValue"
          :disabled="!online || !targetProp"
          :style="sliderStyle"
          @input="onSlide"
          @change="onSlideEnd"
        />
        <div class="scale-ticks">
          <span v-for="(t, i) in ticks" :key="i">{{ t }}</span>
        </div>
      </div>

      <div class="hero-foot">
        <span class="foot-item" :title="indoorHint">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('thermo')" />
          {{ indoorLabel }}
          <b>{{ indoorShown === undefined ? '—' : indoorShown }}</b>
          <em v-if="indoorShown !== undefined">°C</em>
        </span>
        <span v-if="roomHum !== undefined" class="foot-item">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('drop')" />
          湿度
          <b>{{ roomHum }}</b>
          <em>%</em>
        </span>
        <span class="foot-item">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('wind')" />
          风力
          <b>{{ fanLabel || '—' }}</b>
        </span>
      </div>
    </section>

    <!-- 模式：带图标单选，选中色即该模式色 -->
    <section v-if="modes.length" class="blk">
      <h3>模式</h3>
      <div class="mode-grid" :style="{ '--cols': String(modes.length) }">
        <button
          v-for="m in modes"
          :key="String(m.value)"
          class="mode-item"
          :class="{ active: String(m.value) === String(modeValue) }"
          :style="{ '--mode': m.rgb }"
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

    <!-- 风速 -->
    <section v-if="fans.length" class="blk">
      <h3>风速</h3>
      <div class="fan-seg">
        <button
          v-for="f in fans"
          :key="String(f.value)"
          class="fan-item"
          :class="{ active: String(f.value) === String(fanValue) }"
          :disabled="!online"
          @click="pickFan(f.value)"
        >
          <span class="fan-glyph">
            <svg v-if="f.auto" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('auto')" />
            <span v-else class="bars">
              <i v-for="b in maxBars" :key="b" :class="{ lit: b <= f.bars }" :style="{ height: 6 + b * 4 + 'px' }" />
            </span>
          </span>
          <span class="fan-name">{{ f.label }}</span>
        </button>
      </div>
    </section>

    <!-- 其余参数/动作（开关状态切换、温度±1、风速±1 等）收进折叠区 -->
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

        <label v-if="debugSiids.size" class="raw-toggle">
          <input v-model="showRaw" type="checkbox" />
          <span>显示工程参数</span>
        </label>
      </div>
    </details>
  </div>
</template>

<style scoped>
.ac {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.ac > section,
.ac > details {
  animation: fade-up 0.32s var(--ease) both;
}
.ac > section:nth-of-type(2) {
  animation-delay: 0.05s;
}
.ac > section:nth-of-type(3) {
  animation-delay: 0.1s;
}

svg {
  width: 100%;
  height: 100%;
}

/* ------------------------------------------------ 温控主视觉 */
.hero {
  position: relative;
  margin-top: 12px;
  padding: 14px 16px 12px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  overflow: hidden;
  transition: opacity 0.25s ease, border-color 0.3s ease;
}

/* 随模式变色的光斑 */
.hero::before {
  content: '';
  position: absolute;
  top: -60px;
  left: 50%;
  width: 260px;
  height: 260px;
  margin-left: -130px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--mc), 0.2), transparent 66%);
  pointer-events: none;
  animation: breathe 5s ease-in-out infinite;
  transition: background 0.5s ease;
}

.hero.off {
  opacity: 0.92;
}
.hero.off::before {
  animation: none;
  opacity: 0.25;
}
.hero.dim {
  opacity: 0.5;
}

.hero-top {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.mode-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 11px 0 9px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
  color: rgb(var(--mc));
  background: rgba(var(--mc), 0.12);
  border: 1px solid rgba(var(--mc), 0.26);
  transition: color 0.3s ease, background-color 0.3s ease, border-color 0.3s ease;
}

.mode-badge svg {
  width: 14px;
  height: 14px;
  flex: none;
}

.power-btn {
  width: 34px;
  height: 34px;
  flex: none;
  display: grid;
  place-items: center;
  border-radius: 50%;
  color: var(--text-3);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: all 0.2s var(--ease);
}

.power-btn svg {
  width: 16px;
  height: 16px;
}

.power-btn:hover:not(:disabled) {
  color: var(--text);
  border-color: var(--border-strong);
  transform: translateY(-1px);
}

.power-btn.on {
  color: #fff;
  background: var(--grad-accent);
  border-color: transparent;
  box-shadow: var(--glow-accent);
}

.power-btn:disabled {
  opacity: 0.5;
}

/* 温度圆环 */
.thermo {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 20px;
  margin: 6px 0 2px;
}

.step {
  width: 42px;
  height: 42px;
  flex: none;
  display: grid;
  place-items: center;
  border-radius: 50%;
  color: rgb(var(--mc));
  background: rgba(var(--mc), 0.1);
  border: 1px solid rgba(var(--mc), 0.24);
  transition: all 0.16s var(--ease);
  touch-action: none;
}

.step svg {
  width: 18px;
  height: 18px;
}

.step:hover:not(:disabled) {
  background: rgba(var(--mc), 0.18);
  border-color: rgba(var(--mc), 0.45);
  transform: scale(1.06);
  box-shadow: 0 6px 18px -10px rgba(var(--mc), 0.95);
}

.step:active:not(:disabled) {
  transform: scale(0.94);
}

.step:disabled {
  opacity: 0.35;
}

.ring-wrap {
  position: relative;
  flex: none;
}

/* 外圈刻度：只在环带区域显示细密刻痕 */
.ring-wrap::before {
  content: '';
  position: absolute;
  inset: -9px;
  border-radius: 50%;
  background: repeating-conic-gradient(
    from -90deg,
    rgba(var(--mc), 0.42) 0 1deg,
    transparent 1deg 7.5deg
  );
  -webkit-mask: radial-gradient(circle, transparent 0 61px, #000 62px 66px, transparent 67px);
  mask: radial-gradient(circle, transparent 0 61px, #000 62px 66px, transparent 67px);
  pointer-events: none;
  transition: background 0.4s ease;
}

.ring {
  position: relative;
  width: 120px;
  height: 120px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  background: conic-gradient(
    from -90deg,
    rgb(var(--mc)) 0 var(--p),
    var(--surface-3) var(--p) 100%
  );
  box-shadow: 0 0 26px -12px rgba(var(--mc), 1);
  transition: background 0.5s var(--ease), box-shadow 0.4s ease;
}

.ring::after {
  content: '';
  position: absolute;
  inset: 9px;
  border-radius: 50%;
  background: var(--surface);
}

.ring-face {
  position: relative;
  z-index: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0;
}

.ring-val {
  font-size: 42px;
  font-weight: 700;
  line-height: 1.02;
  letter-spacing: -0.5px;
  color: rgb(var(--mc));
  font-variant-numeric: tabular-nums;
  transition: color 0.3s ease;
}

.ring-unit {
  margin-top: -4px;
  font-size: 12px;
  font-weight: 600;
  color: rgb(var(--mc));
  opacity: 0.75;
}

.ring-cap {
  margin-top: 3px;
  font-style: normal;
  font-size: 10px;
  letter-spacing: 0.4px;
  color: var(--text-3);
}

/* 温度滑杆 */
.scale {
  position: relative;
  padding: 4px 3px 0;
}

.scale input[type='range'] {
  -webkit-appearance: none;
  appearance: none;
  display: block;
  width: 100%;
  height: 22px;
  background: transparent;
  cursor: pointer;
}

.scale input[type='range']::-webkit-slider-runnable-track {
  height: 6px;
  border-radius: 6px;
  background: linear-gradient(
    90deg,
    rgb(var(--mc)) 0 var(--p),
    var(--border-strong) var(--p) 100%
  );
  transition: background 0.2s ease;
}

.scale input[type='range']::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 17px;
  height: 17px;
  margin-top: -5.5px;
  border-radius: 50%;
  background: #fff;
  border: 2.5px solid rgb(var(--mc));
  box-shadow: 0 2px 8px rgba(var(--mc), 0.55);
  transition: transform 0.15s var(--ease);
}

.scale input[type='range']:active::-webkit-slider-thumb {
  transform: scale(1.15);
}

.scale input[type='range']:disabled {
  opacity: 0.5;
  cursor: default;
}

.scale-ticks {
  display: flex;
  justify-content: space-between;
  padding: 0 2px;
  margin-top: 1px;
  font-size: 10px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.hero-foot {
  display: flex;
  align-items: center;
  gap: 18px;
  margin-top: 11px;
  padding-top: 10px;
  border-top: 1px dashed var(--border);
}

.foot-item {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  color: var(--text-3);
  white-space: nowrap;
}

.foot-item svg {
  width: 13px;
  height: 13px;
  flex: none;
}

.foot-item b {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.foot-item em {
  font-style: normal;
  font-size: 10px;
  color: var(--text-3);
}

/* ------------------------------------------------ 分区标题 */
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

/* ------------------------------------------------ 模式 */
.mode-grid {
  display: grid;
  grid-template-columns: repeat(var(--cols, 4), minmax(0, 1fr));
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
  background: rgb(var(--mode));
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
  color: rgb(var(--mode));
}

.mode-item.active {
  color: rgb(var(--mode));
  background: rgba(var(--mode), 0.12);
  border-color: rgba(var(--mode), 0.45);
  box-shadow: 0 8px 22px -14px rgba(var(--mode), 1);
}

.mode-item.active .mode-ico {
  color: rgb(var(--mode));
  transform: scale(1.06);
}

.mode-item.active .mode-check {
  opacity: 1;
  transform: scale(1);
}

.mode-item:disabled {
  opacity: 0.5;
}

/* ------------------------------------------------ 风速 */
.fan-seg {
  display: grid;
  grid-auto-flow: column;
  grid-auto-columns: minmax(0, 1fr);
  gap: 8px;
  padding: 5px;
  border-radius: var(--radius-md);
  background: var(--surface-2);
  border: 1px solid var(--border);
}

.fan-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 5px;
  padding: 8px 2px 6px;
  border-radius: 9px;
  color: var(--text-2);
  transition: all 0.18s var(--ease);
}

.fan-glyph {
  display: flex;
  align-items: flex-end;
  justify-content: center;
  height: 18px;
}

.fan-glyph > svg {
  width: 16px;
  height: 16px;
  align-self: center;
}

.bars {
  display: flex;
  align-items: flex-end;
  gap: 2.5px;
  height: 18px;
}

.bars i {
  width: 4px;
  border-radius: 2px;
  background: var(--border-strong);
  transition: background-color 0.2s ease, box-shadow 0.2s ease;
}

.fan-name {
  font-size: 11.5px;
  white-space: nowrap;
}

.fan-item:hover:not(:disabled) {
  background: var(--surface);
  color: var(--text);
}

.fan-item.active {
  background: var(--surface);
  color: var(--accent);
  box-shadow: var(--shadow-sm);
}

.fan-item.active .bars i.lit {
  background: var(--accent);
  box-shadow: 0 0 7px rgba(23, 178, 106, 0.3);
}

.fan-item:disabled {
  opacity: 0.5;
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

.adv-body section {
  margin-bottom: 6px;
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

.raw-toggle {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-top: 10px;
  padding-top: 10px;
  border-top: 1px dashed var(--border);
  font-size: 11.5px;
  color: var(--text-3);
  cursor: pointer;
  user-select: none;
}

.raw-toggle input {
  width: 13px;
  height: 13px;
  accent-color: var(--accent);
  cursor: pointer;
}
</style>
