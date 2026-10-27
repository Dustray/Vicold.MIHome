/**
 * 环境读数历史库。
 *
 * 数据来自两处：
 *  1) 本机采样 —— 定时读一次环境指标 → 落盘。程序没运行时这段就是空白；
 *  2) 云端回填 —— `/user/get_user_device_data` 保留了至少 30 天的读数
 *     （米家 App 的「历史记录」走的就是它），启动时按 siid.piid 把过去
 *     24 小时并进来，关程序/关机造成的断档由它补上。
 *
 * ⚠️ 不要跟设备规格里自带的 history 服务搞混：`read-history-data` /
 * `record-data` 在账号下返回 -704010000，那套接口是读不到东西的。
 *
 * 落盘在后端（LocalAppData/Vicold.MIHome/history.json），前端只保留一份内存副本。
 */
import { reactive, ref } from 'vue'
import { invoke } from './host'

export interface Pt {
  /** Unix 秒 */
  t: number
  v: number
}

/**
 * 全局「现在」（Unix 秒）。
 *
 * 历史曲线按固定时间窗绘制（默认最近 24 小时），这个值决定时间窗的右端，
 * 每 30 秒推进一次，让所有曲线的时间轴跟着真实时钟滑动。
 * 用模块级单例而不是每个图表各开一个定时器。
 */
export const nowTs = ref(Math.floor(Date.now() / 1000))
let nowTimer: number | null = null

export function startNowTicker() {
  if (nowTimer !== null) return
  nowTimer = window.setInterval(() => {
    nowTs.value = Math.floor(Date.now() / 1000)
  }, 30_000)
}

export function stopNowTicker() {
  if (nowTimer === null) return
  window.clearInterval(nowTimer)
  nowTimer = null
}

/** did -> 指标名 -> 点序列 */
export const historyData = reactive<Record<string, Record<string, Pt[]>>>({})

/** 待落盘的采样点 */
const pending: { did: string; key: string; value: number }[] = []
let flushing = false
let timer: number | null = null

/** 采样间隔（毫秒）：默认 5 分钟一条 */
export const SAMPLE_INTERVAL = 5 * 60 * 1000

/** 记一个采样点（内存 + 待落盘队列） */
export function record(did: string, key: string, value: number) {
  if (!did || !key || !Number.isFinite(value)) return
  const bucket = (historyData[did] ||= {})
  const list = (bucket[key] ||= [])
  const now = Math.floor(Date.now() / 1000)
  if (list.length && now - list[list.length - 1].t < 30) {
    // 30 秒内的重复采样：就地更新，不重复落点
    list[list.length - 1] = { t: now, v: value }
  } else {
    list.push({ t: now, v: value })
    if (list.length > 2000) list.splice(0, list.length - 2000)
  }
  pending.push({ did, key, value })
  if (pending.length >= 120) void flush()
}

/** 把待落盘的点写回后端 */
export async function flush(): Promise<void> {
  if (flushing || !pending.length) return
  flushing = true
  const batch = pending.splice(0, pending.length)
  try {
    await invoke('history.append', { points: batch })
  } catch {
    // 历史丢了不影响主功能
  } finally {
    flushing = false
  }
}

/** 读取历史（只取需要的设备与时间窗） */
export async function load(dids: string[], sinceTs: number, limit = 600): Promise<void> {
  if (!dids.length) return
  try {
    const res = await invoke<Record<string, Record<string, [number, number][]>>>('history.list', {
      dids,
      since: sinceTs,
      limit
    })
    for (const [did, series] of Object.entries(res || {})) {
      const bucket = (historyData[did] ||= {})
      for (const [key, arr] of Object.entries(series || {})) {
        const list = (bucket[key] ||= [])
        const merged = [...list, ...arr.map(([t, v]) => ({ t, v }))]
        merged.sort((a, b) => a.t - b.t)
        const out: Pt[] = []
        for (const p of merged) {
          if (out.length && p.t === out[out.length - 1].t) out[out.length - 1] = p
          else out.push(p)
        }
        bucket[key] = out
      }
    }
  } catch {
    // 读不到历史就只显示实时读数
  }
}

export function clear(did: string): Promise<unknown> {
  if (did) delete historyData[did]
  return invoke('history.clear', { did })
}

// ---------------------------------------------------------------------------
// 云端回填
// ---------------------------------------------------------------------------
//
// 本机采样只在程序运行时才有，关掉一夜就是一个大空档。好消息是云端其实存着
// 读数历史（`/user/get_user_device_data`，米家 App 的「历史记录」就是它），
// 至少保留 30 天、点间隔约 37 秒，所以空白段可以从云端补回来。
//
// 几条实测约束（改这块之前先看）：
//  · key 必须是 `siid.piid`，写属性名一律返回空数组；
//  · 单次最多 1000 点，且 `time_start` 会被忽略 —— 只能把 `time_end` 往前
//    递推着翻页，这个翻页在宿主里做；
//  · 一条曲线一次回填约 0.7 秒，所以由前端串行发多条，不要整批塞给宿主，
//    否则串行 job 线程会被堵住，设备开关也跟着卡。

/** 回填窗口（小时）：与曲线的 24 小时时间窗对齐 */
export const BACKFILL_HOURS = 24
/** 两次回填之间至少间隔多久（小时） */
const BACKFILL_GAP_HOURS = 6
const BACKFILL_KEY = 'vicold.backfillAt'

export interface BackfillSeries {
  did: string
  cloudDid: string
  key: string
  siid: number
  piid: number
}

/** 距上次回填是否已经够久（应用启动时调一次就够，不用每次进来都打云端） */
export function shouldBackfill(): boolean {
  const last = Number(localStorage.getItem(BACKFILL_KEY) || 0)
  return !Number.isFinite(last) || Date.now() - last > BACKFILL_GAP_HOURS * 3600 * 1000
}

/**
 * 逐条把云端历史并进本地库，最后重读一次内存。
 * 返回补到的点数（0 表示没有可补的数据）。
 */
export async function backfillCloud(series: BackfillSeries[]): Promise<number> {
  if (!series.length || !shouldBackfill()) return 0
  const to = Math.floor(Date.now() / 1000)
  const from = to - BACKFILL_HOURS * 3600
  let total = 0
  for (const s of series) {
    try {
      const res = await invoke<{ ok: boolean; points: number }>('history.backfill', {
        did: s.did,
        cloud_did: s.cloudDid,
        key: s.key,
        siid: s.siid,
        piid: s.piid,
        from,
        to
      })
      total += res?.points || 0
    } catch {
      // 单条失败不影响其它曲线
    }
  }
  if (total > 0) {
    localStorage.setItem(BACKFILL_KEY, String(Date.now()))
    const dids = [...new Set(series.map((s) => s.did))]
    await load(dids, from, 2000)
  }
  return total
}

/** 启动定时落盘：每分钟把队列写回后端一次 */
export function startAutoFlush() {
  if (timer !== null) return
  timer = window.setInterval(() => void flush(), 60 * 1000)
}

export function stopAutoFlush() {
  if (timer === null) return
  window.clearInterval(timer)
  timer = null
}

/** 取某设备某指标的曲线（可指定时间窗，单位：小时） */
export function series(did: string, key: string, hours = 24): Pt[] {
  const list = historyData[did]?.[key]
  if (!list || !list.length) return []
  if (!hours) return list
  const from = Math.floor(Date.now() / 1000) - hours * 3600
  const out = list.filter((p) => p.t >= from)
  return out.length ? out : list.slice(-2)
}
