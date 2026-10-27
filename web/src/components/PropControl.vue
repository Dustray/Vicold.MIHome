<script setup lang="ts">
import { computed } from 'vue'
import type { PropDef } from '../lib/types'
import { propLabel, valueLabel } from '../lib/zh'

const props = defineProps<{
  def: PropDef
  value: unknown
  disabled?: boolean
  /** 覆盖显示名：官方 comment 有时会和另一个控件撞名，需要额外区分 */
  label?: string
}>()
const emit = defineEmits<{ (e: 'change', value: unknown): void }>()

const writable = computed(() => props.def.rw.includes('w'))
const numeric = computed(() => ['int', 'uint', 'float'].includes(props.def.type))
const range = computed(() => (props.def.range && props.def.range.length >= 2 ? props.def.range : null))
const step = computed(() => (range.value && range.value.length >= 3 ? Math.max(1, range.value[2]) : 1))

/** 中文显示名：调用方覆盖 → 官方 comment → 兜底字典 → 英文原文 */
const labelText = computed(() => props.label || propLabel(props.def))

const enumText = computed(() => {
  const list = props.def.value_list
  if (!list) return ''
  const hit = list.find((i) => i.value === props.value)
  return hit ? valueLabel(hit) : String(props.value ?? '')
})

/** 滑杆已填充百分比，用于轨道渐变 */
const fill = computed(() => {
  if (!range.value) return '50%'
  const v = Number(props.value ?? range.value[0])
  const span = range.value[1] - range.value[0]
  if (!span || !Number.isFinite(v)) return '50%'
  const pct = ((v - range.value[0]) / span) * 100
  return `${Math.min(100, Math.max(0, pct))}%`
})

const display = computed(() => {
  const v = props.value
  if (v === null || v === undefined) return '—'
  if (props.def.type === 'bool') return v ? '开' : '关'
  if (props.def.value_list) return enumText.value
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : v.toFixed(1)
  const s = String(v)
  // 路由配置之类是整段 JSON/列表，原样铺开会撑爆抽屉
  return s.length > 48 ? s.slice(0, 48) + '…' : s
})

/** 截断后的完整值，放 title 里备查 */
const fullText = computed(() => {
  const v = props.value
  return v === null || v === undefined ? '' : String(v)
})

function onRange(e: Event) {
  const raw = Number((e.target as HTMLInputElement).value)
  emit('change', props.def.type === 'float' ? raw : Math.round(raw))
}

function onSelect(e: Event) {
  const raw = (e.target as HTMLSelectElement).value
  if (raw === '') return // 选了占位项，不下发
  const list = props.def.value_list || []
  const hit = list.find((i) => String(i.value) === raw)
  emit('change', hit ? hit.value : raw)
}
</script>

<template>
  <div class="prop" :class="{ disabled }">
    <div class="line">
      <span class="label" :title="def.desc_en || def.description">{{ labelText }}</span>

      <template v-if="!writable">
        <span class="value readonly" :title="fullText">{{ display }}</span>
      </template>

      <template v-else-if="def.type === 'bool'">
        <button class="switch" :class="{ on: !!value }" :disabled="disabled" @click="emit('change', !value)" />
      </template>

      <template v-else-if="def.value_list">
        <select class="select" :value="String(value)" :disabled="disabled" @change="onSelect">
          <!-- 设备没返回该属性值时（部分型号不支持）给个占位，别显示成空白控件 -->
          <option v-if="value === null || value === undefined" value="" disabled>—</option>
          <option v-for="item in def.value_list" :key="String(item.value)" :value="String(item.value)">
            {{ valueLabel(item) }}
          </option>
        </select>
      </template>

      <template v-else-if="numeric && range">
        <span class="value">{{ display }}</span>
      </template>

      <template v-else-if="numeric">
        <input
          class="select num"
          type="number"
          :value="Number(value ?? 0)"
          :disabled="disabled"
          @change="emit('change', Number(($event.target as HTMLInputElement).value))"
        />
      </template>

      <template v-else>
        <input
          class="select text"
          type="text"
          :value="String(value ?? '')"
          :disabled="disabled"
          @change="emit('change', ($event.target as HTMLInputElement).value)"
        />
      </template>
    </div>

    <div v-if="writable && numeric && range" class="slider-row">
      <input
        class="slider"
        type="range"
        :min="range[0]"
        :max="range[1]"
        :step="step"
        :value="Number(value ?? range[0])"
        :disabled="disabled"
        :style="{ '--fill': fill }"
        @change="onRange"
      />
      <span class="hint">{{ range[0] }} – {{ range[1] }}</span>
    </div>
  </div>
</template>

<style scoped>
.prop {
  padding: 11px 0;
  border-bottom: 1px dashed var(--border);
}
.prop:last-child {
  border-bottom: none;
}

.prop.disabled {
  opacity: 0.55;
}

.line {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 30px;
}

.label {
  flex: 1;
  font-size: 13px;
  color: var(--text-2);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.value {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.value.readonly {
  color: var(--text-2);
  font-weight: 400;
}

.select {
  height: 32px;
  min-width: 120px;
  padding: 0 10px;
  border-radius: 9px;
  border: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--text);
  outline: none;
  cursor: pointer;
  transition: border-color 0.16s ease, box-shadow 0.16s ease, background-color 0.16s ease;
}

.select:hover:not(:disabled) {
  border-color: var(--border-strong);
  background: var(--surface);
}

.select:focus {
  border-color: var(--brand);
  background: var(--surface);
  box-shadow: var(--glow-brand-soft);
}

.select.num,
.select.text {
  width: 120px;
}

.slider-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 6px 0 2px;
}

.slider {
  flex: 1;
  appearance: none;
  height: 5px;
  border-radius: 5px;
  background: linear-gradient(90deg, var(--accent) 0%, var(--accent) var(--fill, 50%), var(--border-strong) var(--fill, 50%));
  outline: none;
}

.slider::-webkit-slider-thumb {
  appearance: none;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--accent);
  box-shadow: 0 0 7px rgba(23, 178, 106, 0.28);
  cursor: pointer;
  transition: transform 0.15s var(--ease), box-shadow 0.15s ease;
}

.slider::-webkit-slider-thumb:hover {
  transform: scale(1.15);
  box-shadow: 0 0 8px rgba(23, 178, 106, 0.32);
}

.hint {
  font-size: 11px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
  flex: none;
}
</style>
