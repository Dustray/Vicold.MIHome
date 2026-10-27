<script setup lang="ts">
/**
 * 净水器专属面板。
 *
 * 主视觉是「TDS 进水 → 直饮水」的净化对比：两个大数字 + 净化率。
 * 下面是两套滤芯的寿命条（按 siid 区分：siid3 = 第一级、siid5 = 第二级）、
 * 杯量选择、无陈水模式。
 *
 * ⚠️ 规格里的 `temperature`（siid2/piid3）是**水路温度**，不是室温，
 *    标签要写清楚，别让用户以为是在显示房间温度。
 */
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { waterPurifierProps } from '../lib/spec'
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
  in: '<path d="M12 3.4s5 5.4 5 8.8a5 5 0 0 1-10 0c0-3.4 5-8.8 5-8.8z"/>',
  out: '<path d="M12 3.4s5 5.4 5 8.8a5 5 0 0 1-10 0c0-3.4 5-8.8 5-8.8z"/>',
  filter: '<path d="M4.8 4.8h14.4L13.7 11v7.6l-3.4-1.8V11z"/>',
  cup: '<path d="M4.6 8.4h11v5.8a4.2 4.2 0 0 1-4.2 4.2H8.8a4.2 4.2 0 0 1-4.2-4.2z"/><path d="M15.6 9.6h1.8a2.6 2.6 0 0 1 0 5.2h-1.8"/><path d="M4.6 21h11"/>',
  leaf: '<path d="M20 4.2c-8.2 0-13.1 3.4-13.1 9.1a4.9 4.9 0 0 0 4.9 4.9c5.7 0 8.2-5 8.2-14z"/><path d="M8.4 15.6C11 12.1 14 10.1 18 9.1"/>',
  thermo: '<path d="M14 14.7V5.2a2 2 0 1 0-4 0v9.5a4 4 0 1 0 4 0z"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
  pump: '<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.drop
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const wp = computed(() => waterPurifierProps(props.spec))

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

/** 进水 TDS */
const tdsIn = computed(() => numOf(wp.value.tdsIn))
/** 直饮水 TDS */
const tdsOut = computed(() => numOf(wp.value.tdsOut))

/** 净化率：1 - 出/进 */
const purifyRate = computed(() => {
  const a = tdsIn.value
  const b = tdsOut.value
  if (a === undefined || b === undefined || !a) return undefined
  const r = (1 - b / a) * 100
  if (!Number.isFinite(r)) return undefined
  return Math.min(100, Math.max(0, r))
})

/** 水质评级 */
const quality = computed(() => {
  const b = tdsOut.value
  if (b === undefined) return { text: '—', tone: '148, 163, 184' }
  if (b <= 50) return { text: '优', tone: '23, 178, 106' }
  if (b <= 200) return { text: '良', tone: '124, 184, 20' }
  if (b <= 300) return { text: '一般', tone: '224, 160, 0' }
  return { text: '较差', tone: '229, 72, 77' }
})

const heroStyle = computed(() => ({ '--tone': quality.value.tone }))

/** 工作状态 */
const statusText = computed(() => {
  const p = wp.value.status
  const v = raw(p)
  if (p?.value_list && v !== null && v !== undefined) {
    const hit = p.value_list.find((i) => String(i.value) === String(v))
    if (hit) return valueLabel(hit)
  }
  return ''
})

/** 故障 */
const faultText = computed(() => {
  const p = wp.value.fault
  const v = raw(p)
  if (v === null || v === undefined) return ''
  const code = Number(v)
  if (!code) return ''
  const hit = p?.value_list?.find((i) => String(i.value) === String(code))
  return hit ? valueLabel(hit) : `故障码 ${code}`
})

/** 水路温度（不是室温） */
const waterTemp = computed(() => numOf(wp.value.temperature))

// ---------------------------------------------------------------- 滤芯
interface FilterCard {
  siid: number
  name: string
  pct?: number
  days?: number
  tone: 'ok' | 'warn' | 'bad'
  reset?: { siid: number; aiid: number; name: string }
}

