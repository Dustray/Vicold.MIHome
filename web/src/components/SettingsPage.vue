<script setup lang="ts">
/**
 * 设置页。
 *
 * 由标题栏右上角的齿轮按钮打开，占内容区（侧栏保留，随时能切回环境 / 设备 / 场景）。
 *
 * 「开机自启」和「关闭窗口最小化到托盘」两个开关改的都是宿主的设置，
 * 托盘右键菜单也能改同一项，所以状态由 App.vue 统一持有、这里只负责显示和回调，
 * 免得两处各存一份、改了一边另一边不动。
 */
import { computed, onMounted, ref } from 'vue'
import PowerToggle from './PowerToggle.vue'
import { invoke } from '../lib/host'
import { clearAll } from '../lib/history'

const props = withDefaults(
  defineProps<{
    theme: 'light' | 'dark'
    autostart: boolean
    autostartBusy?: boolean
    closeToTray: boolean
    closeToTrayBusy?: boolean
  }>(),
  { autostartBusy: false, closeToTrayBusy: false }
)

const emit = defineEmits<{
  (e: 'set-theme', t: 'light' | 'dark'): void
  (e: 'toggle-autostart'): void
  (e: 'toggle-close-to-tray'): void
  (e: 'notify', text: string): void
}>()

/**
 * 托盘开关的说明文字要跟着开关状态变。
 *
 * 这一个开关关系到「程序会不会在后台继续跑」，很容易被误解成纯窗口行为，
 * 所以关掉时要把代价说清楚：后台采集也会停，曲线会断。
 */
const closeTip = computed(() =>
  props.closeToTray
    ? '点关闭按钮只是藏起窗口，从托盘图标可以再打开，后台继续采集读数'
    : '点关闭按钮直接退出程序，后台采集也会停止，曲线会断档'
)

/** 关于：版本和数据目录都由宿主给，前端不硬编码（改一次版本号不用两处改） */
const version = ref('')
const dataDir = ref('')

onMounted(async () => {
  try {
    const info = await invoke<{ version: string; data_dir: string }>('app.info')
    version.value = info.version || ''
    dataDir.value = info.data_dir || ''
  } catch {
    /* 宿主没这条命令就留空，不影响其它设置 */
  }
})

/**
 * 清空历史数据：点一次只是「上膛」，4 秒内再点一次才真的清。
 *
 * 用按钮自身的二次确认而不是弹原生确认框 —— 确认框由 WebView2 之外的
 * 系统 UI 画出来，会打断窗口的输入法/焦点状态，这个应用里一直避免用。
 */
const armed = ref(false)
const clearing = ref(false)
let armTimer = 0

function askClear() {
  if (clearing.value) return
  if (!armed.value) {
    armed.value = true
    window.clearTimeout(armTimer)
    armTimer = window.setTimeout(() => (armed.value = false), 4000)
    return
  }
  armed.value = false
  window.clearTimeout(armTimer)
  void doClear()
}

async function doClear() {
  clearing.value = true
  try {
    await clearAll()
    emit('notify', '本机历史数据已清空')
  } catch (e) {
    emit('notify', (e as Error).message || '清空失败')
  } finally {
    clearing.value = false
  }
}
</script>

