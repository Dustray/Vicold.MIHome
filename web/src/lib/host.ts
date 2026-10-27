// 与 C++ 宿主通信：request/response 走 id 匹配，另留一条单向事件通道。

type Resolver = { resolve: (v: unknown) => void; reject: (e: Error) => void }

const pending = new Map<number, Resolver>()
const listeners = new Map<string, ((payload: unknown) => void)[]>()
let seq = 1

interface Envelope {
  id: number
  ok: boolean
  data?: unknown
  error?: string
}

declare global {
  interface Window {
    chrome: { webview: { postMessage: (msg: unknown) => void; addEventListener: Function } }
    __vicold?: {
      resolve: (env: Envelope) => void
      event: (ev: { name: string; payload?: unknown }) => void
    }
  }
}

window.__vicold = {
  resolve(env: Envelope) {
    const entry = pending.get(env.id)
    if (!entry) return
    pending.delete(env.id)
    if (env.ok) entry.resolve(env.data)
    else entry.reject(new Error(env.error || '请求失败'))
  },
  event(ev) {
    for (const fn of listeners.get(ev.name) || []) fn(ev.payload)
  }
}

export function invoke<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  const id = seq++
  return new Promise<T>((resolve, reject) => {
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject })
    try {
      window.chrome.webview.postMessage({ id, cmd, args })
    } catch (e) {
      pending.delete(id)
      reject(e as Error)
    }
  })
}

export function on(name: string, fn: (payload: unknown) => void) {
  const list = listeners.get(name) || []
  list.push(fn)
  listeners.set(name, list)
}

export const host = {
  // 启动原生窗口移动。宿主走 WM_NCLBUTTONDOWN + HTCAPTION，
  // 系统会捕获鼠标并吞掉 mouseup/click/dblclick，只有 mousedown 能到达 DOM，
  // 所以双击判定必须在前端 mousedown 阶段完成（见 TitleBar.vue）。
  drag: () => invoke('window.drag'),
  // 从窗口边缘发起原生缩放（dir: n/s/e/w/ne/nw/se/sw）。
  // Chromium 的子窗口铺满整个窗口，顶层收不到 WM_NCHITTEST，
  // 所以边缘判定放在前端，宿主只负责发起系统缩放循环。
  resize: (dir: string) => invoke('window.resize', { dir }),
  minimize: () => invoke('window.minimize'),
  maximize: () => invoke<{ maximized: boolean }>('window.maximize'),
  close: () => invoke('window.close'),
  // 查询当前是否已最大化（页面加载时同步一次，之后靠 window.state 事件更新）
  state: () => invoke<{ maximized: boolean }>('window.state')
}