/** 「滤芯剩余寿命」→「滤芯」；两套滤芯要按 siid 区分先后 */
function filterStem(p: PropDef): string {
  const zh = p.desc_zh || p.desc_en || ''
  const m = /^(.*?)(剩余寿命|寿命|剩余电量|电量)$/.exec(zh)
  return ((m && m[1]) || zh || '滤芯').trim()
}

/** 按 siid 升序排，siid3 在前（第一级） */
const filters = computed<FilterCard[]>(() => {
  const s = props.spec
  if (!s) return []
  const lives = s.properties
    .filter((p) => p.base_name === 'filter-life-level')
    .slice()
    .sort((a, b) => a.siid - b.siid)

  return lives.map((p, idx) => {
    const name = filterStem(p)
    const v = numOf(p)
    const pct = v === undefined ? undefined : Math.min(100, Math.max(0, v))
    const tone: FilterCard['tone'] = pct === undefined ? 'ok' : pct <= 10 ? 'bad' : pct <= 30 ? 'warn' : 'ok'

    // 剩余时间：官方单位是小时，换算成天；超范围就不猜
    const sibs = s.properties.filter((x) => x.siid === p.siid && x.base_name === 'filter-left-time')
    const leftRaw = sibs.map((x) => numOf(x)).find((n) => n !== undefined && n > 0)
    let days: number | undefined
    if (leftRaw !== undefined) {
      const d = Math.round(leftRaw > 1000 ? leftRaw / 24 : leftRaw)
      if (d >= 1 && d <= 1000) days = d
    }

    const act = s.actions.find((a) => {
      if (a.siid !== p.siid) return false
      if (!a.name.includes('reset')) return false
      return /filter/i.test(a.name)
    })

    return {
      siid: p.siid,
      // ⚠️ 两级滤芯的官方 desc_zh 一模一样（都是「滤芯剩余寿命」），
      // 靠 siid 顺序区分先后级，否则界面上会出现两个同名「滤芯」。
      name: lives.length > 1 ? `${['一级', '二级', '三级'][idx] ?? `第${idx + 1}级`}${name}` : name,
      pct,
      days,
      tone,
      reset: act ? { siid: act.siid, aiid: act.aiid, name: act.name } : undefined,
    }
  })
})

function filterTip(f: FilterCard): string {
  const base =
    f.pct === undefined
      ? '暂无数据'
      : f.pct <= 10
        ? '需要尽快更换'
        : f.pct <= 30
          ? '寿命偏低，建议备货'
          : '状态良好'
  return f.days ? `${base} · 约 ${f.days} 天` : base
}

function resetFilter(f: FilterCard) {
  if (!f.reset || !online.value) return
  emit('run-action', f.reset.siid, f.reset.aiid, f.reset.name)
}

// ---------------------------------------------------------------- 杯量
const cupProp = computed(() => wp.value.cup)
const cupOptions = computed(() => {
  const p = cupProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: cupLabel(it) }))
})

/** 「杯量1」→「1 杯」这种更自然的说法 */
function cupLabel(it: { value: number | string; description: string; desc_zh?: string }): string {
  const raw2 = valueLabel(it)
  const m = /杯量\s*(\d+)/.exec(raw2)
  if (m) return `${m[1]} 杯`
  if (/上次/.test(raw2)) return '上次杯量'
  return raw2
}

const cupValue = computed(() => raw(cupProp.value))

// ---------------------------------------------------------------- 无陈水模式
const oldWaterProp = computed(() => wp.value.oldWater)
const oldWaterOptions = computed(() => {
  const p = oldWaterProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: oldWaterLabel(it) }))
})

function oldWaterLabel(it: { value: number | string; description: string; desc_zh?: string }): string {
  const raw2 = valueLabel(it)
  if (/Default/i.test(raw2)) return '默认'
  if (/Quality/i.test(raw2)) return '水质优先'
  if (/Save/i.test(raw2)) return '节水'
  return raw2
}

