/**
 * 属性批量读的唯一入口。
 *
 * 为什么要有这一层：
 * 卡片摘要原来按品类各发一条 `/miotspec/prop/get` —— 通断开关一条、灯一条、
 * 空调一条、温湿度计一条、空气监测仪一条、音箱一条、鱼缸一条，启动时连着发
 * 7 条。可这 7 条读的是同一批设备的同一批属性，每条也就三五项，40 条的上限
 * 一条都用不满。合并成一批之后启动调用量从 15 次降到 8 次左右。
 *
 * 三个必须守住的点：
 *  1. **按三元组索引回包**，不能按顺序对：云端返回顺序会被重排；
 *  2. **虚拟子设备用父 did 读**，调用方已经换好了，这里原样往下传；
 *  3. **同一个三元组只读一次**：灯和开关、空调和开关会读到同一条 on 属性，
 *     去重后既省配额，也避免同一项在回包里出现两次（第一份才是准的）。
 */
import { invoke } from './host'

/** 一个待读属性：`did` 必须是物理设备的 did（虚拟子设备用父 did） */
export interface PropRef {
  did: string
  siid: number
  piid: number
}

/** 云端回包的一行 */
export interface PropRow {
  did?: string
  siid: number
  piid: number
  code: number
  value?: unknown
}

/** 回包按 `did|siid|piid` 索引后的结果 */
export type PropIndex = Map<string, PropRow>

/**
 * 单批条数。
 *
 * 「一次 40 条」是早先保守写下的假设，实测不是硬上限：一次发 152 项，
 * 云端照样回了 152 行（`prop.get sent=152 got=152`，含读不到的项，code 非 0 但都在）。
 * 于是放宽到 200 —— 一般家庭的卡片摘要一次就能读完，启动少发 3 个请求。
 *
 * 但「能带多少」是云端说了算的，不敢全押：下面还留了 `SAFE_BATCH` 兜底，
 * 一旦发现回包少了（云端静默截断），就按 40 条拆开重发那一段。
 */
const BATCH = 200

/** 兜底批大小：确认安全的老数字，只在发现截断时用 */
const SAFE_BATCH = 40

/** 三元组 → 索引键。did 可能缺失（老接口按 siid/piid 回），用空串兜底 */
export const propKey = (it: { did?: string; siid: number; piid: number }): string =>
  `${it.did || ''}|${it.siid}|${it.piid}`

/** 按三元组取回包行；did 对不上时退回 siid/piid（老固件不回 did） */
export function pickRow(
  index: PropIndex,
  it: { did?: string; siid: number; piid: number }
): PropRow | undefined {
  const hit = index.get(propKey(it))
  if (hit) return hit
  if (it.did) return index.get(`|${it.siid}|${it.piid}`)
  return undefined
}

/** 发一批，返回回包行数（失败当 0 行） */
async function readChunk(chunk: PropRef[]): Promise<PropRow[]> {
  try {
    const ret = await invoke<PropRow[]>('prop.get', {
      items: chunk.map(({ did, siid, piid }) => ({ did, siid, piid })),
    })
    return Array.isArray(ret) ? ret : [ret]
  } catch {
    // 这一批读失败：调用方拿不到就保持卡片原样，不弹错
    return []
  }
}

function put(index: PropIndex, rows: PropRow[]) {
  for (const row of rows) {
    if (!row || typeof row.siid !== 'number') continue
    index.set(propKey(row), row)
    // 没有 did 的回包额外按 siid/piid 存一份，pickRow 才找得到
    if (!row.did) index.set(`|${row.siid}|${row.piid}`, row)
  }
}

/**
 * 批量读属性。去重 → 分批 → 合并成一张索引表。
 * 单批失败不影响其它批（返回的表里缺那批而已），调用方按「读不到就保持原样」处理。
 */
export async function fetchProps(items: PropRef[]): Promise<PropIndex> {
  const index: PropIndex = new Map()
  if (!items.length) return index

  // 去重：同一条属性被两个品类同时要（灯/开关都要 on）时只发一次
  const seen = new Set<string>()
  const uniq: PropRef[] = []
  for (const it of items) {
    const k = propKey(it)
    if (seen.has(k)) continue
    seen.add(k)
    uniq.push(it)
  }

  for (let start = 0; start < uniq.length; start += BATCH) {
    const chunk = uniq.slice(start, start + BATCH)
    let rows = await readChunk(chunk)
    // 回包比请求少：云端可能静默截断了这一批（没报错，就是少给）。
    // 按 40 条拆开重发，宁可多发几个请求，也不能让卡片停在「未读」。
    // 本来就是小批（≤40）的不重发，免得白跑一趟。
    if (chunk.length > SAFE_BATCH && rows.length < chunk.length) {
      rows = []
      for (let s = 0; s < chunk.length; s += SAFE_BATCH) {
        rows = rows.concat(await readChunk(chunk.slice(s, s + SAFE_BATCH)))
      }
    }
    put(index, rows)
  }
  return index
}
