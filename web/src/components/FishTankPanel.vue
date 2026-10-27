<script setup lang="ts">
/**
 * 智能鱼缸专属面板（xiaomi.fishbowl.m200 / `device:fish-tank`）。
 *
 * 主视觉是一缸水：玻璃缸 + 水草 + 游鱼，水泵开着时冒气泡。
 * 右边是**缸内水温**大数字（不是室温！），下面是总开关、水泵（开关 + 挡位）、
 * 喂食（出粮量 1~3 份，走动作参数下发）。
 *
 * ⚠️ **缸内水温 ≠ 室温**：规格里 comment 是「鱼缸当前温度」，
 *    环境页已经用「鱼缸」把它排除在室温之外，这里也别混为一谈。
 *
 * ⚠️ **出粮量（feeding-measure）没有读写权限**（access 是空数组），
 *    它只是「喂食」动作的入参（action.in 指向 piid 5），
 *    所以不能当普通属性设置，只能作为动作参数下发。
 *
 * ⚠️ **水泵状态（water-pump-status）是只读的实际工况**，
 *    和「水泵开关」是两个属性：开关是我们写的，状态是设备报的，
 *    低水位 / 堵转 / 故障都从状态里读。
 */
import { computed, ref } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { fishTankFeeder, fishTankLight, fishTankProps } from '../lib/spec'
import { propLabel, valueLabel } from '../lib/zh'
import PowerToggle from './PowerToggle.vue'
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
  /** params 是动作的入参值数组，顺序与规格 action.in 一致 */
  (e: 'run-action', siid: number, aiid: number, name: string, params?: unknown[]): void
}>()

// ---------------------------------------------------------------- 图标
const ICONS: Record<string, string> = {
  pump: '<circle cx="12" cy="12" r="3.2"/><path d="M12 3.2v3.4M12 17.4v3.4M3.2 12h3.4M17.4 12h3.4M5.8 5.8l2.4 2.4M15.8 15.8l2.4 2.4M18.2 5.8l-2.4 2.4M8.2 15.8l-2.4 2.4"/>',
  food: '<path d="M4.4 10.6h15.2l-1.5 6.2a2.4 2.4 0 0 1-2.3 1.8H8.2a2.4 2.4 0 0 1-2.3-1.8z"/><path d="M8.4 10.6V7.4a3.6 3.6 0 0 1 7.2 0v3.2"/><path d="M3 20.4h18"/>',
  thermo: '<path d="M14 14.7V5.2a2 2 0 1 0-4 0v9.5a4 4 0 1 0 4 0z"/>',
  warn: '<path d="M12 4.2l8.4 15.1H3.6z"/><path d="M12 9.8v4.2"/><circle cx="12" cy="16.8" r=".9"/>',
  drop: '<path d="M12 3.9s5.2 5.6 5.2 9.1A5.2 5.2 0 0 1 12 18.2a5.2 5.2 0 0 1-5.2-5.2C6.8 9.5 12 3.9 12 3.9z"/>',
  power: '<path d="M12 3.4v7.8"/><path d="M7.2 6.9a7 7 0 1 0 9.6 0"/>',
  bulb: '<path d="M9.6 17.4a5.9 5.9 0 1 1 4.8 0"/><path d="M9.8 17.4h4.4"/><path d="M10.4 20.2h3.2"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.pump
}

// ---------------------------------------------------------------- 基础读数
const online = computed(() => props.device.online)
const ft = computed(() => fishTankProps(props.spec))

function raw(def: PropDef | null): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

/** 总开关（缸体主电源） */
const isOn = computed(() => !!raw(ft.value.on))

/** 水泵开关（我们自己写下去的那个） */
const pumpOn = computed(() => !!raw(ft.value.pump))

/**
 * 缸内水温。
 *
 * ⚠️ 规格是 uint8、量程 0~99。0°C 不可能是真水温（多半是设备没上报），
 *    >60°C 也一定是脏值，这两种都按「无数据」处理。
 */
const temperature = computed(() => {
  const v = numOf(ft.value.temperature)
  if (v === undefined) return undefined
  if (v <= 0 || v > 60) return undefined
  return v
})

/** 水泵挡位（一挡 / 二档） */
const fluxOptions = computed(() => {
  const p = ft.value.flux
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({ value: it.value, label: valueLabel(it) }))
})
const fluxValue = computed(() => raw(ft.value.flux))

/** 水泵实际工况（只读） */
const pumpStatus = computed(() => {
  const p = ft.value.pumpStatus
  const v = raw(p)
  if (p?.value_list && v !== null && v !== undefined) {
    const hit = p.value_list.find((i) => String(i.value) === String(v))
    if (hit) return { text: valueLabel(hit), code: Number(v) }
  }
  return { text: '', code: -1 }
})

