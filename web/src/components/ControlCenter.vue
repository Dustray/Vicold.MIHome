<script setup lang="ts">
/**
 * 环境页的控制中心。
 *
 * 与仪表板（EnvironmentView 的左半边）并排，形成「看数据 + 改设备」
 * 一体化的布局。这里只放可操作的设备：吸顶灯 / 空调 / 物理通断开关，
 * 按房间分组，每类都能展开调参（亮度、色温、模式、目标温度）。
 */
import { computed, ref } from 'vue'
import type { EnvAdjust, EnvControl } from '../lib/env'
import { propLabel } from '../lib/zh'
import PowerToggle from './PowerToggle.vue'

const props = defineProps<{
  controls: EnvControl[]
  busy?: Record<string, boolean>
  /** 聚焦的房间（空 = 全屋），与仪表板共用同一个筛选状态 */
  roomFilter?: string
}>()

const emit = defineEmits<{
  (e: 'toggle', c: EnvControl): void
  (e: 'adjust', c: EnvControl, adj: EnvAdjust, value: unknown): void
  (e: 'update:roomFilter', roomId: string): void
}>()

/** 房间筛选：'' = 全屋 */
const roomFilter = computed(() => props.roomFilter || '')

function selectRoom(id: string) {
  emit('update:roomFilter', roomFilter.value === id ? '' : id)
}

const roomTabs = computed(() => {
  const map = new Map<string, string>()
  for (const c of props.controls) {
    if (!map.has(c.device.room_id)) map.set(c.device.room_id, c.roomName)
  }
  return [...map.entries()].map(([id, name]) => ({ id, name }))
})

const list = computed(() =>
  roomFilter.value ? props.controls.filter((c) => c.device.room_id === roomFilter.value) : props.controls
)

interface Group {
  roomId: string
  roomName: string
  lights: EnvControl[]
  acs: EnvControl[]
  switches: EnvControl[]
}

const groups = computed<Group[]>(() => {
  const map = new Map<string, Group>()
  for (const c of list.value) {
    const rid = c.device.room_id
    let g = map.get(rid)
    if (!g) {
      g = { roomId: rid, roomName: c.roomName, lights: [], acs: [], switches: [] }
      map.set(rid, g)
    }
    if (c.kind === 'light') g.lights.push(c)
    else if (c.kind === 'ac') g.acs.push(c)
    else g.switches.push(c)
  }
  return [...map.values()]
})

/** 展开调参面板的设备 */
const expanded = ref('')

function isOpen(c: EnvControl) {
  return expanded.value === c.device.did
}

function toggleExpand(c: EnvControl) {
  expanded.value = expanded.value === c.device.did ? '' : c.device.did
}

function onToggle(c: EnvControl) {
  if (!c.online) return
  emit('toggle', c)
}

/** 调节项当前值（滑杆用） */
function adjValue(adj: EnvAdjust) {
  const v = Number(adj.value)
  return Number.isFinite(v) ? v : adj.min
}

function onRange(c: EnvControl, adj: EnvAdjust, ev: Event) {
  const raw = Number((ev.target as HTMLInputElement).value)
  emit('adjust', c, adj, adj.def.type === 'float' ? raw : Math.round(raw))
}

function onEnum(c: EnvControl, adj: EnvAdjust, value: number | string) {
  if (String(adj.value) === String(value)) return
  emit('adjust', c, adj, value)
}

/** 枚举按钮的显示名 */
function enumLabel(adj: EnvAdjust, value: number | string) {
  return adj.options?.find((o) => String(o.value) === String(value))?.label ?? String(value)
}

/** 滑杆已填充百分比 */
function fillPct(adj: EnvAdjust) {
  const span = adj.max - adj.min
  if (!span) return '50%'
  const v = Number(adj.value)
  const pct = Number.isFinite(v) ? ((v - adj.min) / span) * 100 : 0
  return `${Math.min(100, Math.max(0, pct))}%`
}

function adjLabel(adj: EnvAdjust) {
  return adj.label || propLabel(adj.def)
}

/** 数值调节项的单位 */
function adjUnit(adj: EnvAdjust) {
  if (adj.def.base_name === 'brightness') return '%'
  if (adj.def.base_name === 'color-temperature') return 'K'
  if (adj.def.base_name === 'target-temperature') return '°C'
  return ''
}
</script>

