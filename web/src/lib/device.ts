import type { PropDef, Spec } from './types'

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
