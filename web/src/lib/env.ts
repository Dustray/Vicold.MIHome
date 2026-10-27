/**
 * 环境页的数据层：读数采集、房间聚合、指标评级、设备快捷控制清单。
 *
 * ⚠️ 三条踩过的坑，都写在对应位置：
 *  1. `/miotspec/prop/get` 返回顺序会被重排 —— 回填必须按 did/siid/piid 三元组；
 *  2. 虚拟子设备（`xxx.s2`）的属性要用**父 did** 去读，siid 才是那一路编号；
 *  3. 温度属性有大量干扰项（档位、目标温度、机内温度），必须按
 *     类型 + 描述过滤，不能只看名字。
 */
import { reactive } from 'vue'
import { invoke } from './host'
import { record as historyRecord, series as historySeries } from './history'
import { valueLabel } from './zh'
import type { Pt } from './history'
import type { Device, PropDef, Spec } from './types'
import { isAirConditioner, isLight, propBy, switchChannels, switchForm } from './spec'

// ---------------------------------------------------------------------------
// 指标定义
// ---------------------------------------------------------------------------

export type MetricKey = 't' | 'h' | 'pm25' | 'pm10' | 'hcho' | 'tvoc' | 'co2' | 'aq'

export interface MetricDef {
  key: MetricKey
  label: string
  unit: string
  decimals: number
  /** 优 / 良 / 轻度污染 三档阈值（aq 是 0~5 枚举档位，单独判定） */
  good: number
  ok: number
  bad: number
  /** 刻度上限（画条形/曲线用） */
  scaleMax: number
  /** 等级配色 "r,g,b" */
  color: string
  /** 舒适/安全区间，画在曲线的绿色底上 */
  band?: [number, number]
  /**
   * 舒适档位的分界距离：偏离 `band` 超过第一个值算「略偏」，超过第二个算「偏」，
   * 再远就是「很」。温湿度各用各的绝对距离，不按区间宽度归一化 ——
   * 温度带宽 8°C、湿度带宽 20%，同一个比例在湿度上会过于宽松
   * （65% 这种只是偏湿的会被说成「潮湿」）。
   */
  tiers?: [number, number]
}

export const METRICS: Record<MetricKey, MetricDef> = {
  t: {
    key: 't',
    label: '温度',
    unit: '°C',
    decimals: 1,
    good: 26,
    ok: 28,
    bad: 32,
    scaleMax: 40,
    color: '242,98,15',
    band: [18, 26],
    // 17°C 微凉 / 15°C 偏冷 / 13°C 很冷
    tiers: [1.5, 4]
  },
  h: {
    key: 'h',
    label: '湿度',
    unit: '%',
    decimals: 0,
    good: 60,
    ok: 70,
    bad: 80,
    scaleMax: 100,
    color: '47,109,246',
    band: [40, 60],
    // 38% 略干 / 30% 偏干 / 25% 很干（与 SensorPanel 的 30/40/60/70 阈值对齐）
    tiers: [3, 10]
  },
  pm25: {
    key: 'pm25',
    label: 'PM2.5',
    unit: 'μg/m³',
    decimals: 0,
    good: 35,
    ok: 75,
    bad: 115,
    scaleMax: 150,
    color: '90,140,220'
  },
  pm10: {
    key: 'pm10',
    label: 'PM10',
    unit: 'μg/m³',
    decimals: 0,
    good: 50,
    ok: 150,
    bad: 250,
    scaleMax: 300,
    color: '124,184,20'
  },
  hcho: {
    key: 'hcho',
    label: '甲醛',
    unit: 'mg/m³',
    decimals: 3,
    good: 0.08,
    ok: 0.1,
    bad: 0.12,
    scaleMax: 0.2,
    color: '139,63,191'
  },
  tvoc: {
    key: 'tvoc',
    label: 'TVOC',
    unit: 'mg/m³',
    decimals: 3,
    good: 0.3,
    ok: 0.6,
    bad: 1,
    scaleMax: 2,
    color: '23,178,106'
  },
  co2: {
    key: 'co2',
    label: 'CO₂',
    unit: 'ppm',
    decimals: 0,
    good: 1000,
    ok: 1500,
    bad: 2000,
    scaleMax: 3000,
    color: '229,72,77'
  },
  aq: {
    key: 'aq',
    label: '空气质量',
    unit: '级',
    decimals: 0,
    good: 1,
    ok: 2,
    bad: 3,
    scaleMax: 5,
    color: '23,178,106'
  }
}

/**
 * 仪表量程：仪表指针与进度条共用同一份。
 *
 * 指针表盘必须有确定的 min/max 才能算角度，进度条也靠它算占比；
 * 两处各写一份迟早会对不上（改了一个忘了另一个），统一放这里。
 */
