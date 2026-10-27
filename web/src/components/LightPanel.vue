<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { powerSiid, propBy } from '../lib/spec'
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
  (e: 'refresh'): void
}>()

// ---------------------------------------------------------------- 图标
/** 内联 SVG（仅 path 片段），静态常量 */
const ICONS: Record<string, string> = {
  power: '<path d="M12 3.2v8.4"/><path d="M7.7 6.4a7 7 0 1 0 8.6 0"/>',
  sun: '<circle cx="12" cy="12" r="4.2"/><path d="M12 2.4v2.4M12 19.2v2.4M2.4 12h2.4M19.2 12h2.4M5.2 5.2l1.7 1.7M17.1 17.1l1.7 1.7M18.8 5.2l-1.7 1.7M6.9 17.1l-1.7 1.7"/>',
  moon: '<path d="M20 14.4A8.6 8.6 0 0 1 9.6 4a8.6 8.6 0 1 0 10.4 10.4z"/>',
  book: '<path d="M4 5.4h5.6A2.4 2.4 0 0 1 12 7.8v11.4a2.4 2.4 0 0 0-2.4-2.4H4z"/><path d="M20 5.4h-5.6A2.4 2.4 0 0 0 12 7.8v11.4a2.4 2.4 0 0 1 2.4-2.4H20z"/>',
  tv: '<rect x="3" y="5" width="18" height="12" rx="2"/><path d="M8.6 20.8h6.8"/>',
  monitor: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M12 16v4M8.4 20h7.2"/>',
  spark: '<path d="M12 3.6l1.9 4.9 4.9 1.9-4.9 1.9L12 16.8l-1.9-4.5L5.2 10.4l4.9-1.9z"/><path d="M18.2 16.4l.9 2.2 2.2.9-2.2.9-.9 2.2-.9-2.2-2.2-.9 2.2-.9z"/>',
  cup: '<path d="M4.6 8.4h11v5.8a4.2 4.2 0 0 1-4.2 4.2H8.8a4.2 4.2 0 0 1-4.2-4.2z"/><path d="M15.6 9.6h1.8a2.6 2.6 0 0 1 0 5.2h-1.8"/><path d="M4.6 21h11"/>',
  music: '<path d="M9.2 18V6.4l9.6-2v11.4"/><circle cx="6.8" cy="18.2" r="2.6"/><circle cx="16.4" cy="15.8" r="2.6"/>',
  sunrise: '<path d="M12 3.2v4.2M5.4 6.2l1.9 1.9M18.6 6.2l-1.9 1.9"/><path d="M3.6 18.4h16.8"/><path d="M6.6 18.4a5.4 5.4 0 0 1 10.8 0"/>',
  sunset: '<path d="M3.6 18.4h16.8"/><path d="M6.6 18.4a5.4 5.4 0 0 1 10.8 0"/><path d="M12 9.4V5.2M5.4 11.6L3.5 9.7M18.6 11.6l1.9-1.9"/>',
  palette: '<circle cx="12" cy="12" r="8.6"/><circle cx="9.2" cy="9.4" r="1.15"/><circle cx="14.8" cy="9.4" r="1.15"/><circle cx="9.6" cy="14.8" r="1.15"/>',
  warm: '<path d="M12 3.4c2.6 3 4.4 5.4 4.4 8a4.4 4.4 0 0 1-8.8 0c0-1.5.6-2.9 1.5-3.9.1 1.4.9 2.3 1.7 2.3.9 0 1.4-.8 1.1-2a9 9 0 0 1 .1-4.4z"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.palette
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const spec = computed(() => props.spec)

const brProp = computed(() => propBy(props.spec, 'brightness'))
const ctProp = computed(() => propBy(props.spec, 'color-temperature'))
const modeProp = computed(() => propBy(props.spec, 'mode'))

/** 主开关与亮度同属「灯光」模块，用 powerSiid 定位，避免取到别的模块的 on */
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

// ---------------------------------------------------------------- 色温 → 颜色
/**
 * 色温锚点：用于把坐标轴画成「暖橙 → 暖白 → 冷白 → 淡蓝」。
 *
 * 这里用感知色阶而不是黑体辐射的物理换算 —— 物理换算在 5000K 以上
 * 基本退化成纯白，二维平面会失去"往右变冷"的直观提示。
 */
const CT_STOPS: [number, [number, number, number]][] = [
  [2000, [255, 132, 34]],
  [2700, [255, 168, 88]],
  [3500, [255, 198, 136]],
  [4500, [255, 226, 190]],
  [5500, [245, 243, 255]],
  [6500, [204, 225, 255]],
  [7200, [178, 205, 255]],
]

function ctRgb(k: number): [number, number, number] {
  const first = CT_STOPS[0]
  const last = CT_STOPS[CT_STOPS.length - 1]
  if (k <= first[0]) return first[1]
  if (k >= last[0]) return last[1]
  for (let i = 0; i < CT_STOPS.length - 1; i += 1) {
    const [k0, c0] = CT_STOPS[i]
    const [k1, c1] = CT_STOPS[i + 1]
    if (k >= k0 && k <= k1) {
      const t = (k - k0) / (k1 - k0)
      return [
        Math.round(c0[0] + (c1[0] - c0[0]) * t),
        Math.round(c0[1] + (c1[1] - c0[1]) * t),
        Math.round(c0[2] + (c1[2] - c0[2]) * t),
      ]
    }
  }
  return last[1]
}

function rgbStr(k: number): string {
  return ctRgb(k).join(', ')
}

// ---------------------------------------------------------------- 取值范围
const brMin = computed(() => Number(brProp.value?.range?.[0] ?? 1))
const brMax = computed(() => Number(brProp.value?.range?.[1] ?? 100))
const brStep = computed(() => Number(brProp.value?.range?.[2] ?? 1) || 1)

const ctMin = computed(() => Number(ctProp.value?.range?.[0] ?? 2700))
const ctMax = computed(() => Number(ctProp.value?.range?.[1] ?? 6500))

function clampNum(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v))
}

