import { ref } from 'vue'

/**
 * 拖拽排序（HTML5 原生 drag 事件）。
 *
 * 关键点：**只有把手是 draggable**，列表项本身保持可点击，
 * 否则「点房间筛选」和「拖房间排序」会互相打架。
 *
 * 用法：
 *   const d = useDragSort((from, to) => reorder(from, to))
 *   // 行：@dragover="d.onDragOver($event, id)" @drop="d.onDrop($event, id)"
 *   // 把手：draggable="true" @dragstart="d.onDragStart($event, id)" @dragend="d.onDragEnd()"
 */
export function useDragSort(onReorder: (dragId: string, overId: string) => void) {
  /** 正在被拖动的项 id */
  const dragId = ref('')
  /** 悬停在上方的目标项 id（用于高亮插入位置） */
  const overId = ref('')

  function onDragStart(e: DragEvent, id: string) {
    dragId.value = id
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move'
      try {
        e.dataTransfer.setData('text/plain', id)
      } catch {
        // 少数环境不允许写 dataTransfer，忽略即可（内部状态已经记下了）
      }
    }
  }

  function onDragOver(e: DragEvent, id: string) {
    if (!dragId.value || dragId.value === id) return
    // 不 preventDefault 的话浏览器不会允许落下
    e.preventDefault()
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'
    overId.value = id
  }

  function onDrop(e: DragEvent, id: string) {
    e.preventDefault()
    e.stopPropagation()
    const from = dragId.value || (e.dataTransfer ? e.dataTransfer.getData('text/plain') : '')
    if (from && from !== id) onReorder(from, id)
    reset()
  }

  function onDragEnd() {
    reset()
  }

  function reset() {
    dragId.value = ''
    overId.value = ''
  }

  return { dragId, overId, onDragStart, onDragOver, onDrop, onDragEnd }
}
