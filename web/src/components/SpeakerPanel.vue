<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import type { ActionDef, Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { speakerProps } from '../lib/spec'
import { actionLabel, propLabel } from '../lib/zh'
import PropControl from './PropControl.vue'

const props = defineProps<{
  device: Device
  spec: Spec | null
  values: Record<string, unknown>
  busyProp: string
  groups: SpecGroup[]
}>()
const emit = defineEmits<{
  (e: 'set-prop', def: PropDef, value: unknown): void
  (e: 'run-action', siid: number, aiid: number, name: string): void
  (e: 'refresh'): void
}>()

// ---------------------------------------------------------------- 图标
const ICONS: Record<string, string> = {
  grille:
    '<rect x="6.6" y="2.8" width="10.8" height="18.4" rx="3.4"/><circle cx="12" cy="16.4" r="1.5"/><path d="M9.6 6.6h4.8M9.6 9.8h4.8M9.6 13h4.8"/>',
  play: '<path d="M8.4 5.6l10.2 6.4-10.2 6.4z"/>',
  pause: '<rect x="7.6" y="5.6" width="3.4" height="12.8" rx="1.2"/><rect x="13" y="5.6" width="3.4" height="12.8" rx="1.2"/>',
  stop: '<rect x="6.8" y="6.8" width="10.4" height="10.4" rx="2"/>',
  prev: '<path d="M18.6 5.8v12.4L9.6 12z"/><rect x="5.2" y="5.8" width="2.6" height="12.4" rx="1.2"/>',
  next: '<path d="M5.4 5.8v12.4L14.4 12z"/><rect x="16.2" y="5.8" width="2.6" height="12.4" rx="1.2"/>',
  mic:
    '<rect x="9.2" y="2.8" width="5.6" height="10.4" rx="2.8"/><path d="M5.6 11.4a6.4 6.4 0 0 0 12.8 0"/><path d="M12 17.8v3.4"/>',
  micOff:
    '<path d="M9.2 6.2a2.8 2.8 0 0 1 5.6 0v4.2"/><path d="M5.6 11.4a6.4 6.4 0 0 0 8.8 5.9"/><path d="M18.4 11.4a6.4 6.4 0 0 1-.6 2.7"/><path d="M12 17.8v3.4"/><path d="M4 3.6l16 16.8"/>',
  mute:
    '<path d="M4 9.4h3.4L12 5.4v13.2L7.4 14.6H4z"/><path d="M16.2 9.2l4.4 5.6M20.6 9.2l-4.4 5.6"/>',
  moon: '<path d="M20 14.4A8.4 8.4 0 0 1 9.6 4a8.6 8.6 0 1 0 10.4 10.4z"/>',
  bell:
    '<path d="M18 15.4V10a6 6 0 1 0-12 0v5.4L4.4 18h15.2z"/><path d="M9.6 18a2.4 2.4 0 0 0 4.8 0"/>',
  bellOff:
    '<path d="M17.8 14.6V10a5.8 5.8 0 0 0-9-4.9"/><path d="M6.2 8.7v6.5L4.6 18h13"/><path d="M9.8 18a2.3 2.3 0 0 0 4.4 0"/><path d="M4 3.8l16 16.4"/>',
  spark:
    '<path d="M12 3.2l1.9 5.3 5.3 1.9-5.3 1.9L12 17.6l-1.9-5.3L4.8 10.4l5.3-1.9z"/><path d="M18.4 16.2l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z"/>',
  tv: '<rect x="3" y="5.4" width="18" height="11.6" rx="2.2"/><path d="M8.4 20.6h7.2"/><path d="M12 17v3.6"/>',
  radio:
    '<circle cx="12" cy="12" r="8.4"/><circle cx="12" cy="12" r="2.6"/><path d="M7.4 7.4l2.2 2.2M16.6 16.6l-2.2-2.2M7.4 16.6l2.2-2.2M16.6 7.4l-2.2 2.2"/>',
  music:
    '<circle cx="7.4" cy="17.4" r="2.6"/><circle cx="17.4" cy="15.4" r="2.6"/><path d="M10 17.4V6.6l10-1.6v10.4"/>',
  plus: '<path d="M12 5.4v13.2M5.4 12h13.2"/>',
  minus: '<path d="M5.4 12h13.2"/>',
}

function icon(name: string): string {
  return ICONS[name] || ICONS.grille
}

// ---------------------------------------------------------------- 基础
const online = computed(() => props.device.online)
const core = computed(() => speakerProps(props.spec))

function raw(def: PropDef | null | undefined): unknown {
  return def ? props.values[def.name] : undefined
}

function numOf(def: PropDef | null | undefined): number | undefined {
  const v = raw(def)
  if (v === null || v === undefined || v === '') return undefined
  const n = Number(v)
  return Number.isFinite(n) ? n : undefined
}

function boolOf(def: PropDef | null | undefined): boolean {
  const v = raw(def)
  if (v === true || v === 1 || v === '1' || v === 'true') return true
  return false
}

// ---------------------------------------------------------------- 音量
/**
 * 音量范围按规格取，不能写死 0–100：
 * 小爱音箱 Pro（lx06）的最低音量是 6，写成 0–100 会导致「拖到最左」发一个
 * 越界值被设备拒绝，滑块看起来在动但音量没变。
 */
const volProp = computed(() => core.value.volume)
const volMin = computed(() => Number(volProp.value?.range?.[0] ?? 0))
const volMax = computed(() => Number(volProp.value?.range?.[1] ?? 100))
const volStep = computed(() => Number(volProp.value?.range?.[2] ?? 1) || 1)

const devVol = computed(() => numOf(volProp.value))

/** 拖动期间用本地值，避免等设备回包才跟手 */
const localVol = ref<number | null>(null)
const shownVol = computed(() => localVol.value ?? devVol.value)

const volPct = computed(() => {
  const v = shownVol.value
  if (v === undefined) return 0
  const span = volMax.value - volMin.value
  if (span <= 0) return 0
  return Math.min(100, Math.max(0, ((v - volMin.value) / span) * 100))
})

/** 环的填充角度（从顶部顺时针） */
const fillDeg = computed(() => (volPct.value / 100) * 360)

function snap(v: number): number {
  const step = volStep.value
  const snapped = Math.round((v - volMin.value) / step) * step + volMin.value
  return Math.min(volMax.value, Math.max(volMin.value, snapped))
}

// ---- 拖动 ----
const dialRef = ref<HTMLElement | null>(null)
const dragging = ref(false)
let commitTimer: number | null = null

function commitVolume() {
  if (commitTimer !== null) {
    window.clearTimeout(commitTimer)
    commitTimer = null
  }
  const v = localVol.value
  const def = volProp.value
  if (!def || v === null) return
  if (devVol.value === v) return
  emit('set-prop', def, v)
}

/** 松手后本地值再保持一会儿，等设备回包；否则会先跳回旧值再跳新值 */
function releaseLocalSoon() {
  window.setTimeout(() => {
    localVol.value = null
  }, 900)
}

function volFromPointer(clientX: number, clientY: number) {
  const el = dialRef.value
  if (!el) return
  const r = el.getBoundingClientRect()
  const cx = r.left + r.width / 2
  const cy = r.top + r.height / 2
  const dx = clientX - cx
  const dy = clientY - cy
  // 离圆心太近时角度会剧烈抖动，拖动时忽略这一段
  if (Math.hypot(dx, dy) < r.width * 0.16) return
  // 以正上方为 0°、顺时针为正，与 conic-gradient 的起点一致
  let deg = (Math.atan2(dx, -dy) * 180) / Math.PI
  if (deg < 0) deg += 360
  const ratio = deg / 360
  localVol.value = snap(volMin.value + ratio * (volMax.value - volMin.value))
}

function onPointerDown(e: PointerEvent) {
  if (!online.value || !volProp.value) return
  dragging.value = true
  ;(e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId)
  volFromPointer(e.clientX, e.clientY)
}

function onPointerMove(e: PointerEvent) {
  if (!dragging.value) return
  volFromPointer(e.clientX, e.clientY)
  if (commitTimer !== null) window.clearTimeout(commitTimer)
  commitTimer = window.setTimeout(commitVolume, 150)
}

function onPointerUp() {
  if (!dragging.value) return
  dragging.value = false
  commitVolume()
  releaseLocalSoon()
}

function nudge(delta: number) {
  if (!online.value || !volProp.value) return
  const base = shownVol.value ?? volMin.value
  localVol.value = snap(base + delta)
  commitVolume()
  releaseLocalSoon()
}

onBeforeUnmount(() => {
  if (commitTimer !== null) window.clearTimeout(commitTimer)
})

/** 48 根刻度，随音量点亮 */
const TICKS = 48
const tickOn = computed(() =>
  Array.from({ length: TICKS }, (_, i) => i / TICKS < volPct.value / 100 - 0.0001)
)

const volLabel = computed(() => propLabel(volProp.value || { base_name: 'volume' }))

// ---------------------------------------------------------------- 播放状态
const STATE: Record<string, { label: string; tone: 'play' | 'pause' | 'idle' }> = {
  '1': { label: '播放中', tone: 'play' },
  '0': { label: '已停止', tone: 'idle' },
  '2': { label: '已暂停', tone: 'pause' },
}

const playingRaw = computed(() => {
  const def = core.value.playing
  if (!def) return undefined
  const v = raw(def)
  return v === null || v === undefined || v === '' ? undefined : String(v)
})

const playing = computed(() => (playingRaw.value ? STATE[playingRaw.value] : undefined))
const isPlaying = computed(() => playingRaw.value === '1')

/** playing-state 只 notify 不 read 的机型（lx06）读不到值，如实说明而不是假装待机 */
const playingReadable = computed(() => !!core.value.playing?.rw.includes('r'))

const stateText = computed(() => {
  if (!online.value) return '离线'
  if (!playingReadable.value) return '播放状态不可读'
  return playing.value?.label || '待机'
})

const stateTone = computed(() => {
  if (!online.value) return 'idle'
  if (!playingReadable.value) return 'idle'
  return playing.value?.tone || 'idle'
})

// ---------------------------------------------------------------- 动作
function actByName(...names: string[]): ActionDef | null {
  const list = props.spec?.actions || []
  for (const n of names) {
    const hit = list.find((a) => a.name === n)
    if (hit) return hit
  }
  return null
}

const playA = computed(() => actByName('play'))
const pauseA = computed(() => actByName('pause'))
const stopA = computed(() => actByName('stop'))
const prevA = computed(() => actByName('previous', 'prev'))
const nextA = computed(() => actByName('next'))

/** 中间那颗大按钮：正在播就暂停，否则播放 */
const mainA = computed(() => (isPlaying.value ? pauseA.value || playA.value : playA.value || pauseA.value))
const mainIcon = computed(() => (isPlaying.value ? 'pause' : 'play'))

function run(a: ActionDef | null | undefined) {
  if (!a || !online.value) return
  emit('run-action', a.siid, a.aiid, actionLabel(a))
}

/** 「更多设置」里的动作只有 siid/aiid/中文名，没有完整 ActionDef */
function runRaw(siid: number, aiid: number, name: string) {
  if (!online.value) return
  emit('run-action', siid, aiid, name)
}

const hasTransport = computed(() => !!(playA.value || pauseA.value || stopA.value || prevA.value || nextA.value))

// ---------------------------------------------------------------- 开关磁贴
interface Tile {
  key: string
  label: string
  icon: string
  def: PropDef
  on: boolean
  note: string
}

/**
 * 磁贴只放「开关型」能力：静音、勿扰、睡眠、闹钟。
 * 数值型与文本型（循环模式、铃声、时间段）留在下方「更多设置」，避免混排。
 */
const tiles = computed<Tile[]>(() => {
  const out: Tile[] = []
  const add = (key: string, label: string, ico: string, def: PropDef | null, note: string) => {
    if (!def) return
    out.push({ key, label, icon: ico, def, on: boolOf(def), note })
  }
  add('mic', '麦克风静音', 'mic', core.value.micMute, '关闭后小爱听不到唤醒词')
  add('spk', '扬声器静音', 'mute', core.value.spkMute, '只关声音，播放不中断')
  add('dnd', '勿扰模式', 'bellOff', core.value.noDisturb, '不再播报提醒与通知')
  add('sleep', '睡眠模式', 'moon', core.value.sleepMode, '降低亮度与提示音')
  add('alarm', '闹钟', 'bell', core.value.alarmOn, '关闭后闹钟不会响')
  return out
})

function toggleTile(t: Tile) {
  if (!online.value || props.busyProp === t.def.name) return
  emit('set-prop', t.def, !t.on)
}

/**
 * 磁贴图标反映"当前状态"而不是"点它会怎样"：
 * 麦克风已静音就画划掉的麦克风，勿扰已开启就画划掉的铃铛。
 */
function tileIcon(t: Tile): string {
  if (t.key === 'mic') return t.on ? 'micOff' : 'mic'
  if (t.key === 'dnd') return t.on ? 'bellOff' : 'bell'
  if (t.key === 'spk') return t.on ? 'mute' : 'grille'
  return t.icon
}

// ---------------------------------------------------------------- 快捷指令
/**
 * 主面板只放**无参数**的动作。
 * 朗读文本 / 执行文本指令需要带一段文本参数，界面上没有输入框，
 * 直接调用会失败，所以留在「更多设置」里由通用控件处理。
 */
const QUICK: { keys: string[]; icon: string }[] = [
  { keys: ['wake-up'], icon: 'spark' },
  { keys: ['tv-switchon', 'tv-switch'], icon: 'tv' },
  { keys: ['stop-alarm'], icon: 'bell' },
  { keys: ['play-radio'], icon: 'radio' },
  { keys: ['play-music'], icon: 'music' },
]

const quickActs = computed<{ a: ActionDef; icon: string }[]>(() => {
  const out: { a: ActionDef; icon: string }[] = []
  const used = new Set<string>()
  for (const q of QUICK) {
    const a = actByName(...q.keys)
    if (!a || used.has(a.name)) continue
    used.add(a.name)
    out.push({ a, icon: q.icon })
  }
  return out
})

// ---------------------------------------------------------------- 更多设置
/** 面板已消费的属性 */
const consumedKeys = computed(() => {
  const keys = new Set<string>()
  for (const d of [
    core.value.volume,
    core.value.playing,
    core.value.micMute,
    core.value.spkMute,
    core.value.noDisturb,
    core.value.sleepMode,
    core.value.alarmOn,
  ]) {
    if (d) keys.add(`${d.siid}:${d.piid}`)
  }
  return keys
})

/** 面板已消费的动作（播放控制 + 快捷指令），避免下方重复出现同一个按钮 */
const consumedActions = computed(() => {
  const keys = new Set<string>()
  for (const a of [playA.value, pauseA.value, stopA.value, prevA.value, nextA.value]) {
    if (a) keys.add(`${a.siid}:${a.aiid}`)
  }
  for (const q of quickActs.value) keys.add(`${q.a.siid}:${q.a.aiid}`)
  return keys
})

const restGroups = computed<SpecGroup[]>(() => {
  const pk = consumedKeys.value
  const ak = consumedActions.value
  return props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !pk.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !pk.has(`${p.siid}:${p.piid}`)),
      actions: g.actions.filter((a) => !ak.has(`${a.siid}:${a.aiid}`)),
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})

