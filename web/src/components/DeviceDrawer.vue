<script setup lang="ts">
import { computed } from 'vue'
import type { Device, PropDef, Spec } from '../lib/types'
import type { SpecGroup } from '../lib/spec'
import { buildGroups, isAirConditioner, isAirMonitor, isAirPurifier, isAirer, isCurtain, isFishTank, isHumidifier, isLight, isPresenceSensor, isSpeaker, isSwitchDevice, isTempHumiditySensor, isWaterPurifier } from '../lib/spec'
import PropControl from './PropControl.vue'
import AirPurifierPanel from './AirPurifierPanel.vue'
import AirConditionerPanel from './AirConditionerPanel.vue'
import AirMonitorPanel from './AirMonitorPanel.vue'
import AirerPanel from './AirerPanel.vue'
import CurtainPanel from './CurtainPanel.vue'
import FishTankPanel from './FishTankPanel.vue'
import HumidifierPanel from './HumidifierPanel.vue'
import LightPanel from './LightPanel.vue'
import PresenceSensorPanel from './PresenceSensorPanel.vue'
import SensorPanel from './SensorPanel.vue'
import SpeakerPanel from './SpeakerPanel.vue'
import SwitchPanel from './SwitchPanel.vue'
import WaterPurifierPanel from './WaterPurifierPanel.vue'
import { findOnProp } from '../lib/device'

/** 设备所在房间的环境读数（由环境页聚合算出），给空调面板兜底显示"房间温度" */
interface RoomEnv {
  name: string
  temp?: number
  humidity?: number
  count: number
  stale: boolean
}

const props = defineProps<{
  device: Device
  spec: Spec | null
  values: Record<string, unknown>
  loading: boolean
  busyProp: string
  roomEnv?: RoomEnv | null
  /**
   * siid → 该路开关的实际名称（来自同父设备下的虚拟子设备，如「主灯」「窗帘」）。
   * 父设备的规格里只有「左键 / 中键 / 右键」，用户认的是子设备名。
   */
  channelNames?: Record<number, string>
}>()
const emit = defineEmits<{
  (e: 'close'): void
  (e: 'set-prop', def: PropDef, value: unknown): void
  /** params 是动作入参（喂食出粮量等），顺序与规格 action.in 一致 */
  (e: 'run-action', siid: number, aiid: number, name: string, params?: unknown[]): void
  (e: 'refresh'): void
}>()

/** 电源开关（on 属性）单独提到最前；多键面板按 did 后缀选对应那一路 */
const powerProp = computed<PropDef | null>(() =>
  findOnProp(props.spec, props.device.did)
)

/** 空气净化器有专属面板（模式/风速改用图标单选，滤芯做成寿命条） */
const purifier = computed(() => isAirPurifier(props.spec))

/** 空调有专属面板（温控圆环 + 模式变色 + 风速档位） */
const conditioner = computed(() => isAirConditioner(props.spec))

/** 灯具专属面板（色温×亮度二维调光） */
const lamp = computed(() => isLight(props.spec))

/** 温湿度计专属面板（温湿度仪表盘 + CO₂ + 电池） */
const sensor = computed(() => isTempHumiditySensor(props.spec))

/** 音箱 / 家庭屏专属面板（音量盘 + 播放控制 + 静音/勿扰磁贴） */
const speaker = computed(() => isSpeaker(props.spec))

/** 空气质量监测仪专属面板（甲醛等污染物浓度标尺 + 温湿度对照） */
const airMonitor = computed(() => isAirMonitor(props.spec))

/** 开关 / 插座 / 无线开关专属面板（模拟真实翘板按键 + 通断类型标注） */
const wswitch = computed(() => isSwitchDevice(props.spec))

/** 电动窗帘专属面板（开合度可视化 + 开/停/关） */
const curtain = computed(() => isCurtain(props.spec))

/** 晾衣架专属面板（升起高度可视化 + 升降 + 干衣） */
const airer = computed(() => isAirer(props.spec))

/** 加湿器专属面板（湿度双环 + 模式 + 水位） */
const humidifier = computed(() => isHumidifier(props.spec))

