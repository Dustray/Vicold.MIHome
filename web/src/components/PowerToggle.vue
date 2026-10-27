<script setup lang="ts">
/**
 * 通用电源开关 —— 现代扁平风格，刻意不做拟物。
 *
 * 设计取舍：
 * - **胶囊轨道 + 白色滑块**：这是「开关」的通用视觉语言，一眼可辨，
 *   不需要用真实面板 / 翘板 / 螺丝去模拟实物（模拟反而不高级）。
 * - **滑块内嵌电源符号**：即使缩到最小尺寸，也能看出这是电源开关。
 * - **开启态 = 绿色渐变 + 外发光**：通电是唯一需要强调的状态。
 * - **无线通道用紫色**：和「物理通断」在颜色上直接区分开。
 */
const props = withDefaults(
  defineProps<{
    modelValue: boolean
    disabled?: boolean
    busy?: boolean
    /** sm=36×20 / md=44×24 / lg=60×32 */
    size?: 'sm' | 'md' | 'lg'
    /** accent=通电绿 / violet=无线 / neutral=中性 */
    tone?: 'accent' | 'violet' | 'neutral'
    /** 无障碍标签 */
    label?: string
  }>(),
  {
    disabled: false,
    busy: false,
    size: 'md',
    tone: 'accent',
    label: '开关',
  }
)

const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void }>()

function onClick(e: MouseEvent) {
  // 阻止冒泡：外层卡片可能也有点击处理，避免一次点击被切换两次
  e.stopPropagation()
  if (props.disabled || props.busy) return
  emit('update:modelValue', !props.modelValue)
}
</script>

<template>
  <button
    type="button"
    role="switch"
    class="pt"
    :class="[`sz-${size}`, `tn-${tone}`, { on: modelValue, disabled, busy }]"
    :aria-checked="modelValue"
    :aria-label="label"
    :disabled="disabled || busy"
    @click="onClick"
  >
    <span class="knob">
      <svg class="glyph" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round">
        <path d="M12 3.4v7.8" />
        <path d="M7.2 6.9a7 7 0 1 0 9.6 0" />
      </svg>
    </span>
  </button>
</template>

<style scoped>
.pt {
  position: relative;
  display: inline-flex;
  align-items: center;
  flex: none;
  padding: 0;
  border: none;
  border-radius: 999px;
  cursor: pointer;
  background: var(--surface-3);
  box-shadow: inset 0 0 0 1px var(--border-strong);
  transition: background 0.26s var(--ease), box-shadow 0.26s var(--ease);
}

.pt:disabled {
  cursor: default;
}

/* ------------------------------------------------------------ 尺寸 */
.sz-sm {
  width: 36px;
  height: 20px;
}

.sz-sm .knob {
  width: 14px;
  height: 14px;
}

.sz-sm.on .knob {
  transform: translateX(16px);
}

.sz-sm .glyph {
  width: 8px;
  height: 8px;
  stroke-width: 2.6;
}

.sz-md {
  width: 44px;
  height: 24px;
}

.sz-md .knob {
  width: 18px;
  height: 18px;
}

.sz-md.on .knob {
  transform: translateX(20px);
}

.sz-md .glyph {
  width: 10px;
  height: 10px;
}

.sz-lg {
  width: 60px;
  height: 32px;
}

.sz-lg .knob {
  width: 26px;
  height: 26px;
}

.sz-lg.on .knob {
  transform: translateX(28px);
}

.sz-lg .glyph {
  width: 14px;
  height: 14px;
}

/* ------------------------------------------------------------ 滑块 */
/* 上下用 top/bottom + margin:auto 居中，transform 只留给水平位移 */
.knob {
  position: absolute;
  top: 0;
  bottom: 0;
  left: 3px;
  margin: auto;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: #fff;
  color: var(--text-3);
  box-shadow:
    0 1px 2px rgba(22, 35, 58, 0.22),
    0 0 0 1px rgba(22, 35, 58, 0.05);
  transition: transform 0.26s var(--ease), color 0.24s var(--ease);
}

.glyph {
  display: block;
  transition: color 0.24s var(--ease);
}

/* ------------------------------------------------------------ 通电态 */
.pt.on {
  background: var(--grad-accent);
  box-shadow: inset 0 0 0 1px rgba(23, 178, 106, 0.55);
}

.pt.on.tn-accent {
  box-shadow:
    inset 0 0 0 1px rgba(23, 178, 106, 0.55),
    0 0 14px rgba(23, 178, 106, 0.38);
}

.pt.on.tn-accent .knob {
  color: var(--accent-hover);
}

.pt.on.tn-violet {
  background: linear-gradient(135deg, #a78bfa 0%, #7c5cf0 55%, #6a48e0 100%);
  box-shadow:
    inset 0 0 0 1px rgba(122, 88, 214, 0.55),
    0 0 14px rgba(122, 88, 214, 0.38);
}

.pt.on.tn-violet .knob {
  color: rgb(104, 74, 196);
}

.pt.on.tn-neutral {
  background: linear-gradient(135deg, #8fa4bf 0%, #7189a8 55%, #5e7796 100%);
  box-shadow: inset 0 0 0 1px rgba(113, 137, 168, 0.5);
}

.pt.on.tn-neutral .knob {
  color: #5e7796;
}

/* 关闭态：滑块上的电源符号保持中性灰，不抢注意力 */
.pt:not(.on) .knob {
  color: var(--text-3);
}

.pt.busy {
  opacity: 0.6;
}

[data-theme='dark'] .pt {
  background: #1b2635;
  box-shadow: inset 0 0 0 1px var(--border-strong);
}

[data-theme='dark'] .pt.on.tn-accent .knob {
  color: #34d96f;
}

[data-theme='dark'] .pt.on.tn-violet .knob {
  color: #b39bf5;
}

[data-theme='dark'] .pt.on.tn-neutral .knob {
  color: #9bb0cb;
}

[data-theme='dark'] .knob {
  background: #f2f6fc;
  box-shadow:
    0 1px 2px rgba(0, 0, 0, 0.5),
    0 0 0 1px rgba(255, 255, 255, 0.06);
}
</style>
