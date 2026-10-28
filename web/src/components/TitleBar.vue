<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { host, on } from '../lib/host'

defineProps<{ theme: 'light' | 'dark' }>()
const emit = defineEmits<{ (e: 'toggle-theme'): void }>()

/** 当前是否已最大化（由宿主通过 window.state 事件同步） */
const maximized = ref(false)

/**
 * 标题栏交互：拖动窗口 + 双击最大化/还原。
 *
 * ⚠️ 关键约束：拖动走的是宿主原生窗口移动（WM_NCLBUTTONDOWN + HTCAPTION），
 * 系统会捕获鼠标并吞掉 mouseup / click / dblclick，**只有 mousedown 能到达
 * DOM**（实测第二次 mousedown 带 detail=2）。所以：
 *
 * - 双击判定用浏览器自带的 `e.detail === 2`（在 mousedown 阶段就能拿到）。
 * - 未最大化时，mousedown 立即启动原生拖动 —— 保证拖动跟手、起点精确。
 * - 已最大化时延迟到鼠标真的移动才启动拖动 —— 因为系统对「最大化窗口的
 *   标题栏按下」会先还原再拖动，那样第一次点击就把窗口还原了，
 *   双击的语义就毁了。
 */

const DRAG_PX = 4         // 最大化状态下，启动拖动的移动阈值（像素）

/** 最大化状态下：已按下但还没判定为拖动 */
let pending = false
let downX = 0
let downY = 0

function onWindowMouseMove(e: MouseEvent) {
  if (!pending) return
  // 鼠标已松开却没收到 mouseup（指针移出窗口）→ 取消本次判定
  if (e.buttons === 0) {
    pending = false
    return
  }
  const dx = Math.abs(e.clientX - downX)
  const dy = Math.abs(e.clientY - downY)
  if (dx >= DRAG_PX || dy >= DRAG_PX) {
    pending = false
    void host.drag()
  }
}

function onWindowMouseUp() {
  pending = false
}

function onTitleDown(e: MouseEvent) {
  if (e.button !== 0) return
  // 按钮区域已用 .stop 拦掉，不会走到这里

  // 双击 → 切换最大化/还原
  if (e.detail >= 2) {
    void host.maximize().then((r) => {
      maximized.value = !!r?.maximized
    })
    return
  }

  if (maximized.value) {
    // 最大化状态：先按住，等鼠标真的动了再拖（见上方注释）
    pending = true
    downX = e.clientX
    downY = e.clientY
    return
  }

  // 普通状态：立即启动原生拖动
  void host.drag()
}

function onMaximizeClick() {
  void host.maximize().then((r) => {
    maximized.value = !!r?.maximized
  })
}

/** 宿主推送窗口状态（拖到屏幕边缘吸附也会触发） */
function onWindowState(payload: unknown) {
  const p = payload as { maximized?: boolean } | undefined
  if (p && typeof p.maximized === 'boolean') maximized.value = p.maximized
}

onMounted(async () => {
  // 页面加载时先同步一次真实状态
  try {
    const s = await host.state()
    maximized.value = !!s?.maximized
  } catch {
    /* 宿主不可用时忽略 */
  }
  on('window.state', onWindowState)
  window.addEventListener('mousemove', onWindowMouseMove)
  window.addEventListener('mouseup', onWindowMouseUp)
})

onUnmounted(() => {
  window.removeEventListener('mousemove', onWindowMouseMove)
  window.removeEventListener('mouseup', onWindowMouseUp)
})
</script>

<template>
  <header class="titlebar" @mousedown.prevent="onTitleDown">
    <div class="brand">
      <span class="mark">
        <!-- 米家那个「M 房」标记：圆角方环 + 顶边正中压一道 V -->
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none">
          <path d="M12 8.3 10.9 6.9H7.2v6.6a4.8 4.8 0 0 0 9.6 0V6.9h-4.3Z"
            stroke="currentColor" stroke-width="2.3" stroke-linejoin="round" stroke-linecap="round" />
        </svg>
      </span>
      <span class="name">桌面米家</span>
    </div>

    <div class="spacer" />

    <div class="actions" @mousedown.stop>
      <button class="win-btn" :title="theme === 'dark' ? '切换到浅色' : '切换到深色'" @click="emit('toggle-theme')">
        <svg v-if="theme === 'dark'" width="16" height="16" viewBox="0 0 24 24" fill="none">
          <circle cx="12" cy="12" r="4.2" stroke="currentColor" stroke-width="1.8" />
          <path d="M12 3v2.2M12 18.8V21M3 12h2.2M18.8 12H21M5.6 5.6l1.6 1.6M16.8 16.8l1.6 1.6M18.4 5.6l-1.6 1.6M7.2 16.8l-1.6 1.6" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" />
        </svg>
        <svg v-else width="16" height="16" viewBox="0 0 24 24" fill="none">
          <path d="M20 14.2A8.2 8.2 0 0 1 9.8 4a8.4 8.4 0 1 0 10.2 10.2Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" />
        </svg>
      </button>
      <button class="win-btn" title="最小化" @click="host.minimize()">
        <svg width="16" height="16" viewBox="0 0 24 24"><path d="M6 12h12" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" /></svg>
      </button>
      <button class="win-btn" :title="maximized ? '还原' : '最大化'" @click="onMaximizeClick">
        <svg v-if="maximized" width="16" height="16" viewBox="0 0 24 24" fill="none">
          <rect x="5.5" y="8.5" width="10" height="10" rx="1.6" stroke="currentColor" stroke-width="1.8" />
          <path d="M8.5 8.5v-2a1.5 1.5 0 0 1 1.5-1.5h6.5a1.5 1.5 0 0 1 1.5 1.5V13a1.5 1.5 0 0 1-1.5 1.5h-2" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" />
        </svg>
        <svg v-else width="16" height="16" viewBox="0 0 24 24" fill="none">
          <rect x="6.5" y="6.5" width="11" height="11" rx="2" stroke="currentColor" stroke-width="1.8" />
        </svg>
      </button>
      <button class="win-btn danger" title="关闭（隐藏到托盘继续在后台采样）" @click="host.close()">
        <svg width="16" height="16" viewBox="0 0 24 24"><path d="M7 7l10 10M17 7L7 17" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" /></svg>
      </button>
    </div>
  </header>
</template>

<style scoped>
.titlebar {
  height: var(--titlebar-h);
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 0 0 16px;
  background: var(--surface);
  border-bottom: 1px solid var(--border);
  flex: none;
  position: relative;
  z-index: 30;
  user-select: none;
}

.titlebar::after {
  content: '';
  position: absolute;
  inset: auto 0 -1px 0;
  height: 1px;
  background: var(--grad-line);
  opacity: 0.7;
}

.brand {
  display: flex;
  align-items: center;
  gap: 9px;
  font-weight: 700;
  letter-spacing: 0.2px;
}

.mark {
  width: 25px;
  height: 25px;
  border-radius: 8px;
  background: var(--grad-brand);
  color: #fff;
  display: grid;
  place-items: center;
}

.name {
  font-size: 13.5px;
  background: var(--grad-brand);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}

.spacer {
  flex: 1;
}

.actions {
  display: flex;
  align-items: stretch;
  height: 100%;
}

.win-btn {
  width: 46px;
  display: grid;
  place-items: center;
  color: var(--text-2);
  transition: background-color 0.12s ease, color 0.12s ease;
}

.win-btn:hover {
  background: var(--surface-3);
  color: var(--brand);
}

.win-btn.danger:hover {
  background: #e5484d;
  color: #fff;
}
</style>