/** 工况异常：低水位 / 堵转 / 故障 / 未连接 */
const pumpAlert = computed(() => {
  const t = pumpStatus.value.text
  if (/低水位|缺水/.test(t)) return { tone: 'warn' as const, text: '水位偏低，请及时补水' }
  if (/堵转|卡/.test(t)) return { tone: 'danger' as const, text: '水泵堵转，请检查叶轮' }
  if (/故障|错误/.test(t)) return { tone: 'danger' as const, text: '水泵故障' }
  if (/未连接|未接入/.test(t)) return { tone: 'warn' as const, text: '水泵未连接' }
  return null
})

/**
 * 设备异常位图（siid 12 `trigger-status`，按 bit 从低到高）。
 *
 * 覆盖面比 `water-pump-status` 广：多了灯照过久、喂食过量、滤材过期、水温提醒。
 * 其中水泵那两位和 `pumpAlert` 说的是同一件事，已有具体工况提示时就跳过，
 * 免得同一故障在界面上说两遍。
 */
const EXCEPTION_BITS: { bit: number; text: string; tone: 'warn' | 'danger'; pump?: boolean }[] = [
  { bit: 0, text: '灯照时间过长', tone: 'warn' },
  { bit: 1, text: '水泵异常（低水位 / 堵转 / 故障）', tone: 'danger', pump: true },
  { bit: 2, text: '水泵未连接', tone: 'warn', pump: true },
  { bit: 3, text: '今日喂食份数过多', tone: 'warn' },
  { bit: 4, text: '滤材已过期，建议更换', tone: 'warn' },
  { bit: 5, text: '缸内水温异常', tone: 'danger' },
]

const alerts = computed(() => {
  const out: { text: string; tone: 'warn' | 'danger' }[] = []
  if (pumpAlert.value) out.push({ text: pumpAlert.value.text, tone: pumpAlert.value.tone })
  const bits = numOf(ft.value.exception)
  if (bits && bits > 0) {
    for (const b of EXCEPTION_BITS) {
      if ((bits & (1 << b.bit)) === 0) continue
      if (b.pump && pumpAlert.value) continue
      out.push({ text: b.text, tone: b.tone })
    }
  }
  return out
})

/** 整体运行状态 */
const runState = computed(() => {
  if (!isOn.value) return { text: '已关闭', tone: 'off' }
  if (pumpAlert.value) return { text: pumpStatus.value.text, tone: pumpAlert.value.tone }
  if (/开启|运行/.test(pumpStatus.value.text)) return { text: '运行中', tone: 'on' }
  return { text: '待机', tone: 'idle' }
})

/** 主视觉配色：运行用蓝，停机用中性灰，异常用警示色 */
const toneColor = computed(() => {
  if (pumpAlert.value?.tone === 'danger') return '229, 72, 77'
  if (pumpAlert.value?.tone === 'warn') return '224, 160, 0'
  if (!isOn.value) return '148, 163, 184'
  return '47, 109, 246'
})
const heroStyle = computed(() => ({ '--tone': toneColor.value }))

// ---------------------------------------------------------------- 温度适宜区
/** 热带鱼缸常见适宜区间；偏离只做提示，不下结论 */
const TEMP_MIN = 15
const TEMP_MAX = 35
const TEMP_BAND: [number, number] = [24, 28]

const tempPct = computed(() => {
  const v = temperature.value
  if (v === undefined) return 0
  return Math.min(100, Math.max(0, ((v - TEMP_MIN) / (TEMP_MAX - TEMP_MIN)) * 100))
})
const bandStyle = computed(() => {
  const l = ((TEMP_BAND[0] - TEMP_MIN) / (TEMP_MAX - TEMP_MIN)) * 100
  const r = ((TEMP_BAND[1] - TEMP_MIN) / (TEMP_MAX - TEMP_MIN)) * 100
  return { left: `${l}%`, width: `${r - l}%` }
})
const tempTip = computed(() => {
  const v = temperature.value
  if (v === undefined) return '暂无数据'
  if (v < TEMP_BAND[0]) return '偏冷'
  if (v > TEMP_BAND[1]) return '偏热'
  return '适宜'
})

// ---------------------------------------------------------------- 喂食
/** 出粮量的可选份数：规格 value-range 是 [1, 3, 1] */
const feedOptions = computed(() => {
  const r = ft.value.feedMeasure?.range
  const min = r && r.length >= 2 ? Number(r[0]) : 1
  const max = r && r.length >= 2 ? Number(r[1]) : 3
  const step = r && r.length >= 3 ? Number(r[2]) : 1
  const out: number[] = []
  for (let v = min; v <= max; v += step > 0 ? step : 1) out.push(v)
  return out.length ? out : [1, 2, 3]
})