<template>
  <section class="cc">
    <header class="cc-head">
      <h2>控制中心</h2>
      <span class="cc-count">{{ controls.length }} 台可控制设备</span>
    </header>

    <!-- 房间筛选 -->
    <div class="tabs">
      <button class="tab" :class="{ active: roomFilter === '' }" @click="selectRoom('')">
        全部
      </button>
      <button
        v-for="r in roomTabs"
        :key="r.id"
        class="tab"
        :class="{ active: roomFilter === r.id }"
        @click="selectRoom(r.id)"
      >
        {{ r.name }}
      </button>
    </div>

    <div class="cc-scroll">
      <div v-if="!groups.length" class="cc-empty">这个房间没有可控制的设备</div>

      <article v-for="g in groups" :key="g.roomId" class="grp">
        <h3 class="grp-title">
          {{ g.roomName }}
          <em>{{ g.lights.length + g.acs.length + g.switches.length }}</em>
        </h3>

        <!-- 吸顶灯 -->
        <div v-if="g.lights.length" class="grp-sec">
          <span class="sec-cap">吸顶灯</span>
          <div v-for="c in g.lights" :key="c.device.did" class="item" :class="{ open: isOpen(c) }">
            <div class="row">
              <span class="ico light">
                <svg viewBox="0 0 24 24" fill="none">
                  <path d="M9 18h6M10 21h4" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
                  <path d="M12 3a6 6 0 0 0-3.5 10.9c.6.5.9 1.1 1 1.8l.1.8h4.8l.1-.8c.1-.7.4-1.3 1-1.8A6 6 0 0 0 12 3Z"
                    stroke="currentColor" stroke-width="1.7" stroke-linejoin="round" />
                </svg>
              </span>
              <span class="nm">{{ c.name }}</span>
              <span class="dt">{{ c.detail }}</span>
              <PowerToggle
                :model-value="!!c.on"
                size="sm"
                :disabled="!c.online || !!busy?.[c.device.did]"
                :busy="!!busy?.[c.device.did]"
                :label="`${c.name} 开关`"
                @update:model-value="() => onToggle(c)"
              />
              <button class="more" :class="{ on: isOpen(c) }" @click="toggleExpand(c)">
                <svg viewBox="0 0 24 24" fill="none">
                  <path d="M6 9l6 6 6-6" stroke="currentColor" stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" />
                </svg>
              </button>
            </div>
            <div v-if="isOpen(c) && c.adjust.length" class="adj">
              <div v-for="adj in c.adjust" :key="adj.def.name" class="adj-row">
                <span class="adj-cap">{{ adjLabel(adj) }}</span>
                <template v-if="adj.kind === 'range'">
                  <input
                    class="slider"
                    type="range"
                    :min="adj.min"
                    :max="adj.max"
                    :step="adj.step"
                    :value="adjValue(adj)"
                    :disabled="!c.online"
                    :style="{ '--fill': fillPct(adj) }"
                    @change="onRange(c, adj, $event)"
                  />
                  <span class="adj-val">{{ Math.round(adjValue(adj)) }}{{ adjUnit(adj) }}</span>
                </template>
                <template v-else>
                  <div class="enum-row">
                    <button
                      v-for="o in adj.options"
                      :key="String(o.value)"
                      class="chip"
                      :class="{ active: String(adj.value) === String(o.value) }"
                      :disabled="!c.online"
                      @click="onEnum(c, adj, o.value)"
                    >
                      {{ enumLabel(adj, o.value) }}
                    </button>
                  </div>
                </template>
              </div>
            </div>
          </div>
        </div>

        <!-- 空调 -->
        <div v-if="g.acs.length" class="grp-sec">
          <span class="sec-cap">空调</span>
          <div v-for="c in g.acs" :key="c.device.did" class="item" :class="{ open: isOpen(c) }">
            <div class="row">
              <span class="ico ac">
                <svg viewBox="0 0 24 24" fill="none">
                  <path d="M12 3v18M4.2 7.5l15.6 9M19.8 7.5l-15.6 9" stroke="currentColor" stroke-width="1.7"
                    stroke-linecap="round" />
                </svg>
              </span>
              <span class="nm">{{ c.name }}</span>
              <span class="dt">{{ c.detail }}</span>
              <PowerToggle
                :model-value="!!c.on"
                size="sm"
                :disabled="!c.online || !!busy?.[c.device.did]"
                :busy="!!busy?.[c.device.did]"
                :label="`${c.name} 开关`"
                @update:model-value="() => onToggle(c)"
              />
              <button class="more" :class="{ on: isOpen(c) }" @click="toggleExpand(c)">
                <svg viewBox="0 0 24 24" fill="none">
                  <path d="M6 9l6 6 6-6" stroke="currentColor" stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" />
                </svg>
              </button>
            </div>
            <div v-if="isOpen(c) && c.adjust.length" class="adj">
              <div v-for="adj in c.adjust" :key="adj.def.name" class="adj-row">
                <span class="adj-cap">{{ adjLabel(adj) }}</span>
                <template v-if="adj.kind === 'range'">
                  <input
                    class="slider"
                    type="range"
                    :min="adj.min"
                    :max="adj.max"
                    :step="adj.step"
                    :value="adjValue(adj)"
                    :disabled="!c.online"
                    :style="{ '--fill': fillPct(adj) }"
                    @change="onRange(c, adj, $event)"
                  />
                  <span class="adj-val">{{ Math.round(adjValue(adj)) }}{{ adjUnit(adj) }}</span>
                </template>
                <template v-else>
                  <div class="enum-row">
                    <button
                      v-for="o in adj.options"
                      :key="String(o.value)"
                      class="chip"
                      :class="{ active: String(adj.value) === String(o.value) }"
                      :disabled="!c.online"
                      @click="onEnum(c, adj, o.value)"
                    >
                      {{ enumLabel(adj, o.value) }}
                    </button>
                  </div>
                </template>
              </div>
            </div>
          </div>
        </div>

        <!-- 物理通断开关 -->
        <div v-if="g.switches.length" class="grp-sec">
          <span class="sec-cap">物理通断开关</span>
          <div v-for="c in g.switches" :key="c.device.did" class="item">
            <div class="row">
              <span class="ico swt">
                <svg viewBox="0 0 24 24" fill="none">
                  <path d="M13 2 5 13h6l-1 9 8-11h-6l1-9Z" stroke="currentColor" stroke-width="1.7"
                    stroke-linejoin="round" />
                </svg>
              </span>
              <span class="nm">{{ c.name }}</span>
              <span class="tag">物理通断</span>
              <PowerToggle
                :model-value="!!c.on"
                size="sm"
                :disabled="!c.online || !!busy?.[c.device.did]"
                :busy="!!busy?.[c.device.did]"
                :label="`${c.name} 开关`"
                @update:model-value="() => onToggle(c)"
              />
            </div>
          </div>
        </div>
      </article>

      <div v-if="!controls.length" class="cc-empty">这个家庭还没有可控制的设备</div>
    </div>
  </section>