/** 净水器专属面板（TDS 进/出对比 + 滤芯寿命） */
const waterPurifier = computed(() => isWaterPurifier(props.spec))

/** 存在类传感器专属面板（有人/无人 + 光照度 + 距离） */
const presence = computed(() => isPresenceSensor(props.spec))

/** 智能鱼缸专属面板（缸内水温 + 水泵 + 喂食） */
const fishTank = computed(() => isFishTank(props.spec))

const groups = computed<SpecGroup[]>(() => {
  const list = buildGroups(props.spec, props.device.did)
  // 主开关已在顶部单独展示，模块里不再重复
  const power = powerProp.value
  if (!power) return list
  return list.map((g) =>
    Number(g.key) === power.siid
      ? { ...g, writable: g.writable.filter((p) => p !== power) }
      : g
  )
})
</script>

<template>
  <div class="mask" @click.self="emit('close')">
    <aside class="drawer">
      <header class="head">
        <div class="head-text">
          <h2>{{ device.name }}</h2>
          <p>{{ device.model }}<template v-if="device.room_name"> · {{ device.room_name }}</template></p>
        </div>
        <button class="close" @click="emit('close')">
          <svg width="18" height="18" viewBox="0 0 24 24"><path d="M7 7l10 10M17 7L7 17" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" /></svg>
        </button>
      </header>

      <div class="body">
        <div v-if="loading" class="loading">
          <span class="spinner" />
          正在读取设备规格…
        </div>

        <template v-else-if="spec">
          <!-- 空调：温控圆环 + 模式变色 + 风速档位 -->
          <AirConditionerPanel
            v-if="conditioner"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            :room-env="roomEnv"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 空气净化器：专属面板（模式/风速用图标单选，滤芯用寿命条） -->
          <AirPurifierPanel
            v-else-if="purifier"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 灯具：色温×亮度 二维调光台 -->
          <LightPanel
            v-else-if="lamp"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
            @refresh="emit('refresh')"
          />

          <!-- 温湿度计：温湿度仪表盘 + CO₂ + 电池 -->
          <SensorPanel
            v-else-if="sensor"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
            @refresh="emit('refresh')"
          />

          <!-- 空气质量监测仪：污染物浓度标尺 + 温湿度对照 -->
          <AirMonitorPanel
            v-else-if="airMonitor"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
            @refresh="emit('refresh')"
          />

          <!-- 音箱 / 家庭屏：音量盘 + 播放控制 + 开关磁贴 -->
          <SpeakerPanel
            v-else-if="speaker"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
            @refresh="emit('refresh')"
          />

          <!-- 电动窗帘：开合度可视化 + 开/停/关 -->
          <CurtainPanel
            v-else-if="curtain"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 晾衣架：升起高度可视化 + 升降 + 干衣 -->
          <AirerPanel
            v-else-if="airer"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 加湿器：湿度双环 + 模式 + 水位 -->
          <HumidifierPanel
            v-else-if="humidifier"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 净水器：TDS 进/出对比 + 滤芯寿命 -->
          <WaterPurifierPanel
            v-else-if="waterPurifier"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 存在 / 人体传感器：有人无人 + 光照度 + 距离 -->
          <PresenceSensorPanel
            v-else-if="presence"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <!-- 鱼缸：缸内水温 + 水泵 + 喂食（喂食动作要带出粮量参数） -->
          <FishTankPanel
            v-else-if="fishTank"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n, p) => emit('run-action', s, a, n, p)"
          />

          <!-- 开关 / 插座 / 无线开关：模拟真实面板按键，标注物理通断 or 无线 -->
          <SwitchPanel
            v-else-if="wswitch"
            :device="device"
            :spec="spec"
            :values="values"
            :busy-prop="busyProp"
            :groups="groups"
            :channel-names="channelNames"
            @set-prop="(d, v) => emit('set-prop', d, v)"
            @run-action="(s, a, n) => emit('run-action', s, a, n)"
          />

          <template v-else>
            <section v-if="powerProp" class="power-block">
              <PropControl
                :def="powerProp"
                :value="values[powerProp.name]"
                :disabled="!device.online || busyProp === powerProp.name"
                @change="(v) => emit('set-prop', powerProp!, v)"
              />
            </section>

            <section v-for="g in groups" :key="g.key" class="group">
              <h3>{{ g.title }}</h3>

              <template v-if="g.writable.length">
                <PropControl
                  v-for="p in g.writable"
                  :key="`${p.siid}-${p.piid}`"
                  :def="p"
                  :value="values[p.name]"
                  :disabled="!device.online || busyProp === p.name"
                  @change="(v) => emit('set-prop', p, v)"
                />
              </template>

              <template v-if="g.readable.length">
                <div class="readable">
                  <PropControl
                    v-for="p in g.readable"
                    :key="`${p.siid}-${p.piid}`"
                    :def="p"
                    :value="values[p.name]"
                    disabled
                  />
                </div>
              </template>

              <div v-if="g.actions.length" class="actions">
                <button
                  v-for="a in g.actions"
                  :key="`${a.siid}-${a.aiid}`"
                  class="btn btn-quiet"
                  :disabled="!device.online"
                  @click="emit('run-action', a.siid, a.aiid, a.name)"
                >
                  {{ a.name }}
                </button>
              </div>
            </section>
          </template>
        </template>

        <div v-else class="empty">未取到该设备的规格信息</div>
      </div>
    </aside>
  </div>