// ---------------------------------------------------------------- 手动刷新
const refreshing = ref(false)
function doRefresh() {
  if (refreshing.value) return
  refreshing.value = true
  emit('refresh')
  window.setTimeout(() => {
    refreshing.value = false
  }, 700)
}

const shownVolText = computed(() =>
  shownVol.value === undefined ? '—' : String(Math.round(shownVol.value))
)
</script>

<template>
  <div v-if="spec" class="spp">
    <!-- 音量盘 -->
    <section class="hero" :class="{ off: !online, playing: isPlaying }">
      <header class="hero-head">
        <div class="badge" :class="stateTone">
          <span class="dot" />
          <span class="badge-label">{{ stateText }}</span>
        </div>
        <div class="head-right">
          <span class="where">{{ device.room_name || '在线' }}</span>
          <button
            class="refresh"
            :class="{ spin: refreshing }"
            :disabled="!online || refreshing"
            title="重新读取"
            @click="doRefresh"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">
              <path d="M20 11.5A8 8 0 0 0 6.3 6.6L4 9" />
              <path d="M4 4.6V9h4.4" />
              <path d="M4 12.5A8 8 0 0 0 17.7 17.4L20 15" />
              <path d="M20 19.4V15h-4.4" />
            </svg>
          </button>
        </div>
      </header>

      <div class="dial-wrap">
        <button class="nudge left" :disabled="!online || !volProp" title="音量 -5" @click="nudge(-5)">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" v-html="icon('minus')" />
        </button>

        <div
          ref="dialRef"
          class="dial"
          :class="{ dragging }"
          :style="{ '--deg': `${fillDeg}deg` }"
          @pointerdown="onPointerDown"
          @pointermove="onPointerMove"
          @pointerup="onPointerUp"
          @pointercancel="onPointerUp"
        >
          <!-- 已点亮的刻度：外圈一圈细线，像电平表 -->
          <div class="ticks">
            <i
              v-for="(on, i) in tickOn"
              :key="i"
              :class="{ on }"
              :style="{ transform: `rotate(${(i / TICKS) * 360}deg) translateY(-82px)` }"
            />
          </div>
          <div class="ring" />
          <div class="core">
            <svg
              class="core-ico"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="1.6"
              stroke-linecap="round"
              stroke-linejoin="round"
              v-html="icon(boolOf(core.spkMute) ? 'mute' : 'grille')"
            />
            <b class="core-num">{{ shownVolText }}</b>
            <span class="core-label">{{ volLabel }}</span>
          </div>
        </div>

        <button class="nudge right" :disabled="!online || !volProp" title="音量 +5" @click="nudge(5)">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" v-html="icon('plus')" />
        </button>
      </div>

      <div class="scale">
        <span>{{ volMin }}</span>
        <span class="scale-hint">在圆环上拖动调节</span>
        <span>{{ volMax }}</span>
      </div>
    </section>

    <!-- 播放控制 -->
    <section v-if="hasTransport" class="transport" :class="{ off: !online }">
      <button class="tp" :disabled="!online || !prevA" title="上一首" @click="run(prevA)">
        <svg viewBox="0 0 24 24" fill="currentColor" stroke="none" v-html="icon('prev')" />
      </button>
      <button class="tp main" :disabled="!online || !mainA" :title="mainA ? actionLabel(mainA) : '播放'" @click="run(mainA)">
        <svg viewBox="0 0 24 24" fill="currentColor" stroke="none" v-html="icon(mainIcon)" />
      </button>
      <button class="tp" :disabled="!online || !nextA" title="下一首" @click="run(nextA)">
        <svg viewBox="0 0 24 24" fill="currentColor" stroke="none" v-html="icon('next')" />
      </button>
      <button v-if="stopA" class="tp side" :disabled="!online" :title="actionLabel(stopA)" @click="run(stopA)">
        <svg viewBox="0 0 24 24" fill="currentColor" stroke="none" v-html="icon('stop')" />
      </button>
    </section>

    <!-- 开关磁贴 -->
    <section v-if="tiles.length" class="tiles">
      <button
        v-for="t in tiles"
        :key="t.key"
        class="tile"
        :class="{ on: t.on, busy: busyProp === t.def.name }"
        :disabled="!online || busyProp === t.def.name"
        :title="t.note"
        @click="toggleTile(t)"
      >
        <span class="tile-ico">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="1.7"
            stroke-linecap="round"
            stroke-linejoin="round"
            v-html="icon(tileIcon(t))"
          />
        </span>
        <span class="tile-text">
          <span class="tile-label">{{ t.label }}</span>
          <span class="tile-state">{{ t.on ? '已开启' : '已关闭' }}</span>
        </span>
      </button>
    </section>

    <!-- 快捷指令 -->
    <section v-if="quickActs.length" class="quick">
      <h3>快捷指令</h3>
      <div class="quick-row">
        <button
          v-for="q in quickActs"
          :key="`${q.a.siid}-${q.a.aiid}`"
          class="chip"
          :disabled="!online"
          @click="run(q.a)"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" v-html="icon(q.icon)" />
          {{ actionLabel(q.a) }}
        </button>
      </div>
    </section>

    <!-- 其余参数 / 动作 -->
    <details v-if="restGroups.length" class="adv">
      <summary>
        <span>更多设置</span>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M8 10l4 4 4-4" /></svg>
      </summary>
      <div class="adv-body">
        <section v-for="g in restGroups" :key="g.key">
          <h4>{{ g.title }}</h4>
          <PropControl
            v-for="p in g.writable"
            :key="`${p.siid}-${p.piid}`"
            :def="p"
            :value="values[p.name]"
            :disabled="!online || busyProp === p.name"
            @change="(v) => emit('set-prop', p, v)"
          />
          <div v-if="g.readable.length" class="readable">
            <PropControl
              v-for="p in g.readable"
              :key="`${p.siid}-${p.piid}`"
              :def="p"
              :value="values[p.name]"
              disabled
            />
          </div>
          <div v-if="g.actions.length" class="acts">
            <button
              v-for="a in g.actions"
              :key="`${a.siid}-${a.aiid}`"
              class="btn btn-quiet"
              :disabled="!online"
              @click="runRaw(a.siid, a.aiid, a.name)"
            >
              {{ a.name }}
            </button>
          </div>
        </section>
      </div>
    </details>

    <p class="hint">
      {{ volProp ? `音量范围 ${volMin}–${volMax}` : '该设备未提供音量控制' }}
    </p>
  </div>
