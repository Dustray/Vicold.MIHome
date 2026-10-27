<script setup lang="ts">
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup, SwitchChannel, SwitchForm } from '../lib/spec'
import { channelWireless, switchChannels, switchForm } from '../lib/spec'
import { serviceLabel } from '../lib/zh'
import PropControl from './PropControl.vue'
import PowerToggle from './PowerToggle.vue'

const props = defineProps<{
  device: Device
  spec: Spec | null
  values: Record<string, unknown>
  busyProp: string
  groups: SpecGroup[]
  /**
   * siid → 该路开关的实际名称。
   *
   * 多键面板在云端被拆成虚拟子设备（`xxx.s2`「主灯」、`.s3`「窗帘」……），
   * 父设备规格里只有「左键 / 中键 / 右键」这种出厂叫法。用户认的是
   * 后者，所以由 App 把子设备名按 siid 传进来。
   */
  channelNames?: Record<number, string>
}>()
const emit = defineEmits<{
  (e: 'set-prop', def: PropDef, value: unknown): void
  (e: 'run-action', siid: number, aiid: number, name: string): void
}>()

const online = computed(() => props.device.online)

/** 物理形态：墙面开关 / 插座 / 纯无线遥控 */
const form = computed<SwitchForm>(() => switchForm(props.spec) || 'wall')

/** 所有通道（多键面板的虚拟子设备只有自己那一路） */
const channels = computed<SwitchChannel[]>(() => switchChannels(props.spec, props.device.did))

/** 该路是不是"无线"——不接电路，按下只发事件 */
function wirelessOf(ch: SwitchChannel): boolean {
  return channelWireless(ch, props.values)
}

function onValueOf(ch: SwitchChannel): boolean | undefined {
  if (!ch.on) return undefined
  const v = props.values[ch.on.name]
  return typeof v === 'boolean' ? v : undefined
}

/** 故障提示：`fault` 非 0 时说明继电器有问题（过温 / 过载 / 负载电流低） */
function faultOf(ch: SwitchChannel): string {
  if (!ch.fault) return ''
  const raw = props.values[ch.fault.name]
  if (raw === undefined || raw === null) return ''
  if (String(raw) === '0') return ''
  const hit = ch.fault.value_list?.find((v) => String(v.value) === String(raw))
  return hit ? (hit.desc_zh || hit.description) : `故障码 ${raw}`
}

interface KeyView {
  ch: SwitchChannel
  name: string
  wireless: boolean
  on: boolean
  /** 纯无线通道没有任何可选中的属性，只能看不能点 */
  static: boolean
  fault: string
}

const keys = computed<KeyView[]>(() =>
  channels.value.map((ch) => {
    const svc = (props.spec?.services || []).find((s) => s.siid === ch.siid)
    const named = props.channelNames?.[ch.siid]
    // 单路设备的面板就是这台设备本身，用设备名最直观；
    // 多路面板才需要区分「哪一路」，此时优先用虚拟子设备名。
    const single = channels.value.length === 1
    const fallback =
      (single ? props.device.name : '') ||
      (svc && serviceLabel(svc)) ||
      (ch.index === 0 ? '开关' : `第 ${ch.index + 1} 路`)
    return {
      ch,
      name: named || fallback,
      wireless: wirelessOf(ch),
      on: onValueOf(ch) === true,
      static: !ch.on,
      fault: faultOf(ch),
    }
  })
)
/** 整个面板都是纯无线（遥控器 / 场景开关）：按键没有继电器，点了也不通断电路 */
const allStatic = computed(() => keys.value.length > 0 && keys.value.every((k) => k.static))

/** 有没有任何一路被切成无线模式（mode=1，不驱动继电器） */
const hasWirelessMode = computed(() => keys.value.some((k) => !k.static && k.wireless))

/** 有继电器的通道是不是全部处于无线模式 */
const allWirelessMode = computed(
  () => keys.value.some((k) => !k.static) && keys.value.every((k) => k.static || k.wireless)
)

const onCount = computed(
  // 无线模式的通道不通电，不能算进"几路通电"
  () => keys.value.filter((k) => k.on && !k.wireless).length
)

const plateTitle = computed(() => {
  const n = keys.value.length
  if (form.value === 'plug') return '智能插座'
  if (form.value === 'remote') return n > 1 ? `无线开关 · ${n} 键` : '无线开关'
  return n > 1 ? `${n} 键面板` : '单键面板'
})

