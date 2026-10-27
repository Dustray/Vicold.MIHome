<script setup lang="ts">
import { computed } from 'vue'
import type { Device } from '../lib/types'
import type { Pt } from '../lib/history'
import { devicePhoto } from '../lib/deviceImage'
import SparkLine from './SparkLine.vue'

const props = defineProps<{
  device: Device
  power: boolean | null | undefined
  /** 品类摘要（空调：制冷 27°C），仅开机时展示 */
  summary?: string
  /**
   * 开关类设备的通断类型：
   * `wired` 物理通断（按键驱动继电器）、`wireless` 无线开关（不接电路）。
   * `null` 表示这台设备不是开关类（窗帘、电视这类也有 on，但不是墙面开关）。
   */
  switchKind?: 'wired' | 'wireless' | null
  /** 迷你趋势线：环境读数设备的历史曲线（最近 24 小时） */
  trend?: { color: string; points: Pt[] } | null
  busy: boolean
}>()
const emit = defineEmits<{ (e: 'open'): void; (e: 'toggle'): void }>()

const hues = [212, 152, 264, 22, 190, 340]
const hue = computed(() => {
  let h = 0
  for (const ch of props.device.did) h = (h * 31 + ch.charCodeAt(0)) % 997
  return hues[h % hues.length]
})

const initial = computed(() => (props.device.name || '?').trim().slice(0, 1).toUpperCase())
/** 产品图（home.miot-spec.com 抓来的）；没拿到时回落成首字母块 */
const photo = computed(() => devicePhoto(props.device.model))
const hasPower = computed(() => props.power !== undefined && props.power !== null)

/** 是开关类设备：卡片上换成模拟真实的翘板按键，并在按键上标注通断类型 */
const isSwitch = computed(() => props.switchKind === 'wired' || props.switchKind === 'wireless')
const wireless = computed(() => props.switchKind === 'wireless')

/**
 * 底部标签。
 * - 有开关的设备：关了就显示"已关闭"，开了才给品类摘要（如「制冷 27°C」）；
 * - 纯读数设备（温湿度计没有 on 属性）：没有开关可讲，直接展示读数。
 */
const tagText = computed(() => {
  if (hasPower.value) {
    // 无线通道即使 on=true 也不通电，写"已开启"会让人以为灯亮了
    if (wireless.value) return '不接电路'
    if (!props.power) return '已关闭'
    return props.summary || '已开启'
  }
  // 纯无线开关没有可读的通断状态，说明它是什么比抛一个"详情"更有用
  if (isSwitch.value && wireless.value) return '不接电路'
  return props.summary || '详情'
})

/** 无开关时标签独占整行，读数才不会被截断 */
const wideTag = computed(() => !hasPower.value && !!props.summary)

/** 灰底只用在"没有有效读数"的情况，有读数就用强调色 */
const tagMuted = computed(() => {
  // 无线通道的"不接电路"是性质说明，不是通电状态，别用代表"已开启"的绿底
  if (wireless.value) return true
  if (hasPower.value) return !props.power
  return !props.summary
})
</script>

<template>
  <div
    class="card device-card"
    :class="{ offline: !device.online }"
    :data-model="device.model"
    @click="emit('open')"
  >
    <div class="row">
      <div class="icon" :class="{ photo: !!photo }" :style="{ '--h': hue }">
        <img v-if="photo" class="ph" :src="photo" :alt="device.name" draggable="false" />
        <template v-else>{{ initial }}</template>
      </div>
      <div class="meta">
        <div class="name" :title="device.name">{{ device.name }}</div>
        <div class="sub">
          <span class="dot" :class="{ on: device.online }" />
          {{ device.online ? '在线' : '离线' }}<template v-if="device.room_name"> · {{ device.room_name }}</template>
        </div>
      </div>
    </div>

    <div v-if="trend && trend.points.length > 1" class="spark-wrap">
      <SparkLine :points="trend.points" :color="trend.color" />
    </div>

    <div class="foot">
      <span class="tag" :class="{ muted: tagMuted, wide: wideTag }" :title="tagText">{{ tagText }}</span>

      <!-- 开关类：扁平化开关，一眼看出是通断开关，并标注通断类型 -->
      <button
        v-if="hasPower && isSwitch"
        class="rocker"
        :class="{ on: !!power, wireless, busy }"
        :disabled="!device.online || busy"
        :title="wireless ? '无线开关：不接入电路' : '物理通断：直接控制电路'"
        @click.stop="emit('toggle')"
      >
        <svg class="rk-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round">
          <path d="M12 3.4v7.8" />
          <path d="M7.2 6.9a7 7 0 1 0 9.6 0" />
        </svg>
        <span class="rk-tag">{{ wireless ? '无线' : '通断' }}</span>
      </button>

      <!-- 纯无线开关（遥控器）：没有可写 on，按键点不动，只做类型标注 -->
      <span v-else-if="isSwitch && wireless" class="rocker static" title="无线开关：不接入电路，按键事件在本地触发">
        <svg class="rk-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round">
          <path d="M12 3.4v7.8" />
          <path d="M7.2 6.9a7 7 0 1 0 9.6 0" />
        </svg>
        <span class="rk-tag">无线</span>
      </span>

      <!-- 其它有开关的品类（窗帘、电视等）沿用原来的胶囊开关 -->
      <button
        v-else-if="hasPower"
        class="switch"
        :class="{ on: !!power, busy }"
        :disabled="!device.online || busy"
        @click.stop="emit('toggle')"
      />
    </div>
  </div>
