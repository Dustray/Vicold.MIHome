/**
 * 窗口边缘缩放热区。
 *
 * 为什么放在前端做：
 * WebView2 的 Chromium 会在宿主窗口里铺一层自己的子窗口（Chrome_WidgetWin_0/1、
 * Chrome_RenderWidgetHostHWND、Intermediate D3D Window），鼠标命中测试先落到
 * 最深的那个子窗口，顶层窗口根本收不到 WM_NCHITTEST，所以「拖边框改大小」
 * 在 C++ 侧无路可走（那些子窗口属于 Chromium 的线程，无法子类化）。
 *
 * 于是改成：前端判断鼠标是否落在窗口最外圈 → 通知宿主 → 宿主发起系统原生
 * 缩放循环（WM_NCLBUTTONDOWN + HT*）。和标题栏拖动是同一套机制。
 *
 * 坐标系用的是网页视口（clientX/clientY），正好等于窗口客户区，
 * 视口边缘就是窗口边缘。
 */

/** 热区宽度（CSS 像素） */
export const EDGE_PX = 6

export type EdgeDir = '' | 'n' | 's' | 'e' | 'w' | 'ne' | 'nw' | 'se' | 'sw'

/** 各个方向对应的 CSS 光标 */
export const EDGE_CURSOR: Record<EdgeDir, string> = {
  '': '',
  n: 'ns-resize',
  s: 'ns-resize',
  e: 'ew-resize',
  w: 'ew-resize',
  ne: 'nesw-resize',
  sw: 'nesw-resize',
  nw: 'nwse-resize',
  se: 'nwse-resize'
}

/**
 * 判断某个视口坐标落在哪个边缘热区。
 * 角落优先（同时命中两条边时按对角方向处理）。
 */
export function edgeDirAt(
  x: number,
  y: number,
  w: number = window.innerWidth,
  h: number = window.innerHeight
): EdgeDir {
  const nearL = x <= EDGE_PX
  const nearR = x >= w - EDGE_PX
  const nearT = y <= EDGE_PX
  const nearB = y >= h - EDGE_PX
  if (nearT && nearL) return 'nw'
  if (nearT && nearR) return 'ne'
  if (nearB && nearL) return 'sw'
  if (nearB && nearR) return 'se'
  if (nearL) return 'w'
  if (nearR) return 'e'
  if (nearT) return 'n'
  if (nearB) return 's'
  return ''
}