</template>

<style scoped>
.spp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.spp > section,
.spp > details {
  animation: fade-up 0.32s var(--ease) both;
}

.spp > section:nth-of-type(2) {
  animation-delay: 0.06s;
}

.spp > section:nth-of-type(3) {
  animation-delay: 0.1s;
}

.spp > section:nth-of-type(4) {
  animation-delay: 0.14s;
}

/* ------------------------------------------------ 音量盘 */
.hero {
  margin-top: 12px;
  padding: 14px 14px 12px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--grad-surface);
  box-shadow: var(--shadow-sm);
  position: relative;
  overflow: hidden;
}

.hero::before {
  content: '';
  position: absolute;
  inset: -70% 30% auto -30%;
  height: 240px;
  background: radial-gradient(circle, rgba(52, 140, 255, 0.15), transparent 66%);
  pointer-events: none;
}

.hero.playing::before {
  background: radial-gradient(circle, rgba(23, 178, 106, 0.17), transparent 66%);
}

.hero.off {
  opacity: 0.55;
  filter: saturate(0.5);
}

.hero-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 11px 0 9px;
  border-radius: 999px;
  font-size: 11.5px;
  font-weight: 600;
}

.badge .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex: none;
  background: currentColor;
}

.badge.play {
  color: #12945a;
  background: rgba(23, 178, 106, 0.12);
  border: 1px solid rgba(23, 178, 106, 0.32);
}

