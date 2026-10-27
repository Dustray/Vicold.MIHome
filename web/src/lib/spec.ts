import type { PropDef, Spec } from './types'
import { actionLabel, serviceLabel } from './zh'
import { findOnProp, gangOf, isPowerProp } from './device'

/** 按功能模块（service）聚合后的分组，供抽屉与专属面板共用 */
export interface SpecGroup {
  key: string
  title: string
  writable: PropDef[]
  readable: PropDef[]
  actions: { siid: number; aiid: number; name: string }[]
}

/**
 * 把规格按功能模块分组，保留 miot-spec 里的官方顺序。
 *
 * - 主开关（on）会被调用方单独展示，这里仍返回，由调用方按需剔除；
 * - 虚拟子设备（多键面板的某一路，did 形如 `xxx.s2`）只返回它自己那一路。
 */
export function buildGroups(spec: Spec | null, did: string): SpecGroup[] {
  if (!spec) return []

  const order: number[] = []
  const titleOf = new Map<number, string>()
  for (const svc of spec.services || []) {
    if (!order.includes(svc.siid)) order.push(svc.siid)
    titleOf.set(svc.siid, serviceLabel(svc))
  }
  for (const p of spec.properties) {
    if (!order.includes(p.siid)) {
      order.push(p.siid)
      titleOf.set(p.siid, serviceLabel({ name: p.service, desc_zh: p.service_zh }))
    }
  }

  const map = new Map<number, SpecGroup>()
  for (const siid of order) {
    map.set(siid, { key: String(siid), title: titleOf.get(siid) || '其他', writable: [], readable: [], actions: [] })
  }

  for (const p of spec.properties) {
    const g = map.get(p.siid)
    if (!g) continue
    if (p.rw.includes('w')) g.writable.push(p)
    else if (p.rw.includes('r')) g.readable.push(p)
  }
  for (const a of spec.actions) {
    const g = map.get(a.siid)
    if (!g) continue
    g.actions.push({ siid: a.siid, aiid: a.aiid, name: actionLabel(a) })
  }

  const list = [...map.values()].filter((g) => g.writable.length || g.readable.length || g.actions.length)

  const gang = gangOf(did)
  if (gang !== null) {
    const own = list.filter((g) => Number(g.key) === gang)
    if (own.length) return own
  }
  return list
}

/** 按 base_name 取属性，可限定 siid（同名属性去重后 name 会变，逻辑匹配一律用 base_name） */
export function propBy(spec: Spec | null, base: string, siid?: number): PropDef | null {
  if (!spec) return null
  return (
    spec.properties.find(
      (p) => p.base_name === base && (siid === undefined || p.siid === siid)
    ) || null
  )
}

/** 按 base_name 取全部同名属性（同 siid 下可能有多个，如三套滤芯） */
export function propsBy(spec: Spec | null, base: string, siid?: number): PropDef[] {
  if (!spec) return []
  return spec.properties.filter(
    (p) => p.base_name === base && (siid === undefined || p.siid === siid)
  )
}

/** 规格类型里带 `device:<name>` 的设备类别 */
export function deviceCategory(spec: Spec | null): string {
  if (!spec) return ''
  const m = /:device:([^:]+):/.exec(spec.spec_type || '')
  return m ? m[1] : ''
}

/**
 * 判定空气净化器。
 * 优先看规格类型；老规格类型缺失时退回关键属性组合
 * （模式 + 风机档位 + 空气质量，这三样同时有基本只可能是空净）。
 */
export function isAirPurifier(spec: Spec | null): boolean {
  if (!spec) return false
  const cat = deviceCategory(spec)
  if (cat) return cat === 'air-purifier'
  const names = new Set(spec.properties.map((p) => p.base_name))
  return names.has('mode') && names.has('fan-level') && names.has('air-quality')
}