const oldWaterValue = computed(() => raw(oldWaterProp.value))

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(wp.value.status)
  add(wp.value.tdsIn)
  add(wp.value.tdsOut)
  add(wp.value.cup)
  add(wp.value.oldWater)
  add(wp.value.temperature)
  add(wp.value.fault)
  for (const f of filters.value) {
    const p = props.spec?.properties.find(
      (x) => x.siid === f.siid && x.base_name === 'filter-life-level'
    )
    if (p) add(p)
  }
  return keys
})

const consumedActs = computed(() => {
  const set = new Set<string>()
  for (const f of filters.value) if (f.reset) set.add(`${f.reset.siid}:${f.reset.aiid}`)
  return set
})

const restGroups = computed<SpecGroup[]>(() =>
  props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      actions: g.actions.filter((a) => !consumedActs.value.has(`${a.siid}:${a.aiid}`)),
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
)
</script>

<template>
  <div v-if="spec" class="wp">
    <!-- 故障 -->
    <div v-if="faultText" class="alert-row danger">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ faultText }}</span>
    </div>

    <!-- 主视觉：TDS 进 → 出 -->
    <section class="hero" :style="heroStyle">
      <div class="tds">
        <div class="tds-side">
          <span class="t-label">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('in')" />
            进水
          </span>
          <b class="t-val">{{ tdsIn === undefined ? '—' : tdsIn }}</b>
          <span class="t-unit">TDS</span>
        </div>

        <div class="flow">
          <div class="flow-line">
            <i class="drop" />
          </div>
          <div class="rate">
            <b>{{ purifyRate === undefined ? '—' : Math.round(purifyRate) + '%' }}</b>
            <span>净化率</span>
          </div>
        </div>

        <div class="tds-side out">
          <span class="t-label">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('out')" />
            直饮水
          </span>
          <b class="t-val big">{{ tdsOut === undefined ? '—' : tdsOut }}</b>
          <span class="t-unit">TDS</span>
        </div>
      </div>

      <div class="hero-foot">
        <span class="q-badge">{{ quality.text }}</span>
        <span v-if="statusText" class="st">{{ statusText }}</span>
        <span v-if="waterTemp !== undefined" class="wt">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('thermo')" />
          出水温度 {{ waterTemp.toFixed(1) }}°C
        </span>
      </div>
    </section>

    <!-- 滤芯 -->
    <section v-if="filters.length" class="blk">
      <h3>滤芯</h3>
      <div class="filter-list">
        <div v-for="f in filters" :key="f.siid" class="filter-card" :class="f.tone">
          <div class="f-head">
            <span class="f-name">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('filter')" />
              {{ f.name }}
            </span>
            <b class="f-pct">{{ f.pct === undefined ? '—' : f.pct + '%' }}</b>
          </div>
          <div class="f-bar"><i :style="{ width: (f.pct ?? 0) + '%' }" /></div>
          <div class="f-foot">
            <span class="f-tip">{{ filterTip(f) }}</span>
            <button v-if="f.reset" class="f-reset" :disabled="!online" @click="resetFilter(f)">重置</button>
          </div>
        </div>
      </div>
    </section>

    <!-- 杯量 -->
    <section v-if="cupOptions.length" class="blk">
      <h3>出水量</h3>
      <div class="chip-row">
        <button
          v-for="o in cupOptions"
          :key="String(o.value)"
          class="chip"
          :class="{ active: String(o.value) === String(cupValue) }"
          :disabled="!online"
          @click="emit('set-prop', cupProp!, o.value)"
        >
          {{ o.label }}
        </button>
      </div>
    </section>

    <!-- 无陈水模式 -->
    <section v-if="oldWaterOptions.length" class="blk">
      <h3>无陈水</h3>
      <div class="chip-row">
        <button
          v-for="o in oldWaterOptions"
          :key="String(o.value)"
          class="chip"
          :class="{ active: String(o.value) === String(oldWaterValue) }"
          :disabled="!online"
          @click="emit('set-prop', oldWaterProp!, o.value)"
        >
          {{ o.label }}
        </button>
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
.wp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.wp > section,
.wp > details {
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

/* ------------------------------------------------ 主视觉 */
.hero {
  position: relative;
  margin-top: 12px;
  padding: 16px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  overflow: hidden;
}

.hero::before {
  content: '';
  position: absolute;
  top: -70px;
  right: -40px;
  width: 200px;
  height: 200px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--tone), 0.2), transparent 68%);
  pointer-events: none;
  animation: breathe 4.5s ease-in-out infinite;
}