const feedAmount = ref(2)
const feeding = ref(false)

const feedAction = computed(() => ft.value.feedAction)

function doFeed() {
  const act = feedAction.value
  if (!act || !online.value || feeding.value) return
  feeding.value = true
  try {
    // 动作入参：按规格 action.in 的顺序给值（鱼缸只有一个入参 = 出粮量）
    emit('run-action', act.siid, act.aiid, act.name, [feedAmount.value])
  } finally {
    // 真正的状态由云端返回决定，这里只防止连点
    window.setTimeout(() => (feeding.value = false), 1200)
  }
}

// ---------------------------------------------------------------- 缸内灯
const lt = computed(() => fishTankLight(props.spec))

/** 缸内灯开关（注意：鱼缸有三个 on，灯的那个在 light 模块里） */
const lightOn = computed(() => !!raw(lt.value.on))

/** 灯光模式：日光 / 流光 / 白光 / 彩灯 / 夜灯 */
const lightModes = computed(() => {
  const p = lt.value.mode
  if (!p?.value_list) return []
  return p.value_list.map((it) => ({
    value: it.value,
    label: valueLabel(it),
    color: lightModeColor(it.value),
  }))
})
const lightModeValue = computed(() => raw(lt.value.mode))

/** 当前灯光模式的配色 */
const currentLightColor = computed(() => {
  const hit = lightModes.value.find((m) => String(m.value) === String(lightModeValue.value))
  return hit?.color || '120, 170, 255'
})

/**
 * 缸内灯的辉光强度：跟着当前模式和亮度走。
 *
 * 灯是鱼缸的主视觉氛围来源，开着时水面应该被照亮，
 * 关掉时缸内只剩环境色。
 */
const glowColor = computed(() => {
  if (!lightOn.value) return 'transparent'
  const b = lightBrightness.value
  const alpha = b === undefined ? 0.2 : Math.max(0.05, Math.min(0.4, (b / 100) * 0.4))
  return `rgba(${currentLightColor.value}, ${alpha})`
})

/** 亮度（1~100） */
const lightBrightness = computed(() => numOf(lt.value.brightness))

/** 亮度滑杆的填充百分比 */
const lightBrightnessPct = computed(() => {
  const b = lightBrightness.value
  if (b === undefined) return '0%'
  return `${Math.min(100, Math.max(0, b))}%`
})

/** 当前灯光模式的中文名 */
const currentLightModeLabel = computed(() => {
  const hit = lightModes.value.find((m) => String(m.value) === String(lightModeValue.value))
  return hit?.label || '已点亮'
})

/** 亮度滑杆拖完才下发，避免每动一下都打一次云端 */
function onBrightness(e: Event) {
  const def = lt.value.brightness
  if (!def) return
  emit('set-prop', def, Math.round(Number((e.target as HTMLInputElement).value)))
}

/**
 * 不同灯光模式给不同色，磁贴一眼能看出选中的是哪种光。
 *
 * 彩灯有几个枚举值都叫「彩灯N」，官方没给颜色，这里按序号轮转配色。
 */
function lightModeColor(value: number | string): string {
  const v = Number(value)
  const map: Record<number, string> = {
    0: '255, 214, 150', // 日光
    1: '34, 211, 238', // 流光
    3: '226, 232, 240', // 白光
    4: '244, 114, 182', // 彩灯2
    5: '250, 204, 21', // 彩灯3
    6: '168, 85, 247', // 彩灯4
    7: '139, 122, 246', // 夜灯
    8: '34, 197, 94', // 彩灯5
    9: '56, 189, 248', // 彩灯6
  }
  return map[v] || '120, 170, 255'
}

// ---------------------------------------------------------------- 喂食统计
const fd = computed(() => fishTankFeeder(props.spec))

/** 今日已投喂份数 */
const todayFeed = computed(() => numOf(fd.value.todayNum))

/** 投喂机忙闲：官方枚举只有 Idle / Busy，没给中文 */
const feederBusy = computed(() => {
  const v = raw(fd.value.status)
  if (v === null || v === undefined) return false
  return Number(v) === 1 || String(v).toLowerCase() === 'busy'
})