.badge.play .dot {
  animation: breathe 2s ease-in-out infinite;
}

.badge.pause {
  color: #b47708;
  background: rgba(236, 168, 44, 0.14);
  border: 1px solid rgba(236, 168, 44, 0.34);
}

.badge.idle {
  color: var(--text-2);
  background: var(--surface-3);
  border: 1px solid var(--border);
}

[data-theme='dark'] .badge.play {
  color: #4ade80;
}
[data-theme='dark'] .badge.pause {
  color: #fbbf24;
}

.where {
  font-size: 11px;
  color: var(--text-3);
  flex: none;
}

.head-right {
  display: flex;
  align-items: center;
  gap: 6px;
  flex: none;
}

.refresh {
  width: 26px;
  height: 26px;
  display: grid;
  place-items: center;
  border-radius: 8px;
  color: var(--text-3);
  background: transparent;
  transition: color 0.16s ease, background-color 0.16s ease;
}

.refresh svg {
  width: 14px;
  height: 14px;
}

.refresh:hover:not(:disabled) {
  color: var(--brand);
  background: var(--brand-soft);
}

.refresh:disabled {
  opacity: 0.45;
}

.refresh.spin svg {
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

/* 圆盘 + 两侧加减 */
.dial-wrap {
  margin-top: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.nudge {
  width: 32px;
  height: 32px;
  border-radius: 11px;
  flex: none;
  display: grid;
  place-items: center;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: color 0.16s ease, background-color 0.16s ease, transform 0.14s var(--ease);
}

.nudge svg {
  width: 15px;
  height: 15px;
}

.nudge:hover:not(:disabled) {
  color: #fff;
  background: var(--accent);
  border-color: transparent;
}

.nudge:active:not(:disabled) {
  transform: scale(0.93);
}

.nudge:disabled {
  opacity: 0.4;
}

.dial {
  position: relative;
  width: 176px;
  height: 176px;
  flex: none;
  border-radius: 50%;
  cursor: pointer;
  touch-action: none;
  user-select: none;
  -webkit-user-select: none;
  /* 未填充部分的轨道色。用 surface-3 太接近卡片底色，看不出环的边界 */
  --ring-track: rgba(148, 163, 184, 0.26);
}

[data-theme='dark'] .dial {
  --ring-track: #17253c;
}

/* 环：conic-gradient 从顶部顺时针填充，与角度计算一致 */
.ring {
  position: absolute;
  inset: 12px;
  border-radius: 50%;
  background: conic-gradient(
    from 0deg,
    rgba(23, 178, 106, 0.85) 0deg,
    rgba(52, 140, 255, 0.95) var(--deg),
    var(--ring-track) var(--deg),
    var(--ring-track) 360deg
  );
  /* 必须写 closest-side：默认的 farthest-corner 半径是外接圆的 107.5px，
     而元素只有 76px，68% 换算下来环带只剩 3px，细到几乎看不见 */
  -webkit-mask: radial-gradient(circle closest-side, transparent 86.5%, #000 87%);
  mask: radial-gradient(circle closest-side, transparent 86.5%, #000 87%);
}

[data-theme='dark'] .ring {
  background: conic-gradient(
    from 0deg,
    rgba(74, 222, 128, 0.8) 0deg,
    rgba(96, 165, 250, 0.95) var(--deg),
    var(--ring-track) var(--deg),
    var(--ring-track) 360deg
  );
}

/* 拖动时去掉过渡，避免跟手发飘 */
.dial.dragging .ring {
  transition: none;
}

/* 刻度：外圈细线，点亮的用强调色 */
.ticks {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  pointer-events: none;
}

.ticks i {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 2px;
  height: 5px;
  margin: -2.5px 0 0 -1px;
  border-radius: 1px;
  background: rgba(148, 163, 184, 0.34);
  transition: background-color 0.3s ease;
}

.ticks i.on {
  background: rgba(52, 140, 255, 0.75);
}

[data-theme='dark'] .ticks i {
  background: rgba(148, 163, 184, 0.28);
}

[data-theme='dark'] .ticks i.on {
  background: rgba(96, 165, 250, 0.85);
}

.core {
  position: absolute;
  inset: 30px;
  border-radius: 50%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1px;
  background: var(--surface);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.6), 0 2px 10px rgba(22, 35, 58, 0.06);
}

[data-theme='dark'] .core {
  background: #0d1626;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.04);
}

.core-ico {
  width: 17px;
  height: 17px;
  color: var(--text-3);
  margin-bottom: 1px;
}

.hero.playing .core-ico {
  color: rgba(23, 178, 106, 0.9);
}

.core-num {
  font-size: 34px;
  line-height: 1.02;
  font-weight: 700;
  letter-spacing: -1px;
  font-variant-numeric: tabular-nums;
  color: var(--text);
}

.core-label {
  font-size: 10px;
  color: var(--text-3);
  letter-spacing: 0.4px;
}

.scale {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 9px;
  padding: 0 34px;
  font-size: 9.5px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  position: relative;
  z-index: 1;
}

.scale-hint {
  font-size: 9.5px;
  opacity: 0.8;
}

/* ------------------------------------------------ 播放控制 */
.transport {
  margin-top: 13px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 12px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--surface-2);
}

.transport.off {
  opacity: 0.55;
}

.tp {
  width: 40px;
  height: 40px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  color: var(--text-2);
  background: var(--surface);
  border: 1px solid var(--border);
  transition: color 0.16s ease, background-color 0.16s ease, transform 0.14s var(--ease), box-shadow 0.16s ease;
}

.tp svg {
  width: 17px;
  height: 17px;
}

.tp:hover:not(:disabled) {
  color: var(--brand);
  border-color: rgba(52, 140, 255, 0.35);
  box-shadow: 0 3px 10px rgba(52, 140, 255, 0.16);
}

.tp:active:not(:disabled) {
  transform: scale(0.93);
}

.tp:disabled {
  opacity: 0.36;
}

/* 中间主按钮：唯一实心，视觉焦点 */
.tp.main {
  width: 52px;
  height: 52px;
  color: #fff;
  background: var(--grad-accent);
  border: none;
  box-shadow: 0 6px 16px rgba(23, 178, 106, 0.32);
}

.tp.main svg {
  width: 21px;
  height: 21px;
}

.tp.main:hover:not(:disabled) {
  color: #fff;
  box-shadow: 0 8px 22px rgba(23, 178, 106, 0.42);
}

/* 停止按钮比两侧更弱，避免与主按钮争夺注意力 */
.tp.side {
  width: 34px;
  height: 34px;
  background: transparent;
}

.tp.side svg {
  width: 13px;
  height: 13px;
}

/* ------------------------------------------------ 磁贴 */
.tiles {
  margin-top: 13px;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 9px;
}

.tile {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 11px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
  background: var(--surface-2);
  text-align: left;
  transition: border-color 0.16s ease, background-color 0.16s ease, box-shadow 0.16s ease;
}

.tile:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface-3);
}

