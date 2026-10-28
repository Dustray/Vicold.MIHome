import type { Device, DeviceChannel, PropDef, Spec } from './types'

/**
 * 虚拟子设备的 did 后缀 → siid。
 *
 * 米家云端把多键面板（一开多路）派生成若干虚拟子设备，did 形如
 * `888543391.s2`、`.s3`，**后缀的 N 就是该路在规格里对应的 siid**。
 * 单键设备与父设备本身没有后缀，返回 null。
 */
const SUB_DEVICE_SUFFIX = /\.s(\d+)$/

export function gangOf(did: string | undefined | null): number | null {
  if (!did) return null
  const m = SUB_DEVICE_SUFFIX.exec(did)
  return m ? Number(m[1]) : null
}

/**
 * 虚拟子设备的「物理 did」＝ 去掉 `.sN` 后缀的父设备 did。
 *
 * ⚠️ 实测（2026-10-26）：`/miotspec/prop/get` 对 `946976246.s2` 这类
 * 虚拟子设备**只返回 device-information（siid 1）**，读 siid 2/3/4 会得到
 * -704083036。属性必须回到父 did 上读，`siid` 才是那一路的编号。
 * 也就是说：`did` 用父的、`siid` 用后缀的，两者缺一不可。
 */
export function physicalDid(did: string | undefined | null): string {
  if (!did) return ''
  return did.replace(SUB_DEVICE_SUFFIX, '')
}

/**
 * 不是电源开关的 `on` 所在模块。
 *
 * 智能音箱（如 Xiaomi 智能家庭屏 Mini）的规格里，`on` 出现在 Clock 模块下，
 * 含义是**闹钟开关**。若当成电源开关提到卡片和抽屉最前，会出现两个后果：
 * 卡片上多一个开关、点一下打开的是闹钟而不是设备。
 *
 * `indicator-light` 同理：三键开关的规格里除了 3 路继电器还有一个
 * 指示灯开关，同样是 `on` + bool + 可写。不排除的话四键面板会长出第 4 个键，
 * 而且那个键按下去只是关掉按键背光。
 */
const NON_POWER_SERVICES = new Set(['clock', 'alarm', 'reminder', 'schedule', 'indicator-light'])

/** 该 `on` 属性是否真的是设备电源开关（而不是闹钟之类的开关） */
export function isPowerProp(def: PropDef): boolean {
  return def.base_name === 'on' && def.type === 'bool' && !NON_POWER_SERVICES.has(def.service)
}

/**
 * 找出该设备真正的电源开关属性。
 *
 * **必须按 did 后缀选对应的那一路**：多键面板的规格里有多个 `on`
 * （去重后叫 on-2 / on-3 / on-4），siid 各不相同。直接取第一条的话，
 * 后几路永远在控制第一路（用户表现为"另外几路点不动、开关错灯"）。
 */
export function findOnProp(spec: Spec | null | undefined, did?: string | null): PropDef | null {
  if (!spec) return null
  const onProps = spec.properties.filter(
    (p) => isPowerProp(p) && p.rw.includes('w')
  )
  if (!onProps.length) return null

  const gang = gangOf(did)
  if (gang === null) return onProps[0]

  // 优先按 siid 精确匹配
  const bySiid = onProps.find((p) => p.siid === gang)
  if (bySiid) return bySiid

  // siid 对不上时按顺序兜底：.s2 → 第 1 路、.s3 → 第 2 路……
  const idx = gang - 2
  return onProps[idx] ?? onProps[0]
}

/** 指定 siid 那一路的电源开关属性（合并后的多键面板按路取） */
export function onPropOfSiid(spec: Spec | null | undefined, siid: number): PropDef | null {
  if (!spec) return null
  return (
    spec.properties.find((p) => isPowerProp(p) && p.rw.includes('w') && p.siid === siid) || null
  )
}

/**
 * 合并云端拆出来的虚拟子设备，一台物理设备只留一条。
 *
 * 实测（2026-10-28，113 条条目 / 77 台物理设备）：多键面板在云端不是
 * 「一台设备多个键」，而是**父设备 + 每键一条 `did.sN`**：
 *
 * ```
 * 946987819        玄关三键开关     ← 父设备，真正的商品
 * 946987819.s2     灯总控          ← 第 1 路（siid 2）
 * 946987819.s3     倒台射灯        ← 第 2 路（siid 3）
 * 946987819.s4     玄关射灯        ← 第 3 路（siid 4）
 * ```
 *
 * 直接渲染就是「一个三键开关占 4 张卡片」，而且父卡片上那个开关永远只控制
 * 第一路。合并后卡片按 `channels` 渲染每一路，抽屉里仍是完整面板。
 *
 * 合并规则：
 * - 以 `physicalDid()` 分组，**以父设备（无 `.sN` 后缀）为准**：名称、房间、
 *   在线状态、规格都对得上商品本身；
 * - 子设备按 siid **升序**排（云端返回顺序是乱的，`dids` 里也是乱的）；
 * - 只有父设备、没有子设备（老固件/未拆分的账号）→ 原样返回，等价于单路；
 * - 只有子设备、没有父设备 → **不合并**（凭子设备名拼不出商品名，
 *   强行造一个父设备会让用户看到一台自己不认识的设备）。
 */
export function mergeSplitDevices(list: Device[]): Device[] {
  const order: string[] = []
  const groups = new Map<string, Device[]>()
  for (const d of list) {
    const key = physicalDid(d.did)
    let g = groups.get(key)
    if (!g) {
      g = []
      groups.set(key, g)
      order.push(key)
    }
    g.push(d)
  }

  const out: Device[] = []
  for (const key of order) {
    const members = groups.get(key)!
    const parent = members.find((m) => gangOf(m.did) === null)
    const children = members.filter((m) => gangOf(m.did) !== null)
    if (!parent || !children.length) {
      // 没得合并：把该组原样放回（通常只有一个成员）
      for (const m of members) out.push(m)
      continue
    }
    const channels: DeviceChannel[] = children
      .map((c) => ({ did: c.did, siid: gangOf(c.did)!, name: c.name }))
      .sort((a, b) => a.siid - b.siid)
    out.push({
      ...parent,
      // 父设备自己可能落在「未分配」（roomlist 只登记了子设备），用子设备兜底
      room_id: parent.room_id || children[0].room_id,
      room_name: parent.room_name || children[0].room_name,
      channels
    })
  }
  return out
}

/**
 * 读取/写入某一路状态时用的键。
 *
 * 多键面板用虚拟子设备 did（`xxx.s2`），单路设备用设备自己的 did ——
 * 这样 `powers` / `switchMode` 两张状态表的键在卡片、抽屉、环境页之间通用。
 * 传 `siid` 时取那一路，不传就取第一路。
 */
export function channelKeyOf(device: Device, siid?: number): string {
  const list = device.channels
  if (list && list.length) {
    const hit = siid === undefined ? list[0] : list.find((c) => c.siid === siid)
    return (hit ?? list[0]).did
  }
  return device.did
}