// ---------------------------------------------------------------- 收纳其余
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  const add = (d: PropDef | null | undefined) => {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  add(ft.value.on)
  add(ft.value.pump)
  add(ft.value.flux)
  add(ft.value.pumpStatus)
  add(ft.value.feedMeasure)
  add(ft.value.temperature)
  add(lt.value.on)
  add(lt.value.mode)
  add(lt.value.brightness)
  add(lt.value.flowSpeed)
  add(fd.value.status)
  add(fd.value.todayNum)
  // 异常位图已经在顶部解码成中文提示，别在「更多设置」里再露一个原始数字
  add(ft.value.exception)
  return keys
})

const restGroups = computed<SpecGroup[]>(() =>
  props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !consumedKeys.value.has(`${p.siid}:${p.piid}`)),
      // ⚠️ 动作按 **siid + aiid** 排除，不能按 name 比对：
      //    buildGroups 存的是 actionLabel() 出来的中文标签（"喂食"），
      //    而 feedAction.name 是英文原名（"pet-food-out"），
      //    按 name 比永远对不上，喂食动作就会在「更多设置」里多出一个重复按钮。
      actions: g.actions.filter(
        (a) => !(feedAction.value && a.siid === feedAction.value.siid && a.aiid === feedAction.value.aiid)
      ),
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
)

/**
 * 「更多设置」里的显示名覆盖。
 *
 * 勿扰模式自定义内容里有一组「灯光开关 / 灯光模式 / 水泵开关 / 水泵挡位」，
 * 是**勿扰期间**要设成什么值，和面板上方真正的主控同名，
 * 不加前缀用户会以为在开关水泵。
 */
function advLabel(p: PropDef): string | undefined {
  if (p.base_name.startsWith('light-status-') || p.base_name.startsWith('pump-status-')) {
    return `勿扰时${propLabel(p)}`
  }
  return undefined
}
</script>