/** 主开关所在 siid（即带 mode 的那个 air-purifier 模块） */
export function powerSiid(spec: Spec | null): number | null {
  const mode = propBy(spec, 'mode')
  if (mode) return mode.siid
  const on = findOnProp(spec, null)
  return on ? on.siid : null
}

/**
 * 判定灯具（吸顶灯/氛围灯/阅读灯等）。
 * 优先看规格类型（`device:light`）；类型缺失时退回「有亮度可写」。
 */
export function isLight(spec: Spec | null): boolean {
  if (!spec) return false
  const cat = deviceCategory(spec)
  if (cat) return cat === 'light'
  return spec.properties.some((p) => p.base_name === 'brightness' && p.rw.includes('w'))
}

/**
 * 判定空调。
 * 优先看规格类型（`device:air-conditioner`）；类型缺失时退回关键属性组合
 * （mode + target-temperature 同时有，基本只可能是空调/温控器）。
 */
export function isAirConditioner(spec: Spec | null): boolean {
  if (!spec) return false
  const cat = deviceCategory(spec)
  if (cat) return cat === 'air-conditioner'
  const names = new Set(spec.properties.map((p) => p.base_name))
  return names.has('mode') && names.has('target-temperature') && names.has('fan-level')
}

/**
 * 判定温湿度计（纯读数传感器，没有开关）。
 *
 * 只看规格类型：`device:temperature-humidity-sensor`。
 * **不能**靠「有温度属性」判断——空调、净化器、净水器、热水器都带温度，
 * 但它们读的是机身/水路温度，不是室温，也不该套用传感器的仪表盘。
 */
export function isTempHumiditySensor(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'temperature-humidity-sensor'
}

/** 温度计的核心读数属性（温/湿/CO₂/电池），面板与卡片摘要共用一套口径 */
export function sensorProps(spec: Spec | null): {
  temp: PropDef | null
  humidity: PropDef | null
  co2: PropDef | null
  battery: PropDef | null
} {
  return {
    temp: propBy(spec, 'temperature'),
    humidity: propBy(spec, 'relative-humidity') || propBy(spec, 'humidity'),
    co2: propBy(spec, 'co2-density'),
    battery: propBy(spec, 'battery-level'),
  }
}

/**
 * 判定空气质量监测仪（`device:air-monitor`，甲醛 / PM2.5 / TVOC 这类仪器）。
 *
 * 只看规格类型。**不能**靠"带温湿度"判断——温湿度计也有温湿度，
 * 但那台设备的重点是体感，这台的重点是污染物浓度，面板口径完全不同。
 */
export function isAirMonitor(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'air-monitor'
}

/**
 * 空气质量监测仪的核心读数。
 *
 * ⚠️ 甲醛的 base_name 是 `hcho-density`（HCHO＝甲醛），
 * **不是** `pm2-5`：杜克 HF1 只测甲醛，不测颗粒物，
 * 换机型时按哪个有就显示哪个，别写死成 PM2.5 口径。
 */
export function airProps(spec: Spec | null): {
  hcho: PropDef | null
  pm25: PropDef | null
  pm10: PropDef | null
  tvoc: PropDef | null
  temp: PropDef | null
  humidity: PropDef | null
  battery: PropDef | null
} {
  return {
    hcho: propBy(spec, 'hcho-density'),
    // ⚠️ 规格里的原始短名是 `pm2.5-density`（带小数点），写成 `pm2-5` 永远匹配不上
    pm25: propBy(spec, 'pm2.5-density') || propBy(spec, 'pm2.5') || propBy(spec, 'pm25-density'),
    pm10: propBy(spec, 'pm10-density'),
    tvoc: propBy(spec, 'voc-density') || propBy(spec, 'tvoc-density'),
    temp: propBy(spec, 'temperature'),
    humidity: propBy(spec, 'relative-humidity') || propBy(spec, 'humidity'),
    battery: propBy(spec, 'battery-level'),
  }
}