</template>

<style scoped>
.spark-wrap {
  margin-top: -8px;
  margin-bottom: -6px;
  padding-left: 2px;
  opacity: 0.9;
}

.device-card {
  padding: 18px 18px 16px;
  cursor: pointer;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  gap: 22px;
  min-height: 158px;
  position: relative;
  overflow: hidden;
  background: var(--grad-surface);
}

/* 顶部彩色条：静止时淡显，hover 时高亮 */
.device-card::before {
  content: '';
  position: absolute;
  inset: 0 0 auto 0;
  height: 3px;
  background: hsl(var(--h) 80% 58%);
  opacity: 0.35;
  transition: opacity 0.2s ease;
}

/* 右上角色晕 */
.device-card::after {
  content: '';
  position: absolute;
  top: -46px;
  right: -46px;
  width: 132px;
  height: 132px;
  border-radius: 50%;
  background: radial-gradient(circle, hsl(var(--h) 85% 60% / 0.16), transparent 68%);
  opacity: 0;
  transition: opacity 0.25s ease;
  pointer-events: none;
}

.device-card:hover {
  transform: translateY(-3px);
  box-shadow: var(--shadow-md);
  border-color: hsl(var(--h) 60% 72%);
}

.device-card:hover::before {
  opacity: 1;
  box-shadow: 0 0 8px hsl(var(--h) 80% 58% / 0.32);
}

.device-card:hover::after {
  opacity: 1;
}

.device-card.offline {
  opacity: 0.62;
}

.device-card.offline .icon {
  filter: grayscale(0.85);
}

.row {
  display: flex;
  gap: 13px;
  align-items: center;
  min-width: 0;
  position: relative;
  z-index: 1;
}

.icon {
  width: 52px;
  height: 52px;
  border-radius: 16px;
  flex: none;
  display: grid;
  place-items: center;
  font-size: 19px;
  font-weight: 700;
  color: hsl(var(--h) 62% 32%);
  background: hsl(var(--h) 80% 94%);
  box-shadow: 0 4px 12px hsl(var(--h) 60% 50% / 0.18), inset 0 1px 0 rgba(255, 255, 255, 0.7);
  transition: transform 0.22s var(--ease), box-shadow 0.22s ease;
}

.device-card:hover .icon {
  transform: scale(1.06) rotate(-3deg);
  box-shadow: 0 8px 20px hsl(var(--h) 60% 50% / 0.32), inset 0 1px 0 rgba(255, 255, 255, 0.8);
}

[data-theme='dark'] .icon {
  color: hsl(var(--h) 85% 76%);
  background: hsl(var(--h) 45% 18%);
  box-shadow: 0 4px 14px hsl(var(--h) 60% 40% / 0.3);
}

/*
 * 有产品图时：图标块变成「产品图底托」。
 * 小米的产品图是 168×168 的透明 PNG，很多是**白色机身**（吸顶灯、插座），
 * 纯白底托上会糊成一团看不见 —— 所以底色必须带色相浅渐变；
 * 深色主题同理保持浅色底（商品目录的做法），只在色相上跟设备呼应。
 */
.icon.photo {
  padding: 3px;
  overflow: hidden;
  background: linear-gradient(150deg, hsl(var(--h) 48% 97%) 0%, hsl(var(--h) 38% 88%) 100%);
  box-shadow: 0 4px 12px hsl(var(--h) 60% 50% / 0.16), inset 0 0 0 1px rgba(255, 255, 255, 0.9);
}

.device-card:hover .icon.photo {
  box-shadow: 0 8px 20px hsl(var(--h) 60% 50% / 0.3), inset 0 0 0 1px rgba(255, 255, 255, 0.95);
}

