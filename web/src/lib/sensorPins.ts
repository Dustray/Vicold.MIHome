import { ref } from 'vue'

/**
 * 环境页「传感器钉子板」：用户从任意设备上挑读数钉在这里，顺序与样式自己定。
 *
 * 为什么按家庭分开存：同一个账号下不同家庭的探头完全不同，
 * 共享一份列表会在切家庭时出现一堆对不上的钉子。
 *
 * 存储格式：`{ [homeId]: SensorPin[] }`，数组顺序就是显示顺序。
 */
export type PinStyle = 'gauge' | 'bar'

export interface SensorPin {
  /** `${did}:${key}` —— 同一台设备的温、湿是两个独立的钉子 */
  id: string
  style: PinStyle
}

const KEY = 'vicold.sensorPins'

/** 新钉子的默认样式：跟已有钉子保持一致，没有钉子时用仪表指针 */
const DEFAULT_STYLE: PinStyle = 'gauge'

const store = ref<Record<string, SensorPin[]>>(load())

function load(): Record<string, SensorPin[]> {
  try {
    const raw = localStorage.getItem(KEY)
    const obj = raw ? JSON.parse(raw) : null
    if (!obj || typeof obj !== 'object' || Array.isArray(obj)) return {}
    // 老数据/脏数据兜底：只留下形状正确的条目
    const out: Record<string, SensorPin[]> = {}
    for (const [home, list] of Object.entries(obj as Record<string, unknown>)) {
      if (!Array.isArray(list)) continue
      out[home] = list
        .filter((it): it is SensorPin => !!it && typeof (it as SensorPin).id === 'string')
        .map((it) => ({ id: it.id, style: it.style === 'bar' ? 'bar' : 'gauge' }))
    }
    return out
  } catch {
    return {}
  }
}

function persist() {
  try {
    localStorage.setItem(KEY, JSON.stringify(store.value))
  } catch {
    // 隐私模式 / 存储写满时忽略：钉子板只是便利功能，不能因此崩掉
  }
}

/** 当前家庭钉住的读数（按显示顺序） */
export function pinsOf(homeId: string): SensorPin[] {
  return store.value[homeId] || []
}

/** 这个读数是否已经钉住 */
export function isPinned(homeId: string, id: string): boolean {
  return pinsOf(homeId).some((p) => p.id === id)
}

/** 钉住 / 取消钉住 */
export function togglePin(homeId: string, id: string) {
  const list = pinsOf(homeId)
  const next = list.some((p) => p.id === id)
    ? list.filter((p) => p.id !== id)
    : [...list, { id, style: list[list.length - 1]?.style ?? DEFAULT_STYLE }]
  store.value = { ...store.value, [homeId]: next }
  persist()
}

/** 单条钉子换样式 */
export function setPinStyle(homeId: string, id: string, style: PinStyle) {
  const list = pinsOf(homeId)
  if (!list.some((p) => p.id === id && p.style !== style)) return
  store.value = {
    ...store.value,
    [homeId]: list.map((p) => (p.id === id ? { ...p, style } : p))
  }
  persist()
}

/** 全部换成同一样式（表头的「样式」开关用它） */
export function setAllPinStyle(homeId: string, style: PinStyle) {
  const list = pinsOf(homeId)
  if (!list.length || list.every((p) => p.style === style)) return
  store.value = { ...store.value, [homeId]: list.map((p) => ({ ...p, style })) }
  persist()
}

/**
 * 拖动排序：把 dragId 挪到 overId 的位置。
 *
 * `currentIds` 传当前的完整顺序，落库时写全 ——
 * 否则被过滤掉（设备离线/已删除）的钉子会悄悄丢掉。
 */
export function movePin(homeId: string, currentIds: string[], dragId: string, overId: string) {
  if (dragId === overId) return
  const list = currentIds.filter((id) => id !== dragId)
  const at = list.indexOf(overId)
  if (at < 0) return
  list.splice(at, 0, dragId)
  const byId = new Map(pinsOf(homeId).map((p) => [p.id, p]))
  store.value = {
    ...store.value,
    [homeId]: list.map((id) => byId.get(id) ?? { id, style: DEFAULT_STYLE })
  }
  persist()
}

/** 清空当前家庭的钉子 */
export function clearPins(homeId: string) {
  if (!store.value[homeId]?.length) return
  const next = { ...store.value }
  delete next[homeId]
  store.value = next
  persist()
}