/**
 * 主污染物：甲醛 > PM2.5 > TVOC。
 *
 * 卡片摘要和面板必须走同一个优先级，否则会出现
 * "面板显示甲醛 0.02，卡片却显示 PM2.5 12" 这种自相矛盾。
 */
export function mainPollutant(spec: Spec | null): {
  def: PropDef
  key: 'hcho' | 'pm25' | 'tvoc'
  label: string
} | null {
  const c = airProps(spec)
  if (c.hcho) return { def: c.hcho, key: 'hcho', label: '甲醛' }
  if (c.pm25) return { def: c.pm25, key: 'pm25', label: 'PM2.5' }
  if (c.tvoc) return { def: c.tvoc, key: 'tvoc', label: 'TVOC' }
  return null
}

// ---------------------------------------------------------------------------
// 窗帘 / 晾衣架 / 加湿器 / 净水器 / 存在传感器
// ---------------------------------------------------------------------------

/** 判定电动窗帘（`device:curtain`）。 */
export function isCurtain(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'curtain'
}

/**
 * 窗帘的核心属性。
 *
 * ⚠️ **两个型号的 `motor-control` 枚举含义完全不同**：
 *  - bjkcz.curtain.kcz4：1=打开 2=关闭 0=暂停 3=开/停/关 4=校准
 *  - xiaomi.curtain.acn012：0=打开 1=关闭 2=暂停 3=开/停/关
 * 所以**绝不能写死 value**，必须按中文标签匹配（见 CurtainPanel::motorValue）。
 */
export function curtainProps(spec: Spec | null): {
  motor: PropDef | null
  status: PropDef | null
  current: PropDef | null
  target: PropDef | null
  speed: PropDef | null
  reverse: PropDef | null
  fault: PropDef | null
} {
  return {
    motor: propBy(spec, 'motor-control'),
    status: propBy(spec, 'status'),
    current: propBy(spec, 'current-position'),
    target: propBy(spec, 'target-position'),
    speed: propBy(spec, 'speed-level'),
    reverse: propBy(spec, 'motor-reverse'),
    fault: propBy(spec, 'fault'),
  }
}

/** 判定晾衣架（`device:airer`）。 */
export function isAirer(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'airer'
}

/**
 * 晾衣架的核心属性。
 *
 * ⚠️ **两个型号的 `motor-control` 枚举含义完全不同**：
 *  - xiaomi.airer.1sfun：0=暂停 1=升高 2=降低
 *  - ouchen.airer.m01：0=升高 1=暂停 2=降低
 * 同样必须按中文标签匹配，不能写死 value。
 *
 * ⚠️ `target-position` 的方向是 **0=最高、100=最低**（跟窗帘正好相反），
 * 展示时必须翻转成「升起高度百分比」。
 */
export function airerProps(spec: Spec | null): {
  motor: PropDef | null
  status: PropDef | null
  target: PropDef | null
  current: PropDef | null
  dryer: PropDef | null
  uv: PropDef | null
  light: PropDef | null
  brightness: PropDef | null
  fault: PropDef | null
} {
  const light = propBy(spec, 'on', 3)
  return {
    motor: propBy(spec, 'motor-control'),
    status: propBy(spec, 'status'),
    target: propBy(spec, 'target-position'),
    current: propBy(spec, 'current-position'),
    dryer: propBy(spec, 'dryer'),
    uv: propBy(spec, 'uv'),
    light: light && light.rw.includes('w') ? light : null,
    brightness: light ? propBy(spec, 'brightness', light.siid) : null,
    fault: propBy(spec, 'fault'),
  }
}

/** 判定加湿器（`device:humidifier`）。 */
export function isHumidifier(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'humidifier'
}