.icon .ph {
  display: block;
  width: 100%;
  height: 100%;
  object-fit: contain;
  /* 原图四周自带 ~20% 留白：放大一点让产品主体撑满，overflow 已裁掉溢出部分 */
  transform: scale(1.12);
  /*
   * 白色机身的吸顶灯/插座放在浅色底托上几乎看不见，
   * 用 drop-shadow 给轮廓描一圈阴影（PNG 有 alpha，阴影会贴着产品形状走）。
   */
  filter: drop-shadow(0 1.5px 2px rgba(16, 26, 44, 0.22));
  user-select: none;
  -webkit-user-drag: none;
}

[data-theme='dark'] .icon.photo {
  /* 深色界面里一整块亮白太刺眼；压到 76~92% 仍然比白色机身暗一档，产品轮廓还在 */
  background: linear-gradient(150deg, hsl(var(--h) 30% 92%) 0%, hsl(var(--h) 26% 76%) 100%);
}

.meta {
  min-width: 0;
  flex: 1;
}

.name {
  font-size: 14.5px;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.sub {
  margin-top: 4px;
  font-size: 12px;
  color: var(--text-2);
  display: flex;
  align-items: center;
  gap: 6px;
}

.dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--text-3);
  flex: none;
}

.dot.on {
  background: var(--accent);
  box-shadow: 0 0 0 3px var(--accent-soft);
  animation: breathe 2.4s ease-in-out infinite;
}

.foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 30px;
  padding-top: 12px;
  border-top: 1px solid var(--border);
  position: relative;
  z-index: 1;
}

.tag {
  font-size: 11.5px;
  color: var(--accent);
  background: var(--accent-soft);
  padding: 3px 10px;
  border-radius: 999px;
  font-weight: 500;
  max-width: 68%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tag.muted {
  color: var(--text-2);
  background: var(--surface-3);
}

/* 纯读数设备（温湿度计）没有右侧开关，标签放宽，读数不会被省略号吃掉 */
.tag.wide {
  max-width: 100%;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.2px;
}

.switch.busy {
  opacity: 0.6;
}

/* ------------------------------------------------ 开关药丸 */
/* 不做拟物翘板（rotateX 只会把按键压扁，反而显得廉价）。
   改成一个扁平胶囊按钮：左端电源符号，右端通断类型标注，
   通电时整颗变绿 —— 一眼就知道这是个开关。 */
.rocker {
  position: relative;
  height: 26px;
  flex: none;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 0 9px 0 7px;
  border-radius: 999px;
  cursor: pointer;
  color: var(--text-3);
  background: var(--surface-3);
  box-shadow: inset 0 0 0 1px var(--border-strong);
  transition: background 0.24s var(--ease), box-shadow 0.24s var(--ease), color 0.24s var(--ease);
}

.rk-ico {
  width: 12px;
  height: 12px;
  flex: none;
  display: block;
  transition: color 0.24s var(--ease);
}

.rocker:hover:not(:disabled):not(.static) {
  box-shadow: inset 0 0 0 1px var(--text-3);
}

.rocker:active:not(:disabled):not(.static) {
  transform: scale(0.96);
  transition-duration: 0.08s;
}

/* 通电：整颗变绿 + 一点点外发光 */
.rocker.on {
  color: #fff;
  background: var(--grad-accent);
  box-shadow:
    inset 0 0 0 1px rgba(23, 178, 106, 0.5),
    0 0 8px rgba(23, 178, 106, 0.2);
}

.rocker.on .rk-ico {
  color: #fff;
}

/* 无线通道：紫色系，与"通电"区分 */
.rocker.wireless {
  color: rgb(104, 74, 196);
  box-shadow: inset 0 0 0 1px rgba(122, 88, 214, 0.34);
}

.rocker.wireless.on {
  color: #fff;
  background: linear-gradient(135deg, #a78bfa 0%, #7c5cf0 55%, #6a48e0 100%);
  box-shadow:
    inset 0 0 0 1px rgba(122, 88, 214, 0.5),
    0 0 8px rgba(122, 88, 214, 0.2);
}

/* 类型标注：一眼看出通不通电 */
.rk-tag {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.2px;
  pointer-events: none;
  user-select: none;
  white-space: nowrap;
}

[data-theme='dark'] .rocker {
  background: #1b2635;
  box-shadow: inset 0 0 0 1px var(--border-strong);
}

[data-theme='dark'] .rocker.wireless {
  color: #b39bf5;
  box-shadow: inset 0 0 0 1px rgba(160, 132, 240, 0.32);
}

/* 纯无线开关：描边单独标出，明确表示"这里不通电" */
.rocker.static {
  cursor: default;
  box-shadow: inset 0 0 0 1px rgba(122, 88, 214, 0.3);
}

[data-theme='dark'] .rocker.static {
  box-shadow: inset 0 0 0 1px rgba(160, 132, 240, 0.28);
}

.rocker.busy {
  opacity: 0.6;
}
</style>