</template>

<style scoped>
.cc {
  display: flex;
  flex-direction: column;
  min-height: 0;
  height: 100%;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-sm);
  /*
   * 卡片自身不滚，滚动交给内部的 .cc-scroll。
   * 外层 .env-wrap 已改为 overflow:hidden，这里靠 grid 行高（100%）拿到确定高度，
   * .cc-scroll 的 flex:1 + overflow-y:auto 才真正生效 —— 滚轮只滚内容区。
   */
  overflow: hidden;
  overscroll-behavior: contain;
}

.cc-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  padding: 14px 16px 10px;
}

.cc-head h2 {
  margin: 0;
  font-size: 15px;
  font-weight: 650;
  color: var(--text);
}

.cc-count {
  font-size: 11px;
  color: var(--text-3);
}

.tabs {
  display: flex;
  flex-wrap: wrap;
  gap: 5px;
  padding: 0 16px 10px;
  border-bottom: 1px solid var(--border);
}

.tab {
  font-size: 11.5px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 3px 11px;
  transition: all 0.16s var(--ease);
}

.tab:hover {
  border-color: var(--border-strong);
  color: var(--text);
}

.tab.active {
  color: #fff;
  background: var(--grad-brand);
  border-color: transparent;
  box-shadow: var(--glow-brand);
}

.cc-scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  /* contain：滚到顶/底后不再把滚动链传给页面 */
  overscroll-behavior: contain;
  scrollbar-gutter: stable;
  /*
   * 关掉滚动锚定：设备是异步拉回来的，内容从空变满时 Chromium 会挑一个
   * 视口内的元素当锚点来「保持位置」，结果是列表一加载就跳到中段。
   */
  overflow-anchor: none;
  padding: 12px 16px 18px;
}