/** 设备回读值 */
const devBr = computed(() => numOf(brProp.value))
const devCt = computed(() => numOf(ctProp.value))

/** 拖动中的本地值：松手前只本地渲染，不下发 */
const localBr = ref<number | null>(null)
const localCt = ref<number | null>(null)
const dragging = ref(false)

const brNow = computed(() => Math.round(localBr.value ?? devBr.value ?? brMax.value))
const ctNow = computed(() =>
  Math.round(localCt.value ?? devCt.value ?? (ctMin.value + ctMax.value) / 2)
)

/** 光标在平面里的位置（百分比）：横轴色温、纵轴亮度（上亮下暗） */
const xPct = computed(() => {
  const span = ctMax.value - ctMin.value || 1
  return clampNum(((ctNow.value - ctMin.value) / span) * 100, 0, 100)
})

const yPct = computed(() => {
  const span = brMax.value - brMin.value || 1
  return clampNum(100 - ((brNow.value - brMin.value) / span) * 100, 0, 100)
})

/** 光标横向位置：月光档色温被锁在设备值上，光标居中显示成一维滑杆 */
const cursorX = computed(() => (nightMode.value ? 50 : xPct.value))

/** 当前光标颜色 */
const cursorRgb = computed(() => rgbStr(ctNow.value))

const stageStyle = computed(() => ({
  '--cr': cursorRgb.value,
}))

/** 平面背景的 5 个色温锚点 */
const padStyle = computed(() => {
  const lo = ctMin.value
  const span = ctMax.value - lo
  return {
    '--c0': `rgb(${rgbStr(lo)})`,
    '--c1': `rgb(${rgbStr(lo + span * 0.25)})`,
    '--c2': `rgb(${rgbStr(lo + span * 0.5)})`,
    '--c3': `rgb(${rgbStr(lo + span * 0.75)})`,
    '--c4': `rgb(${rgbStr(ctMax.value)})`,
  }
})

// ---------------------------------------------------------------- 坐标轴刻度
/** 横轴：色温五等分，取整到 50K */
const ctTicks = computed(() => {
  const lo = ctMin.value
  const span = ctMax.value - lo
  return [0, 1, 2, 3, 4].map((i) => Math.round((lo + (span * i) / 4) / 50) * 50)
})

/** 纵轴：亮度五档，自上而下对应 100% → 下限 */
const brTicks = computed(() => {
  const hi = brMax.value
  const lo = brMin.value
  return [0, 0.25, 0.5, 0.75, 1].map((f) => Math.round(hi - (hi - lo) * f))
})

// ---------------------------------------------------------------- 拖动
const pad = ref<HTMLElement | null>(null)

