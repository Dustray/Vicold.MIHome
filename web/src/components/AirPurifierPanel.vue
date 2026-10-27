<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { propBy, propsBy, powerSiid } from '../lib/spec'
import { actionLabel, propLabel, valueLabel } from '../lib/zh'
import PropControl from './PropControl.vue'
import TrendSection from './TrendSection.vue'
import type { MetricKey } from '../lib/env'

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
/** 内联 SVG（仅 path 片段），静态常量，不涉及用户输入 */
const ICONS: Record<string, string> = {
  power: '<path d="M12 3.2v8.4"/><path d="M7.7 6.4a7 7 0 1 0 8.6 0"/>',
  auto: '<path d="M20.4 12a8.4 8.4 0 1 1-2.6-6.1"/><path d="M20.6 4.6V9h-4.4"/>',
  sleep: '<path d="M20.4 14.9A8.4 8.4 0 1 1 9.1 3.6a8.4 8.4 0 0 0 11.3 11.3z"/>',
  heart:
    '<path d="M12 20.1s-7.1-4.4-7.1-9.2A4 4 0 0 1 12 8.4a4 4 0 0 1 7.1 2.5c0 4.8-7.1 9.2-7.1 9.2z"/>',
  manual:
    '<path d="M4 8h8.2M16.4 8H20M4 16h3.4M11.8 16H20"/><circle cx="14.2" cy="8" r="2.2"/><circle cx="9.6" cy="16" r="2.2"/>',
  ion: '<path d="M11.4 3.4l1.7 4.5 4.5 1.7-4.5 1.7-1.7 4.5-1.7-4.5L5.2 9.6l4.5-1.7z"/><path d="M18.2 15.2l.7 1.9 1.9.7-1.9.7-.7 1.9-.7-1.9-1.9-.7 1.9-.7z"/>',
  uv: '<circle cx="12" cy="12" r="3.9"/><path d="M12 2.6v2.2M12 19.2v2.2M2.6 12h2.2M19.2 12h2.2M5.3 5.3l1.6 1.6M17.1 17.1l1.6 1.6M18.7 5.3l-1.6 1.6M6.9 17.1l-1.6 1.6"/>',
  screen:
    '<rect x="3" y="4.4" width="18" height="12.6" rx="2.4"/><path d="M9 20.4h6M12 17v3.4"/>',
  bell: '<path d="M18 15.4V11a6 6 0 1 0-12 0v4.4l-1.6 2.2h15.2z"/><path d="M10 20.1a2.2 2.2 0 0 0 4 0"/>',
  lock: '<rect x="4.8" y="10.4" width="14.4" height="9.6" rx="2.4"/><path d="M8.3 10.4V8a3.7 3.7 0 0 1 7.4 0v2.4"/><circle cx="12" cy="15.2" r="1.2"/>',
  fan: '<path d="M3 8.6h11.2a3 3 0 1 0-3-3"/><path d="M3 15.6h8.6a2.8 2.8 0 1 1-2.8 2.8"/>',
  thermo: '<path d="M14 14.7V5.2a2 2 0 1 0-4 0v9.5a4 4 0 1 0 4 0z"/>',
  drop: '<path d="M12 3.9s5.2 5.6 5.2 9.1A5.2 5.2 0 0 1 12 18.2a5.2 5.2 0 0 1-5.2-5.2C6.8 9.5 12 3.9 12 3.9z"/>',
  leaf: '<path d="M20 4.2c-8.2 0-13.1 3.4-13.1 9.1a4.9 4.9 0 0 0 4.9 4.9c5.7 0 8.2-5 8.2-14z"/><path d="M8.4 15.6C11 12.1 14 10.1 18 9.1"/>',
  filter: '<path d="M4.8 4.8h14.4L13.7 11v7.6l-3.4-1.8V11z"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.fan
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const spec = computed(() => props.spec)

/** 主开关：与功率模块同一个 siid 的 on，不能取到显示屏那条 on */
const powerProp = computed<PropDef | null>(() => {
  const siid = powerSiid(props.spec)
  return siid === null ? null : propBy(props.spec, 'on', siid)
})

const isOn = computed(() => {
  const p = powerProp.value
  return !!(p && props.values[p.name])
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

const modeProp = computed(() => propBy(props.spec, 'mode'))
/** 风机档位要限定在空气净化器模块里，别取到「最爱模式配置」那条 */
const fanProp = computed(() =>
  modeProp.value ? propBy(props.spec, 'fan-level', modeProp.value.siid) : null
)
const aqProp = computed(() => propBy(props.spec, 'air-quality'))
const pm25Prop = computed(() => propBy(props.spec, 'pm2.5-density') || propBy(props.spec, 'pm2.5'))
const pm10Prop = computed(() => propBy(props.spec, 'pm10-density'))
const tempProp = computed(() => propBy(props.spec, 'temperature'))
const humProp = computed(() => propBy(props.spec, 'relative-humidity') || propBy(props.spec, 'humidity'))
const hchoProp = computed(() => propBy(props.spec, 'hcho-density'))

/** 这台设备能画出哪些曲线（按实际存在的属性决定） */
const trendMetrics = computed<MetricKey[]>(() => {
  const list: MetricKey[] = []
  if (pm25Prop.value) list.push('pm25')
  if (pm10Prop.value) list.push('pm10')
  if (hchoProp.value) list.push('hcho')
  if (aqProp.value) list.push('aq')
  if (tempProp.value) list.push('t')
  if (humProp.value) list.push('h')
  return list
})
const faultProp = computed(() =>
  modeProp.value ? propBy(props.spec, 'fault', modeProp.value.siid) : propBy(props.spec, 'fault')
)

// ---------------------------------------------------------------- 空气质量
/** 六档配色，索引与 air-quality 枚举值一致（0 优 → 5 严重污染） */
const AQ_LEVELS = [
  { color: '23, 178, 106' },
  { color: '124, 184, 20' },
  { color: '224, 160, 0' },
  { color: '242, 98, 15' },
  { color: '229, 72, 77' },
  { color: '139, 63, 191' },
]

const pm25 = computed(() => numOf(pm25Prop.value))

const level = computed(() => {
  const aq = numOf(aqProp.value)
  if (aq !== undefined && aq >= 0 && aq <= 5) return Math.round(aq)
  const pm = pm25.value
  if (pm === undefined) return 0
  if (pm <= 35) return 0
  if (pm <= 75) return 1
  if (pm <= 115) return 2
  if (pm <= 150) return 3
  if (pm <= 250) return 4
  return 5
})

const aqText = computed(() => {
  const p = aqProp.value
  const v = raw(p)
  if (p?.value_list && v !== null && v !== undefined) {
    const hit = p.value_list.find((i) => String(i.value) === String(v))
    if (hit) return valueLabel(hit)
  }
  return ['空气优', '空气良', '轻度污染', '中度污染', '重度污染', '严重污染'][level.value]
})

/** 环填充按 PM2.5 相对 250 的比例，越脏越满 */
const ringPct = computed(() => {
  const pm = pm25.value
  if (pm === undefined) return 0
  return Math.min(100, Math.max(1, (pm / 250) * 100))
})

const heroStyle = computed(() => ({
  '--aq': AQ_LEVELS[level.value].color,
  '--p': `${ringPct.value}%`,
}))

const pmText = computed(() => (pm25.value === undefined ? '—' : String(Math.round(pm25.value))))

const faultText = computed(() => {
  const p = faultProp.value
  const v = raw(p)
  if (v === null || v === undefined) return ''
  const code = Number(v)
  if (!code) return ''
  const hit = p?.value_list?.find((i) => String(i.value) === String(code))
  return hit ? valueLabel(hit) : `故障码 ${code}`
})

/** 主视觉下方的四项环境读数 */
const envMetrics = computed(() => {
  const list: { icon: string; label: string; value: string; unit: string }[] = []
  const t = numOf(tempProp.value)
  if (t !== undefined) list.push({ icon: 'thermo', label: '温度', value: t.toFixed(1), unit: '°C' })
  const h = numOf(humProp.value)
  if (h !== undefined) list.push({ icon: 'drop', label: '湿度', value: String(Math.round(h)), unit: '%' })
  const ho = numOf(hchoProp.value)
  if (ho !== undefined) list.push({ icon: 'leaf', label: '甲醛', value: ho.toFixed(3), unit: 'mg/m³' })
  const p10 = numOf(pm10Prop.value)
  if (p10 !== undefined) list.push({ icon: 'fan', label: 'PM10', value: String(Math.round(p10)), unit: 'μg/m³' })
  return list
})

// ---------------------------------------------------------------- 模式
function modeIcon(label: string): string {
  if (/自动|auto/i.test(label)) return 'auto'
  if (/睡眠|静音|sleep/i.test(label)) return 'sleep'
  if (/最爱|收藏|love|favorite/i.test(label)) return 'heart'
  if (/手动|挡位|档位|manual/i.test(label)) return 'manual'
  return 'fan'
}

const modes = computed(() => {
  const p = modeProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it) => {
    const label = valueLabel(it)
    return { value: it.value, label, icon: modeIcon(label) }
  })
})

const modeValue = computed(() => raw(modeProp.value))

function pickMode(value: unknown) {
  const p = modeProp.value
  if (!p || !online.value) return
  if (String(value) === String(modeValue.value)) return
  emit('set-prop', p, value)
}

// ---------------------------------------------------------------- 风速档位
const fans = computed(() => {
  const p = fanProp.value
  if (!p?.value_list) return []
  return p.value_list.map((it, i) => ({
    value: it.value,
    label: valueLabel(it),
    bars: i + 1,
    total: p.value_list!.length,
  }))
})

const fanValue = computed(() => raw(fanProp.value))

function pickFan(value: unknown) {
  const p = fanProp.value
  if (!p || !online.value) return
  if (String(value) === String(fanValue.value)) return
  emit('set-prop', p, value)
}

/** 手动档模式的叫法（va3 叫「手动」、ua1a 叫「挡位」），用来提示何时能调档 */
const manualModeLabel = computed(
  () => modes.value.find((m) => /手动|挡位|档位/.test(m.label))?.label || ''
)

/** 当前模式是否允许调档：手动/挡位模式才有意义 */
const fanHint = computed(() => {
  const label = modes.value.find((m) => String(m.value) === String(modeValue.value))?.label || ''
  if (/手动|挡位|档位/.test(label)) return ''
  return manualModeLabel.value ? `切到${manualModeLabel.value}可调` : ''
})

// ---------------------------------------------------------------- 开关磁贴
interface Tile {
  def: PropDef
  label: string
  icon: string
  on: boolean
  busy: boolean
}

/** 「开关」这类通用词做磁贴标题没有信息量，用场景化的名字 */
const GENERIC_LABEL = /^(开关|状态|电源|开关状态|设置)$/

function tileOf(
  def: PropDef | null,
  fallbackLabel: string,
  iconName: string,
  maxLen = 6
): Tile | null {
  if (!def || !def.rw.includes('w')) return null
  const official = propLabel(def)
  const label =
    !official || GENERIC_LABEL.test(official) || official.length > maxLen ? fallbackLabel : official
  return {
    def,
    label,
    icon: iconName,
    on: !!props.values[def.name],
    busy: props.busyProp === def.name,
  }
}

/** 净化相关：负离子 / 等离子 + UV */
const purifyTiles = computed<Tile[]>(() => {
  const list: Tile[] = []
  const ion = propBy(props.spec, 'anion') || propBy(props.spec, 'plasma')
  const t1 = tileOf(ion, '负离子', 'ion')
  if (t1) list.push(t1)
  const t2 = tileOf(propBy(props.spec, 'uv'), 'UV 杀菌', 'uv')
  if (t2) list.push(t2)
  return list
})

/** 设备相关：显示屏 / 提示音 / 童锁 —— 显示屏的 on 和主开关同名，靠 siid 区分 */
const deviceTiles = computed<Tile[]>(() => {
  const list: Tile[] = []
  const mainSiid = powerProp.value?.siid
  const screenOn = propsBy(props.spec, 'on').find((p) => p.siid !== mainSiid)
  const t1 = tileOf(screenOn || null, '显示屏', 'screen', 4)
  if (t1) list.push(t1)
  const t2 = tileOf(propBy(props.spec, 'alarm'), '提示音', 'bell', 4)
  if (t2) list.push(t2)
  const t3 = tileOf(propBy(props.spec, 'physical-controls-locked'), '童锁', 'lock', 4)
  if (t3) list.push(t3)
  return list
})

function flip(t: Tile) {
  if (!online.value || t.busy) return
  emit('set-prop', t.def, !t.on)
}

// ---------------------------------------------------------------- 滤芯
interface FilterCard {
  name: string
  pct?: number
  /** 预计剩余天数（设备上报的是小时，换算后超出合理范围就不显示） */
  days?: number
  tone: 'ok' | 'warn' | 'bad'
  reset?: { siid: number; aiid: number; name: string }
}

/** 「自洁滤芯剩余寿命」→「自洁滤芯」 */
function filterStem(p: PropDef): string {
  const zh = p.desc_zh || p.desc_en || ''
  const m = /^(.*?)(剩余寿命|寿命|剩余电量|电量)$/.exec(zh)
  return ((m && m[1]) || zh || '滤芯').trim()
}

const filters = computed<FilterCard[]>(() => {
  const s = spec.value
  if (!s) return []
  const lives = s.properties.filter((p) => p.base_name === 'filter-life-level')
  return lives.map((p) => {
    const name = filterStem(p)
    const v = numOf(p)
    const pct = v === undefined ? undefined : Math.min(100, Math.max(0, v))
    const tone: FilterCard['tone'] = pct === undefined ? 'ok' : pct <= 10 ? 'bad' : pct <= 30 ? 'warn' : 'ok'

    // 剩余时间：官方单位是小时（如 4792 → 约 200 天），超范围就不猜
    const sibs = s.properties.filter((x) => x.siid === p.siid && x.base_name === 'filter-left-time')
    const leftRaw = sibs.map((x) => ({ n: numOf(x), zh: x.desc_zh || x.desc_en || '' }))
      .find((x) => x.n !== undefined && x.zh.includes(name))?.n
    let days: number | undefined
    if (leftRaw !== undefined && leftRaw > 0) {
      const d = Math.round(leftRaw > 1000 ? leftRaw / 24 : leftRaw)
      if (d >= 1 && d <= 1000) days = d
    }

    const act = s.actions.find((a) => {
      if (a.siid !== p.siid) return false
      if (!a.name.includes('reset')) return false
      const zh = a.desc_zh || a.desc_en || ''
      return zh.includes(name) || looseMatch(zh, name)
    })
    return {
      name,
      pct,
      days,
      tone,
      reset: act ? { siid: act.siid, aiid: act.aiid, name: actionLabel(act) } : undefined,
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

/** 「重置滤芯」这种把动词放前面的写法，名字不在开头也要能配上 */
function looseMatch(zh: string, name: string): boolean {
  const core = name.replace(/滤芯|滤网|阵列/g, '')
  return !!core && zh.includes(core)
}

function resetFilter(f: FilterCard) {
  if (!f.reset || !online.value) return
  emit('run-action', f.reset.siid, f.reset.aiid, f.reset.name)
}

// ---------------------------------------------------------------- 收纳其余参数
/** 面板已消费的属性/动作，剩下的收进「更多设置」，功能一个都不丢 */
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(powerProp.value)
  add(modeProp.value)
  add(fanProp.value)
  add(aqProp.value)
  add(pm25Prop.value)
  for (const p of propsBy(props.spec, 'pm2.5')) add(p)
  add(pm10Prop.value)
  add(tempProp.value)
  add(humProp.value)
  add(hchoProp.value)
  add(faultProp.value)
  for (const t of purifyTiles.value) add(t.def)
  for (const t of deviceTiles.value) add(t.def)
  const s = spec.value
  if (s) for (const p of s.properties) if (p.base_name === 'filter-life-level') add(p)
  return keys
})

const consumedActs = computed(() => {
  const set = new Set<string>()
  for (const f of filters.value) if (f.reset) set.add(`${f.reset.siid}:${f.reset.aiid}`)
  return set
})

/** 工程/调试类模块，默认不展示（米家 App 也不给用户看），勾选后可显示 */
const DEBUG_SERVICES = new Set(['filter-debug', 'filter-tag', 'custom-service', 'aqi'])
/** siid → service 短名 */
const svcNameOf = computed(() => {
  const map = new Map<string, string>()
  for (const s of props.spec?.services || []) map.set(String(s.siid), s.name)
  return map
})

const showRaw = ref(false)

const debugSiids = computed(() => {
  const set = new Set<string>()
  for (const [siid, name] of svcNameOf.value) if (DEBUG_SERVICES.has(name)) set.add(siid)
  return set
})

const restGroups = computed<SpecGroup[]>(() => {
  const keys = consumedKeys.value
  const acts = consumedActs.value
  const hidden = showRaw.value ? new Set<string>() : debugSiids.value
  return props.groups
    .filter((g) => !hidden.has(String(g.key)))
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !keys.has(`${p.siid}:${p.piid}`)),
      actions: g.actions.filter((a) => !acts.has(`${a.siid}:${a.aiid}`)),
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})
</script>

<template>
  <div v-if="spec" class="pur">
    <!-- 故障提示 -->
    <div v-if="faultText" class="fault-row">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ faultText }}</span>
    </div>

    <!-- 空气质量主视觉 -->
    <section class="hero" :class="{ off: !isOn }" :style="heroStyle">
      <div class="ring">
        <div class="ring-face">
          <b class="ring-val">{{ pmText }}</b>
          <span class="ring-unit">PM2.5</span>
        </div>
      </div>

      <div class="hero-side">
        <div class="aq-badge">
          <i class="aq-dot" />
          {{ aqText }}
        </div>
        <button
          class="power-chip"
          :class="{ on: isOn }"
          :disabled="!online || !powerProp || busyProp === powerProp.name"
          @click="powerProp && emit('set-prop', powerProp, !isOn)"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" v-html="icon('power')" />
          <span>{{ isOn ? '已开启' : '已关闭' }}</span>
        </button>
      </div>

      <div class="metrics">
        <div v-for="m in envMetrics" :key="m.label" class="metric">
          <span class="m-label">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(m.icon)" />
            {{ m.label }}
          </span>
          <b class="m-val">{{ m.value }}<em>{{ m.unit }}</em></b>
        </div>
      </div>
    </section>

    <!-- 模式：带图标的单选按钮 -->
    <section v-if="modes.length" class="blk">
      <h3>模式</h3>
      <div class="mode-grid" :style="{ '--cols': String(modes.length) }">
        <button
          v-for="m in modes"
          :key="String(m.value)"
          class="mode-item"
          :class="{ active: String(m.value) === String(modeValue), dim: !online }"
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

    <!-- 风速：分段按钮 + 档位条 -->
    <section v-if="fans.length" class="blk">
      <h3>
        风速
        <em v-if="fanHint" class="h3-hint">{{ fanHint }}</em>
      </h3>
      <div class="fan-seg">
        <button
          v-for="f in fans"
          :key="String(f.value)"
          class="fan-item"
          :class="{ active: String(f.value) === String(fanValue), dim: !online }"
          :disabled="!online"
          @click="pickFan(f.value)"
        >
          <span class="bars">
            <i v-for="b in f.total" :key="b" :class="{ lit: b <= f.bars }" :style="{ height: 5 + b * 4 + 'px' }" />
          </span>
          <span class="fan-name">{{ f.label }}</span>
        </button>
      </div>
    </section>

    <!-- 净化辅助 -->
    <section v-if="purifyTiles.length" class="blk">
      <h3>净化辅助</h3>
      <div class="tile-grid">
        <button
          v-for="t in purifyTiles"
          :key="t.def.name"
          class="tile"
          :class="{ on: t.on, dim: !online || t.busy }"
          :disabled="!online || t.busy"
          @click="flip(t)"
        >
          <span class="tile-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(t.icon)" />
          </span>
          <span class="tile-body">
            <span class="tile-name">{{ t.label }}</span>
            <span class="tile-state">{{ t.on ? '工作中' : '已关闭' }}</span>
          </span>
          <span class="tile-sw"><i /></span>
        </button>
      </div>
    </section>

    <!-- 设备开关 -->
    <section v-if="deviceTiles.length" class="blk">
      <h3>设备</h3>
      <div class="tile-grid compact">
        <button
          v-for="t in deviceTiles"
          :key="t.def.name"
          class="tile compact"
          :class="{ on: t.on, dim: !online || t.busy }"
          :disabled="!online || t.busy"
          @click="flip(t)"
        >
          <span class="tile-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(t.icon)" />
          </span>
          <span class="tile-body">
            <span class="tile-name">{{ t.label }}</span>
          </span>
        </button>
      </div>
    </section>

    <!-- 滤芯寿命 -->
    <section v-if="filters.length" class="blk">
      <h3>滤芯</h3>
      <div class="filter-list">
        <div v-for="f in filters" :key="f.name" class="filter-card" :class="f.tone">
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

    <!-- 历史曲线：本机采样记录，PM2.5/PM10/甲醛/空气质量走势 -->
    <TrendSection :did="device.did" :metrics="trendMetrics" title="历史曲线" />

    <!-- 其余参数（自检、亮度、最爱档位、自定义等）收进折叠区 -->
    <details v-if="restGroups.length || debugSiids.size" class="adv">
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
          <span>显示工程参数（转速、调试项）</span>
        </label>
      </div>
    </details>
  </div>
</template>

<style scoped>
.pur {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.pur > section,
.pur > details {
  animation: fade-up 0.32s var(--ease) both;
}
.pur > section:nth-child(2) {
  animation-delay: 0.03s;
}
.pur > section:nth-child(3) {
  animation-delay: 0.06s;
}
.pur > section:nth-child(4) {
  animation-delay: 0.09s;
}
.pur > section:nth-child(5) {
  animation-delay: 0.12s;
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
  grid-template-columns: auto 1fr;
  align-items: center;
  gap: 6px 18px;
  overflow: hidden;
  transition: opacity 0.25s ease;
}

/* 随空气质量变色的环境光 */
.hero::before {
  content: '';
  position: absolute;
  top: -70px;
  right: -50px;
  width: 220px;
  height: 220px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--aq), 0.26), transparent 68%);
  pointer-events: none;
  animation: breathe 4.5s ease-in-out infinite;
}

