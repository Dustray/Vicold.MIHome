<script setup lang="ts">
/**
 * 电动窗帘专属面板。
 *
 * 主视觉是一个「窗户」示意图：两片帘布按当前位置百分比左右合拢/分开，
 * 下面挂一条目标位置滑杆。操作区是「打开 / 暂停 / 关闭」三个大按钮。
 *
 * ⚠️ **motor-control 的枚举值两个型号完全不同**，必须按中文标签匹配：
 *  - bjkcz.curtain.kcz4：1=打开 2=关闭 0=暂停 3=开/停/关 4=校准
 *  - xiaomi.curtain.acn012：0=打开 1=关闭 2=暂停 3=开/停/关
 */
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { curtainProps } from '../lib/spec'
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
  open: '<path d="M4 4h16v4H4z"/><path d="M12 8v12"/><path d="M8 13l4-4 4 4"/>',
  stop: '<rect x="7" y="7" width="10" height="10" rx="1.6"/>',
  close: '<path d="M4 4h16v4H4z"/><path d="M12 8v12"/><path d="M8 17l4 4 4-4"/>',
  speed: '<path d="M4 17a8 8 0 1 1 16 0"/><path d="M12 17l4-5"/><circle cx="12" cy="17" r="1.3"/>',
  reverse: '<path d="M4 9h13l-3.4-3.4M20 15H7l3.4 3.4"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2.6v2.2M12 19.2v2.2M2.6 12h2.2M19.2 12h2.2"/>',
  moon: '<path d="M20 14.4A8.6 8.6 0 0 1 9.6 4a8.6 8.6 0 1 0 10.4 10.4z"/>',
  curtain: '<path d="M3.4 4h17.2"/><path d="M6 4v16h4V4M14 4v16h4V4"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.curtain
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const cp = computed(() => curtainProps(props.spec))

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

/** 当前位置：0 = 全闭，100 = 全开 */
const currentPos = computed(() => numOf(cp.value.current))
/** 目标位置 */
const targetPos = computed(() => numOf(cp.value.target))

/** 当前开合度（没有当前位置时用目标位置兜底） */
const openPct = computed(() => {
  const v = currentPos.value ?? targetPos.value
  if (v === undefined) return 0
  return Math.min(100, Math.max(0, v))
})

/** 目标位置（滑杆） */
const targetPct = computed(() => {
  const v = targetPos.value ?? openPct.value
  return Math.min(100, Math.max(0, v))
})

/** 运行状态文案 */
const statusText = computed(() => {
  const p = cp.value.status
  const v = raw(p)
  if (p?.value_list && v !== null && v !== undefined) {
    const hit = p.value_list.find((i) => String(i.value) === String(v))
    if (hit) return valueLabel(hit)
  }
  return ''
})

/** 是否正在运行 */
const running = computed(() => /打开中|关闭中|上升|下降|运行/.test(statusText.value))

/** 故障文案 */
const faultText = computed(() => {
  const p = cp.value.fault
  const v = raw(p)
  if (v === null || v === undefined) return ''
  const code = Number(v)
  if (!code) return ''
  const hit = p?.value_list?.find((i) => String(i.value) === String(code))
  return hit ? valueLabel(hit) : `故障码 ${code}`
})

// ---------------------------------------------------------------- 电机按钮
/**
 * 按中文标签找枚举项的值。
 *
 * ⚠️ 这是本面板最关键的一处：两个型号的 motor-control 数值含义不同，
 * 写死 value 会导致「打开」变成「关闭」。
 */
const openValue = computed(() => {
  const p = cp.value.motor
  if (!p?.value_list) return undefined
  // 优先精确匹配「打开」，避免命中「开/停/关」
  const exact = p.value_list.find((it) => (it.desc_zh || it.description || '').trim() === '打开')
  if (exact) return exact.value
  const hit = p.value_list.find((it) => /打开|^开$/.test(it.desc_zh || it.description || ''))
  return hit ? hit.value : undefined
})

const closeValue = computed(() => {
  const p = cp.value.motor
  if (!p?.value_list) return undefined
  const exact = p.value_list.find((it) => (it.desc_zh || it.description || '').trim() === '关闭')
  if (exact) return exact.value
  const hit = p.value_list.find((it) => /关闭|^关$/.test(it.desc_zh || it.description || ''))
  return hit ? hit.value : undefined
})

