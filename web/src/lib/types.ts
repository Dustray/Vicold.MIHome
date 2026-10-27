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