export const METRIC_RANGE: Record<MetricKey, { min: number; max: number }> = {
  // 温度基准 0~40 °C：40 是夏天没开空调的房间/空调回风口的常态上限，
  // 卡在 35 会让指针顶死、进度条满格，看不出还剩多少余量；
  // 读数真的越界时走 fitRange() 动态扩量程（冬天没暖气的房间会到 0 以下）。
  t: { min: 0, max: 40 },
  h: { min: 0, max: 100 },
  pm25: { min: 0, max: 150 },
  pm10: { min: 0, max: 300 },
  hcho: { min: 0, max: 0.2 },
  tvoc: { min: 0, max: 2 },
  co2: { min: 300, max: 3000 },
  aq: { min: 0, max: 5 }
}

/**
 * 把跨度换成「好读的刻度步长」（1 / 2 / 5 的十倍数）。
 *
 * 扩量程时如果直接取 `Math.ceil(v)`，刻度数字就会变成 41、0.23 这种碎值，
 * 表盘读起来很别扭；按 1/2/5 取步长，扩出来一定是 45、50、0.25 这类整数倍。
 */
function niceStep(span: number) {
  if (!Number.isFinite(span) || span <= 0) return 1
  const raw = span / 8
  const mag = Math.pow(10, Math.floor(Math.log10(raw)))
  const n = raw / mag
  const mul = n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10
  return mul * mag
}

/** 消掉 0.1 + 0.2 这类浮点尾巴，否则刻度会显示 45.000000000000006 */
function tidy(v: number) {
  return Number(v.toPrecision(12))
}

/**
 * 按当前读数动态扩量程。
 *
 * 固定量程一定会被真实数据打穿：冬天没暖气的房间零下、西晒房间 42°C、
 * 刚装修完甲醛爆表、做饭时 PM2.5 四百多 —— 指针顶死在端点、进度条满格，
 * 读数本身还在，但**看不出超出多少**，等于把「超标」画成了「刚好满」。
 *
 * 只扩越界的那一端，另一侧保持不动（否则刻度会整体漂移，同一读数的位置天天变）。
 * 扩展量多留半个步长，指针不会停在离端点 1px 的地方；步长本身是 1/2/5 的十倍数，
 * 所以扩出来的 max 一定是 45、50、0.25 这种整数刻度，不会出现 42.7。
 */
export function fitRange(key: MetricKey, v?: number | null): { min: number; max: number } {
  const base = METRIC_RANGE[key]
  if (v === undefined || v === null || !Number.isFinite(v)) return base
  if (v <= base.max && v >= base.min) return base
  const { min, max } = base
  const step = niceStep(max - min)
  if (v > max) return { min, max: tidy(Math.ceil((v + step * 0.5) / step) * step) }
  return { min: tidy(Math.floor((v - step * 0.5) / step) * step), max }
}

/** 空气质量的六档（与设备 air-quality 枚举一致） */
export const AQ_LABEL = ['优', '良', '轻度污染', '中度污染', '重度污染', '严重污染']

export const LEVEL_LABEL = ['优', '良', '轻度污染', '中度污染', '重度污染', '严重污染']

export const LEVEL_COLOR = [
  '23,178,106',
  '124,184,20',
  '224,160,0',
  '242,98,15',
  '229,72,77',
  '139,63,191'
]

/** 0=优 1=良 2=轻度污染 3=中度污染 4=重度污染 5=严重污染 */
export function levelOf(key: MetricKey, v: number | undefined): number | null {
  if (v === undefined || !Number.isFinite(v)) return null
  if (key === 'aq') return Math.max(0, Math.min(5, Math.round(v)))
  const m = METRICS[key]
  if (v <= m.good) return 0
  if (v <= m.ok) return 1
  if (v <= m.bad) return 2
  if (v <= m.bad + (m.scaleMax - m.bad) * 0.4) return 3
  if (v <= m.bad + (m.scaleMax - m.bad) * 0.7) return 4
  return 5
}

/**
 * 温湿度舒适度：档位 + 偏离方向。
 *
 * ⚠️ **方向必须单独返回**。温湿度是双侧区间（湿度 40~60%），30% 和 70%
 *    偏离舒适区的距离可以一样，但一个是「干」一个是「湿」。
 *    早先只返回距离档位，结果 30% 被显示成「很湿」。
 */
export interface Comfort {
  /** 0 舒适 / 1 略偏 / 2 偏 / 3 很 */
  level: 0 | 1 | 2 | 3
  /** -1 偏冷·偏干 / 0 舒适 / 1 偏热·偏湿 */
  dir: -1 | 0 | 1
}