<template>
  <div v-if="spec" class="tank">
    <!-- 异常提示：水泵工况 + 设备异常位图（灯照过久 / 滤材过期 / 水温异常…） -->
    <div v-for="(a, i) in alerts" :key="i" class="alert-row" :class="a.tone">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('warn')" />
      <span>{{ a.text }}</span>
    </div>

    <!-- 主视觉：一缸水 -->
    <section class="hero" :class="{ off: !isOn }" :style="heroStyle">
      <div class="scene">
        <svg viewBox="0 0 220 140" preserveAspectRatio="xMidYMid slice">
          <defs>
            <linearGradient id="tk-water" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stop-color="rgba(120,180,255,0.30)" />
              <stop offset="100%" stop-color="rgba(23,92,168,0.46)" />
            </linearGradient>
            <linearGradient id="tk-sand" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stop-color="rgba(214,190,140,0.55)" />
              <stop offset="100%" stop-color="rgba(180,152,102,0.45)" />
            </linearGradient>
            <clipPath id="tk-clip">
              <rect x="6" y="8" width="208" height="124" rx="14" />
            </clipPath>
            <filter id="tk-glow" x="-60%" y="-60%" width="220%" height="220%">
              <feGaussianBlur stdDeviation="13" />
            </filter>
          </defs>

          <!-- 缸体 -->
          <rect class="glass" x="6" y="8" width="208" height="124" rx="14" fill="url(#tk-water)" />
          <g clip-path="url(#tk-clip)">
            <!-- 缸内灯的辉光：颜色跟着灯光模式走，强度跟着亮度走 -->
            <ellipse
              v-if="lightOn"
              class="light-glow"
              cx="110"
              cy="16"
              rx="76"
              ry="32"
              :fill="glowColor"
              filter="url(#tk-glow)"
            />
            <!-- 水面 -->
            <path class="wave" d="M6 30 q 25 -6 50.5 0 t 50.5 0 t 50.5 0 t 50.5 0" />
            <!-- 底砂 -->
            <path d="M6 110 Q 58 100 110 108 T 214 104 V 132 H 6 Z" fill="url(#tk-sand)" />
            <!-- 水草 -->
            <g class="weeds">
              <path d="M40 112 C 34 92, 46 84, 40 66" />
              <path d="M52 112 C 60 96, 48 84, 58 70" />
              <path d="M170 112 C 162 94, 176 86, 168 70" />
              <path d="M182 112 C 190 98, 178 88, 188 74" />
            </g>
            <!-- 气泡：只有水泵开着才冒 -->
            <g v-if="pumpOn" class="bubbles">
              <circle cx="196" cy="104" r="2.6" />
              <circle cx="201" cy="104" r="1.8" />
              <circle cx="193" cy="104" r="1.4" />
            </g>
            <!-- 鱼 -->
            <g class="fish f1">
              <path d="M0 0 c 7 -6, 15 -6, 21 0 c -6 6, -14 6, -21 0 z" />
              <path d="M-1 0 l -7 -5 l 0 10 z" class="tail" />
              <circle cx="15" cy="-1.6" r="1.1" class="eye" />
            </g>
            <g class="fish f2">
              <path d="M0 0 c 5.4 -4.6, 11.6 -4.6, 16.2 0 c -4.6 4.6, -10.8 4.6, -16.2 0 z" />
              <path d="M-1 0 l -5.4 -3.8 l 0 7.6 z" class="tail" />
              <circle cx="11.4" cy="-1.2" r="0.9" class="eye" />
            </g>
          </g>
          <!-- 缸口高光 -->
          <path class="rim" d="M6 22 Q 110 8, 214 22" />
        </svg>
      </div>

      <div class="hero-side">
        <div class="temp-big">
          <b>{{ temperature === undefined ? '—' : Math.round(temperature) }}</b><span>°C</span>
        </div>
        <div class="temp-cap">缸内水温</div>
        <div class="status-chip" :class="runState.tone">
          <i class="dot" />
          {{ runState.text }}
        </div>
      </div>
    </section>

    <!-- 温度适宜区：线性标尺（与弧形仪表不同的表现方式） -->
    <section class="blk">
      <h3>
        温度适宜区
        <em class="h3-hint">热带鱼 24~28°C</em>
      </h3>
      <div class="scale">
        <div class="s-track">
          <div class="s-band" :style="bandStyle" />
          <div class="s-mark" :style="{ left: tempPct + '%' }" />
        </div>
        <div class="s-ticks">
          <span>{{ TEMP_MIN }}°</span>
          <span>{{ TEMP_BAND[0] }}~{{ TEMP_BAND[1] }}°</span>
          <span>{{ TEMP_MAX }}°</span>
        </div>
      </div>
      <div class="scale-note">
        当前
        <b>{{ temperature === undefined ? '—' : Math.round(temperature) + '°C' }}</b>
        <span class="tip" :class="{ warn: tempTip !== '适宜' }">{{ tempTip }}</span>
      </div>
    </section>

    <!-- 总开关 -->
    <section v-if="ft.on" class="blk">
      <h3>电源</h3>
      <button
        class="tile wide"
        :class="{ on: isOn }"
        :disabled="!online || busyProp === ft.on?.name"
        @click="emit('set-prop', ft.on!, !isOn)"
      >
        <span class="t-ico">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('power')" />
        </span>
        <span class="t-body">
          <span class="t-name">鱼缸电源</span>
          <span class="t-state">{{ isOn ? '已通电' : '已关闭' }}</span>
        </span>
        <PowerToggle
          :model-value="isOn"
          size="lg"
          :disabled="!online || busyProp === ft.on?.name"
          :busy="busyProp === ft.on?.name"
          label="鱼缸电源"
          @update:model-value="emit('set-prop', ft.on!, !isOn)"
        />
      </button>
    </section>

    <!-- 水泵 -->
    <section class="blk">
      <h3>
        水泵
        <em class="h3-hint">{{ pumpStatus.text || '未知' }}</em>
      </h3>

      <!-- 水泵开关单独占一整行：和「挡位」磁贴挤在一格的话，
           图标 + 文案 + 开关会把文字压成竖排一列 -->
      <button
        v-if="ft.pump"
        class="tile wide"
        :class="{ on: pumpOn }"
        :disabled="!online || busyProp === ft.pump?.name"
        @click="emit('set-prop', ft.pump, !pumpOn)"
      >
        <span class="t-ico">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('pump')" />
        </span>
        <span class="t-body">
          <span class="t-name">水泵</span>
          <span class="t-state">{{ pumpOn ? '已开启' : '已关闭' }}</span>
        </span>
        <PowerToggle
          :model-value="pumpOn"
          size="md"
          :disabled="!online || busyProp === ft.pump?.name"
          :busy="busyProp === ft.pump?.name"
          label="水泵"
          @update:model-value="emit('set-prop', ft.pump!, !pumpOn)"
        />
      </button>

      <!-- 挡位：一挡 / 二档 -->
      <div v-if="fluxOptions.length" class="tile-grid">
        <button
          v-for="o in fluxOptions"
          :key="String(o.value)"
          class="tile"
          :class="{ on: String(o.value) === String(fluxValue) }"
          :disabled="!online || busyProp === ft.flux?.name"
          @click="emit('set-prop', ft.flux!, o.value)"
        >
          <span class="t-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('drop')" />
          </span>
          <span class="t-body">
            <span class="t-name">挡位</span>
            <span class="t-state">{{ o.label }}</span>
          </span>
          <span class="t-check" />
        </button>
      </div>
    </section>

    <!-- 缸内灯 -->
    <section v-if="lt.on" class="blk">
      <h3>
        缸内灯
        <em class="h3-hint">{{ lightOn ? currentLightModeLabel : '已关闭' }}</em>
      </h3>
      <div class="tile-grid">
        <button
          class="tile"
          :class="{ on: lightOn }"
          :disabled="!online || busyProp === lt.on?.name"
          @click="emit('set-prop', lt.on!, !lightOn)"
        >
          <span class="t-ico">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('bulb')" />
          </span>
          <span class="t-body">
            <span class="t-name">灯光</span>
            <span class="t-state">{{ lightOn ? '已点亮' : '已熄灭' }}</span>
          </span>
          <PowerToggle
            :model-value="lightOn"
            size="md"
            :disabled="!online || busyProp === lt.on?.name"
            :busy="busyProp === lt.on?.name"
            label="缸内灯"
            @update:model-value="emit('set-prop', lt.on!, !lightOn)"
          />
        </button>
      </div>

      <!-- 模式磁贴：每个模式给一种颜色，选中什么光一眼可见 -->
      <div v-if="lightModes.length" class="chips">
        <button
          v-for="m in lightModes"
          :key="String(m.value)"
          class="chip"
          :class="{ active: String(m.value) === String(lightModeValue) }"
          :style="{ '--c': m.color }"
          :disabled="!online || busyProp === lt.mode?.name"
          @click="emit('set-prop', lt.mode!, m.value)"
        >
          <i class="dot" />
          {{ m.label }}
        </button>
      </div>

      <!-- 亮度 -->
      <div v-if="lt.brightness" class="slider-row">
        <span class="s-label">亮度</span>
        <input
          class="slider"
          type="range"
          min="1"
          max="100"
          step="1"
          :value="lightBrightness ?? 1"
          :disabled="!online || busyProp === lt.brightness?.name"
          :style="{ '--fill': lightBrightnessPct }"
          @change="onBrightness"
        />
        <span class="s-val">{{ lightBrightness === undefined ? '—' : Math.round(lightBrightness) + '%' }}</span>
      </div>
    </section>

    <!-- 喂食 -->
    <section v-if="feedAction" class="blk">
      <h3>喂食</h3>
      <div class="feed-row">
        <div class="dose-group">
          <button
            v-for="n in feedOptions"
            :key="n"
            class="dose"
            :class="{ active: n === feedAmount }"
            :disabled="!online"
            @click="feedAmount = n"
          >
            {{ n }} 份
          </button>
        </div>
        <button class="feed-btn" :disabled="!online || feeding" @click="doFeed">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon('food')" />
          <span>{{ feeding ? '投喂中…' : '投喂' }}</span>
        </button>
      </div>
      <!-- 今日投喂统计：官方 feeder 模块给的（今日已投喂份数 + 投喂机忙闲） -->
      <div v-if="fd.todayNum || fd.status" class="feed-stat">
        <span>
          今日已投喂
          <b>{{ todayFeed === undefined ? '—' : todayFeed }}</b>
          份
        </span>
        <span v-if="fd.status" class="fs-chip" :class="{ busy: feederBusy }">
          {{ feederBusy ? '投喂中' : '空闲' }}
        </span>
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
            :label="advLabel(p)"
            :value="values[p.name]"
            :disabled="!online || busyProp === p.name"
            @change="(v) => emit('set-prop', p, v)"
          />
          <div v-if="g.readable.length" class="readable">
            <PropControl
              v-for="p in g.readable"
              :key="`${p.siid}-${p.piid}`"
              :def="p"
              :label="advLabel(p)"
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
.tank {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.tank > section,
.tank > details {
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
  padding: 14px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  display: grid;
  grid-template-columns: 1fr auto;
  align-items: center;
  gap: 16px;
  overflow: hidden;
}

.hero::before {
  content: '';
  position: absolute;
  top: -70px;
  right: -50px;
  width: 190px;
  height: 190px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--tone), 0.2), transparent 68%);
  pointer-events: none;
  animation: breathe 4.5s ease-in-out infinite;
}