.hero.off {
  opacity: 0.72;
}
.hero.off::before {
  animation: none;
  opacity: 0.35;
}

.ring {
  position: relative;
  width: 104px;
  height: 104px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  background: conic-gradient(
    from -90deg,
    rgb(var(--aq)) 0 var(--p),
    var(--surface-3) var(--p) 100%
  );
  box-shadow: 0 0 22px -10px rgba(var(--aq), 0.95);
  transition: background 0.55s var(--ease), box-shadow 0.4s ease;
}

.ring::after {
  content: '';
  position: absolute;
  inset: 8px;
  border-radius: 50%;
  background: var(--surface);
}

.ring-face {
  position: relative;
  z-index: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1px;
}

.ring-val {
  font-size: 27px;
  font-weight: 700;
  line-height: 1;
  letter-spacing: 0.5px;
  color: rgb(var(--aq));
  font-variant-numeric: tabular-nums;
  transition: color 0.4s ease;
}

.ring-unit {
  font-size: 10px;
  color: var(--text-3);
  letter-spacing: 0.6px;
}

.hero-side {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 10px;
  min-width: 0;
}

.aq-badge {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 4px 12px;
  border-radius: 999px;
  font-size: 13px;
  font-weight: 600;
  color: rgb(var(--aq));
  background: rgba(var(--aq), 0.13);
  border: 1px solid rgba(var(--aq), 0.26);
}