export function comfortOf(key: 't' | 'h', v: number | undefined): Comfort | null {
  if (v === undefined || !Number.isFinite(v)) return null
  const def = METRICS[key]
  const band = def.band
  if (!band) {
    // 没有区间定义时退化成「离目标值多远」
    const dist = Math.abs(v - def.good)
    return { level: dist <= 2 ? 0 : dist <= 5 ? 1 : 2, dir: 0 }
  }
  const [lo, hi] = band
  if (v >= lo && v <= hi) return { level: 0, dir: 0 }
  const dir: -1 | 1 = v < lo ? -1 : 1
  const dist = v < lo ? lo - v : v - hi
  // 每个指标用自己的绝对分界距离；没定义时按区间宽度折算
  const [near, mid] = def.tiers ?? [(hi - lo) * 0.15, (hi - lo) * 0.5]
  const level = dist <= near ? 1 : dist <= mid ? 2 : 3
  return { level, dir }
}

/** 只要档位时用这个（颜色分级、曲线着色） */
export function comfortLevel(key: 't' | 'h', v: number | undefined): number | null {
  return comfortOf(key, v)?.level ?? null
}

/** 档位配色：绿 → 黄绿 → 黄 → 橙。干湿冷热共用同一套「越偏越警示」的色阶 */
export const COMFORT_COLOR = ['23,178,106', '124,184,20', '224,160,0', '242,98,15']

/**
 * 温湿度的人话评价。
 *
 * 文案必须带方向：「30%」是「偏干」，「75%」是「很湿」，不能都用一套词。
 */
export function comfortText(key: 't' | 'h', v: number | undefined): { text: string; color: string } {
  const c = comfortOf(key, v)
  if (!c) return { text: '无数据', color: '143,160,186' }
  const dry = c.dir < 0
  const table =
    key === 'h'
      ? dry
        ? ['舒适', '略干', '偏干', '很干']
        : ['舒适', '略湿', '偏湿', '很湿']
      : dry
        ? ['舒适', '微凉', '偏冷', '很冷']
        : ['舒适', '微热', '偏热', '很热']
  return { text: table[c.level], color: COMFORT_COLOR[c.level] }
}

/**
 * 某个读数该用什么颜色、配什么评价词。
 *
 * 温湿度走双侧区间（`comfortText`），污染物走档位（`levelOf`），
 * 仪表盘、房间卡片、传感器钉子板全用这一份判定，
 * 免得同一读数在三处显示成三种颜色。
 */
export function metricState(key: MetricKey, v: number | undefined): { color: string; note: string } {
  if (key === 't' || key === 'h') {
    const c = comfortText(key, v)
    return { color: c.color, note: c.text }
  }
  const lv = levelOf(key, v)
  if (lv === null) return { color: '143,160,186', note: '无数据' }
  return { color: LEVEL_COLOR[lv], note: LEVEL_LABEL[lv] }
}

// ---------------------------------------------------------------------------
// 读数
// ---------------------------------------------------------------------------

export interface EnvReading {
  t?: number
  h?: number
  pm25?: number
  pm10?: number
  hcho?: number
  tvoc?: number
  co2?: number
  aq?: number
}

/** did -> 读数 */
export const envReadings = reactive<Record<string, EnvReading>>({})

/**
 * 温度属性的干扰项。
 * 规格里叫 temperature 的东西有一半不是室温：
 *   temperature(int)         档位型（空调、净化器）
 *   temperature(uint)        设备/鱼缸/出风口温度
 *   target-temperature       目标温度
 *   描述含"目标/设备/鱼缸"  一律不是室温
 */
const ENV_TEMP_DESC_BAD = /目标|设备|鱼缸|档位|进水|出水|水温|加热/

/** 整机自身温度的设备靠名字兜底排除：属性跟真探头完全同形，分不出来 */
const ENV_EXCLUDE_NAME = /净水器|热水器|饮水机|管线机|即热|净水机/

function descOf(p: PropDef) {
  return `${p.desc_zh || ''} ${p.desc_en || ''}`
}

/**
 * 从规格里挑出该设备能提供的环境指标。
 *
 * 温湿度要按 siid 分组挑「温+湿齐全」的那一组，否则会读到设备自身发热温度
 * 这类无效值（同一台设备可能有好几组都叫 temperature）。
 */