.hero.off {
  opacity: 0.82;
}

.hero.off::before {
  animation: none;
  opacity: 0.35;
}

/* 缸体 */
.scene {
  position: relative;
  aspect-ratio: 22 / 14;
  border-radius: var(--radius-md);
  overflow: hidden;
  border: 1px solid var(--border);
  background: linear-gradient(180deg, var(--surface-2), var(--surface-3));
}

.scene svg {
  display: block;
  width: 100%;
  height: 100%;
}

.glass {
  stroke: rgba(255, 255, 255, 0.42);
  stroke-width: 1.4;
}

.rim {
  fill: none;
  stroke: rgba(255, 255, 255, 0.5);
  stroke-width: 2;
  stroke-linecap: round;
}

/* 水面波动 */
.wave {
  fill: none;
  stroke: rgba(255, 255, 255, 0.55);
  stroke-width: 1.6;
  animation: tk-wave 5s linear infinite;
}

@keyframes tk-wave {
  from {
    transform: translateX(0);
  }
  to {
    transform: translateX(-50.5px);
  }
}

/* 水草轻摆 */
.weeds path {
  fill: none;
  stroke: rgba(46, 140, 96, 0.75);
  stroke-width: 2.4;
  stroke-linecap: round;
  transform-box: fill-box;
  transform-origin: bottom center;
  animation: tk-sway 3.6s ease-in-out infinite;
}