.tile:disabled {
  opacity: 0.5;
}

.tile.on {
  border-color: rgba(23, 178, 106, 0.4);
  background: rgba(23, 178, 106, 0.08);
}

.tile.busy {
  opacity: 0.6;
}

/* 磁贴数是奇数时（3 个或 5 个），最后一个占满整行，右侧不留半格空洞 */
.tile:last-child:nth-child(odd):not(:first-child) {
  grid-column: span 2;
}

.tile-ico {
  width: 30px;
  height: 30px;
  border-radius: 9px;
  flex: none;
  display: grid;
  place-items: center;
  color: var(--text-3);
  background: var(--surface-3);
  transition: color 0.16s ease, background-color 0.16s ease;
}

.tile-ico svg {
  width: 15px;
  height: 15px;
}

.tile.on .tile-ico {
  color: #fff;
  background: var(--accent);
}

.tile-text {
  display: flex;
  flex-direction: column;
  gap: 1px;
  min-width: 0;
}

.tile-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tile-state {
  font-size: 10px;
  color: var(--text-3);
}

.tile.on .tile-state {
  color: rgba(23, 178, 106, 0.9);
}

[data-theme='dark'] .tile.on .tile-state {
  color: #4ade80;
}

/* ------------------------------------------------ 快捷指令 */
.quick {
  margin-top: 15px;
}