export function envPropsOf(spec: Spec): Partial<Record<MetricKey, PropDef>> {
  const out: Partial<Record<MetricKey, PropDef>> = {}
  const readable = spec.properties.filter((p) => p.rw.includes('r'))
  if (!readable.length) return out

  const isEnvTemp = (p: PropDef) =>
    p.base_name === 'temperature' &&
    p.type === 'float' &&
    !ENV_TEMP_DESC_BAD.test(descOf(p))
  const isEnvHum = (p: PropDef) =>
    (p.base_name === 'relative-humidity' || p.base_name === 'humidity') &&
    !ENV_TEMP_DESC_BAD.test(descOf(p))

  const temps = readable.filter(isEnvTemp)
  const hums = readable.filter(isEnvHum)
  if (temps.length || hums.length) {
    const siids = new Set<number>([...temps, ...hums].map((p) => p.siid))
    const both = [...siids].filter((siid) => {
      const t = temps.find((p) => p.siid === siid)
      const h = hums.find((p) => p.siid === siid)
      return !!t && !!h
    })
    const order = both.length ? both : [...siids]
    for (const siid of order) {
      const t = temps.find((p) => p.siid === siid)
      const h = hums.find((p) => p.siid === siid)
      if (t) out.t = t
      if (h) out.h = h
      if (t && h) break
    }
  }

  const pick = (bases: string[]): PropDef | undefined => {
    for (const b of bases) {
      const p = propBy(spec, b)
      if (p) return p
    }
    return undefined
  }
  // ⚠️ base_name 是规格里的原始短名，PM2.5 是 `pm2.5-density`（带小数点），
  // 写成 `pm2-5` 永远匹配不上。
  out.pm25 = pick(['pm2.5-density', 'pm2.5', 'pm25-density'])
  out.pm10 = pick(['pm10-density', 'pm10'])
  out.hcho = pick(['hcho-density', 'hcho'])
  out.tvoc = pick(['voc-density', 'tvoc-density'])
  out.co2 = pick(['co2-density', 'co2'])
  out.aq = pick(['air-quality'])
  return out
}

/** 读数合理区间，超出即视为脏值丢弃 */
function validFor(key: MetricKey, v: number): boolean {
  switch (key) {
    case 't':
      return v >= -10 && v <= 38
    case 'h':
      return v >= 0 && v <= 100
    case 'pm25':
      return v >= 0 && v <= 2000
    case 'pm10':
      return v >= 0 && v <= 3000
    case 'hcho':
      return v >= 0 && v <= 10
    case 'tvoc':
      return v >= 0 && v <= 20
    case 'co2':
      return v >= 300 && v <= 10000
    case 'aq':
      return v >= 0 && v <= 5
    default:
      return true
  }
}

/** 把读数写进内存 + 历史库 */
export function putReading(did: string, key: MetricKey, value: number) {
  if (!Number.isFinite(value) || !validFor(key, value)) return
  const r = envReadings[did] || (envReadings[did] = {})
  r[key] = value
  historyRecord(did, key, value)
}

/** 从抽屉读到的值同步进环境读数（并记历史） */
export function recordValues(did: string, spec: Spec, values: Record<string, unknown>) {
  const props = envPropsOf(spec)
  for (const key of Object.keys(props) as MetricKey[]) {
    const def = props[key]
    if (!def) continue
    const n = Number(values[def.name])
    if (Number.isFinite(n)) putReading(did, key, n)
  }
}

interface EnvItem {
  did: string
  siid: number
  piid: number
  key: MetricKey
}

/**
 * 批量读所有设备的环境指标。
 *
 * 一次 `prop.get` 最多 40 条，分批发；返回按三元组匹配回填。
 */
export async function loadEnvReadings(
  devices: Device[],
  specs: Map<string, Spec>,
  onProgress?: (done: number, total: number) => void
): Promise<void> {
  const items: EnvItem[] = []
  for (const d of devices) {
    const spec = specs.get(d.did)
    if (!spec) continue
    const props = envPropsOf(spec)
    for (const key of Object.keys(props) as MetricKey[]) {
      const def = props[key]
      if (!def) continue
      // 虚拟子设备要用父 did 读，siid 才是那一路编号
      const pdid = d.did.replace(/\.s\d+$/, '')
      items.push({ did: pdid, siid: def.siid, piid: def.piid, key })
    }
  }
  if (!items.length) return

  let done = 0
  for (let start = 0; start < items.length; start += 40) {
    const chunk = items.slice(start, start + 40)
    try {
      const ret = await invoke<
        { did?: string; siid?: number; piid?: number; code: number; value?: unknown }[]
      >('prop.get', { items: chunk.map(({ did, siid, piid }) => ({ did, siid, piid })) })
      const rows = Array.isArray(ret) ? ret : [ret]
      for (const row of rows) {
        if (row.code !== 0) continue
        const hit = chunk.find(
          (it) => it.did === row.did && it.siid === row.siid && it.piid === row.piid
        )
        if (!hit) continue
        const n = Number(row.value)
        if (!Number.isFinite(n)) continue
        // 写回时要用「界面上的 did」，子设备的读数要落到子设备名下
        const uiDid = devices.find((d) => d.did.replace(/\.s\d+$/, '') === hit.did)?.did || hit.did
        putReading(uiDid, hit.key, n)
      }
    } catch {
      // 某一批失败不影响其它批
    }
    done += chunk.length
    onProgress?.(done, items.length)
  }
}