.weeds path:nth-child(2) {
  animation-delay: -0.8s;
}

.weeds path:nth-child(3) {
  animation-delay: -1.6s;
}

.weeds path:nth-child(4) {
  animation-delay: -2.4s;
}

@keyframes tk-sway {
  0%,
  100% {
    transform: rotate(-3deg);
  }
  50% {
    transform: rotate(3deg);
  }
}

/* 气泡：只有水泵开着才渲染（v-if） */
.bubbles circle {
  fill: rgba(255, 255, 255, 0.75);
  animation: tk-rise 2.6s ease-in infinite;
}

.bubbles circle:nth-child(2) {
  animation-delay: -0.9s;
}

.bubbles circle:nth-child(3) {
  animation-delay: -1.7s;
}

@keyframes tk-rise {
  0% {
    transform: translateY(0);
    opacity: 0;
  }
  15% {
    opacity: 0.85;
  }
  100% {
    transform: translateY(-74px);
    opacity: 0;
  }
}

/* 游鱼 */
.fish {
  fill: rgb(var(--tone));
  animation: tk-swim 11s linear infinite;
}

.fish .tail {
  fill: rgb(var(--tone));
  opacity: 0.7;
  transform-box: fill-box;
  transform-origin: right center;
  animation: tk-tail 0.5s ease-in-out infinite alternate;
}

.fish .eye {
  fill: #fff;
}

.fish.f2 {
  fill: rgb(224, 160, 0);
  animation-duration: 15s;
  animation-delay: -6s;
}

@keyframes tk-swim {
  0% {
    transform: translate(24px, 58px);
  }
  50% {
    transform: translate(140px, 66px);
  }
  100% {
    transform: translate(24px, 58px);
  }
}

@keyframes tk-tail {
  from {
    transform: rotate(-12deg);
  }
  to {
    transform: rotate(12deg);
  }
}

/* 右侧读数 */
.hero-side {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 3px;
  min-width: 92px;
}

.temp-big {
  display: flex;
  align-items: baseline;
  gap: 2px;
  color: rgb(var(--tone));
}

.temp-big b {
  font-size: 34px;
  font-weight: 700;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.6px;
}

.temp-big span {
  font-size: 13px;
  color: var(--text-3);
}

.temp-cap {
  font-size: 11px;
  color: var(--text-3);
}

.status-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin-top: 8px;
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
  animation: breathe 1.8s ease-in-out infinite;
}

.status-chip.warn {
  color: #b07800;
  background: rgba(224, 160, 0, 0.12);
  border-color: rgba(224, 160, 0, 0.32);
}

.status-chip.warn .dot {
  background: #e0a000;
}

.status-chip.danger {
  color: var(--danger);
  background: rgba(229, 72, 77, 0.1);
  border-color: rgba(229, 72, 77, 0.3);
}