/** 主状态文案 */
function stateLabel(k: KeyView): string {
  if (k.static) return '无线遥控'
  if (k.wireless) return '无线模式'
  return k.on ? '已通电' : '已断电'
}

/** 副说明：说清楚这一路到底会不会驱动电路 */
function stateHint(k: KeyView): string {
  if (k.static) return '不接入电路'
  if (k.wireless) return '不驱动继电器'
  return k.on ? '电路已闭合' : '电路已断开'
}

/** 开关配色：通电绿 / 无线紫 */
function toneOf(k: KeyView): 'accent' | 'violet' {
  return k.wireless ? 'violet' : 'accent'
}

/**
 * 开关尺寸随键数收敛。
 *
 * 键数越多，每张卡越窄（横排布局），大号开关会溢出，
 * 所以三键及以上用中小号，单键用大号。
 */
const toggleSize = computed<'sm' | 'md' | 'lg'>(() => {
  const n = keys.value.length
  if (n <= 1) return 'lg'
  if (n <= 3) return 'md'
  return 'sm'
})

function toggle(k: KeyView, want?: boolean) {
  if (k.static || !k.ch.on || !online.value) return
  emit('set-prop', k.ch.on, want === undefined ? !k.on : want)
}

function busyOf(k: KeyView): boolean {
  return !k.ch.on ? false : props.busyProp === k.ch.on.name
}

// ---------------------------------------------------------------- 收纳其余参数
/**
 * 主面板已经展示的属性不再重复。
 *
 * 只收掉各路的 `on` —— `mode`（有线和无线开关）、`fault`、
 * 上电状态这些恰恰是开关类设备的主要可配置项，留在「更多设置」里
 * 让用户能改，而不是因为面板做了定制就把它藏死。
 */
const consumedKeys = computed(() => {
  const set = new Set<string>()
  for (const ch of channels.value) {
    if (ch.on) set.add(`${ch.on.siid}:${ch.on.piid}`)
  }
  return set
})

const restGroups = computed<SpecGroup[]>(() => {
  const set = consumedKeys.value
  return props.groups
    .map((g) => ({
      key: g.key,
      title: g.title,
      writable: g.writable.filter((p) => !set.has(`${p.siid}:${p.piid}`)),
      readable: g.readable.filter((p) => !set.has(`${p.siid}:${p.piid}`)),
      actions: g.actions,
    }))
    .filter((g) => g.writable.length || g.readable.length || g.actions.length)
})
</script>