/** 色温细度过高（3800K 跨度）时取整到 10K，避免拖一格跳几十个值 */
function snapCt(v: number): number {
  const coarse = Math.round(v / 10) * 10
  return clampNum(coarse, ctMin.value, ctMax.value)
}

function snapBr(v: number): number {
  const step = brStep.value
  const snapped = Math.round(v / step) * step
  return clampNum(snapped, brMin.value, brMax.value)
}

function applyPoint(clientX: number, clientY: number) {
  const el = pad.value
  if (!el) return
  const r = el.getBoundingClientRect()
  if (!r.width || !r.height) return
  const fx = clampNum((clientX - r.left) / r.width, 0, 1)
  const fy = clampNum((clientY - r.top) / r.height, 0, 1)
  // 月光档位只允许改亮度：横向不动，才不会把设备顶回日光
  if (ctEditable.value) localCt.value = snapCt(ctMin.value + fx * (ctMax.value - ctMin.value))
  localBr.value = snapBr(brMin.value + (1 - fy) * (brMax.value - brMin.value))
}

/** 防抖下发：拖动过程中不发请求，停手 220ms / 松手时统一提交 */
let commitTimer: number | null = null
let clearTimer: number | null = null

function clearLocalSoon() {
  if (clearTimer !== null) clearTimeout(clearTimer)
  // 兜底：4 秒内没等到回读就放弃本地值，避免显示与设备长期不符
  clearTimer = window.setTimeout(() => {
    localBr.value = null
    localCt.value = null
    clearTimer = null
  }, 4000)
}

function commit() {
  if (commitTimer !== null) {
    clearTimeout(commitTimer)
    commitTimer = null
  }
  const b = localBr.value
  const c = localCt.value
  const bp = brProp.value
  const cp = ctProp.value
  // 月光档没有色温可调，绝不往这个属性写值——写了设备就跳回日光
  const changedCt = ctEditable.value && cp !== null && c !== null && c !== devCt.value
  const changedBr = bp !== null && b !== null && b !== devBr.value
  let sent = false

  if (changedCt) {
    emit('set-prop', cp as PropDef, c as number)
    sent = true
  }
  if (changedBr) {
    emit('set-prop', bp as PropDef, b as number)
    sent = true
  }
  if (sent) clearLocalSoon()
}

function scheduleCommit(delay = 220) {
  if (commitTimer !== null) clearTimeout(commitTimer)
  commitTimer = window.setTimeout(commit, delay)
}

function onDown(e: PointerEvent) {
  if (!online.value || !brProp.value) return
  dragging.value = true
  applyPoint(e.clientX, e.clientY)
  e.preventDefault()
}

function onMove(e: PointerEvent) {
  if (!dragging.value) return
  applyPoint(e.clientX, e.clientY)
  e.preventDefault()
}

function onUp(e: PointerEvent) {
  if (!dragging.value) return
  dragging.value = false
  commit()
  e.preventDefault()
}

// 移动/松开挂在 window 上：合成事件与真实鼠标都能跟手，且移出平面也能正常收尾
onMounted(() => {
  window.addEventListener('pointermove', onMove)
  window.addEventListener('pointerup', onUp)
  window.addEventListener('pointercancel', onUp)
})

/** 键盘：方向键微调，Shift 加速 */
function onKey(e: KeyboardEvent) {
  if (!online.value || !brProp.value) return
  const fast = e.shiftKey ? 8 : 1
  let hit = true
  if (e.key === 'ArrowUp') localBr.value = snapBr(brNow.value + brStep.value * 2 * fast)
  else if (e.key === 'ArrowDown') localBr.value = snapBr(brNow.value - brStep.value * 2 * fast)
  else if (e.key === 'ArrowLeft' && ctEditable.value) localCt.value = snapCt(ctNow.value - 100 * fast)
  else if (e.key === 'ArrowRight' && ctEditable.value) localCt.value = snapCt(ctNow.value + 100 * fast)
  else hit = false
  if (!hit) return
  e.preventDefault()
  scheduleCommit(420)
}

// 关掉抽屉时把定时器和监听清干净
onBeforeUnmount(() => {
  if (commitTimer !== null) clearTimeout(commitTimer)
  if (clearTimer !== null) clearTimeout(clearTimer)
  window.removeEventListener('pointermove', onMove)
  window.removeEventListener('pointerup', onUp)
  window.removeEventListener('pointercancel', onUp)
})

/**
 * 设备回读到目标值后就撤掉本地值，交给设备值渲染。
 * 这样不会因为「本地值一直盖着」而在别处改了灯之后显示不变。
 */