// ---------------------------------------------------------------------------
// 云端历史回填
// ---------------------------------------------------------------------------

export interface CloudSeries {
  /** 界面上的 did（存盘用；虚拟子设备是 xxx.s2） */
  did: string
  /** 云端 did：子设备的读数挂在父设备上 */
  cloudDid: string
  key: MetricKey
  siid: number
  piid: number
}

/**
 * 所有设备的环境指标 → 云端回填所需的 siid/piid 对照表。
 *
 * 云端 `/user/get_user_device_data` 的 key 必须是 `siid.piid` 字符串
 * （写属性名一律返回空数组），所以这里必须逐条给出坐标。
 */
export function envSeriesOf(devices: Device[], specs: Map<string, Spec>): CloudSeries[] {
  const out: CloudSeries[] = []
  const seen = new Set<string>()
  for (const d of devices) {
    const spec = specs.get(d.did)
    if (!spec) continue
    const props = envPropsOf(spec)
    for (const key of Object.keys(props) as MetricKey[]) {
      const def = props[key]
      if (!def) continue
      const id = `${d.did}|${key}`
      if (seen.has(id)) continue
      seen.add(id)
      out.push({ did: d.did, cloudDid: d.did.replace(/\.s\d+$/, ''), key, siid: def.siid, piid: def.piid })
    }
  }
  return out
}

// ---------------------------------------------------------------------------
// 房间聚合
// ---------------------------------------------------------------------------

export interface EnvSensor {
  did: string
  name: string
  reading: EnvReading
  online: boolean
}

export interface RoomEnv {
  id: string
  name: string
  temp?: number
  humidity?: number
  /** 房间代表值（污染物取房间内最差的一台） */
  pm25?: number
  pm10?: number
  hcho?: number
  tvoc?: number
  co2?: number
  aq?: number
  /** 综合等级 0~5 */
  level: number
  sensors: EnvSensor[]
  stale: boolean
  /** 房间里的可控制设备数量（灯 / 空调 / 物理通断开关） */
  controls: number
}

function round1(v: number) {
  return Math.round(v * 10) / 10
}

function avgOf(list: (number | undefined)[]): number | undefined {
  const nums = list.filter((v): v is number => typeof v === 'number')
  if (!nums.length) return undefined
  return nums.reduce((a, b) => a + b, 0) / nums.length
}

function maxOf(list: (number | undefined)[]): number | undefined {
  const nums = list.filter((v): v is number => typeof v === 'number')
  if (!nums.length) return undefined
  return Math.max(...nums)
}

const PROBE_NAME = /温度计|温湿度|传感器|温感|探头/i

/**
 * 这台设备的温湿度算不算「环境读数」。
 *
 * 属性层已经排除掉档位/目标/机内温度等干扰项，但还有一类设备属性完全同形：
 *   净水器 1200G Pro  s2/p3 temperature(float) "Temperature / 温度" => 21
 * 描述、类型跟真探头一模一样，读到的是水路温度。
 * 可靠判据是「是否带湿度」：能读到 relative-humidity 的设备内置的就是环境探头；
 * 纯温度设备必须名字带"温度计/传感器"才算数。
 */
export function isEnvSensor(name: string, r: EnvReading): boolean {
  if (ENV_EXCLUDE_NAME.test(name)) return false
  if (r.h !== undefined) return true
  if (r.t === undefined) return false
  return PROBE_NAME.test(name)
}

