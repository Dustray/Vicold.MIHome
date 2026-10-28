/**
 * 多键面板合并后的一路（云端派生出来的虚拟子设备）。
 *
 * `did` 形如 `946976246.s2`，后缀的数字就是该路在规格里的 siid；
 * 它同时是 `powers` / `switchMode` 等状态表的键（`key`），
 * 所以卡片、抽屉、环境页三处开关状态天然一致。
 */
export interface DeviceChannel {
  /** 虚拟子设备 did，形如 `946976246.s2` */
  did: string
  /** 该路的 siid（父设备规格里的开关模块编号） */
  siid: number
  /** 米家里给这一路起的名字（主灯 / 窗帘 / 右键-xxx） */
  name: string
}

export interface Device {
  did: string
  name: string
  model: string
  spec_type: string
  online: boolean
  home_id: string
  home_name: string
  room_id: string
  room_name: string
  /**
   * 被合并的虚拟子设备（多键面板的每一路）。
   *
   * 云端把三键开关拆成「父设备 + 3 个 `did.sN`」共 4 条记录，
   * 合并后只保留一条，`did` 是父设备的 did，每一路挂在 `channels` 上。
   * 单路设备为空数组 → 所有逻辑按整台设备一个通道处理。
   */
  channels?: DeviceChannel[]
}

export interface HomeRoom {
  id: string
  name: string
}

export interface Home {
  id: string
  name: string
  uid: number
  rooms: HomeRoom[]
}

export interface PropDef {
  name: string
  /** 去重前的原始短名（同名属性会被追加 -siid 后缀，逻辑匹配请用这个） */
  base_name: string
  /** 官方中文描述（comment 字段） */
  desc_zh: string
  /** 官方英文描述 */
  desc_en: string
  /** 所属功能模块（service）短名 */
  service: string
  /** 所属功能模块中文名 */
  service_zh: string
  description: string
  type: 'bool' | 'int' | 'uint' | 'float' | 'string'
  rw: string
  range: number[] | null
  value_list: { value: number | string; description: string; desc_zh?: string }[] | null
  siid: number
  piid: number
}

export interface ActionDef {
  name: string
  description: string
  desc_zh: string
  desc_en: string
  service: string
  service_zh: string
  siid: number
  aiid: number
}

export interface SpecService {
  siid: number
  name: string
  desc_zh: string
  description: string
}

export interface Spec {
  name: string
  spec_type: string
  properties: PropDef[]
  actions: ActionDef[]
  services: SpecService[]
}

export interface Scene {
  scene_id: string
  name: string
  home_id: string
  owner_uid?: number
  [key: string]: unknown
}

/**
 * 卡片上要渲染的一路开关（`DeviceCard.vue` 的 `keys` prop）。
 *
 * 多键面板合并前是 N 张卡片各带一个开关，合并后是一张卡片带 N 个开关，
 * 每个开关带着自己那一路的名字与通断类型。
 */
export interface CardKeyView {
  /** 状态键（虚拟子设备 did），点击时按它回写 `powers` */
  key: string
  /** 该路在规格里的 siid */
  siid: number
  /** 这一路的名字（米家里给按键起的名字） */
  name: string
  /** 通断状态；`null` = 没有可写的 `on`（纯无线开关）或还没读到 */
  on: boolean | null
  /** 无线模式（不驱动继电器） */
  wireless: boolean
  busy: boolean
}