watch([devBr, devCt], ([b, c]) => {
  if (localBr.value !== null && b === localBr.value) localBr.value = null
  if (localCt.value !== null && c === localCt.value) localCt.value = null
})

// ---------------------------------------------------------------- 模式
interface ModeMeta {
  re: RegExp
  icon: string
}

const MODE_META: ModeMeta[] = [
  { re: /白天|日光|day/i, icon: 'sun' },
  { re: /夜间|月光|助眠|night|sleep/i, icon: 'moon' },
  { re: /阅读|看书|reading/i, icon: 'book' },
  { re: /电视|影院|tv/i, icon: 'tv' },
  { re: /电脑|办公|computer/i, icon: 'monitor' },
  { re: /温馨|暖|warmth/i, icon: 'warm' },
  { re: /会客|待客|hospitality/i, icon: 'cup' },
  { re: /娱乐|party|entertainment/i, icon: 'music' },
  { re: /唤醒|起床|wake/i, icon: 'sunrise' },
  { re: /黄昏|日落|dusk/i, icon: 'sunset' },
]

function metaOf(label: string): ModeMeta {
  return MODE_META.find((m) => m.re.test(label)) || { re: /./, icon: 'palette' }
}

const modes = computed(() => {
  const p = modeProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => {
    const label = valueLabel(it)
    return { value: it.value, label, icon: metaOf(label).icon }
  })
})

/** 模式多了就用 4 列紧凑网格，两个模式时铺成两张大卡 */
const modeCols = computed(() => (modes.value.length <= 2 ? modes.value.length : 4))
const modeCompact = computed(() => modes.value.length > 6)

const modeValue = computed(() => raw(modeProp.value))
const modeLabel = computed(
  () => modes.value.find((m) => String(m.value) === String(modeValue.value))?.label || ''
)

/**
 * 这盏灯的两种模式语义完全不同：
 *   日光 —— 亮度 + 色温都能调；
 *   月光 —— 只有亮度（设备把色温锁死），一旦写色温就会被踢回日光。
 * 所以色温轴可不可动，取决于当前模式，不是取决于规格里有没有这个属性。
 */
const nightMode = computed(() => /月光|夜间|助眠|night/i.test(modeLabel.value))
const ctEditable = computed(() => !!ctProp.value && !nightMode.value)

/** 模式卡副标题：把「这个模式能调什么」直接写在脸上 */
function modeSub(label: string): string {
  if (/月光|夜间|助眠|night/i.test(label)) return '只调亮度'
  if (/日光|白天|day/i.test(label)) return '亮度 + 色温'
  return '预设场景'
}

/** 第一个模式值视为常规照明（日光），其余的是预设场景 */

/** 徽章图标：关机时用电源符号，不显示"夜间"这种会误导的图案 */
const badgeIcon = computed(() => {
  if (!isOn.value) return 'power'
  return modes.value.find((m) => String(m.value) === String(modeValue.value))?.icon || 'sun'
})

function pickMode(value: unknown) {
  const p = modeProp.value
  if (!p || !online.value) return
  if (String(value) === String(modeValue.value)) return
  // 模式切换后设备会自己改亮度和色温，本地临时值先作废，免得盖住新状态
  localBr.value = null
  localCt.value = null
  emit('set-prop', p, value)
  window.setTimeout(() => emit('refresh'), 1100)
}

// ---------------------------------------------------------------- 主开关
function togglePower() {
  const p = powerProp.value
  if (!p || !online.value) return
  emit('set-prop', p, !isOn.value)
  window.setTimeout(() => emit('refresh'), 1100)
}

// ---------------------------------------------------------------- 收纳其余参数
/** 面板已消费的属性，剩下的收进「更多设置」，功能一个都不丢 */
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  for (const d of [powerProp.value, brProp.value, ctProp.value, modeProp.value]) {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  return keys
})

const showRaw = ref(false)