const pauseValue = computed(() => {
  const p = cp.value.motor
  if (!p?.value_list) return undefined
  const exact = p.value_list.find((it) => (it.desc_zh || it.description || '').trim() === '暂停')
  if (exact) return exact.value
  const hit = p.value_list.find((it) => /暂停|停止/.test(it.desc_zh || it.description || ''))
  return hit ? hit.value : undefined
})

/** 「开/停/关」这种一位控制：点一次就切换 */
const toggleValue = computed(() => {
  const p = cp.value.motor
  if (!p?.value_list) return undefined
  const hit = p.value_list.find((it) => /开\/停\/关|开关\/停\/关/.test(it.desc_zh || it.description || ''))
  return hit ? hit.value : undefined
})

function fire(def: PropDef | null, value: unknown) {
  if (!def || value === undefined || !online.value) return
  emit('set-prop', def, value)
}

// ---------------------------------------------------------------- 位置滑杆
const posRange = computed(() => {
  const r = cp.value.target?.range
  if (r && r.length >= 2) return { min: Number(r[0]), max: Number(r[1]) }
  return { min: 0, max: 100 }
})

const posFill = computed(() => {
  const { min, max } = posRange.value
  const span = max - min
  if (!span) return '0%'
  return `${Math.min(100, Math.max(0, ((targetPct.value - min) / span) * 100))}%`
})

function onPos(e: Event) {
  const raw2 = Number((e.target as HTMLInputElement).value)
  const def = cp.value.target
  if (!def) return
  emit('set-prop', def, def.type === 'float' ? raw2 : Math.round(raw2))
}

// ---------------------------------------------------------------- 速度 / 反向
/** 运行速度：数值型（0~100）或枚举型（0=8cm/s 1=12cm/s 2=16cm/s） */
const speedProp = computed(() => cp.value.speed)
const speedIsEnum = computed(() => !!speedProp.value?.value_list?.length)

const speedValue = computed(() => raw(speedProp.value))

const speedOptions = computed(() => {
  const p = speedProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: valueLabel(it) }))
})

const reverseProp = computed(() => cp.value.reverse)
const isReversed = computed(() => !!raw(reverseProp.value))

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(cp.value.motor)
  add(cp.value.status)
  add(cp.value.current)
  add(cp.value.target)
  add(cp.value.speed)
  add(cp.value.reverse)
  add(cp.value.fault)
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

/** 窗口透进来的光：开度越大越亮 */
const daylight = computed(() => {
  const pct = openPct.value
  return Math.min(1, pct / 100)
})
</script>