<template>
  <div class="set-scroll">
    <!-- ---------------------------------------------- 外观 -->
    <section class="set-card">
      <header class="set-head">
        <h2>外观</h2>
        <p>配色方案，选完立即生效并记住</p>
      </header>

      <div class="row">
        <div class="row-text">
          <div class="row-title">主题</div>
          <p class="row-tip">标题栏右上角的月亮按钮是同一个开关</p>
        </div>
        <div class="seg">
          <button class="seg-btn" :class="{ on: theme === 'light' }" @click="emit('set-theme', 'light')">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none">
              <circle cx="12" cy="12" r="4.1" stroke="currentColor" stroke-width="1.8" />
              <path
                d="M12 3.4v2.1M12 18.5v2.1M3.4 12h2.1M18.5 12h2.1M5.9 5.9l1.5 1.5M16.6 16.6l1.5 1.5M18.1 5.9l-1.5 1.5M7.4 16.6l-1.5 1.5"
                stroke="currentColor" stroke-width="1.8" stroke-linecap="round"
              />
            </svg>
            浅色
          </button>
          <button class="seg-btn" :class="{ on: theme === 'dark' }" @click="emit('set-theme', 'dark')">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none">
              <path d="M20 14.2A8.2 8.2 0 0 1 9.8 4a8.4 8.4 0 1 0 10.2 10.2Z" stroke="currentColor"
                stroke-width="1.8" stroke-linejoin="round" />
            </svg>
            深色
          </button>
        </div>
      </div>
    </section>

    <!-- ---------------------------------------------- 启动与窗口 -->
    <section class="set-card">
      <header class="set-head">
        <h2>启动与窗口</h2>
        <p>决定程序什么时候在后台跑</p>
      </header>

      <div class="row">
        <div class="row-text">
          <div class="row-title">开机自动启动</div>
          <p class="row-tip">开机后静默驻留托盘，后台继续采集读数，曲线不会断档</p>
        </div>
        <PowerToggle
          :model-value="autostart"
          :busy="autostartBusy"
          label="开机自动启动"
          @update:model-value="emit('toggle-autostart')"
        />
      </div>

      <div class="row">
        <div class="row-text">
          <div class="row-title">关闭窗口时最小化到托盘</div>
          <p class="row-tip">{{ closeTip }}</p>
        </div>
        <PowerToggle
          :model-value="closeToTray"
          :busy="closeToTrayBusy"
          label="关闭窗口时最小化到托盘"
          @update:model-value="emit('toggle-close-to-tray')"
        />
      </div>
    </section>

    <!-- ---------------------------------------------- 数据 -->
    <section class="set-card">
      <header class="set-head">
        <h2>数据</h2>
        <p>环境读数的历史记录只存在这台电脑上</p>
      </header>

      <div class="row">
        <div class="row-text">
          <div class="row-title">本机历史数据</div>
          <p class="row-tip">
            程序运行时每 5 分钟记一条读数，启动时再从云端补回最近 24 小时，
            设备卡片和环境页的曲线都靠它画。清空后曲线上以前的记录就没了。
          </p>
        </div>
        <button
          class="btn btn-ghost danger-btn"
          :class="{ armed }"
          :disabled="clearing"
          @click="askClear"
        >
          {{ clearing ? '清空中…' : armed ? '再点一次确认' : '清空历史数据' }}
        </button>
      </div>
    </section>

    <!-- ---------------------------------------------- 关于 -->
    <section class="set-card">
      <header class="set-head">
        <h2>关于</h2>
        <p>桌面米家 —— 米家设备的 Windows 桌面控制端</p>
      </header>

      <dl class="kv">
        <div>
          <dt>版本</dt>
          <dd>{{ version || '—' }}</dd>
        </div>
        <div>
          <dt>数据目录</dt>
          <dd class="path">{{ dataDir || '—' }}</dd>
        </div>
      </dl>
    </section>
  </div>
</template>

<style scoped>
.set-scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  width: 100%;
  max-width: 820px;
  padding-bottom: 28px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

/* 每张卡是一条条设置项，项之间用细线分隔，不再各自做成卡片 */
.set-card {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-sm);
  padding: 0 18px;
  animation: fade-up 0.26s var(--ease) both;
}

.set-head {
  padding: 15px 0 11px;
}

.set-head h2 {
  margin: 0;
  font-size: 13.5px;
  font-weight: 650;
  letter-spacing: 0.3px;
  color: var(--text);
}

.set-head p {
  margin: 3px 0 0;
  font-size: 11.5px;
  color: var(--text-3);
}

.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  padding: 14px 0;
  border-top: 1px solid var(--border);
}

.row:last-child {
  padding-bottom: 16px;
}

.row-text {
  min-width: 0;
}

.row-title {
  font-size: 13px;
  font-weight: 550;
  color: var(--text);
}

.row-tip {
  margin: 3px 0 0;
  max-width: 52ch;
  font-size: 11.5px;
  line-height: 1.6;
  color: var(--text-3);
}

/* ---------------------------------------------- 主题分段选择 */
.seg {
  flex: none;
  display: flex;
  gap: 2px;
  padding: 3px;
  border: 1px solid var(--border);
  border-radius: 11px;
  background: var(--surface-3);
}

.seg-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 30px;
  padding: 0 12px;
  border-radius: 8px;
  font-size: 12.5px;
  color: var(--text-2);
  transition: background-color 0.16s ease, color 0.16s ease, box-shadow 0.16s ease;
}

.seg-btn:hover {
  color: var(--text);
}

.seg-btn.on {
  background: var(--surface);
  color: var(--brand);
  font-weight: 550;
  box-shadow: var(--shadow-xs);
}

/* ---------------------------------------------- 清空按钮 */
.danger-btn {
  flex: none;
  height: 34px;
  padding: 0 14px;
  font-size: 12.5px;
}

.danger-btn:hover:not(:disabled) {
  color: var(--danger);
  border-color: var(--danger);
  background: var(--surface);
  box-shadow: none;
}

/* 已上膛：整颗变红，和普通按钮彻底区分开 */
.danger-btn.armed,
.danger-btn.armed:hover:not(:disabled) {
  color: #fff;
  background: var(--danger);
  border-color: var(--danger);
}

/* ---------------------------------------------- 关于 */
.kv {
  margin: 0;
  padding: 13px 0 15px;
  border-top: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.kv > div {
  display: flex;
  align-items: baseline;
  gap: 14px;
  font-size: 12.5px;
}

.kv dt {
  flex: none;
  width: 64px;
  color: var(--text-3);
}

.kv dd {
  margin: 0;
  min-width: 0;
  color: var(--text-2);
}

/* 路径要能选中复制（body 全局是 user-select:none） */
.kv .path {
  font-family: 'Cascadia Mono', Consolas, monospace;
  font-size: 11.5px;
  word-break: break-all;
  user-select: text;
}

@media (max-width: 760px) {
  .row {
    gap: 12px;
  }
  .row-tip {
    max-width: none;
  }
}
</style>