/** 遥控器配对、逐级调光这类工程模块，默认折叠 */
const DEBUG_SERVICES = new Set(['remote', 'scene'])

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
  <div v-if="spec" class="lp">
    <!-- 二维调光台：横轴色温、纵轴亮度，可直接拖动 -->
    <section v-if="brProp" class="stage" :class="{ off: !isOn, dim: !online }" :style="stageStyle">
      <header class="stage-head">
        <div class="badge">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(badgeIcon)" />
          <span>{{ isOn ? modeLabel || '照明中' : '已关闭' }}</span>
        </div>
        <button
          class="power"
          :class="{ on: isOn }"
          :disabled="!online || !powerProp || busyProp === powerProp?.name"
          :title="isOn ? '关闭' : '开启'"
          @click="togglePower"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" v-html="icon('power')" />
        </button>
      </header>

      <!-- 交互平面：wrap 负责把光标托出裁剪层，贴边时圆点也完整 -->
      <div class="pad-wrap">
        <div
          ref="pad"
          class="pad"
          :class="{ grabbing: dragging, night: nightMode }"
          :style="padStyle"
          role="slider"
          tabindex="0"
          :aria-valuenow="brNow"
          :aria-valuetext="nightMode ? `亮度 ${brNow}%` : `亮度 ${brNow}%，色温 ${ctNow}K`"
          :aria-label="nightMode ? '月光模式：拖动调节亮度' : '拖动调节亮度与色温'"
          @pointerdown="onDown"
          @pointermove="onMove"
          @pointerup="onUp"
          @pointercancel="onUp"
          @keydown="onKey"
        >
          <div class="grid" />
          <div class="cross cross-h" :style="{ top: `${yPct}%` }" />
          <div class="cross cross-v" :class="{ locked: nightMode }" :style="{ left: `${cursorX}%` }" />

          <div class="readout">
            <template v-if="nightMode">
              <span class="ro-mode">月光</span>
              <span class="ro-dot" />
              <span class="ro-br">{{ brNow }}<em>%</em></span>
            </template>
            <template v-else>
              <span class="ro-ct">{{ ctNow }}<em>K</em></span>
              <span class="ro-dot" />
              <span class="ro-br">{{ brNow }}<em>%</em></span>
            </template>
          </div>

          <div class="y-axis">
            <span v-for="(t, i) in brTicks" :key="i" :style="{ top: `${i * 25}%` }">{{ t }}</span>
          </div>

          <!-- 月光档色温被设备锁住，把这个限制直接标在轴上 -->
          <div v-if="nightMode" class="lock-tip">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="10.4" width="14" height="9.4" rx="2.2" /><path d="M8.4 10.4V7.8a3.6 3.6 0 0 1 7.2 0v2.6" /></svg>
            <span>月光只调亮度</span>
          </div>
        </div>

        <span
          class="cursor"
          :class="{ grabbing: dragging }"
          :style="{ left: `${cursorX}%`, top: `${yPct}%` }"
        >
          <i />
        </span>
      </div>

      <div class="x-axis" :class="{ locked: nightMode }">
        <span v-if="nightMode" class="x-locked">月光模式色温固定</span>
        <span v-for="(t, i) in nightMode ? [] : ctTicks" :key="i">{{ t }}<em>K</em></span>
      </div>

      <p class="tip">
        {{ nightMode ? '月光模式只调亮度，想要色温就切回日光' : '拖动圆点：左右调色温，上下调亮度' }}
      </p>
    </section>

    <!-- 模式：带图标单选 -->
    <section v-if="modes.length" class="blk">
      <h3>模式</h3>
      <div class="mode-grid" :class="{ compact: modeCompact }" :style="{ '--cols': String(modeCols) }">
        <button
          v-for="m in modes"
          :key="String(m.value)"
          class="mode-item"
          :class="{ active: String(m.value) === String(modeValue) }"
          :disabled="!online"
          @click="pickMode(m.value)"
        >
          <span class="mode-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(m.icon)" />
          </span>
          <span class="mode-name">{{ m.label }}</span>
          <span class="mode-sub">{{ modeSub(m.label) }}</span>
          <i class="mode-check" />
        </button>
      </div>
    </section>

    <!-- 其余参数/动作收进折叠区 -->
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
.lp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.lp > section,
.lp > details {
  animation: fade-up 0.32s var(--ease) both;
}
.lp > section:nth-of-type(2) {
  animation-delay: 0.06s;
}

svg {
  width: 100%;
  height: 100%;
}

/* ------------------------------------------------ 调光台 */
.stage {
  margin-top: 12px;
  padding: 14px 14px 12px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  transition: opacity 0.25s ease;
}

/* 当前色温会渗到强调色上（徽章/焦点环都跟着变） */
.stage.off {
  opacity: 0.96;
}

