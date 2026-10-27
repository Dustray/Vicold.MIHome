import { ref } from 'vue'

/**
 * 房间顺序：用户自己拖出来的顺序，按家庭分别保存。
 *
 * 为什么要自己存：
 * - 米家返回的房间顺序不保证稳定，设备卡片又是按房间分组的，
 *   顺序一变卡片位置就乱跳，用户找不到刚才看到的设备；
 * - 用户对房间是有偏好的（常用的排前面），米家那边改不了，只能本地记。
 *
 * 存储格式：`{ [homeId]: string[] }`，房间 id 的顺序。
 */
const KEY = 'vicold.roomOrder'

/** homeId -> 房间 id 顺序。用 ref 包一层，computed 才能跟着重算。 */
const store = ref<Record<string, string[]>>(load())

function load(): Record<string, string[]> {
  try {
    const raw = localStorage.getItem(KEY)
    const obj = raw ? JSON.parse(raw) : null
    return obj && typeof obj === 'object' && !Array.isArray(obj) ? obj : {}
  } catch {
    return {}
  }
}

function persist() {
  try {
    localStorage.setItem(KEY, JSON.stringify(store.value))
  } catch {
    // 隐私模式 / 存储写满时忽略：排序只是锦上添花，不能因此崩掉
  }
}

/** 当前家庭的自定义顺序（没有就是空数组） */
export function roomOrder(homeId: string): string[] {
  return store.value[homeId] || []
}

/** 这个家庭是否已经被用户排过序 */
export function hasCustomOrder(homeId: string): boolean {
  return roomOrder(homeId).length > 0
}

/**
 * 按用户顺序排序房间列表。
 *
 * 用户没排过的房间保持原来的相对顺序，接在已排序的房间后面
 * （`Array.prototype.sort` 在 ES2019 后是稳定的，所以不用额外比较器）。
 */
export function sortRooms<T extends { id: string }>(homeId: string, list: T[]): T[] {
  const order = roomOrder(homeId)
  if (!order.length) return list
  const idx = new Map(order.map((id, i) => [id, i]))
  return [...list].sort((a, b) => {
    const ia = idx.get(a.id)
    const ib = idx.get(b.id)
    if (ia !== undefined && ib !== undefined) return ia - ib
    if (ia !== undefined) return -1
    if (ib !== undefined) return 1
    return 0
  })
}

/**
 * 把 dragId 拖到 overId 的位置（占用 overId 的位置）。
 *
 * `currentIds` 传当前的完整房间 id 顺序，落库时一并写全，
 * 这样以后新增的房间也会跟着排在后面，而不是被漏掉。
 */
export function moveRoom(homeId: string, currentIds: string[], dragId: string, overId: string) {
  if (dragId === overId) return
  const list = currentIds.filter((id) => id !== dragId)
  const at = list.indexOf(overId)
  if (at < 0) return
  list.splice(at, 0, dragId)
  store.value = { ...store.value, [homeId]: list }
  persist()
}

/** 恢复米家给的默认顺序 */
export function resetRoomOrder(homeId: string) {
  if (!store.value[homeId]) return
  const next = { ...store.value }
  delete next[homeId]
  store.value = next
  persist()
}