export function humidifierProps(spec: Spec | null): {
  on: PropDef | null
  mode: PropDef | null
  target: PropDef | null
  humidity: PropDef | null
  temperature: PropDef | null
  waterLevel: PropDef | null
  waterStatus: PropDef | null
  filter: PropDef | null
  fault: PropDef | null
} {
  return {
    on: propBy(spec, 'on', 2),
    mode: propBy(spec, 'mode'),
    target: propBy(spec, 'target-humidity'),
    humidity: propBy(spec, 'relative-humidity', 3),
    temperature: propBy(spec, 'temperature', 3),
    waterLevel: propBy(spec, 'water-level'),
    waterStatus: propBy(spec, 'water-status'),
    filter: propBy(spec, 'filter-life-level'),
    fault: propBy(spec, 'fault'),
  }
}

/** 判定净水器（`device:water-purifier`）。 */
export function isWaterPurifier(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'water-purifier'
}

export function waterPurifierProps(spec: Spec | null): {
  status: PropDef | null
  tdsIn: PropDef | null
  tdsOut: PropDef | null
  filters: PropDef[]
  cup: PropDef | null
  oldWater: PropDef | null
  temperature: PropDef | null
  fault: PropDef | null
} {
  return {
    status: propBy(spec, 'status'),
    tdsIn: propBy(spec, 'tds-in'),
    tdsOut: propBy(spec, 'tds-out'),
    filters: propsBy(spec, 'filter-life-level'),
    cup: propBy(spec, 'out-water-cup'),
    oldWater: propBy(spec, 'no-old-water-mode'),
    // ⚠️ 这是水路温度，不是室温
    temperature: propBy(spec, 'temperature'),
    fault: propBy(spec, 'fault'),
  }
}

/**
 * 判定智能鱼缸（`device:fish-tank`）。
 *
 * 只看规格类型。鱼缸的 `temperature` 是**缸内水温**（不是室温），
 * 所以不能靠"有温度属性"判断——净水器、饮水机也都有温度。
 */
export function isFishTank(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'fish-tank'
}

/**
 * 在指定功能模块（service）里取属性。
 *
 * ⚠️ 鱼缸这类设备有**三个** `on`：鱼缸总电源（fish-tank）、缸内灯（light）、
 *    指示灯（indicator-light）。展平后名字是 on-2 / on-3 / on-8，
 *    只按 base_name 取第一条会拿错，所以灯这类要按 service 定位。
 */
export function propInService(spec: Spec | null, base: string, service: string): PropDef | null {
  if (!spec) return null
  return (
    spec.properties.find((p) => p.base_name === base && p.service === service) || null
  )
}

/**
 * 鱼缸的缸内灯（light 模块）。
 *
 * 灯是这个品类的主功能之一（模式有日光/流光/彩灯/夜灯，还有亮度与流速），
 * 不该埋在通用分组里。
 */
export function fishTankLight(spec: Spec | null): {
  on: PropDef | null
  mode: PropDef | null
  brightness: PropDef | null
  flowSpeed: PropDef | null
} {
  return {
    on: propInService(spec, 'on', 'light'),
    mode: propInService(spec, 'mode', 'light'),
    brightness: propInService(spec, 'brightness', 'light'),
    flowSpeed: propInService(spec, 'flow-speed-level', 'light'),
  }
}

/**
 * 鱼缸的喂食统计（feeder 模块）。
 *
 * - `feeder-status`：投喂机忙闲（Idle / Busy），官方没给中文；
 * - `today-feeded-num`：今日已投喂份数。
 *
 * ⚠️ 同模块的 feed-target-num / feed-success-num 的 access 是空数组，读不到。
 */
export function fishTankFeeder(spec: Spec | null): {
  status: PropDef | null
  todayNum: PropDef | null
} {
  return {
    status: propInService(spec, 'feeder-status', 'feeder'),
    todayNum: propInService(spec, 'today-feeded-num', 'feeder'),
  }
}