</template>

<style scoped>
.mask {
  position: absolute;
  inset: var(--titlebar-h) 0 0 0;
  background: rgba(10, 17, 29, 0.34);
  backdrop-filter: blur(2px);
  display: flex;
  justify-content: flex-end;
  animation: fade-in 0.16s ease both;
  z-index: 20;
}

@keyframes fade-in {
  from {
    opacity: 0;
  }
}

.drawer {
  width: 430px;
  height: 100%;
  background: var(--surface);
  border-left: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  animation: slide-in 0.26s cubic-bezier(0.2, 0.8, 0.2, 1) both;
  box-shadow: var(--shadow-lg);
}

@keyframes slide-in {
  from {
    transform: translateX(28px);
    opacity: 0;
  }
}

.head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  padding: 20px 20px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--grad-surface);
}

h2 {
  margin: 0;
  font-size: 17px;
  font-weight: 700;
  letter-spacing: 0.2px;
}

.head p {
  margin: 4px 0 0;
  font-size: 12px;
  color: var(--text-2);
}

.close {
  width: 30px;
  height: 30px;
  border-radius: 9px;
  display: grid;
  place-items: center;
  color: var(--text-2);
  transition: background-color 0.15s ease, color 0.15s ease;
}

.close:hover {
  background: var(--surface-3);
  color: var(--danger);
}

.body {
  flex: 1;
  overflow-y: auto;
  padding: 4px 20px 28px;
}

h3 {
  margin: 20px 0 6px;
  font-size: 12.5px;
  font-weight: 600;
  letter-spacing: 0.4px;
  color: var(--text-2);
  display: flex;
  align-items: center;
  gap: 8px;
}

h3::before {
  content: '';
  width: 3px;
  height: 12px;
  border-radius: 2px;
  background: var(--grad-brand);
}

/* 电源开关单独一块，突出显示 */
.power-block {
  margin: 12px 0 4px;
  padding: 12px 14px;
  border-radius: var(--radius-md);
  background: var(--grad-surface);
  border: 1px solid var(--border);
  box-shadow: var(--shadow-sm);
}

.power-block :deep(.prop) {
  border-bottom: none;
  padding: 0;
}

/* 只读状态区：弱化视觉，让可操作项更突出 */
.readable {
  margin-top: 2px;
  padding: 2px 10px 6px;
  border-radius: 10px;
  background: var(--surface-2);
}

.readable :deep(.prop) {
  border-bottom: none;
  padding: 7px 0;
}

.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  padding-top: 8px;
}

.loading,
.empty {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 40px 0;
  color: var(--text-2);
  font-size: 13px;
}

.spinner {
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: 2px solid var(--border-strong);
  border-top-color: var(--brand);
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