h3 {
  margin: 0 0 8px;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.3px;
  color: var(--text-2);
  display: flex;
  align-items: center;
  gap: 7px;
}

h3::before {
  content: '';
  width: 3px;
  height: 11px;
  border-radius: 2px;
  background: var(--grad-brand);
}

.quick-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 12px;
  border-radius: 999px;
  font-size: 11.5px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  transition: color 0.16s ease, border-color 0.16s ease, background-color 0.16s ease;
}

.chip svg {
  width: 13px;
  height: 13px;
  opacity: 0.85;
}

.chip:hover:not(:disabled) {
  color: var(--brand);
  border-color: rgba(52, 140, 255, 0.35);
  background: var(--brand-soft);
}

.chip:disabled {
  opacity: 0.45;
}

/* ------------------------------------------------ 更多设置 */
.adv {
  margin-top: 16px;
  border-top: 1px solid var(--border);
  padding-top: 4px;
}

.adv summary {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 0;
  font-size: 12.5px;
  color: var(--text-2);
  cursor: pointer;
  list-style: none;
}

.adv summary::-webkit-details-marker {
  display: none;
}

.adv summary:hover {
  color: var(--text);
}

.adv summary svg {
  width: 15px;
  height: 15px;
  transition: transform 0.2s var(--ease);
}

.adv[open] summary svg {
  transform: rotate(180deg);
}

h4 {
  margin: 10px 0 2px;
  font-size: 11.5px;
  font-weight: 600;
  color: var(--text-3);
}

.readable {
  padding: 2px 10px 4px;
  border-radius: 10px;
  background: var(--surface-2);
}

.readable :deep(.prop) {
  border-bottom: none;
  padding: 6px 0;
}

.acts {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  padding-top: 8px;
}

.hint {
  margin: 14px 0 0;
  font-size: 10.5px;
  color: var(--text-3);
  text-align: center;
  line-height: 1.6;
}
</style>