/**
 * 鱼缸的核心属性。
 *
 * ⚠️ **缸内水温不是室温**：规格里 comment 是「鱼缸当前温度」，
 *    环境页的取数逻辑已经用「鱼缸」把它排除掉了，这里也别当成室温展示。
 *
 * ⚠️ `feeding-measure`（出粮量）**没有 read/write 权限**（access 为空数组），
 *    它只是「喂食」动作的入参（action 的 `in` 字段指向 piid 5）。
 *    所以出粮量不能当普通属性设置，只能作为动作参数下发。
 */
export function fishTankProps(spec: Spec | null): {
  on: PropDef | null
  pump: PropDef | null
  flux: PropDef | null
  pumpStatus: PropDef | null
  feedMeasure: PropDef | null
  temperature: PropDef | null
  feedAction: { siid: number; aiid: number; name: string } | null
  /**
   * 异常位图（siid 12 `trigger-status`）：按 bit 从低到高——
   * 1 灯照时间过长 / 2 水泵故障·低水位·堵转 / 3 水泵未连接 /
   * 4 今日喂食份数过多 / 5 滤材过期 / 6 水温提醒。
   *
   * ⚠️ 这个属性比 `water-pump-status` 覆盖面更广（多了滤材过期、水温提醒），
   *    两者要一起看：状态位给「当前工况」，位图给「需要用户处理的异常」。
   */
  exception: PropDef | null
} {
  const s = spec
  if (!s) {
    return {
      on: null,
      pump: null,
      flux: null,
      pumpStatus: null,
      feedMeasure: null,
      temperature: null,
      feedAction: null,
      exception: null,
    }
  }
  // 喂食动作：名字里带 feed / food 的那个（鱼缸叫 pet-food-out）
  const act =
    s.actions.find((a) => /feed|food/i.test(a.name)) ||
    s.actions.find((a) => /喂食|出粮/.test(a.desc_zh || '')) ||
    null
  return {
    on: propBy(s, 'on'),
    pump: propBy(s, 'water-pump'),
    flux: propBy(s, 'pump-flux'),
    pumpStatus: propBy(s, 'water-pump-status'),
    feedMeasure: propBy(s, 'feeding-measure'),
    temperature: propBy(s, 'temperature'),
    feedAction: act ? { siid: act.siid, aiid: act.aiid, name: act.name } : null,
    exception: propBy(s, 'trigger-status'),
  }
}

/**
 * 判定「存在类」传感器：`device:occupancy-sensor` / `device:motion-sensor`。
 *
 * 这两类都是「有没有人」的门磁/人体感应，界面口径一致，
 * 合成一个面板；但型号差异很大（有的一个可写属性都没有），面板按属性存在与否渲染。
 */
export function isPresenceSensor(spec: Spec | null): boolean {
  if (!spec) return false
  const cat = deviceCategory(spec)
  return cat === 'occupancy-sensor' || cat === 'motion-sensor'
}

export function presenceProps(spec: Spec | null): {
  occupancy: PropDef | null
  illumination: PropDef | null
  battery: PropDef | null
  distance: PropDef | null
  hasDuration: PropDef | null
  noDuration: PropDef | null
  noMotion: PropDef | null
  sensitivity: PropDef | null
} {
  return {
    occupancy: propBy(spec, 'occupancy-status'),
    illumination: propBy(spec, 'illumination'),
    battery: propBy(spec, 'battery-level'),
    distance: propBy(spec, 'body-distance'),
    hasDuration: propBy(spec, 'has-someone-duration'),
    noDuration: propBy(spec, 'no-one-duration'),
    noMotion: propBy(spec, 'no-motion-duration'),
    // 灵敏度在不同型号里叫 sensitivity / sensitivity-level
    sensitivity: propBy(spec, 'sensitivity') || propBy(spec, 'sensitivity-level'),
  }
}

/**
 * 判定智能音箱 / 家庭屏（`device:speaker`）。
 *
 * 只看规格类型。**不能**靠「有 volume 属性」判断——电视、摄像头、门锁、
 * 门铃都带各自的音量属性，套上媒体面板会显得莫名其妙。
 */