.aq-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: rgb(var(--aq));
  box-shadow: 0 0 0 3px rgba(var(--aq), 0.2);
  animation: breathe 2.2s ease-in-out infinite;
}

.power-chip {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  height: 32px;
  padding: 0 14px 0 11px;
  border-radius: 999px;
  font-size: 12.5px;
  font-weight: 600;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: all 0.18s var(--ease);
}

.power-chip svg {
  width: 15px;
  height: 15px;
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
  opacity: 0.55;
}

.metrics {
  grid-column: 1 / -1;
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 6px;
  margin-top: 12px;
  padding-top: 12px;
  border-top: 1px dashed var(--border);
}

.metric {
  display: flex;
  flex-direction: column;
  gap: 3px;
  min-width: 0;
}

.m-label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--text-3);
  white-space: nowrap;
}

.m-label svg {
  width: 12px;
  height: 12px;
  flex: none;
}

.m-val {
  font-size: 14px;
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

.h3-hint {
  font-style: normal;
  font-size: 11px;
  font-weight: 400;
  color: var(--text-3);
}

/* ------------------------------------------------ 模式单选 */
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
  box-shadow: 0 0 8px rgba(23, 178, 106, 0.65);
}

.fan-item.dim {
  opacity: 0.5;
}

/* ------------------------------------------------ 开关磁贴 */
.tile-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
}