/* 关了灯就把调光面降饱和：设定值还能看，但一眼知道现在没亮 */
.stage.off .pad,
.stage.off .cursor {
  filter: saturate(0.42) brightness(0.8);
}

.stage.off .readout {
  opacity: 0.82;
}

.stage.dim {
  opacity: 0.5;
}

.stage-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 11px 0 9px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
  color: rgb(var(--cr));
  background: rgba(var(--cr), 0.12);
  border: 1px solid rgba(var(--cr), 0.28);
  transition: color 0.3s ease, background-color 0.3s ease, border-color 0.3s ease;
  /* 6500K 附近的色温色接近纯白，浅色主题下直接看不见，压暗一档 */
  filter: brightness(0.62) saturate(1.5);
}

[data-theme='dark'] .badge {
  filter: none;
}

.badge svg {
  width: 14px;
  height: 14px;
  flex: none;
}

.power {
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

.power svg {
  width: 16px;
  height: 16px;
}

.power:hover:not(:disabled) {
  color: var(--text);
  border-color: var(--border-strong);
  transform: translateY(-1px);
}

.power.on {
  color: #fff;
  background: var(--grad-accent);
  border-color: transparent;
  box-shadow: var(--glow-accent);
}

.power:disabled {
  opacity: 0.5;
}

/* 二维平面 */
.pad-wrap {
  position: relative;
  margin-top: 12px;
}

.pad {
  position: relative;
  height: 206px;
  border-radius: 14px;
  overflow: hidden;
  cursor: crosshair;
  touch-action: none;
  outline: none;
  background:
    linear-gradient(
      180deg,
      rgba(255, 255, 255, 0.04) 0%,
      rgba(0, 0, 0, 0.14) 48%,
      rgba(0, 0, 0, 0.58) 100%
    ),
    linear-gradient(
      90deg,
      var(--c0) 0%,
      var(--c1) 25%,
      var(--c2) 50%,
      var(--c3) 75%,
      var(--c4) 100%
    );
  box-shadow: inset 0 0 0 1px rgba(22, 35, 58, 0.1), var(--shadow-sm);
  transition: box-shadow 0.25s ease, filter 0.3s ease;
}

/* 暗色底上换成浅色描边才看得出边界 */
[data-theme='dark'] .pad {
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.22), var(--shadow-sm);
}

.pad:focus-visible {
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.4), 0 0 0 3px rgba(var(--cr), 0.35);
}

.pad.grabbing {
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.45), 0 10px 26px -14px rgba(var(--cr), 1);
}

/* 25% 网格 */
.grid {
  position: absolute;
  inset: 0;
  background-image:
    repeating-linear-gradient(90deg, rgba(255, 255, 255, 0.16) 0 1px, transparent 1px 25%),
    repeating-linear-gradient(0deg, rgba(255, 255, 255, 0.16) 0 1px, transparent 1px 25%);
  pointer-events: none;
}

/* 月光档色温被锁，只剩纵向一维：网格也只留横线 */
.pad.night {
  cursor: ns-resize;
  background:
    linear-gradient(
      180deg,
      rgba(255, 255, 255, 0.03) 0%,
      rgba(0, 0, 0, 0.18) 46%,
      rgba(0, 0, 0, 0.62) 100%
    ),
    linear-gradient(160deg, #33456f 0%, #1e2c4b 46%, #121a2a 100%);
}

.pad.night .grid {
  background-image: repeating-linear-gradient(
    0deg,
    rgba(255, 255, 255, 0.16) 0 1px,
    transparent 1px 25%
  );
}

/* 十字准线 */
.cross {
  position: absolute;
  pointer-events: none;
  opacity: 0.5;
  transition: opacity 0.2s ease;
}

.cross-h {
  left: 0;
  right: 0;
  height: 0;
  border-top: 1px dashed rgba(255, 255, 255, 0.62);
}

.cross-v {
  top: 0;
  bottom: 0;
  width: 0;
  border-left: 1px dashed rgba(255, 255, 255, 0.62);
}

.pad.grabbing .cross {
  opacity: 0.85;
}

/* 光标 */
.cursor {
  position: absolute;
  width: 26px;
  height: 26px;
  margin: -13px 0 0 -13px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  pointer-events: none;
  transition: left 0.14s var(--ease), top 0.14s var(--ease);
}

.cursor.grabbing {
  transition: none;
}

.cursor i {
  display: block;
  width: 100%;
  height: 100%;
  border-radius: 50%;
  background: rgb(var(--cr));
  border: 2.5px solid #fff;
  box-shadow: 0 0 0 1px rgba(0, 0, 0, 0.16), 0 4px 14px rgba(0, 0, 0, 0.35),
    0 0 22px 2px rgba(var(--cr), 0.85);
  transition: transform 0.16s var(--ease), background-color 0.2s ease;
}

.cursor.grabbing i {
  transform: scale(1.16);
}

/* 实时读数 */
.readout {
  position: absolute;
  top: 10px;
  left: 10px;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  height: 27px;
  padding: 0 11px;
  border-radius: 999px;
  background: rgba(10, 17, 29, 0.42);
  border: 1px solid rgba(255, 255, 255, 0.24);
  backdrop-filter: blur(6px);
  color: #fff;
  font-size: 12.5px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  pointer-events: none;
  white-space: nowrap;
}

.readout em {
  font-style: normal;
  margin-left: 1px;
  font-size: 10px;
  font-weight: 500;
  opacity: 0.72;
}

.ro-dot {
  width: 3px;
  height: 3px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.55);
}