<template>
  <div v-if="spec" class="swp">
    <!-- 状态条：一眼看清这是什么形态、几路在通电 -->
    <header class="bar">
      <span class="form-tag" :class="form">{{ plateTitle }}</span>
      <span v-if="keys.length > 1 && form !== 'plug'" class="count">
        {{ onCount }}/{{ keys.length }} 路通电
      </span>
      <span class="where">{{ online ? device.room_name || '在线' : '设备离线' }}</span>
    </header>

    <!-- ------------------------------------------------ 墙面开关 / 无线开关 -->
    <section
      v-if="form !== 'plug'"
      class="swpanel"
      :class="[`form-${form}`, { off: !online }]"
      :data-n="keys.length"
    >
      <div class="grid" :style="{ '--n': keys.length }">
        <article
          v-for="k in keys"
          :key="k.ch.siid"
          class="ch"
          :class="{
            on: k.on,
            wireless: k.wireless,
            static: k.static,
            busy: busyOf(k),
            fault: !!k.fault,
          }"
        >
          <header class="ch-head">
            <span class="ch-name" :title="k.name">{{ k.name }}</span>
            <span class="ch-kind" :class="{ wl: k.wireless }">
              {{ k.wireless ? '无线开关' : '物理通断' }}
            </span>
          </header>

          <div class="ch-body">
            <div class="ch-state">
              <b>{{ stateLabel(k) }}</b>
              <span>{{ stateHint(k) }}</span>
            </div>
            <PowerToggle
              :model-value="k.on"
              :size="toggleSize"
              :tone="toneOf(k)"
              :disabled="k.static || !online"
              :busy="busyOf(k)"
              :label="`${k.name} 开关`"
              @update:model-value="(v) => toggle(k, v)"
            />
          </div>
        </article>
      </div>

      <footer class="plate-foot">
        <span class="model">{{ device.model }}</span>
        <span v-for="k in keys" :key="`f-${k.ch.siid}`" v-show="k.fault" class="fault-note">
          {{ k.name }}：{{ k.fault }}
        </span>
      </footer>
    </section>

    <!-- ------------------------------------------------ 智能插座 -->
    <section v-else class="swpanel plug" :class="{ off: !online }">
      <div class="plug-card" :class="{ live: keys[0]?.on }">
        <div class="socket">
          <svg viewBox="0 0 132 132" class="holes" aria-hidden="true">
            <!-- 两位：两孔插座（竖向扁孔，比三孔短） -->
            <rect x="48" y="26" width="11" height="17" rx="5.5" />
            <rect x="73" y="26" width="11" height="17" rx="5.5" />
            <!-- 三位：上地线孔（短） + 下零火线孔（长） -->
            <rect x="60" y="57" width="11" height="13" rx="5.5" />
            <rect x="38" y="83" width="11" height="23" rx="5.5" />
            <rect x="83" y="83" width="11" height="23" rx="5.5" />
          </svg>
          <span class="socket-glow" />
        </div>

        <div class="plug-side">
          <div class="plug-meta">
            <b class="plug-state">{{ keys[0]?.on ? '供电中' : '已断电' }}</b>
            <span class="ch-kind" :class="{ wl: keys[0]?.wireless }">
              {{ keys[0]?.wireless ? '无线开关' : '物理通断' }}
            </span>
          </div>
          <PowerToggle
            :model-value="!!keys[0]?.on"
            size="lg"
            :tone="keys[0]?.wireless ? 'violet' : 'accent'"
            :disabled="!online || !keys[0] || (keys[0] && busyOf(keys[0]))"
            :busy="!!keys[0] && busyOf(keys[0])"
            label="插座开关"
            @update:model-value="(v) => keys[0] && toggle(keys[0], v)"
          />
        </div>
      </div>

      <footer class="plate-foot">
        <span class="model">{{ device.model }}</span>
        <span v-if="keys[0]?.fault" class="fault-note">{{ keys[0]?.fault }}</span>
      </footer>
    </section>

    <!-- 纯无线开关：说清楚它为什么点不动，比让按键灰着更好 -->
    <p v-if="allStatic" class="note wireless-note">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
        <path d="M4.5 9.5a11 11 0 0 1 15 0M7.6 12.8a6.6 6.6 0 0 1 8.8 0M10.6 16.1a2.6 2.6 0 0 1 2.8 0" />
        <circle cx="12" cy="19.4" r="0.9" fill="currentColor" stroke="none" />
      </svg>
      <span>
        这是<b>无线开关</b>：不接入任何电路，按键事件由设备本地发出。
        在米家 App 里把它绑定到场景或设备，按下去才会产生动作。
      </span>
    </p>
    <p v-else-if="hasWirelessMode" class="note wireless-note">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
        <path d="M4.5 9.5a11 11 0 0 1 15 0M7.6 12.8a6.6 6.6 0 0 1 8.8 0" />
        <circle cx="12" cy="19.4" r="0.9" fill="currentColor" stroke="none" />
      </svg>
      <span v-if="allWirelessMode">
        这个面板的<b>全部通道都工作在无线模式</b>：按键不通断电路，只上报按键事件。
        可在下方「更多设置 → 开关模式」把某一路切回「有线和无线开关」。
      </span>
      <span v-else>
        有通道被设为<b>无线开关</b>模式：该路不通断电路，只上报按键事件。
        可在下方「更多设置 → 开关模式」切回「有线和无线开关」。
      </span>
    </p>

    <!-- 更多设置：把 mode / 上电状态 / 指示灯 / 按键转无线等全部保留 -->
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
              @click="emit('run-action', a.siid, a.aiid, a.name)"
            >
              {{ a.name }}
            </button>
          </div>
        </section>
      </div>
    </details>
  </div>
</template>

<style scoped>
.swp {
  display: flex;
  flex-direction: column;
  padding-bottom: 8px;
}

.swp > section,
.swp > details,
.swp > p {
  animation: fade-up 0.32s var(--ease) both;
}

