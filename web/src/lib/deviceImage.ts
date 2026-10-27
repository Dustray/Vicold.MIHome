// 设备产品图：宿主去 home.miot-spec.com 抓 og:image 存到 <data>/img，
// 网页通过第二个虚拟主机（https://img.vicold.mihome）直接 <img src> 加载。
//
// 为什么不让网页自己 fetch：产品图地址带签名、且产品页跨域，网页里取不到；
// 也不把 base64 塞进桥接消息 —— 37 台设备就是 1.5 MB 的 JSON。
//
// 时序：卡片先渲染首字母图标，图到了再替换，所以不会出现破图（404 也不会被缓存）。
import { reactive } from 'vue'
import { invoke, on } from './host'

const HOST = 'https://img.vicold.mihome'

interface ImageSync {
  images?: Record<string, string>
  missing?: string[]
}

/** model → 本地文件名；没有条目表示「还没拿到 / 这个型号没有图」 */
const files = reactive(new Map<string, string>())
/** 已经问过宿主的型号（拿到没拿到都算问过，避免每次刷新重问） */
const asked = new Set<string>()
/** 待抓取的型号队列 */
const queue = new Set<string>()
let running = false

/** 卡片的图片地址；空串 = 用首字母图标兜底 */
export function devicePhoto(model: string | undefined | null): string {
  if (!model) return ''
  const name = files.get(model)
  return name ? `${HOST}/${encodeURIComponent(name)}` : ''
}

// 宿主每抓到一张就推一条：不用等整批抓完，卡片逐张点亮
on('image.ready', (payload) => {
  const p = payload as { model?: string; name?: string } | undefined
  if (p?.model && p.name) files.set(p.model, p.name)
})

async function drain() {
  if (running) return
  running = true
  while (queue.size) {
    // 一次性把队列全交给宿主：宿主内部 8 路并发连续跑，不会因为某一批里
    // 有个慢页面（产品页服务端渲染 1~40s 不等）把后面的型号全堵住
    const batch = [...queue]
    queue.clear()
    try {
      const res = await invoke<ImageSync>('image.sync', { models: batch })
      for (const [model, name] of Object.entries(res.images || {})) files.set(model, name)
    } catch {
      // 断网这类临时失败：放回 asked，下次刷新还能再试
      for (const m of batch) asked.delete(m)
    }
  }
  running = false
}

/**
 * 确保这些型号的产品图已就绪。可重复调用：已问过的型号直接跳过，
 * 宿主那边也有磁盘缓存，所以第二次启动基本是本地读文件。
 */
export function ensureDeviceImages(models: (string | undefined | null)[]) {
  let added = false
  for (const model of models) {
    if (!model || asked.has(model) || files.has(model)) continue
    asked.add(model)
    queue.add(model)
    added = true
  }
  if (added) void drain()
}