export function isSpeaker(spec: Spec | null): boolean {
  if (!spec) return false
  return deviceCategory(spec) === 'speaker'
}

/**
 * 音箱的核心属性。
 *
 * ⚠️ `mute` 在规格里会出现**两次**（扬声器静音 + 麦克风静音），
 * base_name 完全相同，只能按所属模块区分：
 *  - x8f 家庭屏：只有麦克风静音；
 *  - x4b / lx06：扬声器、麦克风各一个。
 * 若只按 base_name 取第一条，两个静音会互相覆盖，用户控制麦克风时关掉的是喇叭。
 */
export function speakerProps(spec: Spec | null): {
  volume: PropDef | null
  playing: PropDef | null
  micMute: PropDef | null
  spkMute: PropDef | null
  noDisturb: PropDef | null
  sleepMode: PropDef | null
  alarmOn: PropDef | null
  loopMode: PropDef | null
} {
  const pick = (base: string, svc?: string): PropDef | null => {
    if (!spec) return null
    return (
      spec.properties.find(
        (p) => p.base_name === base && (svc === undefined || p.service === svc)
      ) || null
    )
  }
  return {
    volume: pick('volume'),
    playing: pick('playing-state'),
    micMute: pick('mute', 'microphone'),
    spkMute: pick('mute', 'speaker'),
    noDisturb: pick('no-disturb'),
    sleepMode: pick('sleep-mode'),
    alarmOn: pick('on', 'clock'),
    loopMode: pick('play-loop-mode'),
  }
}

// ---------------------------------------------------------------------------
// 开关类设备
// ---------------------------------------------------------------------------

/**
 * 开关类设备的物理形态。
 *
 * - `wall`   墙面开关（86 型翘板按键，1–3 键）
 * - `plug`   智能插座（通断整个插孔回路）
 * - `remote` 无线开关 / 场景遥控（只有按键事件，不接任何电路）
 */
export type SwitchForm = 'wall' | 'plug' | 'remote'

/** 归入开关界面的规格品类。`control-panel` 是带屏 86 面板，按键同样走强电。 */
const SWITCH_CATS = new Set(['switch', 'outlet', 'remote-control', 'control-panel'])

export function switchForm(spec: Spec | null): SwitchForm | null {
  const cat = deviceCategory(spec)
  if (!cat || !SWITCH_CATS.has(cat)) return null
  if (cat === 'remote-control') return 'remote'
  if (cat === 'outlet') return 'plug'
  return 'wall'
}

/**
 * 一路开关。
 *
 * `on` 为 null 表示这一路**没有继电器**（纯无线），只能靠物理按键发出事件。
 * `mode` 是厂商给的通道模式，取值见 `MODE_*`。`localKey` 表示该路还带一个
 * 本地按键事件模块（`switch-sensor`），即"这个按键真实存在，可以按"。
 */
export interface SwitchChannel {
  siid: number
  on: PropDef | null
  mode: PropDef | null
  fault: PropDef | null
  localKey: boolean
  /** 面板上的第几路，从 0 开始 */
  index: number
}

/** 规格里 `switch.mode` 的枚举：0=有线和无线开关（物理通断），1=无线开关。 */
export function isWirelessModeItem(item: { description?: string; desc_zh?: string }): boolean {
  const zh = item.desc_zh || ''
  if (zh) return /无线/.test(zh) && !/有线/.test(zh)
  return /^wireless$/i.test((item.description || '').trim())
}

/** 该设备是不是开关类（开关 / 插座 / 无线开关 / 带屏面板） */
export function isSwitchDevice(spec: Spec | null): boolean {
  if (!switchForm(spec)) return false
  return switchChannels(spec, null).length > 0
}