.tds {
  position: relative;
  display: grid;
  grid-template-columns: 1fr auto 1fr;
  align-items: center;
  gap: 12px;
}

.tds-side {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}

.t-label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--text-3);
}

.t-label svg {
  width: 12px;
  height: 12px;
}

.t-val {
  font-size: 26px;
  font-weight: 700;
  line-height: 1.1;
  color: var(--text-2);
  font-variant-numeric: tabular-nums;
}

.t-val.big {
  font-size: 34px;
  color: rgb(var(--tone));
  letter-spacing: -0.5px;
  transition: color 0.4s ease;
}

.t-unit {
  font-size: 9.5px;
  color: var(--text-3);
  letter-spacing: 0.6px;
}

/* 中间的净化流程 */
.flow {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 7px;
  min-width: 74px;
}

.flow-line {
  position: relative;
  width: 100%;
  height: 3px;
  border-radius: 3px;
  background: linear-gradient(90deg, var(--border-strong), rgb(var(--tone)));
  overflow: hidden;
}

.flow-line .drop {
  position: absolute;
  top: -3px;
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: rgb(var(--tone));
  box-shadow: 0 0 8px rgba(var(--tone), 0.8);
  animation: slide 2.4s linear infinite;
}

@keyframes slide {
  0% { left: 0; opacity: 0; }
  12% { opacity: 1; }
  88% { opacity: 1; }
  100% { left: calc(100% - 9px); opacity: 0; }
}

.rate {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.rate b {
  font-size: 15px;
  font-weight: 700;
  color: rgb(var(--tone));
  font-variant-numeric: tabular-nums;
}

.rate span {
  font-size: 10px;
  color: var(--text-3);
}

.hero-foot {
  position: relative;
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 14px;
  padding-top: 12px;
  border-top: 1px dashed var(--border);
  flex-wrap: wrap;
}

.q-badge {
  padding: 3px 12px;
  border-radius: 999px;
  font-size: 12.5px;
  font-weight: 600;
  color: rgb(var(--tone));
  background: rgba(var(--tone), 0.13);
  border: 1px solid rgba(var(--tone), 0.26);
}

.st {
  font-size: 11.5px;
  color: var(--text-3);
}

.wt {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  margin-left: auto;
  font-size: 11px;
  color: var(--text-3);
}

.wt svg {
  width: 12px;
  height: 12px;
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

/* ------------------------------------------------ 滤芯 */
.filter-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.filter-card {
  padding: 11px 12px 9px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.f-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.f-name {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.f-name svg {
  width: 13px;
  height: 13px;
  flex: none;
  color: var(--text-3);
}

.f-pct {
  font-size: 13px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
  color: var(--accent);
  flex: none;
}

.f-bar {
  height: 5px;
  margin: 8px 0 7px;
  border-radius: 5px;
  background: var(--border-strong);
  overflow: hidden;
}

.f-bar i {
  display: block;
  height: 100%;
  border-radius: 5px;
  background: var(--grad-accent);
  transition: width 0.5s var(--ease);
}

.f-foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.f-tip {
  font-size: 11px;
  color: var(--text-3);
}

.f-reset {
  height: 22px;
  padding: 0 9px;
  border-radius: 999px;
  font-size: 11px;
  color: var(--text-2);
  background: var(--surface);
  border: 1px solid var(--border);
  transition: all 0.16s ease;
}

.f-reset:hover:not(:disabled) {
  color: var(--brand);
  border-color: var(--brand);
  background: var(--brand-soft);
}

.filter-card.warn .f-pct {
  color: #e0a000;
}

.filter-card.warn .f-bar i {
  background: linear-gradient(90deg, #f5c451, #e0a000);
}

.filter-card.bad .f-pct {
  color: var(--danger);
}

.filter-card.bad .f-bar i {
  background: linear-gradient(90deg, #f0715f, #e5484d);
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