.cc-empty {
  padding: 30px 0;
  text-align: center;
  font-size: 12px;
  color: var(--text-3);
}

.grp + .grp {
  margin-top: 16px;
  padding-top: 14px;
  border-top: 1px dashed var(--border);
}

.grp-title {
  margin: 0 0 8px;
  font-size: 12.5px;
  font-weight: 650;
  color: var(--text);
  display: flex;
  align-items: center;
  gap: 6px;
}

.grp-title em {
  font-style: normal;
  font-size: 10px;
  color: var(--text-3);
  background: var(--surface-2);
  border-radius: 999px;
  padding: 1px 7px;
}

.grp-sec + .grp-sec {
  margin-top: 10px;
}

.sec-cap {
  display: block;
  font-size: 10px;
  color: var(--text-3);
  letter-spacing: 0.5px;
  margin-bottom: 4px;
  padding-left: 2px;
}

.item {
  border-radius: 10px;
  background: var(--surface-2);
  border: 1px solid transparent;
  transition: border-color 0.18s var(--ease), background 0.18s var(--ease);
}

.item.open {
  background: var(--surface);
  border-color: var(--border);
  box-shadow: var(--shadow-sm);
}

.row {
  display: flex;
  align-items: center;
  gap: 9px;
  padding: 7px 9px;
}

.ico {
  width: 27px;
  height: 27px;
  flex: none;
  border-radius: 8px;
  display: grid;
  place-items: center;
  background: var(--surface);
  border: 1px solid var(--border);
  color: var(--text-3);
  transition: all 0.2s var(--ease);
}

.ico svg {
  width: 15px;
  height: 15px;
}

.item:has(.sw.on) .ico.light {
  color: var(--accent);
  border-color: rgba(23, 178, 106, 0.4);
  background: var(--accent-soft);
}

.item:has(.sw.on) .ico.ac {
  color: var(--brand);
  border-color: rgba(47, 109, 246, 0.4);
  background: var(--brand-soft);
}

.item:has(.sw.on) .ico.swt {
  color: var(--accent);
  border-color: rgba(23, 178, 106, 0.4);
  background: var(--accent-soft);
}

.nm {
  font-size: 12.5px;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 128px;
}

.dt {
  font-size: 10.5px;
  color: var(--text-3);
  margin-left: auto;
  white-space: nowrap;
}

.tag {
  font-size: 9.5px;
  color: var(--accent);
  background: var(--accent-soft);
  border-radius: 999px;
  padding: 1px 7px;
  white-space: nowrap;
}

.more {
  width: 22px;
  height: 22px;
  flex: none;
  display: grid;
  place-items: center;
  border-radius: 6px;
  color: var(--text-3);
  transition: transform 0.22s var(--ease), background 0.18s var(--ease);
}

.more svg {
  width: 13px;
  height: 13px;
}

.more:hover {
  background: var(--surface-3);
}

.more.on {
  transform: rotate(180deg);
  color: var(--brand);
}

.adj {
  padding: 4px 10px 10px 45px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.adj-row {
  display: flex;
  align-items: center;
  gap: 9px;
}

.adj-cap {
  font-size: 10.5px;
  color: var(--text-3);
  width: 42px;
  flex: none;
}

.slider {
  flex: 1;
  min-width: 0;
  appearance: none;
  height: 5px;
  border-radius: 5px;
  background: linear-gradient(90deg, var(--accent) 0%, var(--accent) var(--fill, 50%), var(--border-strong) var(--fill, 50%));
  outline: none;
}

.slider::-webkit-slider-thumb {
  appearance: none;
  width: 15px;
  height: 15px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--accent);
  box-shadow: 0 0 9px rgba(23, 178, 106, 0.5);
  cursor: pointer;
}

.adj-val {
  font-size: 11px;
  color: var(--text);
  font-variant-numeric: tabular-nums;
  width: 46px;
  text-align: right;
  flex: none;
}

.enum-row {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.chip {
  font-size: 11px;
  color: var(--text-2);
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 3px 10px;
  transition: all 0.16s var(--ease);
}

.chip:hover:not(:disabled) {
  border-color: var(--brand);
  color: var(--brand);
}

.chip.active {
  color: #fff;
  background: var(--grad-brand);
  border-color: transparent;
}

.chip:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
</style>