.tile-grid.compact {
  grid-template-columns: repeat(3, minmax(0, 1fr));
}

.tile {
  display: flex;
  align-items: center;
  gap: 9px;
  padding: 10px 11px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  text-align: left;
  transition: all 0.18s var(--ease);
}

.tile-ico {
  width: 18px;
  height: 18px;
  flex: none;
  color: var(--text-3);
  transition: color 0.18s ease;
}

.tile-body {
  display: flex;
  flex-direction: column;
  gap: 1px;
  min-width: 0;
  flex: 1;
}

.tile-name {
  font-size: 12px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tile-state {
  font-size: 10px;
  color: var(--text-3);
}

.tile-sw {
  position: relative;
  width: 26px;
  height: 15px;
  flex: none;
  border-radius: 999px;
  background: var(--border-strong);
  transition: background-color 0.2s ease;
}

.tile-sw i {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 11px;
  height: 11px;
  border-radius: 50%;
  background: #fff;
  transition: transform 0.2s var(--ease);
}

.tile:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface);
  transform: translateY(-1px);
}

.tile.on {
  border-color: rgba(23, 178, 106, 0.42);
  background: var(--accent-soft);
  color: var(--accent);
}

.tile.on .tile-ico {
  color: var(--accent);
}

.tile.on .tile-sw {
  background: var(--accent);
  box-shadow: 0 0 10px rgba(23, 178, 106, 0.55);
}

.tile.on .tile-sw i {
  transform: translateX(11px);
}

.tile.dim {
  opacity: 0.55;
}

/* 三列紧凑磁贴：放不下滑块，状态靠底色与图标颜色表达 */
.tile.compact {
  justify-content: flex-start;
  gap: 7px;
  padding: 10px 9px;
}

.tile.compact .tile-name {
  font-size: 11.5px;
}

.tile.compact::after {
  content: '';
  width: 5px;
  height: 5px;
  margin-left: auto;
  border-radius: 50%;
  background: var(--border-strong);
  transition: background-color 0.2s ease, box-shadow 0.2s ease;
}

.tile.compact.on::after {
  background: var(--accent);
  box-shadow: 0 0 7px rgba(23, 178, 106, 0.8);
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