/** 按房间聚合。温度/湿度取代表值，污染物取最差值。 */
export function computeRooms(
  rooms: { id: string; name: string }[],
  devices: Device[],
  readings: Record<string, EnvReading>
): RoomEnv[] {
  const out: RoomEnv[] = []
  for (const room of rooms) {
    if (room.id === '__none__') continue
    const devs = devices.filter((d) => d.room_id === room.id && readings[d.did])
    if (!devs.length) continue

    const sensors: EnvSensor[] = devs.map((d) => ({
      did: d.did,
      name: d.name,
      reading: readings[d.did],
      online: d.online
    }))
    // 温湿度只认环境探头；污染物不限（净化器/监测仪就是拿来测污染的）
    const envSensors = sensors.filter((s) => isEnvSensor(s.name, s.reading))
    if (!envSensors.length) continue

    // 离线设备读到的是最后一次上报的缓存值，混进来会把房间读数带偏
    // （实测书房有台离线温度计停在 95%，把房间湿度从 50% 拉到 62%）。
    const live = envSensors.filter((s) => s.online)
    const base = live.length ? live : envSensors

    // 房间代表值：优先专用温湿度探头（名字带"温度计/温湿度"），其次全房间平均
    const probes = base.filter((s) => PROBE_NAME.test(s.name))
    const probeT = probes.filter((s) => s.reading.t !== undefined)
    const probeH = probes.filter((s) => s.reading.h !== undefined)
    const temp = probeT.length
      ? avgOf(probeT.map((s) => s.reading.t))
      : avgOf(base.map((s) => s.reading.t))
    const humidity = probeH.length
      ? avgOf(probeH.map((s) => s.reading.h))
      : avgOf(base.map((s) => s.reading.h))

    // 污染物取最差的一台：空气质量看的是房间里最脏的地方
    const pm25 = maxOf(base.map((s) => s.reading.pm25))
    const pm10 = maxOf(base.map((s) => s.reading.pm10))
    const hcho = maxOf(base.map((s) => s.reading.hcho))
    const tvoc = maxOf(base.map((s) => s.reading.tvoc))
    const co2 = maxOf(base.map((s) => s.reading.co2))
    const aq = maxOf(base.map((s) => s.reading.aq))

    const levels = ([
      levelOf('pm25', pm25),
      levelOf('pm10', pm10),
      levelOf('hcho', hcho),
      levelOf('tvoc', tvoc),
      levelOf('co2', co2),
      levelOf('aq', aq)
    ] as (number | null)[]).filter((v): v is number => v !== null)
    const level = levels.length ? Math.max(...levels) : 0

    out.push({
      id: room.id,
      name: room.name,
      temp: temp === undefined ? undefined : round1(temp),
      humidity: humidity === undefined ? undefined : Math.round(humidity),
      pm25: pm25 === undefined ? undefined : Math.round(pm25),
      pm10: pm10 === undefined ? undefined : Math.round(pm10),
      hcho: hcho === undefined ? undefined : round1(hcho * 1000) / 1000,
      tvoc: tvoc === undefined ? undefined : round1(tvoc * 1000) / 1000,
      co2: co2 === undefined ? undefined : Math.round(co2),
      aq: aq === undefined ? undefined : Math.round(aq),
      level,
      sensors,
      stale: !live.length,
      controls: 0
    })
  }
  return out
}

// ---------------------------------------------------------------------------
// 传感器钉子板的候选读数
// ---------------------------------------------------------------------------

/** 一个可以钉在环境页上的读数（设备 + 指标） */
export interface SensorPinCandidate {
  /** `${did}:${key}` —— 同一台设备的温、湿是两条独立的钉子 */
  id: string
  did: string
  key: MetricKey
  /** 设备名 */
  device: string
  room: string
  online: boolean
  value?: number
}

/** 指标在挑选面板里的顺序：常见的排前面 */
const PIN_KEY_ORDER: MetricKey[] = ['t', 'h', 'pm25', 'pm10', 'hcho', 'tvoc', 'co2', 'aq']

/**
 * 列出所有可钉的读数。
 *
 * 判据不是「设备品类」而是「现在真的读到了什么」—— 这样空气监测仪、
 * 空调回风口、净化器上的探头都能钉，不会因为品类没覆盖到就漏掉。
 * 同时排除掉「读到了温度但其实是水路/机内温度」的设备（净水器那类），
 * 否则挑选面板里会出现一堆和室温无关的数字。
 */
export function buildPinCandidates(
  devices: Device[],
  readings: Record<string, EnvReading>
): SensorPinCandidate[] {
  const out: SensorPinCandidate[] = []
  for (const d of devices) {
    const r = readings[d.did]
    if (!r) continue
    const keys = PIN_KEY_ORDER.filter((k) => r[k] !== undefined)
    if (!keys.length) continue
    // 只有温湿度读数时，必须是真正的环境探头才算数
    const onlyTH = keys.every((k) => k === 't' || k === 'h')
    if (onlyTH && !isEnvSensor(d.name, r)) continue
    for (const key of keys) {
      out.push({
        id: `${d.did}:${key}`,
        did: d.did,
        key,
        device: d.name,
        room: d.room_name || '未分配',
        online: d.online,
        value: r[key]
      })
    }
  }
  // 顺序稳定：按设备名排，同设备内按指标顺序（米家返回的设备顺序会变）
  return out.sort((a, b) => {
    const byName = a.device.localeCompare(b.device, 'zh-Hans-CN')
    if (byName) return byName
    return PIN_KEY_ORDER.indexOf(a.key) - PIN_KEY_ORDER.indexOf(b.key)
  })
}