/* ------------------------------------------------ 顶部状态条 */
.bar {
  margin-top: 12px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.form-tag {
  font-size: 11px;
  font-weight: 600;
  padding: 3px 10px;
  border-radius: 999px;
  color: var(--brand);
  background: var(--brand-soft);
}

.form-tag.plug {
  color: rgb(190, 108, 22);
  background: rgba(236, 168, 44, 0.14);
}

.form-tag.remote {
  color: rgb(122, 88, 214);
  background: rgba(122, 88, 214, 0.12);
}

.count {
  font-size: 11px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.where {
  margin-left: auto;
  font-size: 11px;
  color: var(--text-3);
}

/* ------------------------------------------------ 通道卡片 */
/* 刻意不做 86 型面板 / 螺丝 / 翘板这些拟物元素：
   扁平卡片 + 一个明确的开关控件，既能一眼认出是开关，也更克制高级。 */
.swpanel {
  margin-top: 12px;
  transition: opacity 0.25s ease;
}

.swpanel.off {
  opacity: 0.55;
}

/* N 键横排：跟真实墙面面板一样，三开就三个并排、两开就两个并排 */
.grid {
  display: grid;
  gap: 10px;
  grid-template-columns: repeat(var(--n, 1), minmax(0, 1fr));
}

/* 键数多时压缩内边距与字号，保证一行放得下 */
.ch {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 12px;
  min-height: 148px;
  padding: 13px 10px 14px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--surface);
  box-shadow: var(--shadow-sm);
  overflow: hidden;
  transition: border-color 0.26s var(--ease), box-shadow 0.26s var(--ease), background 0.26s var(--ease);
}

/* 通电时：绿色描边 + 顶部一道极淡的辉光，不做整块染色 */
.ch.on {
  border-color: rgba(23, 178, 106, 0.42);
  box-shadow:
    var(--shadow-sm),
    0 0 0 1px rgba(23, 178, 106, 0.12),
    0 6px 20px rgba(23, 178, 106, 0.14);
}

.ch.on::before {
  content: '';
  position: absolute;
  inset: 0 0 auto;
  height: 2px;
  background: linear-gradient(90deg, transparent, rgba(23, 178, 106, 0.65), transparent);
}

.ch.wireless.on {
  border-color: rgba(122, 88, 214, 0.42);
  box-shadow:
    var(--shadow-sm),
    0 0 0 1px rgba(122, 88, 214, 0.12),
    0 6px 20px rgba(122, 88, 214, 0.14);
}

.ch.wireless.on::before {
  background: linear-gradient(90deg, transparent, rgba(122, 88, 214, 0.65), transparent);
}

.ch.fault {
  border-color: rgba(229, 72, 77, 0.5);
}

/* 单键时横排铺满；多键时每张卡是竖排的窄条 */
.swpanel[data-n='1'] .ch {
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  min-height: 0;
  padding: 15px 18px 16px;
}

/* 单键横排时：名称区在左，状态+开关在右 */
.swpanel[data-n='1'] .ch-body {
  flex: none;
}

.ch-head {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 6px;
  min-width: 0;
}

.ch-name {
  font-size: 13.5px;
  font-weight: 650;
  color: var(--text);
  line-height: 1.25;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
}

.swpanel[data-n='1'] .ch-name {
  font-size: 16px;
}

/* 类型标注：一眼看出通不通电 */
.ch-kind {
  align-self: flex-start;
  max-width: 100%;
  font-size: 9.5px;
  font-weight: 600;
  letter-spacing: 0.2px;
  padding: 2px 7px;
  border-radius: 999px;
  color: var(--accent-hover);
  background: var(--accent-soft);
  box-shadow: inset 0 0 0 1px rgba(23, 178, 106, 0.22);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.ch-kind.wl {
  color: rgb(104, 74, 196);
  background: rgba(122, 88, 214, 0.12);
  box-shadow: inset 0 0 0 1px rgba(122, 88, 214, 0.26);
}

[data-theme='dark'] .ch-kind {
  color: #4ade80;
  background: rgba(34, 197, 94, 0.14);
  box-shadow: inset 0 0 0 1px rgba(34, 197, 94, 0.3);
}

[data-theme='dark'] .ch-kind.wl {
  color: #b39bf5;
  background: rgba(150, 120, 240, 0.16);
  box-shadow: inset 0 0 0 1px rgba(160, 132, 240, 0.32);
}

/* 多键：竖向堆叠 —— 状态在上、开关在下居中 */
.ch-body {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 10px;
  min-width: 0;
  margin-top: auto;
}

/* 开关居中，视觉上像面板上的一颗按键 */
.ch-body :deep(.pt) {
  margin: 0 auto;
}

.ch-state {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
  text-align: left;
  width: 100%;
}

.ch-state b {
  font-size: 12.5px;
  font-weight: 650;
  color: var(--text-2);
  line-height: 1.2;
}

.ch-state span {
  font-size: 10px;
  color: var(--text-3);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

/* 单键横排：状态在左、开关在右 */
.swpanel[data-n='1'] .ch-body {
  flex-direction: row;
  align-items: center;
  gap: 16px;
  margin-top: 0;
}

.swpanel[data-n='1'] .ch-state {
  width: auto;
  text-align: right;
}

.swpanel[data-n='1'] .ch-body :deep(.pt) {
  margin: 0;
}

.ch.on .ch-state b {
  color: var(--accent-hover);
}

.ch.wireless.on .ch-state b {
  color: rgb(104, 74, 196);
}

[data-theme='dark'] .ch.on .ch-state b {
  color: #4ade80;
}

[data-theme='dark'] .ch.wireless.on .ch-state b {
  color: #b39bf5;
}

/* 纯无线通道：按钮不可点，用中性色调说明 */
.ch.static {
  background: var(--surface-2);
}

.ch.static .ch-state b {
  color: var(--text-3);
}

.ch.busy {
  opacity: 0.68;
}

.plate-foot {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 10px;
  font-size: 10px;
  color: var(--text-3);
  min-height: 14px;
}

.model {
  font-variant-numeric: tabular-nums;
}

.fault-note {
  margin-left: auto;
  color: var(--danger);
  font-size: 10px;
}

/* ------------------------------------------------ 插座 */
.plug-card {
  display: flex;
  align-items: center;
  gap: 22px;
  padding: 16px 18px;
  border-radius: var(--radius-lg);
  border: 1px solid var(--border);
  background: var(--surface);
  box-shadow: var(--shadow-sm);
  transition: border-color 0.26s var(--ease), box-shadow 0.26s var(--ease);
}

.plug-card.live {
  border-color: rgba(23, 178, 106, 0.42);
  box-shadow:
    var(--shadow-sm),
    0 0 0 1px rgba(23, 178, 106, 0.12),
    0 6px 20px rgba(23, 178, 106, 0.14);
}

.socket {
  position: relative;
  width: 108px;
  height: 108px;
  flex: none;
  border-radius: 16px;
  background: var(--surface-2);
  box-shadow: inset 0 0 0 1px var(--border);
  display: grid;
  place-items: center;
  transition: box-shadow 0.3s ease;
}

.holes {
  width: 88px;
  height: 88px;
  fill: var(--text-3);
  opacity: 0.55;
  transition: fill 0.3s ease, opacity 0.3s ease;
}

/* 通电时插孔颜色转深，并透出一层绿光 */
.plug-card.live .holes {
  fill: var(--accent-hover);
  opacity: 0.8;
}

.socket-glow {
  position: absolute;
  inset: 0;
  border-radius: 16px;
  pointer-events: none;
  opacity: 0;
  transition: opacity 0.35s ease;
  background: radial-gradient(circle at 50% 55%, rgba(34, 197, 94, 0.2), transparent 66%);
  box-shadow: inset 0 0 0 1.5px rgba(34, 197, 94, 0.3);
}

.plug-card.live .socket-glow {
  opacity: 1;
}

.plug-side {
  display: flex;
  align-items: center;
  gap: 18px;
  flex: 1;
  min-width: 0;
}

.plug-meta {
  display: flex;
  flex-direction: column;
  gap: 7px;
  align-items: flex-start;
  min-width: 0;
}

.plug-state {
  font-size: 16px;
  font-weight: 650;
  color: var(--text);
}

.plug-card.live .plug-state {
  color: var(--accent-hover);
}

[data-theme='dark'] .plug-card.live .plug-state {
  color: #4ade80;
}

/* ------------------------------------------------ 无线说明 */
.note {
  margin: 10px 2px 0;
  display: flex;
  gap: 7px;
  font-size: 10.5px;
  line-height: 1.6;
  color: var(--text-3);
}

.note svg {
  width: 14px;
  height: 14px;
  flex: none;
  margin-top: 2px;
  color: rgb(122, 88, 214);
}

[data-theme='dark'] .note svg {
  color: #a78bfa;
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

.adv-body section {
  margin-bottom: 6px;
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
</style>