/* 纵轴（亮度）刻度：与 25% 网格严格对齐，半透明不抢视线 */
.y-axis {
  position: absolute;
  top: 12px;
  bottom: 12px;
  right: 9px;
  left: 0;
  pointer-events: none;
  font-size: 9.5px;
  font-weight: 600;
  color: rgba(255, 255, 255, 0.68);
  text-shadow: 0 1px 3px rgba(0, 0, 0, 0.5);
  font-variant-numeric: tabular-nums;
}

.y-axis span {
  position: absolute;
  right: 0;
  transform: translateY(-50%);
  line-height: 1;
}

/* 月光档读数里用文字代替色温 */
.ro-mode {
  font-size: 11.5px;
  letter-spacing: 0.4px;
}

/* 月光：色温被设备锁住，横轴连准线一起失效 */
.lock-tip {
  position: absolute;
  left: 50%;
  bottom: 9px;
  transform: translateX(-50%);
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 10px;
  border-radius: 999px;
  background: rgba(10, 17, 29, 0.52);
  border: 1px solid rgba(255, 255, 255, 0.26);
  backdrop-filter: blur(6px);
  color: #fff;
  font-size: 10px;
  line-height: 1.35;
  white-space: nowrap;
  pointer-events: none;
}

.lock-tip svg {
  width: 11px;
  height: 11px;
  flex: none;
  opacity: 0.85;
}

.cross-v.locked {
  border-left-style: solid;
  opacity: 0.26;
}

/* 横轴（色温）刻度 */
.x-axis {
  display: flex;
  justify-content: space-between;
  padding: 6px 2px 0;
  font-size: 10px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.x-axis em {
  font-style: normal;
  font-size: 9px;
  margin-left: 1px;
  opacity: 0.75;
}

/* 月光档横轴失效：整行换成一句说明，不再摆一堆点不动的刻度 */
.x-axis.locked {
  justify-content: center;
}

.x-locked {
  font-size: 10px;
  letter-spacing: 0.3px;
  color: var(--text-3);
  opacity: 0.8;
}

.tip {
  margin: 7px 0 0;
  font-size: 10.5px;
  line-height: 1.5;
  color: var(--text-3);
  text-align: center;
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
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

/* 副标题直接说明这个模式能调什么 */
.mode-sub {
  font-size: 9.5px;
  line-height: 1;
  color: var(--text-3);
  opacity: 0.85;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

.mode-item.active .mode-sub {
  color: var(--accent);
  opacity: 1;
}

.mode-check {
  position: absolute;
  top: 6px;
  right: 6px;
  width: 13px;
  height: 13px;
  border-radius: 50%;
  background: var(--grad-accent);
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
  box-shadow: 0 8px 22px -14px rgba(23, 178, 106, 0.95);
}

.mode-item.active .mode-ico {
  color: var(--accent);
  transform: scale(1.06);
}

.mode-item.active .mode-check {
  opacity: 1;
  transform: scale(1);
}

.mode-item:disabled {
  opacity: 0.5;
}

/* 模式多时收紧一档 */
.mode-grid.compact .mode-item {
  padding: 9px 3px 7px;
  gap: 4px;
}

.mode-grid.compact .mode-ico {
  width: 19px;
  height: 19px;
}

.mode-grid.compact .mode-name {
  font-size: 10.5px;
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