// ---------------------------------------------------------------------------
// 设备快捷控制（吸顶灯 / 物理通断开关 / 空调）
// ---------------------------------------------------------------------------

export type EnvControlKind = 'light' | 'switch' | 'ac'

/** 可在控制中心里直接调节的属性 */
export interface EnvAdjust {
  /** 'range' = 数值滑杆，'enum' = 枚举按钮组 */
  kind: 'range' | 'enum'
  /** 属性的完整定义（siid/piid/range/value_list 都在里面） */
  def: PropDef
  label: string
  value: unknown
  min: number
  max: number
  step: number
  /** 枚举项（kind==='enum'） */
  options?: { value: number | string; label: string }[]
}

export interface EnvControl {
  device: Device
  kind: EnvControlKind
  name: string
  roomName: string
  /** 开关电源属性所在的 siid/piid（虚拟子设备是面板里的某一路） */
  siid: number
  piid: number
  online: boolean
  /** 仅 switch：这一路是不是无线开关（无线的不接电路，物理通断的才显示） */
  wireless: boolean
  on: boolean | null
  /** 摘要：亮度/色温 或 空调模式/目标温度 */
  detail: string
  /** 可直接调节的属性（亮度 / 色温 / 空调模式 / 目标温度） */
  adjust: EnvAdjust[]
}

const CEILING_MODEL = /ceiling|leshi\.light/i

/** 吸顶灯：yeelink.ceiling19/20、乐喜 wy0b01 这类装在天花板上的灯 */
export function isCeilingLight(device: Device): boolean {
  if (CEILING_MODEL.test(device.model)) return true
  return /吸顶/.test(device.name)
}

/** 数值属性的缺省范围（规格里没写 value-range 时兜底） */
const DEFAULT_RANGE: Record<string, [number, number, number]> = {
  brightness: [1, 100, 1],
  'color-temperature': [2700, 6500, 50],
  'target-temperature': [16, 32, 1]
}

/** 从属性定义里取出可调区间 */
function rangeOf(def: PropDef): [number, number, number] {
  const r = def.range
  if (r && r.length >= 2 && Number.isFinite(r[0]) && Number.isFinite(r[1]) && r[1] > r[0]) {
    return [r[0], r[1], r.length >= 3 && r[2] > 0 ? r[2] : 1]
  }
  return DEFAULT_RANGE[def.base_name] || [0, 100, 1]
}

/** 造一个数值调节项 */
function rangeAdjust(def: PropDef, value: unknown, label: string): EnvAdjust {
  const [min, max, step] = rangeOf(def)
  return { kind: 'range', def, label, value, min, max, step }
}

/** 造一个枚举调节项（空调模式 / 灯光场景） */
function enumAdjust(def: PropDef, value: unknown, label: string): EnvAdjust | null {
  const list = def.value_list
  if (!list || !list.length) return null
  return {
    kind: 'enum',
    def,
    label,
    value,
    min: 0,
    max: 0,
    step: 1,
    options: list.map((it) => ({ value: it.value, label: valueLabel(it) }))
  }
}

/**
 * 某个开关设备的这一路是「物理通断」还是「无线开关」。
 *
 * 判据与卡片/抽屉完全一致（spec.ts 的 switchChannels + mode 枚举），
 * 免得环境页说「物理通断」而抽屉里写「无线开关」。
 */
export function channelIsWireless(
  spec: Spec,
  did: string,
  modeValue: number | undefined
): boolean {
  const form = switchForm(spec)
  if (form === 'remote') return true
  const ch = switchChannels(spec, did)[0]
  if (!ch || !ch.on) return true
  if (!ch.mode) return false
  if (modeValue === undefined) return false
  const hit = ch.mode.value_list?.find((v) => String(v.value) === String(modeValue))
  if (!hit) return String(modeValue) === '1'
  const zh = hit.desc_zh || ''
  if (zh) return /无线/.test(zh) && !/有线/.test(zh)
  return /^wireless$/i.test((hit.description || '').trim())
}