/**
 * 拆出设备的所有开关通道。
 *
 * 多键面板在云端会被派生为若干虚拟子设备（did 形如 `xxx.s3`），
 * 传了 did 时只返回它自己那一路——后缀 N 就是该路的 siid。
 *
 * ⚠️ 纯无线开关（`remote-control`）**没有任何** `on` 可写属性，
 * 但它的按键是真实存在的，因此没有继电器时也要给出一路占位通道，
 * 否则面板会渲染成空白。
 */
export function switchChannels(spec: Spec | null, did: string | null | undefined): SwitchChannel[] {
  if (!spec) return []
  const ons = spec.properties.filter((p) => isPowerProp(p) && p.rw.includes('w'))

  // 本地按键事件模块按出现顺序与开关通道一一对应（siid 并不相同：
  // 开关是 2/3/4，按键传感器是 5/6/7，不能按 siid 直接对）
  const keyCount = (spec.services || []).filter((s) => /^switch-sensor/.test(s.name)).length

  const build = (on: PropDef | null, siid: number, index: number): SwitchChannel => ({
    siid,
    on,
    mode: propBy(spec, 'mode', siid),
    fault: propBy(spec, 'fault', siid),
    localKey: index < keyCount,
    index,
  })

  let list: SwitchChannel[]
  if (ons.length) {
    list = ons.map((on, i) => build(on, on.siid, i))
  } else if (switchForm(spec) === 'remote') {
    // 无线遥控：没有可选中的通道，用第一个按键传感器模块的 siid 占位
    const sensor = (spec.services || []).find((s) => /^switch-sensor/.test(s.name))
    list = [build(null, sensor ? sensor.siid : 0, 0)]
  } else {
    return []
  }

  const gang = gangOf(did)
  if (gang !== null) {
    const own = list.find((c) => c.siid === gang)
    if (own) return [own]
    // 后缀对不上 siid 时按顺序兜底：.s2 → 第 1 路、.s3 → 第 2 路……
    const idx = gang - 2
    if (idx >= 0 && idx < list.length) return [list[idx]]
  }
  return list
}

/**
 * 这一路是「物理通断」还是「无线开关」。
 *
 * 判据优先级：
 *  1. 没有继电器（无 on）→ 必然是无线开关；
 *  2. 有 `mode` 通道模式 → 按规格枚举判（厂商在这里明确写了无线＝不通断电路）；
 *  3. 没有 mode（如插座）→ 物理通断。
 *
 * 读不到 mode 值时按物理通断处理：绝大多数开关出货默认是"有线和无线"模式，
 * 反而是误判成无线会让用户以为按键点不动。
 */
export function channelWireless(
  ch: SwitchChannel,
  values: Record<string, unknown>
): boolean {
  if (!ch.on) return true
  return channelIsWirelessMode(ch, values)
}

/** 该路在配置上被切成无线模式（这种按键按下去不会有继电器动作） */
export function channelIsWirelessMode(ch: SwitchChannel, values: Record<string, unknown>): boolean {
  if (!ch.mode) return false
  const raw = values[ch.mode.name]
  if (raw === undefined || raw === null) return false
  const hit = ch.mode.value_list?.find((v) => String(v.value) === String(raw))
  return hit ? isWirelessModeItem(hit) : String(raw) === '1'
}

/**
 * 卡片的类型标注（不打开抽屉也能给）。
 *
 * 只依赖 `mode` 的已读值：读到了就按真实模式标，没读到（还没预取到）
 * 就退回设备品类——插座一定是物理通断，无线遥控一定是无线。
 */
export function cardSwitchWireless(
  spec: Spec | null,
  did: string | null | undefined,
  values: Record<string, unknown> | undefined
): boolean | null {
  const form = switchForm(spec)
  if (!form) return null
  if (form === 'remote') return true
  const ch = switchChannels(spec, did)[0]
  if (!ch) return form === 'plug' ? false : null
  if (!ch.on) return true
  if (!values || !ch.mode) return form === 'plug' ? false : null
  return channelWireless(ch, values)
}