<template>
  <div v-if="spec" class="cur">
    <!-- 故障提示 -->
    <div v-if="faultText" class="fault-row">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ faultText }}</span>
    </div>

    <!-- 主视觉：窗户 + 两片帘布 -->
    <section class="hero" :class="{ running }" :style="{ '--day': String(daylight) }">
      <div class="win">
        <div class="sky">
          <span class="orb" :class="openPct > 50 ? 'sun' : 'moon'" />
        </div>
        <div class="pane" />
        <!-- 左帘：从两侧向中间合拢 -->
        <div class="drape left" :style="{ width: (50 - openPct / 2) + '%' }">
          <i v-for="n in 6" :key="n" class="fold" :style="{ left: n * 16 + '%' }" />
        </div>
        <div class="drape right" :style="{ width: (50 - openPct / 2) + '%' }">
          <i v-for="n in 6" :key="n" class="fold" :style="{ right: n * 16 + '%' }" />
        </div>
        <div class="rail" />
      </div>

      <div class="hero-side">
        <div class="pct-big">
          <b>{{ Math.round(openPct) }}</b><span>%</span>
        </div>
        <div class="pct-cap">{{ openPct > 92 ? '全开' : openPct < 8 ? '全闭' : '开合度' }}</div>
        <div class="status-chip" :class="{ on: running }">
          <i class="dot" />
          {{ statusText || '已停止' }}
        </div>
      </div>
    </section>

    <!-- 电机控制：打开 / 暂停 / 关闭 -->
    <section class="blk">
      <h3>开合</h3>
      <div class="motor-row">
        <button
          class="m-btn open"
          :disabled="!online || openValue === undefined"
          @click="fire(cp.motor, openValue)"
        >
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('open')" />
          </span>
          <span class="m-name">打开</span>
        </button>
        <button
          class="m-btn stop"
          :disabled="!online || pauseValue === undefined"
          @click="fire(cp.motor, pauseValue)"
        >
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('stop')" />
          </span>
          <span class="m-name">暂停</span>
        </button>
        <button
          class="m-btn close"
          :disabled="!online || closeValue === undefined"
          @click="fire(cp.motor, closeValue)"
        >
          <span class="m-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('close')" />
          </span>
          <span class="m-name">关闭</span>
        </button>
      </div>
      <!-- 只有一位控制的机型（开/停/关）额外给个切换键 -->
      <button
        v-if="toggleValue !== undefined"
        class="toggle-btn"
        :disabled="!online"
        @click="fire(cp.motor, toggleValue)"
      >
        一键开 / 停 / 关
      </button>
    </section>

    <!-- 目标位置 -->
    <section v-if="cp.target" class="blk">
      <h3>
        目标位置
        <em class="h3-hint">{{ Math.round(targetPct) }}%</em>
      </h3>
      <div class="pos-row">
        <span class="pos-cap">全闭</span>
        <input
          class="slider"
          type="range"
          :min="posRange.min"
          :max="posRange.max"
          :step="1"
          :value="targetPct"
          :disabled="!online"
          :style="{ '--fill': posFill }"
          @change="onPos"
        />
        <span class="pos-cap">全开</span>
      </div>
    </section>

    <!-- 运行速度 -->
    <section v-if="speedProp" class="blk">
      <h3>运行速度</h3>
      <div v-if="speedIsEnum" class="chip-row">
        <button
          v-for="o in speedOptions"
          :key="String(o.value)"
          class="chip"
          :class="{ active: String(o.value) === String(speedValue) }"
          :disabled="!online"
          @click="fire(speedProp, o.value)"
        >
          {{ o.label }}
        </button>
      </div>
      <div v-else class="pos-row">
        <span class="pos-cap">慢</span>
        <input
          class="slider"
          type="range"
          :min="Number(speedProp.range?.[0] ?? 0)"
          :max="Number(speedProp.range?.[1] ?? 100)"
          :step="Number(speedProp.range?.[2] ?? 1) || 1"
          :value="Number(speedValue ?? 0)"
          :disabled="!online"
          :style="{ '--fill': ((Number(speedValue ?? 0) - Number(speedProp.range?.[0] ?? 0)) / Math.max(1, Number(speedProp.range?.[1] ?? 100) - Number(speedProp.range?.[0] ?? 0))) * 100 + '%' }"
          @change="(e) => emit('set-prop', speedProp!, Number((e.target as HTMLInputElement).value))"
        />
        <span class="pos-cap">快</span>
      </div>
    </section>

    <!-- 电机反向 -->
    <section v-if="reverseProp" class="blk">
      <h3>安装方向</h3>
      <button
        class="rev-btn"
        :class="{ on: isReversed }"
        :disabled="!online"
        @click="emit('set-prop', reverseProp!, !isReversed)"
      >
        <span class="r-ico">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('reverse')" />
        </span>
        <span class="r-body">
          <span class="r-name">电机反向</span>
          <span class="r-state">{{ isReversed ? '已反转' : '正向' }}</span>
        </span>
        <span class="r-sw"><i /></span>
      </button>
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
.cur {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.cur > section,
.cur > details {
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

/* 窗户 */
.win {
  position: relative;
  aspect-ratio: 16 / 10;
  border-radius: var(--radius-md);
  overflow: hidden;
  background: var(--surface-2);
  border: 1px solid var(--border);
  box-shadow: inset 0 2px 10px rgba(0, 0, 0, 0.14);
}

/* 窗外的天光：开度越大越亮 */
.sky {
  position: absolute;
  inset: 0;
  background: linear-gradient(
    180deg,
    rgba(120, 168, 232, calc(0.12 + var(--day) * 0.5)),
    rgba(198, 222, 250, calc(0.06 + var(--day) * 0.34))
  );
  transition: background 0.5s var(--ease);
}

.orb {
  position: absolute;
  top: 13%;
  right: 16%;
  width: 20px;
  height: 20px;
  border-radius: 50%;
  transition: all 0.45s var(--ease);
}

.orb.sun {
  background: radial-gradient(circle, #ffd97a, #f5b53d);
  box-shadow: 0 0 12px rgba(245, 181, 61, 0.5);
  opacity: calc(0.25 + var(--day) * 0.75);
}

.orb.moon {
  background: radial-gradient(circle, #dbe7ff, #9fb6dd);
  box-shadow: 0 0 10px rgba(159, 182, 221, 0.4);
  opacity: 0.75;
}

/* 窗棂 */
.pane {
  position: absolute;
  left: 50%;
  top: 0;
  bottom: 0;
  width: 3px;
  margin-left: -1.5px;
  background: var(--border-strong);
  opacity: 0.55;
}

/* 帘布 */
.drape {
  position: absolute;
  top: 0;
  bottom: 0;
  transition: width 0.55s var(--ease);
  overflow: hidden;
}

.drape.left {
  left: 0;
  background: linear-gradient(90deg, var(--surface-3), rgba(47, 109, 246, 0.12));
  border-right: 1px solid rgba(47, 109, 246, 0.22);
}

.drape.right {
  right: 0;
  background: linear-gradient(270deg, var(--surface-3), rgba(47, 109, 246, 0.12));
  border-left: 1px solid rgba(47, 109, 246, 0.22);
}

/* 褶皱 */
.fold {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 2px;
  background: rgba(0, 0, 0, 0.07);
}

.drape.left .fold {
  background: linear-gradient(90deg, rgba(0, 0, 0, 0.09), transparent);
}

.drape.right .fold {
  background: linear-gradient(270deg, rgba(0, 0, 0, 0.09), transparent);
}

/* 窗帘杆 */
.rail {
  position: absolute;
  left: 0;
  right: 0;
  top: 0;
  height: 4px;
  background: var(--border-strong);
  opacity: 0.7;
}

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
  box-shadow: 0 0 6px rgba(23, 178, 106, 0.3);
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

.m-btn.open:hover:not(:disabled) .m-ico {
  color: var(--accent);
}

.m-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

/* 打开：绿 */
.m-btn.open:hover:not(:disabled) {
  border-color: rgba(23, 178, 106, 0.42);
}

/* 关闭：品牌蓝 */
.m-btn.close:hover:not(:disabled) {
  border-color: rgba(47, 109, 246, 0.42);
}

.toggle-btn {
  width: 100%;
  margin-top: 8px;
  height: 34px;
  border-radius: var(--radius-md);
  font-size: 12.5px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: all 0.18s var(--ease);
}

.toggle-btn:hover:not(:disabled) {
  color: var(--brand);
  border-color: var(--brand);
  background: var(--brand-soft);
}

.toggle-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
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
  padding: 4px 13px;
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

/* ------------------------------------------------ 电机反向 */
.rev-btn {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 11px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  transition: all 0.18s var(--ease);
}

.r-ico {
  width: 18px;
  height: 18px;
  flex: none;
  color: var(--text-3);
}

.r-body {
  display: flex;
  flex-direction: column;
  gap: 1px;
  flex: 1;
  text-align: left;
}

.r-name {
  font-size: 12.5px;
}

.r-state {
  font-size: 10.5px;
  color: var(--text-3);
}

.r-sw {
  position: relative;
  width: 30px;
  height: 17px;
  flex: none;
  border-radius: 999px;
  background: var(--border-strong);
  transition: background-color 0.2s ease;
}

.r-sw i {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 13px;
  height: 13px;
  border-radius: 50%;
  background: #fff;
  transition: transform 0.2s var(--ease);
}

.rev-btn:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface);
}

.rev-btn.on {
  color: var(--accent);
  border-color: rgba(23, 178, 106, 0.42);
  background: var(--accent-soft);
}

.rev-btn.on .r-ico {
  color: var(--accent);
}

.rev-btn.on .r-sw {
  background: var(--accent);
}

.rev-btn.on .r-sw i {
  transform: translateX(13px);
}

.rev-btn:disabled {
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