/** 房间里的吸顶灯 / 物理通断开关 / 空调，供环境页做快捷开关 */
export function buildControls(
  devices: Device[],
  specs: Map<string, Spec>,
  states: {
    powers: Record<string, boolean | null>
    switchMode: Record<string, number>
    lightState: Record<string, { br?: number; ct?: number; mode?: string; modeV?: number | string }>
    acState: Record<string, { temp?: number; mode?: string; modeV?: number | string }>
  }
): EnvControl[] {
  const out: EnvControl[] = []
  for (const d of devices) {
    const spec = specs.get(d.did)
    if (!spec) continue
    const on = states.powers[d.did] ?? null

    if (isAirConditioner(spec)) {
      const onProp = spec.properties.find((p) => p.base_name === 'on' && p.rw.includes('w'))
      if (!onProp) continue
      const ac = states.acState[d.did]
      const detail = [ac?.mode, ac?.temp !== undefined ? `${ac.temp}°C` : '']
        .filter(Boolean)
        .join(' ')
      // 空调可在控制中心里直接调模式与目标温度
      const adjust: EnvAdjust[] = []
      const modeProp = propBy(spec, 'mode')
      if (modeProp?.rw.includes('w')) {
        const a = enumAdjust(modeProp, ac?.modeV, '模式')
        if (a) adjust.push(a)
      }
      const tProp = propBy(spec, 'target-temperature')
      if (tProp?.rw.includes('w')) {
        adjust.push(rangeAdjust(tProp, ac?.temp, '目标温度'))
      }
      out.push({
        device: d,
        kind: 'ac',
        name: d.name,
        roomName: d.room_name || '未分配',
        siid: onProp.siid,
        piid: onProp.piid,
        online: d.online,
        wireless: false,
        on,
        detail,
        adjust
      })
      continue
    }

    if (isLight(spec)) {
      if (!isCeilingLight(d)) continue
      const onProp = spec.properties.find((p) => p.base_name === 'on' && p.rw.includes('w'))
      if (!onProp) continue
      const li = states.lightState[d.did]
      const night = !!li?.mode && /月光|夜间|night/i.test(li.mode)
      const parts: string[] = []
      if (night) parts.push('月光')
      if (li?.br !== undefined) parts.push(`${Math.round(li.br)}%`)
      if (!night && li?.ct !== undefined) parts.push(`${Math.round(li.ct)}K`)
      // 灯光可调亮度；月光模式下色温不可调（设备会自己接管）
      const adjust: EnvAdjust[] = []
      const brProp = propBy(spec, 'brightness')
      if (brProp?.rw.includes('w')) adjust.push(rangeAdjust(brProp, li?.br, '亮度'))
      const ctProp = propBy(spec, 'color-temperature')
      if (ctProp?.rw.includes('w') && !night) {
        adjust.push(rangeAdjust(ctProp, li?.ct, '色温'))
      }
      const mdProp = propBy(spec, 'mode')
      if (mdProp?.rw.includes('w')) {
        const a = enumAdjust(mdProp, li?.modeV, '场景')
        if (a) adjust.push(a)
      }
      out.push({
        device: d,
        kind: 'light',
        name: d.name,
        roomName: d.room_name || '未分配',
        siid: onProp.siid,
        piid: onProp.piid,
        online: d.online,
        wireless: false,
        on,
        detail: parts.join(' · '),
        adjust
      })
      continue
    }

    // 开关类：只留物理通断那一路
    const form = switchForm(spec)
    if (!form || form === 'remote') continue
    const ch = switchChannels(spec, d.did)[0]
    if (!ch || !ch.on) continue
    if (channelIsWireless(spec, d.did, states.switchMode[d.did])) continue
    out.push({
      device: d,
      kind: 'switch',
      name: d.name,
      roomName: d.room_name || '未分配',
      siid: ch.on.siid,
      piid: ch.on.piid,
      online: d.online,
      wireless: false,
      on,
      detail: '',
      adjust: []
    })
  }
  // 顺序：吸顶灯 → 物理通断开关 → 空调，同类内保持房间里的原有次序
  const order: Record<EnvControlKind, number> = { light: 0, switch: 1, ac: 2 }
  return out.sort((a, b) => order[a.kind] - order[b.kind])
}

/** 某设备某指标的历史曲线 */
export function trendOf(did: string, key: MetricKey, hours = 24) {
  return historySeries(did, key, hours)
}

/**
 * 多台设备的同指标曲线合并（房间级 / 全屋级曲线）。
 *
 * 各设备采样时刻不完全一致，按时间桶取平均即可，
 * 直接按数组下标合并会得到锯齿状的假曲线。
 */
export function mergeTrend(
  dids: string[],
  key: MetricKey,
  hours = 24,
  bucketSec = 300
): Pt[] {
  const buckets = new Map<number, { sum: number; n: number }>()
  for (const did of dids) {
    for (const p of historySeries(did, key, hours)) {
      const b = Math.floor(p.t / bucketSec) * bucketSec
      const cur = buckets.get(b) || { sum: 0, n: 0 }
      cur.sum += p.v
      cur.n++
      buckets.set(b, cur)
    }
  }
  return [...buckets.entries()]
    .sort((a, b) => a[0] - b[0])
    .map(([t, v]) => ({ t, v: v.sum / v.n }))
}