.status-chip.danger .dot {
  background: var(--danger);
  box-shadow: 0 0 6px rgba(229, 72, 77, 0.32);
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

/* ------------------------------------------------ 温度标尺 */
.scale {
  padding: 12px 12px 8px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.s-track {
  position: relative;
  height: 8px;
  border-radius: 999px;
  background: var(--border-strong);
  overflow: visible;
}

.s-band {
  position: absolute;
  top: 0;
  bottom: 0;
  border-radius: 999px;
  background: linear-gradient(90deg, #4cc38a, #17b26a);
}

.s-mark {
  position: absolute;
  top: 50%;
  width: 14px;
  height: 14px;
  margin-left: -7px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid rgb(var(--tone));
  box-shadow: 0 0 7px rgba(var(--tone), 0.28);
  transform: translateY(-50%);
  transition: left 0.5s var(--ease);
}

.s-ticks {
  display: flex;
  justify-content: space-between;
  margin-top: 8px;
  font-size: 10px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.scale-note {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 9px;
  font-size: 11.5px;
  color: var(--text-3);
}

.scale-note b {
  color: var(--text);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
}

.scale-note .tip {
  margin-left: auto;
  padding: 2px 9px;
  border-radius: 999px;
  font-size: 11px;
  color: var(--accent);
  background: var(--accent-soft);
}

.scale-note .tip.warn {
  color: #b07800;
  background: rgba(224, 160, 0, 0.12);
}

/* ------------------------------------------------ 磁贴 */
.tile-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(0, 1fr));
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

.tile.wide {
  width: 100%;
}

/* 水泵开关整行与下面「挡位」磁贴之间留缝 */
.blk > .tile + .tile-grid {
  margin-top: 9px;
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

.t-check {
  width: 14px;
  height: 14px;
  flex: none;
  border-radius: 50%;
  border: 1.5px solid var(--border-strong);
  transition: all 0.2s var(--ease);
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

.tile.on .t-check {
  background: var(--accent);
  border-color: var(--accent);
  box-shadow: 0 0 6px rgba(23, 178, 106, 0.26);
}

.tile:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* ------------------------------------------------ 喂食 */
.feed-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.dose-group {
  display: flex;
  gap: 6px;
  flex: 1;
  min-width: 0;
}

.dose {
  flex: 1;
  min-width: 0;
  height: 32px;
  border-radius: 9px;
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  font-size: 12px;
  white-space: nowrap;
  transition: all 0.16s var(--ease);
}

.dose:hover:not(:disabled) {
  border-color: var(--border-strong);
  color: var(--text);
}

.dose.active {
  color: var(--accent);
  background: var(--accent-soft);
  border-color: rgba(23, 178, 106, 0.45);
  font-weight: 600;
}

.dose:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.feed-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 14px;
  flex: none;
  border-radius: 9px;
  font-size: 12.5px;
  font-weight: 600;
  color: #fff;
  background: var(--grad-brand);
  box-shadow: var(--glow-brand);
  transition: all 0.18s var(--ease);
}

.feed-btn svg {
  width: 15px;
  height: 15px;
}

.feed-btn:hover:not(:disabled) {
  transform: translateY(-1px);
}

.feed-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
  box-shadow: none;
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

/* ------------------------------------------------ 缸内灯 */
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
  margin-top: 10px;
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 28px;
  padding: 0 11px;
  border-radius: 999px;
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text-2);
  font-size: 11.5px;
  transition: all 0.16s var(--ease);
}

.chip .dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: rgb(var(--c, 120, 170, 255));
  box-shadow: 0 0 5px rgba(var(--c, 120, 170, 255), 0.32);
  flex: none;
}

.chip:hover:not(:disabled) {
  border-color: var(--border-strong);
  color: var(--text);
}

.chip.active {
  color: var(--text);
  border-color: rgba(var(--c, 120, 170, 255), 0.55);
  background: rgba(var(--c, 120, 170, 255), 0.12);
  font-weight: 600;
}

.chip:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* 亮度滑杆 */
.slider-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 11px;
  padding: 9px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.s-label {
  font-size: 11.5px;
  color: var(--text-3);
  flex: none;
}

.slider {
  flex: 1;
  appearance: none;
  height: 5px;
  border-radius: 5px;
  background: linear-gradient(90deg, var(--accent) 0%, var(--accent) var(--fill, 0%), var(--border-strong) var(--fill, 0%));
  outline: none;
}

.slider::-webkit-slider-thumb {
  appearance: none;
  width: 15px;
  height: 15px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--accent);
  box-shadow: 0 0 6px rgba(23, 178, 106, 0.26);
  cursor: pointer;
  transition: transform 0.15s var(--ease);
}

.slider::-webkit-slider-thumb:hover {
  transform: scale(1.12);
}

.slider:disabled {
  opacity: 0.5;
}

.s-val {
  font-size: 11.5px;
  color: var(--text);
  font-variant-numeric: tabular-nums;
  min-width: 34px;
  text-align: right;
  flex: none;
}

/* 缸内灯辉光 */
.light-glow {
  pointer-events: none;
  animation: breathe 5s ease-in-out infinite;
}

/* ------------------------------------------------ 喂食统计 */
.feed-stat {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 9px;
  padding: 8px 12px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  font-size: 11.5px;
  color: var(--text-3);
}

.feed-stat b {
  color: var(--text);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
  margin: 0 2px;
}

.fs-chip {
  margin-left: auto;
  padding: 2px 9px;
  border-radius: 999px;
  font-size: 11px;
  color: var(--text-3);
  background: var(--surface-3);
  border: 1px solid var(--border);
  white-space: nowrap;
}

.fs-chip.busy {
  color: var(--accent);
  background: var(--accent-soft);
  border-color: rgba(23, 178, 106, 0.35);
}
</style>
