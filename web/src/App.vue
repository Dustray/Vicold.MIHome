<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, ref, shallowReactive } from 'vue'
import QRCode from 'qrcode'
import TitleBar from './components/TitleBar.vue'
import LoginCard from './components/LoginCard.vue'
import DeviceCard from './components/DeviceCard.vue'
import DeviceDrawer from './components/DeviceDrawer.vue'
import EnvironmentView from './components/EnvironmentView.vue'
import SettingsPage from './components/SettingsPage.vue'
import { invoke, on, host } from './lib/host'
import { getSettings, setAutostart, setCloseToTray } from './lib/settings'
import { ensureDeviceImages } from './lib/deviceImage'
import { EDGE_CURSOR, edgeDirAt } from './lib/edgeResize'
import { channelKeyOf, findOnProp, groupByRoom, isPowerProp, mergeSplitDevices, onPropOfSiid, physicalDid } from './lib/device'
import { airProps, channelWireless, fishTankProps, isAirConditioner, isAirMonitor, isFishTank, isLight, isSpeaker, isTempHumiditySensor, mainPollutant, propBy, sensorProps, speakerProps, switchChannels, switchForm, type SwitchChannel } from './lib/spec'
import { valueLabel } from './lib/zh'
import {
  buildControls,
  buildPinCandidates,
  computeRooms,
  envPropsOf,
  envReadings,
  envSeriesOf,
  loadEnvReadings,
  METRICS,
  recordValues,
  trendOf,
  type EnvAdjust,
  type EnvControl,
  type MetricKey
} from './lib/env'
import { backfillCloud, BACKFILL_HOURS, flush as flushHistory, load as loadHistory, startAutoFlush, startNowTicker, stopAutoFlush, stopNowTicker } from './lib/history'
import { hasCustomOrder, moveRoom as moveRoomOrder, resetRoomOrder, sortRooms } from './lib/roomOrder'
import { useDragSort } from './lib/sortable'
import type { CardKeyView, Device, Home, PropDef, Scene, Spec } from './lib/types'

// ---------------- 主题 ----------------
const theme = ref<'light' | 'dark'>('light')
function applyTheme(next: 'light' | 'dark') {
  theme.value = next
  document.documentElement.dataset.theme = next
  localStorage.setItem('vicold.theme', next)
}
function toggleTheme() {
  applyTheme(theme.value === 'dark' ? 'light' : 'dark')
}

// ---------------- 登录 ----------------
const authed = ref(false)
const loginStatus = ref<'loading' | 'scan' | 'error'>('loading')
const qr = ref('')
const loginMessage = ref('')

async function startLogin() {
  loginStatus.value = 'loading'
  loginMessage.value = ''
  qr.value = ''
  try {
    const res = await invoke<{ refreshed: boolean; login_url: string }>('auth.begin')
    if (res.refreshed) {
      authed.value = true
      await loadDevices()
      return
    }
    qr.value = await QRCode.toDataURL(res.login_url, { margin: 1, width: 280, color: { dark: '#101828', light: '#ffffff' } })
    loginStatus.value = 'scan'
    await invoke('auth.wait')
    authed.value = true
    await loadDevices()
  } catch (e) {
    loginStatus.value = 'error'
    loginMessage.value = (e as Error).message || '登录失败'
  }
}

async function logout() {
  await invoke('auth.logout').catch(() => undefined)
  authed.value = false
  rawDevices.value = []
  startLogin()
}

// ---------------- 后台常驻 ----------------
/**
 * 开机自启 = 写 HKCU\...\Run 一个带 --tray 的命令。
 * 开机时静默进托盘，进程一直活着，宿主每分钟推 sample.tick 继续采样，
 * 历史曲线才不会因为「没开程序」断出大空档。
 *
 * 状态由宿主回读（注册表可能被组策略挡住），前端这份只是镜像；
 * 托盘右键菜单也能改，改完宿主推 autostart.changed 回来。
 */
const autostart = ref(false)
const autostartBusy = ref(false)
/**
 * 关闭窗口时最小化到托盘。
 *
 * 设置页和托盘右键菜单改的是同一个字段，宿主回读的才算数；
 * 关掉之后点关闭按钮是真退出，后台采样也跟着停 —— 界面上要把这层说清。
 */
const closeToTray = ref(true)
const closeToTrayBusy = ref(false)

async function loadSettings() {
  const s = await getSettings()
  autostart.value = !!s.autostart
  closeToTray.value = !!s.closeToTray
}

async function toggleAutostart() {
  if (autostartBusy.value) return
  autostartBusy.value = true
  try {
    autostart.value = await setAutostart(!autostart.value)
  } catch {
    // 写失败就保持原状态，不要显示成已开
    await loadSettings()
  } finally {
    autostartBusy.value = false
  }
}

async function toggleCloseToTray() {
  if (closeToTrayBusy.value) return
  closeToTrayBusy.value = true
  try {
    closeToTray.value = await setCloseToTray(!closeToTray.value)
  } catch {
    // 写失败（settings.json 不可写）就退回真实值
    await loadSettings()
  } finally {
    closeToTrayBusy.value = false
  }
}

// ---------------- 设备 ----------------
/**
 * 云端原始设备清单。
 *
 * ⚠️ 这里**保留拆分后的原始条目**（一台三键开关=父设备+2~3 条 `did.sN`），
 *    界面上一律用下面合并过的 `devices`。保留原样是因为：
 *    虚拟子设备的 did 同时是「每一路开关状态」的键，也是「这一路叫什么」的
 *    唯一来源（父设备规格里只有「左键/中键/右键」）。
 */
const rawDevices = ref<Device[]>([])
/** 合并后的一台物理设备一条（多键面板的每一路挂在 `channels` 上） */
const devices = computed(() => mergeSplitDevices(rawDevices.value))
const loading = ref(false)
const keyword = ref('')
const activeRoom = ref<string>('all')
/**
 * 通断状态表。
 *
 * 键是**通道键**（`channelKeyOf`）：多键面板按虚拟子设备 did（`xxx.s2`）分别存，
 * 单路设备就是设备自己的 did。合并成一张卡片后，三个键仍然各有各的状态，
 * 抽屉里改哪一路、卡片上就跟着变。
 */
const powers = reactive<Record<string, boolean | null>>({})
/**
 * 开关通道模式：0 = 有线和无线（物理通断），1 = 无线开关。
 *
 * 这是规格里唯一能区分「按键是不是真的通断电路」的权威字段，
 * 卡片上的「物理通断 / 无线开关」标注就靠它。键同 `powers`（按路存）。
 */
const switchMode = reactive<Record<string, number>>({})
/** 空调卡片摘要：模式 + 目标温度（其余品类不读） */
const acState = reactive<Record<string, { temp?: number; mode?: string; modeV?: number | string }>>({})
/** 灯具卡片摘要：亮度 + 色温（关机时也用，方便一眼看设定值） */
const lightState = reactive<Record<string, { br?: number; ct?: number; mode?: string; modeV?: number | string }>>({})
/** 温湿度计卡片摘要：纯读数设备没有开关，卡片上直接给温湿度 */
const sensorState = reactive<Record<string, { temp?: number; hum?: number }>>({})
/** 空气质量监测仪卡片摘要：主污染物浓度是读数本身，温度带上做参照 */
const airState = reactive<Record<string, { main?: number; label?: string; temp?: number }>>({})
/** 音箱 / 家庭屏卡片摘要：音量 + 播放状态（这类设备常开，没有电源开关） */
const speakerState = reactive<Record<string, { vol?: number; playing?: string; muted?: boolean }>>({})
/**
 * 鱼缸卡片摘要：缸内水温 + 水泵工况。
 *
 * ⚠️ 这里的温度是**缸内水温**，不是室温；环境页已把「鱼缸」字样的
 *    温度属性排除在室温之外，两者别混。
 */
const tankState = reactive<Record<string, { temp?: number; pump?: string; alert?: boolean }>>({})
/**
 * 设备规格缓存。
 *
 * ⚠️ 必须用 shallowReactive：普通 Map 不可追踪，环境页/卡片这些 computed
 * 依赖 specs 里的内容，规格后到一步，界面就永远停在"没数据"的状态。
 * shallow 是为了不去深度代理几十份大 JSON，只追踪 Map 本身的增删。
 */
const specs = shallowReactive(new Map<string, Spec>())
const specBusy = reactive<Record<string, boolean>>({})
const toast = ref('')
let toastTimer = 0

function flash(text: string) {
  toast.value = text
  window.clearTimeout(toastTimer)
  toastTimer = window.setTimeout(() => (toast.value = ''), 2600)
}

// ---------------- 家庭 / 房间 ----------------
const homeList = ref<Home[]>([])
const currentHomeId = ref('')

const currentHome = computed(() => homeList.value.find((h) => h.id === currentHomeId.value) || null)

const homeDevices = computed(() => devices.value.filter((d) => d.home_id === currentHomeId.value))

/** 房间以家庭信息为准，没有设备的房间也要列出来 */
const rooms = computed(() => {
  const base = (currentHome.value?.rooms || []).map((r) => ({
    id: r.id,
    name: r.name,
    count: homeDevices.value.filter((d) => d.room_id === r.id).length
  }))
  const known = new Set(base.map((r) => r.id))
  const orphan = homeDevices.value.filter((d) => !known.has(d.room_id)).length
  if (orphan) base.push({ id: '__none__', name: '未分配', count: orphan })
  // 顺序由用户自己拖出来的决定；没排过就保持米家给的顺序
  return sortRooms(currentHomeId.value, base)
})

/**
 * 房间排序：侧栏列表和环境页房间卡片共用这一份顺序。
 *
 * 拖动结束才写库，落库的是**完整**的房间 id 顺序，
 * 以后新增的房间会按当前顺序接在后面。
 */
function reorderRooms(dragId: string, overId: string) {
  moveRoomOrder(currentHomeId.value, rooms.value.map((r) => r.id), dragId, overId)
}

const {
  dragId: roomDragId,
  overId: roomDragOver,
  onDragStart: roomDragStart,
  onDragOver: roomDragOverAt,
  onDrop: roomDrop,
  onDragEnd: roomDragEnd
} = useDragSort(reorderRooms)

/** 恢复米家给的默认房间顺序 */
function resetRoomSort() {
  resetRoomOrder(currentHomeId.value)
}

/** 当前家庭是否被用户排过序（决定要不要显示「重置顺序」） */
const roomSorted = computed(() => hasCustomOrder(currentHomeId.value))

function selectHome(id: string) {
  currentHomeId.value = id
  activeRoom.value = 'all'
  localStorage.setItem('vicold.home', id)
}

async function loadHomes() {
  try {
    const list = await invoke<Home[]>('home.list')
    // 共享设备单独作为一项，方便查看别人共享过来的设备
    if (devices.value.some((d) => d.home_id === 'shared')) {
      list.push({ id: 'shared', name: '共享设备', uid: 0, rooms: [] })
    }
    homeList.value = list
    const saved = localStorage.getItem('vicold.home')
    const hit = list.find((h) => h.id === saved)
    currentHomeId.value = hit ? hit.id : list[0]?.id || ''
  } catch (e) {
    flash((e as Error).message)
  }
}

const filtered = computed(() => {
  const kw = keyword.value.trim().toLowerCase()
  const list = homeDevices.value.filter((d) => {
    if (activeRoom.value !== 'all') {
      if (activeRoom.value === '__none__') {
        const known = new Set(rooms.value.map((r) => r.id))
        if (known.has(d.room_id)) return false
      } else if (d.room_id !== activeRoom.value) {
        return false
      }
    }
    if (!kw) return true
    // 合并后的多键面板要能按「某一路的名字」搜到（用户记的是「玄关射灯」）
    const names = [d.name, d.model, ...(d.channels || []).map((c) => c.name)]
    return names.some((n) => n.toLowerCase().includes(kw))
  })
  // 米家返回的设备清单每次顺序都不同，直接渲染会让卡片位置每次刷新都乱跳，
  // 用户找不到设备。按「房间顺序 + 设备名」固定下来，位置就稳定了。
  const roomOrder = new Map(rooms.value.map((r, i) => [r.id, i]))
  return [...list].sort((a, b) => {
    const ra = roomOrder.get(a.room_id) ?? Number.MAX_SAFE_INTEGER
    const rb = roomOrder.get(b.room_id) ?? Number.MAX_SAFE_INTEGER
    if (ra !== rb) return ra - rb
    return a.name.localeCompare(b.name, 'zh-Hans-CN')
  })
})

/**
 * 设备卡片按房间分段，组的先后就是侧栏房间列表的顺序（用户拖过就是拖出来的顺序）。
 * 排序本身交给 filtered（房间顺序 + 设备名），分组只切段不重排，口径只维护一处。
 */
const deviceGroups = computed(() => groupByRoom(filtered.value, rooms.value))

async function loadDevices() {
  loading.value = true
  try {
    rawDevices.value = await invoke<Device[]>('device.list')
    await loadHomes()
    void ensureSpecs()
    // 产品图：宿主抓一次落盘，之后本地加载（首次启动几十台设备要几秒）
    ensureDeviceImages(homeDevices.value.map((d) => d.model))
    // 读 7 天内的历史采样，曲线一打开就有内容
    void loadHistory(
      homeDevices.value.map((d) => d.did),
      Math.floor(Date.now() / 1000) - 7 * 24 * 3600,
      2000
    )
    void backfillHistory()
  } catch (e) {
    flash((e as Error).message)
  } finally {
    loading.value = false
  }
}

/**
 * 从云端补历史曲线。
 *
 * 本机采样只在程序运行时进行，关一夜就是十几小时的空档；云端其实一直存着
 * 读数历史（米家 App 的「历史记录」走的就是 `/user/get_user_device_data`），
 * 启动时把过去 24 小时并进本地库，断档就被填上了。
 *
 * 回填要 siid/piid，规格没到位就不知道，所以必须等 ensureSpecs()。
 * 6 小时内只补一次（见 lib/history.ts 的 shouldBackfill）。
 */
let backfillTask: Promise<void> | null = null
function backfillHistory(): Promise<void> {
  if (!backfillTask) backfillTask = runBackfill()
  return backfillTask
}

async function runBackfill() {
  try {
    await ensureSpecs()
    const n = await backfillCloud(envSeriesOf(homeDevices.value, specs))
    if (n > 0) flash(`已载入最近 ${BACKFILL_HOURS} 小时环境记录`)
  } catch {
    // 回填失败不影响主功能，本机采样照常继续
  }
}

async function fetchSpec(device: Device): Promise<Spec | null> {
  const cached = specs.get(device.did)
  if (cached) return cached
  if (!device.spec_type) return null
  try {
    const spec = await invoke<Spec>('device.spec', { spec_type: device.spec_type })
    specs.set(device.did, spec)
    return spec
  } catch {
    return null
  }
}

/** 后台补齐卡片上的开关状态：规格 → 批量读 on 属性 */
let hydrateTask: Promise<void> | null = null
function ensureSpecs(): Promise<void> {
  if (!hydrateTask) hydrateTask = hydratePowers()
  return hydrateTask
}

async function hydratePowers() {
  const queue = [...devices.value]
  const items: PowerItem[] = []
  const acItems: AcItem[] = []
  const lightItems: LightItem[] = []
  const sensorItems: SensorItem[] = []
  const airItems: AirItem[] = []
  const speakerItems: SpeakerItem[] = []
  const tankItems: TankItem[] = []

  async function worker() {
    for (;;) {
      const device = queue.shift()
      if (!device) return
      const spec = await fetchSpec(device)
      if (!spec) continue

      // 虚拟子设备（.s2 这类）只能用父 did 读属性，siid 才是那一路编号
      const pDid = physicalDid(device.did)
      const uiDid = device.did

      // 温湿度计是纯读数设备，没有 on 属性，走自己的摘要通道
      if (isTempHumiditySensor(spec)) {
        const core = sensorProps(spec)
        if (core.temp) {
          sensorItems.push({ did: pDid, uiDid, siid: core.temp.siid, piid: core.temp.piid, kind: 'temp' })
        }
        if (core.humidity) {
          sensorItems.push({ did: pDid, uiDid, siid: core.humidity.siid, piid: core.humidity.piid, kind: 'hum' })
        }
        continue
      }

      // 空气质量监测仪同样是纯读数设备（甲醛仪连 on 都没有），摘要给主污染物浓度
      if (isAirMonitor(spec)) {
        const core = airProps(spec)
        const mp = mainPollutant(spec)
        if (mp) {
          airItems.push({
            did: pDid,
            uiDid,
            siid: mp.def.siid,
            piid: mp.def.piid,
            kind: 'main',
            label: mp.label,
          })
        }
        if (core.temp) {
          airItems.push({ did: pDid, uiDid, siid: core.temp.siid, piid: core.temp.piid, kind: 'temp' })
        }
        continue
      }

      // 音箱 / 家庭屏常开，多数机型没有电源 on（Mini 的 on 是闹钟开关），走自己的摘要通道
      if (isSpeaker(spec)) {
        const sp = speakerProps(spec)
        if (sp.volume) {
          speakerItems.push({ did: pDid, uiDid, siid: sp.volume.siid, piid: sp.volume.piid, kind: 'vol' })
        }
        // playing-state 只 notify 的机型（lx06）读不到，不发这条请求
        if (sp.playing && sp.playing.rw.includes('r')) {
          speakerItems.push({
            did: pDid,
            uiDid,
            siid: sp.playing.siid,
            piid: sp.playing.piid,
            kind: 'playing',
            def: sp.playing,
          })
        }
        // 卡片摘要里的"静音"只认扬声器静音：麦克风静音不等于没声音，
        // x8f 这类只有麦克风静音的机型干脆不收集，卡片就只显示播放状态
        if (sp.spkMute) {
          speakerItems.push({ did: pDid, uiDid, siid: sp.spkMute.siid, piid: sp.spkMute.piid, kind: 'mute' })
        }
        continue
      }

      // 合并后的多键面板：**每一路**都要读各自那一格 on（siid 各不相同），
      // 状态按通道键（虚拟子设备 did）分开存；单路设备走下面那条老路。
      const chans = device.channels && device.channels.length ? device.channels : null
      if (chans) {
        for (const ch of chans) {
          const on = onPropOfSiid(spec, ch.siid)
          if (!on) continue
          items.push({ did: pDid, state: ch.did, siid: on.siid, piid: on.piid, kind: 'on' })
          // 顺带读这一路的通道模式：卡片要据此标注「物理通断 / 无线开关」。
          // mode 只 read/notify 的机型（少数固件）读不到，就按物理通断兜底。
          const mode = propBy(spec, 'mode', ch.siid)
          if (mode && mode.rw.includes('r')) {
            items.push({
              did: pDid,
              state: ch.did,
              siid: mode.siid,
              piid: mode.piid,
              kind: 'mode',
              key: mode.name,
            })
          }
        }
      } else {
        // 单路设备（一个 on）或旧固件没有拆分条目的多键面板：只读第一路
        const onProp = findOnProp(spec, device.did)
        if (!onProp) continue
        items.push({ did: pDid, state: uiDid, siid: onProp.siid, piid: onProp.piid, kind: 'on' })

        const chMode = propBy(spec, 'mode', onProp.siid)
        if (chMode && chMode.rw.includes('r')) {
          items.push({
            did: pDid,
            state: uiDid,
            siid: chMode.siid,
            piid: chMode.piid,
            kind: 'mode',
            key: chMode.name,
          })
        }
      }

      // 空调卡片额外读「模式 + 目标温度」，让卡片直接显示「制冷 27°C」
      if (isAirConditioner(spec)) {
        const t = propBy(spec, 'target-temperature')
        const m = propBy(spec, 'mode')
        if (t) acItems.push({ did: pDid, uiDid, siid: t.siid, piid: t.piid, kind: 'temp', spec })
        if (m) acItems.push({ did: pDid, uiDid, siid: m.siid, piid: m.piid, kind: 'mode', spec })
      }

      // 鱼缸卡片额外读「缸内水温 + 水泵工况」，让卡片直接显示「26°C · 水泵运行中」
      if (isFishTank(spec)) {
        const ft = fishTankProps(spec)
        if (ft.temperature) {
          tankItems.push({ did: pDid, uiDid, siid: ft.temperature.siid, piid: ft.temperature.piid, kind: 'temp' })
        }
        if (ft.pumpStatus) {
          tankItems.push({
            did: pDid,
            uiDid,
            siid: ft.pumpStatus.siid,
            piid: ft.pumpStatus.piid,
            kind: 'status',
            spec,
          })
        }
      }

      // 灯具卡片额外读「亮度 + 色温 + 模式」，让卡片直接显示「62% · 4000K」或「月光 · 12%」
      if (isLight(spec)) {
        const b = propBy(spec, 'brightness')
        const c = propBy(spec, 'color-temperature')
        const m = propBy(spec, 'mode')
        if (b) lightItems.push({ did: pDid, uiDid, siid: b.siid, piid: b.piid, kind: 'br' })
        if (c) lightItems.push({ did: pDid, uiDid, siid: c.siid, piid: c.piid, kind: 'ct' })
        if (m) lightItems.push({ did: pDid, uiDid, siid: m.siid, piid: m.piid, kind: 'mode', def: m })
      }

      if (items.length % 40 === 0) await flushPowerBatch(items)
    }
  }
  await Promise.all([worker(), worker(), worker(), worker()])
  await flushPowerBatch(items)
  await flushAcBatch(acItems)
  await flushLightBatch(lightItems)
  await flushSensorBatch(sensorItems)
  await flushAirBatch(airItems)
  await flushSpeakerBatch(speakerItems)
  await flushTankBatch(tankItems)
}

interface SpeakerItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'vol' | 'playing' | 'mute'
  def?: PropDef
}

const PLAY_STATE_ZH: Record<string, string> = { '0': '已停止', '1': '播放中', '2': '已暂停' }

/** 音箱摘要批量读取：同前几个，按 did/siid/piid 三元组回填，不依赖返回顺序 */
async function flushSpeakerBatch(items: SpeakerItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const st = speakerState[it.uiDid] || (speakerState[it.uiDid] = {})
      if (it.kind === 'mute') {
        st.muted = row.value === true || row.value === 1 || row.value === '1'
        continue
      }
      if (it.kind === 'playing') {
        const key = String(row.value)
        const hit = it.def?.value_list?.find((v) => String(v.value) === key)
        st.playing = hit ? valueLabel(hit) : PLAY_STATE_ZH[key] || ''
        continue
      }
      const n = Number(row.value)
      if (Number.isFinite(n)) st.vol = n
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

interface SensorItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'temp' | 'hum'
}

/** 温湿度计摘要批量读取：与前两个一样按 did/siid/piid 三元组回填 */
async function flushSensorBatch(items: SensorItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const n = Number(row.value)
      if (!Number.isFinite(n)) continue
      // 与环境页同一道过滤：超出 -10~38°C / 0~100% 的读数不当作室温
      if (it.kind === 'temp' && (n < -10 || n > 38)) continue
      if (it.kind === 'hum' && (n < 0 || n > 100)) continue
      const st = sensorState[it.uiDid] || (sensorState[it.uiDid] = {})
      if (it.kind === 'temp') st.temp = n
      else st.hum = n
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

interface AirItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'main' | 'temp'
  /** 主污染物的中文名（甲醛 / PM2.5 / TVOC），摘要要用 */
  label?: string
}

/** 空气质量监测仪摘要批量读取：同样按 did/siid/piid 三元组回填 */
async function flushAirBatch(items: AirItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const n = Number(row.value)
      if (!Number.isFinite(n)) continue
      // 甲醛读数是 0 属于正常，不能当"无数据"丢掉；只挡明显越界的脏值
      if (it.kind === 'main' && (n < 0 || n > 10)) continue
      if (it.kind === 'temp' && (n < -10 || n > 38)) continue
      const st = airState[it.uiDid] || (airState[it.uiDid] = {})
      if (it.kind === 'main') {
        st.main = n
        if (it.label) st.label = it.label
      } else {
        st.temp = n
      }
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

interface LightItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'br' | 'ct' | 'mode'
  def?: PropDef
}

/** 灯具摘要批量读取：同样按 did/siid/piid 三元组回填，不依赖返回顺序 */
async function flushLightBatch(items: LightItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const st = lightState[it.uiDid] || (lightState[it.uiDid] = {})
      if (it.kind === 'mode') {
        const hit = it.def?.value_list?.find((v) => String(v.value) === String(row.value))
        st.mode = hit ? valueLabel(hit) : String(row.value)
        // 原始枚举值：控制中心要靠它高亮当前模式
        st.modeV = row.value as number | string as number | string
        continue
      }
      const n = Number(row.value)
      if (!Number.isFinite(n)) continue
      if (it.kind === 'br') st.br = n
      else st.ct = n
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

interface AcItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'temp' | 'mode'
  spec: Spec
}

/** 空调摘要批量读取：返回顺序会被重排，必须按 did/siid/piid 三元组回填 */
async function flushAcBatch(items: AcItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const st = acState[it.uiDid] || (acState[it.uiDid] = {})
      if (it.kind === 'temp') {
        const n = Number(row.value)
        if (Number.isFinite(n)) st.temp = n
      } else {
        const def = it.spec.properties.find((p) => p.siid === it.siid && p.piid === it.piid)
        const hit = def?.value_list?.find((v) => String(v.value) === String(row.value))
        st.mode = hit ? valueLabel(hit) : ''
        // 原始枚举值：控制中心要靠它高亮当前模式
        st.modeV = row.value as number | string
      }
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

interface TankItem {
  did: string
  /** 界面上的 did：虚拟子设备按子 did 存状态，请求一律用父 did */
  uiDid: string
  siid: number
  piid: number
  kind: 'temp' | 'status'
  spec?: Spec
}

/**
 * 水泵工况在卡片上的短标签。
 *
 * 正常态（开启 / 关闭）翻成「水泵运行 / 水泵关闭」，
 * 异常态（低水位 / 堵转 / 故障 / 未连接）保留原文，一眼能看出问题。
 */
function tankPumpLabel(text: string): string {
  if (/开启|运行/.test(text)) return '水泵运行'
  if (/关闭/.test(text)) return '水泵关闭'
  return text
}

/** 水泵工况是否属于异常 */
function tankPumpAlert(text: string): boolean {
  return /低水位|堵转|故障|未连接|缺水/.test(text)
}

/**
 * 鱼缸摘要批量读取：返回顺序会被重排，必须按 did/siid/piid 三元组回填。
 */
async function flushTankBatch(items: TankItem[]) {
  if (!items.length) return
  try {
    const ret = await invoke<{ did?: string; siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: items.map(({ did, siid, piid }) => ({ did, siid, piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of items) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      const st = tankState[it.uiDid] || (tankState[it.uiDid] = {})
      if (it.kind === 'temp') {
        const n = Number(row.value)
        // 缸内水温：0°C 与 >60°C 都是脏值（设备没上报），别显示成 0°
        if (Number.isFinite(n) && n > 0 && n <= 60) st.temp = n
        continue
      }
      const def = it.spec?.properties.find((p) => p.siid === it.siid && p.piid === it.piid)
      const hit = def?.value_list?.find((v) => String(v.value) === String(row.value))
      const text = hit ? valueLabel(hit) : ''
      st.pump = tankPumpLabel(text)
      // 低水位 / 堵转 / 故障 / 未连接：卡片上要能一眼看出异常
      st.alert = tankPumpAlert(text)
    }
  } catch {
    // 摘要只是锦上添花，读失败就让卡片保持原样
  }
}

/** 卡片底部摘要文案（仅开机时给） */
function cardSummary(device: Device): string {
  const se = sensorState[device.did]
  if (se) {
    const parts: string[] = []
    // 去掉多余的 .0：22.0°C 看着别扭，22°C / 24.1°C 才对
    if (se.temp !== undefined) parts.push(`${Number(se.temp.toFixed(1))}°C`)
    if (se.hum !== undefined) parts.push(`${Math.round(se.hum)}%`)
    return parts.join(' · ')
  }
  const air = airState[device.did]
  if (air && air.main !== undefined) {
    // 甲醛保留三位小数（规格步长 0.001），两位会把 0.032 显示成 0.03，和国标线对不齐
    const main = air.main < 1 ? air.main.toFixed(3) : String(Math.round(air.main))
    const parts = [`${air.label || '甲醛'} ${main}`]
    if (air.temp !== undefined) parts.push(`${Number(air.temp.toFixed(1))}°C`)
    return parts.join(' · ')
  }
  const ac = acState[device.did]
  if (ac) {
    const parts: string[] = []
    if (ac.mode) parts.push(ac.mode)
    if (ac.temp !== undefined) parts.push(`${ac.temp}°C`)
    return parts.join(' ')
  }
  // 鱼缸：缸内水温 + 水泵工况（「运行中」正常，「低水位」等异常直接写出来）
  const tk = tankState[device.did]
  if (tk) {
    const parts: string[] = []
    if (tk.temp !== undefined) parts.push(`${Math.round(tk.temp)}°C`)
    if (tk.pump) parts.push(tk.pump)
    return parts.join(' · ')
  }
  const li = lightState[device.did]
  if (li) {
    // 月光档没有色温可调，色温数字就毫无意义，改成「月光 · 30%」
    const night = !!li.mode && /月光|夜间|night/i.test(li.mode)
    const parts: string[] = []
    if (night) parts.push('月光')
    if (li.br !== undefined) parts.push(`${Math.round(li.br)}%`)
    if (!night && li.ct !== undefined) parts.push(`${Math.round(li.ct)}K`)
    return parts.join(' · ')
  }
  const sp = speakerState[device.did]
  if (sp) {
    // 静音优先于播放状态：静音时写「播放中」会让人以为有声音
    const parts: string[] = []
    if (sp.muted) parts.push('静音')
    else if (sp.playing) parts.push(sp.playing)
    if (sp.vol !== undefined) parts.push(`${Math.round(sp.vol)}%`)
    return parts.join(' · ')
  }
  return ''
}

interface PowerItem {
  did: string
  /**
   * 状态键（`powers` / `switchMode` 的键）：多键面板按虚拟子设备 did 分开存，
   * 单路设备就是设备自己的 did。请求一律用父 did + 这一路的 siid。
   */
  state: string
  siid: number
  piid: number
  kind: 'on' | 'mode'
  /** mode 属性的 name（values 的键），回填时写进 switchMode */
  key?: string
}

/**
 * 卡片上要渲染的开关键。
 *
 * - 合并后的多键面板：每一路一个键（名字来自虚拟子设备「玄关射灯」），
 *   卡片上横排成小胶囊，一个卡片就能分别开关三路；
 * - 单路开关 / 插座 / 无线遥控：一个键，沿用原来的扁平翘板按钮；
 * - 非开关品类（窗帘、空调……）：空数组，卡片走通用胶囊开关。
 *
 * 判据在 spec.ts 里和面板共用同一套（`switchChannels` + `channelWireless`），
 * 所以抽屉里写「无线开关」时，卡片上不会是「物理通断」。
 */
function cardKeys(device: Device): CardKeyView[] {
  const spec = specs.get(device.did)
  if (!spec || !switchForm(spec)) return []
  const all = switchChannels(spec, null)

  const view = (siid: number, key: string, name: string, fallback: SwitchChannel | null): CardKeyView => {
    const ch = all.find((c) => c.siid === siid) || fallback
    if (!ch) return { key, siid, name, on: null, wireless: true, busy: !!specBusy[key] }
    const raw = switchMode[key]
    const wireless = ch.on
      ? channelWireless(ch, ch.mode && raw !== undefined ? { [ch.mode.name]: raw } : {})
      : true
    const val = powers[key]
    return {
      key,
      siid: ch.siid,
      name,
      on: ch.on ? (typeof val === 'boolean' ? val : null) : null,
      wireless,
      busy: !!specBusy[key],
    }
  }

  const chans = device.channels && device.channels.length ? device.channels : null
  if (chans) {
    return chans.map((c) => {
      // 子设备的 siid 一定在规格里有对应的开关模块；对不上就按顺序兜底
      const fallback = switchChannels(spec, c.did)[0] || null
      return view(c.siid, c.did, c.name, fallback)
    })
  }

  const ch = switchChannels(spec, device.did)[0]
  if (!ch) return []
  return [view(ch.siid, device.did, device.name, ch)]
}

/**
 * 卡片上的迷你趋势线。
 *
 * 只有能读环境指标的设备才有：优先主污染物（甲醛/PM2.5），其次温度；
 * 少于两个点就不画（一条线说明不了趋势）。
 * 带上要素名，卡片上那条线下面才好标注「这是温度还是湿度」。
 */
function cardTrend(
  device: Device
): { color: string; points: { t: number; v: number }[]; label: string } | null {
  const spec = specs.get(device.did)
  if (!spec) return null
  const p = envPropsOf(spec)
  const key: MetricKey | null = p.hcho
    ? 'hcho'
    : p.pm25
      ? 'pm25'
      : p.co2
        ? 'co2'
        : p.t
          ? 't'
          : null
  if (!key) return null
  const points = trendOf(device.did, key, 24)
  if (points.length < 2) return null
  return { color: METRICS[key].color, points, label: METRICS[key].label }
}

/**
 * 父设备面板上每一路的真实名称。
 *
 * 多键开关在云端被拆成虚拟子设备（`xxx.s2`「主灯」、`.s3`「窗帘」），
 * 规格里只写了「左键 / 中键 / 右键」。用户认的是子设备名，
 * 合并时已经把名字挂在 `device.channels` 上，这里直接摊平成 siid → 名字。
 */
function channelNamesFor(device: Device): Record<number, string> {
  const out: Record<number, string> = {}
  for (const ch of device.channels || []) out[ch.siid] = ch.name
  return out
}

/**
 * 批量读开关状态 + 通道模式。
 *
 * ⚠️ `/miotspec/prop/get` 的返回顺序会和请求顺序**不一致**，
 * 按数组下标回填会把 A 设备的开关状态写到 B 设备上。
 * 必须用 did/siid/piid 三元组匹配；匹配不上就跳过，
 * 让卡片保持「未读」而不是显示一个错的通断状态。
 */
async function flushPowerBatch(items: PowerItem[]) {
  if (!items.length) return
  const chunk = items.splice(0, items.length)
  try {
    const ret = await invoke<
      { did?: string; siid: number; piid: number; code: number; value?: unknown }[]
    >('prop.get', { items: chunk.map(({ did, siid, piid }) => ({ did, siid, piid })) })
    const list = Array.isArray(ret) ? ret : [ret]
    for (const it of chunk) {
      const row = list.find(
        (r) => r && r.did === it.did && r.siid === it.siid && r.piid === it.piid
      )
      if (!row || row.code !== 0) continue
      if (it.kind === 'on') {
        // 必须拿到明确布尔值才写：设备偶发不返回该属性时，`!!undefined`
        // 会把开着的设备误刷成「已关闭」
        if (typeof row.value === 'boolean') powers[it.state] = row.value
      } else {
        const n = Number(row.value)
        if (Number.isFinite(n)) switchMode[it.state] = n
      }
    }
  } catch {
    // 批量读失败就让卡片保持"未读"状态，不阻塞界面
  }
}

async function togglePower(device: Device) {
  const spec = await fetchSpec(device)
  // 多键面板要按 did 后缀选对应那一路的 on 属性，否则永远在开关第一路
  const onProp = findOnProp(spec, device.did)
  if (!onProp) return
  const next = !powers[device.did]
  powers[device.did] = next
  specBusy[device.did] = true
  try {
    await invoke('prop.set', { did: physicalDid(device.did), siid: onProp.siid, piid: onProp.piid, value: next })
  } catch (e) {
    powers[device.did] = !next
    flash((e as Error).message)
  } finally {
    specBusy[device.did] = false
  }
}

/**
 * 卡片上按「某一路」开关。
 *
 * 合并后一张三键卡片上有三个开关，必须用**这一路自己的 siid** 下发，
 * 状态也写回这一路的通道键；拿 `findOnProp(did)` 会永远开关第一路
 * （合并前按 `did.sN` 后缀取，合并后父 did 没有后缀，只能靠 siid）。
 */
async function toggleChannel(device: Device, k: CardKeyView) {
  const spec = await fetchSpec(device)
  const onProp = onPropOfSiid(spec, k.siid)
  if (!onProp || !device.online) return
  const next = !k.on
  powers[k.key] = next
  specBusy[k.key] = true
  try {
    await invoke('prop.set', { did: physicalDid(device.did), siid: onProp.siid, piid: onProp.piid, value: next })
  } catch (e) {
    powers[k.key] = k.on ?? null
    flash((e as Error).message)
  } finally {
    specBusy[k.key] = false
  }
}

// ---------------- 详情抽屉 ----------------
const current = ref<Device | null>(null)
const currentSpec = ref<Spec | null>(null)
const currentValues = ref<Record<string, unknown>>({})
/** 抽屉里多键开关每一路的真实名称（父设备规格里只有"左/中/右键"） */
const currentChannelNames = ref<Record<number, string>>({})
const detailLoading = ref(false)
const busyProp = ref('')

/**
 * 把读到的属性回填成卡片上的通断状态。
 *
 * 多键面板每一路各取自己那格 `on`（siid 各不相同），状态写回**通道键**；
 * 单路设备就是整台设备一个键。合并前这里是按 `did.sN` 后缀取，
 * 合并后父 did 没有后缀，只能按 siid 取。
 */
function syncPowerStates(device: Device, spec: Spec, values: Record<string, unknown>) {
  const chans = device.channels && device.channels.length ? device.channels : null
  if (chans) {
    for (const ch of chans) {
      const on = onPropOfSiid(spec, ch.siid)
      const v = on ? values[on.name] : undefined
      if (typeof v === 'boolean') powers[ch.did] = v
      const mode = propBy(spec, 'mode', ch.siid)
      if (mode && values[mode.name] !== undefined) {
        const n = Number(values[mode.name])
        if (Number.isFinite(n)) switchMode[ch.did] = n
      }
    }
    return
  }
  const onProp = findOnProp(spec, device.did)
  const onValue = onProp ? values[onProp.name] : undefined
  if (typeof onValue === 'boolean') powers[device.did] = onValue
}

async function openDevice(device: Device) {
  // 面板要靠房间环境读数兜底显示"房间现在几度"，没拉过就先补一次
  if (!Object.keys(envReadings).length) void loadEnv()
  current.value = device
  currentSpec.value = null
  currentValues.value = {}
  currentChannelNames.value = {}
  detailLoading.value = true
  const spec = await fetchSpec(device)
  if (!spec) {
    detailLoading.value = false
    return
  }
  currentSpec.value = spec
  currentChannelNames.value = channelNamesFor(device)
  const readable = spec.properties.filter((p) => p.rw.includes('r'))
  try {
    const items = readable.map((p) => ({ did: physicalDid(device.did), siid: p.siid, piid: p.piid }))
    const ret = await invoke<{ siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    const values: Record<string, unknown> = {}
    readable.forEach((p) => {
      const row = list.find((r) => r.siid === p.siid && r.piid === p.piid)
      if (row && row.code === 0) values[p.name] = row.value
    })
    currentValues.value = values
    syncReadings(device.did, spec, values)
    syncPowerStates(device, spec, values)
  } catch (e) {
    flash((e as Error).message)
  } finally {
    detailLoading.value = false
  }
}

function closeDrawer() {
  current.value = null
  currentSpec.value = null
}

/** 卡片摘要里的灯具亮度/色温/模式跟随操作实时变化 */
function syncLightSummary(did: string, base: string, value: unknown) {
  const li = lightState[did]
  if (!li) return
  const n = Number(value)
  if (!Number.isFinite(n)) return
  if (base === 'brightness') li.br = n
  else if (base === 'color-temperature') li.ct = n
}

/** 模式是枚举值，得先查成中文再进摘要 */
function syncLightMode(did: string, def: PropDef, value: unknown) {
  const li = lightState[did]
  if (!li) return
  const hit = def.value_list?.find((v) => String(v.value) === String(value))
  li.mode = hit ? valueLabel(hit) : String(value)
  li.modeV = value as number | string
}

/** 音箱卡片摘要里的音量/静音跟随抽屉操作实时变化 */
function syncSpeakerSummary(did: string, def: PropDef, value: unknown) {
  const st = speakerState[did]
  if (!st) return
  if (def.base_name === 'volume') {
    const n = Number(value)
    if (Number.isFinite(n)) st.vol = n
  } else if (def.base_name === 'mute' && def.service === 'speaker') {
    // 只认扬声器静音，麦克风静音不影响"有没有声音"
    st.muted = value === true || value === 1 || value === '1'
  }
}

/**
 * 鱼缸卡片摘要里的水泵工况跟随抽屉操作实时变化。
 *
 * ⚠️ `water-pump` 是我们写下去的开关，`water-pump-status` 才是设备报的实际工况。
 *    这里乐观更新开关那一份，等设备上报后 `syncReadings` 会用真实状态覆盖。
 */
function syncTankSummary(did: string, def: PropDef, value: unknown) {
  const st = tankState[did]
  if (!st) return
  if (def.base_name === 'water-pump') {
    st.pump = value ? '水泵运行' : '水泵关闭'
    st.alert = false
  }
}

async function setProp(def: PropDef, value: unknown) {
  const device = current.value
  if (!device) return
  const prev = currentValues.value[def.name]
  // 状态键按「这一路」算：合并后的三键面板在抽屉里改哪一路，卡片上就是哪一路
  const chKey = channelKeyOf(device, def.siid)
  currentValues.value = { ...currentValues.value, [def.name]: value }
  if (isPowerProp(def)) powers[chKey] = !!value
  // 卡片摘要跟随抽屉里的操作实时变化
  const ac = acState[device.did]
  if (ac) {
    if (def.base_name === 'target-temperature') ac.temp = Number(value)
    else if (def.base_name === 'mode') {
      const hit = def.value_list?.find((v) => String(v.value) === String(value))
      if (hit) ac.mode = valueLabel(hit)
      ac.modeV = value as number | string
    }
  }
  syncLightSummary(device.did, def.base_name, value)
  if (def.base_name === 'mode') syncLightMode(device.did, def, value)
  syncSpeakerSummary(device.did, def, value)
  syncTankSummary(device.did, def, value)
  // 开关通道模式改了（有线和无线 ↔ 无线），卡片的通断标注要立刻跟着变
  const swSpec = specs.get(device.did)
  const prevMode = switchMode[chKey]
  if (def.base_name === 'mode' && swSpec && switchForm(swSpec)) {
    const n = Number(value)
    if (Number.isFinite(n)) switchMode[chKey] = n
  }
  busyProp.value = def.name
  try {
    await invoke('prop.set', { did: physicalDid(device.did), siid: def.siid, piid: def.piid, value })
  } catch (e) {
    currentValues.value = { ...currentValues.value, [def.name]: prev }
    if (isPowerProp(def)) powers[chKey] = !!prev
    syncLightSummary(device.did, def.base_name, prev)
    if (def.base_name === 'mode') syncLightMode(device.did, def, prev)
    syncSpeakerSummary(device.did, def, prev)
    syncTankSummary(device.did, def, prev)
    if (def.base_name === 'mode' && swSpec && switchForm(swSpec)) {
      if (prevMode === undefined) delete switchMode[chKey]
      else switchMode[chKey] = prevMode
    }
    flash((e as Error).message)
  } finally {
    busyProp.value = ''
  }
}

/**
 * 刷新后把读数同步回卡片摘要。
 * 温度计 / 甲醛仪这类纯读数设备，卡片上那行数字就是它的全部内容，
 * 抽屉里点了刷新而卡片还停在旧值，会让人以为数据根本没更新。
 */
function syncReadings(did: string, spec: Spec, values: Record<string, unknown>) {
  // 顺带把环境读数写进历史库，抽屉里看到的数值和环境页的曲线是同一份
  recordValues(did, spec, values)
  if (isTempHumiditySensor(spec)) {
    const c = sensorProps(spec)
    const st = sensorState[did] || (sensorState[did] = {})
    const t = c.temp ? Number(values[c.temp.name]) : NaN
    const h = c.humidity ? Number(values[c.humidity.name]) : NaN
    if (Number.isFinite(t)) st.temp = t
    if (Number.isFinite(h)) st.hum = h
    return
  }
  if (isAirMonitor(spec)) {
    const c = airProps(spec)
    const mp = mainPollutant(spec)
    const st = airState[did] || (airState[did] = {})
    const m = mp ? Number(values[mp.def.name]) : NaN
    const t = c.temp ? Number(values[c.temp.name]) : NaN
    if (Number.isFinite(m)) {
      st.main = m
      st.label = mp?.label
    }
    if (Number.isFinite(t)) st.temp = t
  }
  if (isFishTank(spec)) {
    // 鱼缸：缸内水温 + 水泵工况。抽屉里刷新后，卡片上的数字要跟着变
    const ft = fishTankProps(spec)
    const st = tankState[did] || (tankState[did] = {})
    const t = ft.temperature ? Number(values[ft.temperature.name]) : NaN
    if (Number.isFinite(t) && t > 0 && t <= 60) st.temp = t
    if (ft.pumpStatus) {
      const v = values[ft.pumpStatus.name]
      const hit = ft.pumpStatus.value_list?.find((x) => String(x.value) === String(v))
      const text = hit ? valueLabel(hit) : ''
      st.pump = tankPumpLabel(text)
      st.alert = tankPumpAlert(text)
    }
  }
}

/** 重新读取抽屉里这台设备的全部可读属性（灯具切模式后设备会自己改亮度/色温） */
async function refreshValues() {
  const device = current.value
  const spec = currentSpec.value
  if (!device || !spec) return
  const readable = spec.properties.filter((p) => p.rw.includes('r'))
  if (!readable.length) return
  try {
    const ret = await invoke<{ siid: number; piid: number; code: number; value?: unknown }[]>(
      'prop.get',
      { items: readable.map((p) => ({ did: physicalDid(device.did), siid: p.siid, piid: p.piid })) }
    )
    const list = Array.isArray(ret) ? ret : [ret]
    const next = { ...currentValues.value }
    readable.forEach((p) => {
      const row = list.find((r) => r.siid === p.siid && r.piid === p.piid)
      if (row && row.code === 0) next[p.name] = row.value
    })
    currentValues.value = next
    syncReadings(device.did, spec, next)
    // 设备在别处被按过（或本地按键改了这一路）时，卡片上的开关也要跟上
    syncPowerStates(device, spec, next)
  } catch {
    // 刷新失败就保持现有读数，不打断操作
  }
}

/**
 * 执行设备动作。
 *
 * `params` 是动作的入参（MIoT-Spec-V2 的 action.in），值按规格里
 * `in` 数组的顺序排列 —— 鱼缸的「喂食」就是靠它传出粮量。
 */
async function runAction(siid: number, aiid: number, name: string, params?: unknown[]) {
  const device = current.value
  if (!device) return
  try {
    await invoke('action.run', {
      did: physicalDid(device.did),
      siid,
      aiid,
      params: params && params.length ? params : [],
    })
    flash(`已执行：${name}`)
  } catch (e) {
    flash((e as Error).message)
  }
}

// ---------------- 场景 ----------------
const view = ref<'devices' | 'scenes' | 'env' | 'settings'>('env')
const envLoading = ref(false)

/**
 * 打开设置页（标题栏齿轮按钮）。
 *
 * 抽屉先关掉：设置页在抽屉下面，不关的话点齿轮看着像没反应。
 */
function openSettings() {
  closeDrawer()
  view.value = 'settings'
}

/**
 * 采集环境读数：温湿度 + PM2.5 / PM10 / 甲醛 / CO₂ / 空气质量。
 *
 * 属性挑选与范围过滤的口径都在 lib/env.ts 里，采集完顺手写进历史库，
 * 环境页和设备抽屉的折线图共用同一份历史。
 */
async function loadEnv() {
  envLoading.value = true
  try {
    await ensureSpecs()
    await loadEnvReadings(homeDevices.value, specs)
    await flushHistory()
  } catch (e) {
    flash((e as Error).message)
  } finally {
    envLoading.value = false
  }
}

/** 环境页聚焦的房间（空 = 全屋），仪表板与控制中心共用 */
const envRoomFilter = ref('')

/** 环境页的设备快捷控制：吸顶灯 / 物理通断开关 / 空调 */
const envControls = computed<EnvControl[]>(() =>
  buildControls(homeDevices.value, specs, { powers, switchMode, lightState, acState })
)

/** 房间聚合：温湿度取代表值，污染物取房间内最差的一台 */
const envRooms = computed(() => {
  const base = computeRooms(rooms.value, homeDevices.value, envReadings)
  const byId = new Map(base.map((r) => [r.id, r]))
  // 有吸顶灯/空调但没有传感器的房间也要出现 —— 用户要看到这些设备的状态
  for (const c of envControls.value) {
    const rid = c.device.room_id
    if (!rid || byId.has(rid)) continue
    const room = rooms.value.find((r) => r.id === rid)
    if (!room) continue
    byId.set(rid, {
      id: rid,
      name: room.name,
      level: 0,
      sensors: [],
      stale: false,
      controls: 0
    })
  }
  // 按房间顺序输出
  return rooms.value.map((r) => byId.get(r.id)).filter((r): r is NonNullable<typeof r> => !!r)
})

const envSensorCount = computed(() =>
  envRooms.value.reduce((a, r) => a + r.sensors.length, 0)
)

/**
 * 可钉到「传感器」钉子板的读数。
 *
 * 用户可以从任意设备挑任意指标钉住，所以候选来源是「现在真的读到什么」，
 * 不是设备品类 —— 空气监测仪、净化器、温湿度计、空调回风口都能钉。
 */
const envPinCandidates = computed(() => buildPinCandidates(homeDevices.value, envReadings))

/**
 * 当前抽屉里那台设备所在房间的环境读数。
 *
 * 空调、净化器这类设备自身的 temperature 读的是回风口/机内温度，
 * 关机时干脆不返回；用户真正想知道的是"房间现在几度"。
 * 这里把环境页算好的房间代表值透传给设备面板做兜底展示。
 */
const currentRoomEnv = computed(() => {
  const d = current.value
  if (!d || !d.room_id) return null
  const m = envRooms.value.find((r) => r.id === d.room_id)
  if (!m) return null
  return {
    name: m.name,
    temp: m.temp,
    humidity: m.humidity,
    count: m.sensors.length,
    stale: m.stale
  }
})

const toolbarTitle = computed(() => {
  if (view.value === 'env') return '环境'
  if (view.value === 'scenes') return '手动场景'
  if (view.value === 'settings') return '设置'
  return currentHome.value?.name || '我的设备'
})

const toolbarSubtitle = computed(() => {
  if (view.value === 'env') {
    const home = currentHome.value?.name || ''
    return `${home} · ${envRooms.value.length} 间 · ${envSensorCount.value} 个传感器`
  }
  if (view.value === 'scenes') return `${homeScenes.value.length} 个场景`
  if (view.value === 'settings') return '改完立即生效，不需要保存'
  return `${filtered.value.length} 台设备 · ${rooms.value.length} 个房间`
})

function openEnv() {
  view.value = 'env'
  if (!Object.keys(envReadings).length) void loadEnv()
}

/** 环境页的快捷开关：灯 / 空调 / 物理通断开关都是开关电源 */
async function toggleEnvControl(c: EnvControl) {
  const device = c.device
  const spec = specs.get(device.did)
  if (!spec) return
  // 合并后的多键面板：控制中心那一行代表第一路，状态键要跟着 siid 走
  const key = channelKeyOf(device, c.siid)
  const next = !powers[key]
  powers[key] = next
  specBusy[device.did] = true
  try {
    await invoke('prop.set', {
      did: physicalDid(device.did),
      siid: c.siid,
      piid: c.piid,
      value: next
    })
  } catch (e) {
    powers[key] = !next
    flash((e as Error).message)
  } finally {
    specBusy[device.did] = false
  }
}

/**
 * 控制中心里改设备的可调参数（亮度 / 色温 / 空调模式 / 目标温度）。
 *
 * 与抽屉里的 setProp 同源，但走的是控制中心自己的设备，
 * 所以不能复用 setProp（它绑在抽屉当前设备上）。
 */
async function adjustEnvControl(c: EnvControl, adj: EnvAdjust, value: unknown) {
  const device = c.device
  const def = adj.def
  const prev = adj.value
  // 乐观更新本地缓存，界面立刻反映
  applyEnvAdjust(c, adj, value)
  specBusy[device.did] = true
  try {
    await invoke('prop.set', {
      did: physicalDid(device.did),
      siid: def.siid,
      piid: def.piid,
      value
    })
  } catch (e) {
    applyEnvAdjust(c, adj, prev)
    flash((e as Error).message)
  } finally {
    specBusy[device.did] = false
  }
}

/** 把调参结果写回本地状态缓存 */
function applyEnvAdjust(c: EnvControl, adj: EnvAdjust, value: unknown) {
  const did = c.device.did
  const base = adj.def.base_name
  if (base === 'brightness') {
    const st = lightState[did] || (lightState[did] = {})
    st.br = Number(value)
  } else if (base === 'color-temperature') {
    const st = lightState[did] || (lightState[did] = {})
    st.ct = Number(value)
  } else if (base === 'target-temperature') {
    const st = acState[did] || (acState[did] = {})
    st.temp = Number(value)
  } else if (base === 'mode') {
    const hit = adj.def.value_list?.find((v) => String(v.value) === String(value))
    const label = hit ? valueLabel(hit) : String(value)
    if (c.kind === 'ac') {
      const st = acState[did] || (acState[did] = {})
      st.mode = label
      st.modeV = value as number | string
    } else {
      const st = lightState[did] || (lightState[did] = {})
      st.mode = label
      st.modeV = value as number | string
    }
  }
}

const scenes = ref<Scene[]>([])
const sceneBusy = ref('')
// 只展示当前家庭的场景
const homeScenes = computed(() => scenes.value.filter((s) => s.home_id === currentHomeId.value))

async function loadScenes() {
  view.value = 'scenes'
  if (scenes.value.length) return
  try {
    scenes.value = await invoke<Scene[]>('scene.list')
  } catch (e) {
    flash((e as Error).message)
  }
}

async function runScene(scene: Scene) {
  sceneBusy.value = scene.scene_id
  try {
    await invoke('scene.run', {
      scene_id: scene.scene_id,
      home_id: scene.home_id,
      owner_uid: scene.owner_uid ?? 0
    })
    flash(`已执行：${scene.name}`)
  } catch (e) {
    flash((e as Error).message)
  } finally {
    sceneBusy.value = ''
  }
}

onMounted(async () => {
  applyTheme((localStorage.getItem('vicold.theme') as 'light' | 'dark') || 'light')
  startAutoFlush()
  startNowTicker()
  // 托盘菜单里改开机自启，这边开关跟着动
  on('autostart.changed', (p) => {
    autostart.value = !!(p as { enabled?: boolean } | null)?.enabled
  })
  void loadSettings()
  try {
    const status = await invoke<{ authed: boolean }>('auth.status')
    authed.value = status.authed
    if (authed.value) {
      await loadDevices()
      if (view.value === 'env') openEnv()
    }
    else startLogin()
  } catch (e) {
    loginStatus.value = 'error'
    loginMessage.value = (e as Error).message
  }
})

/**
 * 环境读数的历史曲线靠定时采样攒出来。
 *
 * 采样频率分两档：刚启动的半小时按 1 分钟采（很快能看出曲线走向），
 * 之后就按 5 分钟采，避免太频繁地打云端接口。
 *
 * ⚠️ 窗口隐藏到托盘时，Chromium 会节流网页里的定时器，
 * 所以除了网页自己的定时器，还订阅了宿主推来的 sample.tick
 * —— 宿主按分钟推一次，两条路径都走同一个 maybeSample()，
 * 保证后台时也能持续记录，历史曲线才攒得满 24 小时。
 */
let envTimer = 0
let lastSampleAt = 0
const envStartAt = Date.now()

function maybeSample() {
  if (!authed.value) return
  const now = Date.now()
  const need = now - envStartAt < 30 * 60 * 1000 ? 60_000 : 5 * 60_000
  if (now - lastSampleAt < need) return
  lastSampleAt = now
  // loadEnv 末尾本来就会 flush 一次；这里再兜一次：后台落盘是「历史不断档」的
  // 关键路径，宁可多调一个空 flush，也不要哪天 loadEnv 内部调整后点只留在内存里。
  void loadEnv()
    .then(() => flushHistory())
    .catch(() => undefined)
}

onMounted(() => {
  envTimer = window.setInterval(maybeSample, 30_000)
  on('sample.tick', maybeSample)
})

// ---- 窗口边缘缩放 ----
/**
 * Chromium 的子窗口铺满整个窗口，顶层窗口收不到 WM_NCHITTEST，
 * 所以边缘热区的判定只能放在前端：命中 → 通知宿主 → 宿主发起系统原生
 * 缩放循环（与标题栏拖动同一套机制）。详见 lib/edgeResize.ts。
 */
const winMaximized = ref(false)
let edgeCursor = ''

function setMaximized(v: boolean) {
  winMaximized.value = v
  // 同步到根元素，方便 CSS 与调试查看
  document.documentElement.dataset.maximized = v ? '1' : '0'
  if (v) onEdgeLeave()
}

function onEdgeMove(e: MouseEvent) {
  if (winMaximized.value) {
    onEdgeLeave()
    return
  }
  // 标题栏右侧的窗口按钮位于窗口右上角，别让缩放光标盖住它们
  const t = e.target as HTMLElement | null
  if (t && typeof t.closest === 'function' && t.closest('.actions')) {
    onEdgeLeave()
    return
  }
  const cur = EDGE_CURSOR[edgeDirAt(e.clientX, e.clientY)]
  if (cur === edgeCursor) return
  edgeCursor = cur
  document.documentElement.style.cursor = cur
}

function onEdgeDown(e: MouseEvent) {
  if (e.button !== 0 || winMaximized.value) return
  // 标题栏右侧的窗口按钮不能被边缘热区抢走（点击要留给按钮自己）
  const t = e.target as HTMLElement | null
  if (t && typeof t.closest === 'function' && t.closest('.actions')) return
  const dir = edgeDirAt(e.clientX, e.clientY)
  if (!dir) return
  e.preventDefault()
  e.stopPropagation()
  void host.resize(dir)
}

function onEdgeLeave() {
  if (!edgeCursor) return
  edgeCursor = ''
  document.documentElement.style.cursor = ''
}

onMounted(async () => {
  try {
    const s = await host.state()
    setMaximized(!!s?.maximized)
  } catch {
    /* 宿主不可用时忽略 */
  }
  on('window.state', (p) => {
    const v = (p as { maximized?: boolean } | undefined)?.maximized
    if (typeof v === 'boolean') setMaximized(v)
  })
  // 捕获阶段监听：必须抢在标题栏的 mousedown 之前，
  // 否则顶边会被当成拖动窗口而不是缩放
  window.addEventListener('mousemove', onEdgeMove)
  window.addEventListener('mousedown', onEdgeDown, true)
  window.addEventListener('mouseleave', onEdgeLeave)
  window.addEventListener('blur', onEdgeLeave)
})

onUnmounted(() => {
  window.removeEventListener('mousemove', onEdgeMove)
  window.removeEventListener('mousedown', onEdgeDown, true)
  window.removeEventListener('mouseleave', onEdgeLeave)
  window.removeEventListener('blur', onEdgeLeave)
})

onUnmounted(() => {
  window.clearInterval(envTimer)
  stopAutoFlush()
  stopNowTicker()
  void flushHistory()
})
</script>

<template>
  <div class="app">
    <TitleBar
      :theme="theme"
      :homes="homeList"
      :home-id="currentHomeId"
      :settings-open="view === 'settings'"
      @toggle-theme="toggleTheme"
      @select-home="selectHome"
      @open-settings="openSettings"
    />

    <LoginCard
      v-if="!authed"
      :status="loginStatus"
      :qr="qr"
      :message="loginMessage"
      @retry="startLogin"
    />

    <main v-else class="shell">
      <aside class="sidebar">
        <button class="nav" :class="{ active: view === 'env' }" @click="openEnv()">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none">
            <path d="M12 3.5a2.6 2.6 0 0 0-2.6 2.6v6.6a4.2 4.2 0 1 0 5.2 0V6.1A2.6 2.6 0 0 0 12 3.5Z" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round" />
            <path d="M12 9.5v5.2" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
          </svg>
          环境
        </button>
        <button class="nav" :class="{ active: view === 'devices' }" @click="view = 'devices'">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none">
            <rect x="3.5" y="3.5" width="7" height="7" rx="2" stroke="currentColor" stroke-width="1.7" />
            <rect x="13.5" y="3.5" width="7" height="7" rx="2" stroke="currentColor" stroke-width="1.7" />
            <rect x="3.5" y="13.5" width="7" height="7" rx="2" stroke="currentColor" stroke-width="1.7" />
            <rect x="13.5" y="13.5" width="7" height="7" rx="2" stroke="currentColor" stroke-width="1.7" />
          </svg>
          设备
        </button>
        <button class="nav" :class="{ active: view === 'scenes' }" @click="loadScenes()">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none">
            <path d="M13 3 5 13.5h5.5L11 21l8-10.5h-5.5L13 3Z" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round" />
          </svg>
          场景
        </button>

        <div class="side-scroll">
          <template v-if="view === 'scenes'">
            <p class="side-hint">共 {{ homeScenes.length }} 个手动场景</p>
          </template>
          <p v-else-if="view === 'settings'" class="side-hint">设置是应用级的，对所有家庭生效</p>
          <template v-else-if="view === 'devices'">
            <button class="room" :class="{ active: activeRoom === 'all' }" @click="activeRoom = 'all'">
              <span class="grip ghost" aria-hidden="true" />
              <span class="room-name">全屋</span><em>{{ homeDevices.length }}</em>
            </button>
            <button
              v-for="room in rooms"
              :key="room.id"
              class="room"
              :class="{
                active: activeRoom === room.id,
                dragging: roomDragId === room.id,
                'drop-target': roomDragOver === room.id
              }"
              @click="activeRoom = room.id"
              @dragover="roomDragOverAt($event, room.id)"
              @drop="roomDrop($event, room.id)"
            >
              <span
                class="grip"
                draggable="true"
                title="拖动调整房间顺序"
                @dragstart="roomDragStart($event, room.id)"
                @dragend="roomDragEnd()"
                @click.stop
              >
                <svg width="10" height="14" viewBox="0 0 10 14" fill="currentColor" aria-hidden="true">
                  <circle cx="2.5" cy="3" r="1.1" /><circle cx="7.5" cy="3" r="1.1" />
                  <circle cx="2.5" cy="7" r="1.1" /><circle cx="7.5" cy="7" r="1.1" />
                  <circle cx="2.5" cy="11" r="1.1" /><circle cx="7.5" cy="11" r="1.1" />
                </svg>
              </span>
              <span class="room-name">{{ room.name }}</span>
              <em v-if="room.count">{{ room.count }}</em>
            </button>
            <button v-if="roomSorted" class="room-reset" @click="resetRoomSort">恢复默认顺序</button>
          </template>
        </div>

        <!-- 后台常驻等应用级开关移到设置页了，侧栏只留导航、房间和退出 -->
        <button class="logout" @click="logout">退出登录</button>
      </aside>

      <section class="content">
        <header class="toolbar">
          <div>
            <h1>{{ toolbarTitle }}</h1>
            <p class="count">{{ toolbarSubtitle }}</p>
          </div>
          <div class="tools">
            <div v-if="view === 'devices'" class="search">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none">
                <circle cx="11" cy="11" r="6.5" stroke="currentColor" stroke-width="1.7" />
                <path d="M16 16l4 4" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
              </svg>
              <input v-model="keyword" placeholder="搜索设备名称或型号" />
            </div>
            <button v-if="view === 'env'" class="btn btn-ghost" :disabled="envLoading" @click="loadEnv()">
              {{ envLoading ? '读取中…' : '刷新' }}
            </button>
            <!-- 设置页没有「刷新」这个概念，别放一颗点不动的按钮 -->
            <button v-else-if="view !== 'settings'" class="btn btn-ghost" :disabled="loading" @click="loadDevices()">
              {{ loading ? '刷新中…' : '刷新' }}
            </button>
          </div>
        </header>

        <div v-if="view === 'devices'" class="dev-scroll">
          <section v-for="g in deviceGroups" :key="g.id" class="dev-group">
            <header class="dg-head">
              <span class="dg-name">{{ g.name }}</span>
              <span class="dg-count">{{ g.devices.length }} 台</span>
            </header>
            <div class="grid">
              <DeviceCard
                v-for="d in g.devices"
                :key="d.did"
                :device="d"
                :power="powers[d.did]"
                :summary="cardSummary(d)"
                :keys="cardKeys(d)"
                :trend="cardTrend(d)"
                :busy="!!specBusy[d.did]"
                @open="openDevice(d)"
                @toggle="togglePower(d)"
                @toggle-channel="(k) => toggleChannel(d, k)"
              />
            </div>
          </section>
          <div v-if="!deviceGroups.length && !loading" class="blank">这里还没有设备</div>
        </div>

        <div v-else-if="view === 'env'" class="env-wrap">
          <EnvironmentView
            :rooms="envRooms"
            :controls="envControls"
            :sensors="envPinCandidates"
            :home-id="currentHomeId"
            :loading="envLoading"
            :busy="specBusy"
            :room-filter="envRoomFilter"
            :sorted="roomSorted"
            @update:room-filter="envRoomFilter = $event"
            @reorder-room="reorderRooms"
            @reset-order="resetRoomSort"
            @toggle="toggleEnvControl"
            @adjust="adjustEnvControl"
            @refresh="loadEnv()"
          />
        </div>

        <div v-else-if="view === 'settings'" class="settings-wrap">
          <SettingsPage
            :theme="theme"
            :autostart="autostart"
            :autostart-busy="autostartBusy"
            :close-to-tray="closeToTray"
            :close-to-tray-busy="closeToTrayBusy"
            @set-theme="applyTheme"
            @toggle-autostart="toggleAutostart"
            @toggle-close-to-tray="toggleCloseToTray"
            @notify="flash"
          />
        </div>

        <div v-else class="scene-grid">
          <div v-for="s in homeScenes" :key="s.scene_id" class="card scene-card">
            <div>
              <div class="scene-name">{{ s.name }}</div>
            </div>
            <button class="btn btn-primary" :disabled="sceneBusy === s.scene_id" @click="runScene(s)">
              执行
            </button>
          </div>
          <div v-if="!homeScenes.length" class="blank">这个家还没有手动场景</div>
        </div>
      </section>
    </main>

    <DeviceDrawer
      v-if="current"
      :device="current"
      :spec="currentSpec"
      :values="currentValues"
      :loading="detailLoading"
      :busy-prop="busyProp"
      :room-env="currentRoomEnv"
      :channel-names="currentChannelNames"
      @close="closeDrawer"
      @set-prop="setProp"
      @run-action="runAction"
      @refresh="refreshValues"
    />

    <transition name="toast">
      <div v-if="toast" class="toast">{{ toast }}</div>
    </transition>
  </div>
</template>

<style scoped>
.app {
  height: 100%;
  display: flex;
  flex-direction: column;
  position: relative;
  background: var(--bg);
}

.shell {
  flex: 1;
  display: flex;
  min-height: 0;
}

.sidebar {
  width: 216px;
  flex: none;
  background: var(--surface);
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  padding: 12px 10px;
  gap: 2px;
  position: relative;
  box-shadow: 1px 0 0 rgba(255, 255, 255, 0.6) inset;
}

/* 侧边栏顶部品牌渐变辉光 */
.sidebar::before {
  content: '';
  position: absolute;
  inset: 0 0 auto 0;
  height: 140px;
  background: radial-gradient(220px 120px at 20% 0%, var(--brand-soft), transparent 70%);
  pointer-events: none;
}

.nav {
  position: relative;
  display: flex;
  align-items: center;
  gap: 9px;
  height: 40px;
  padding: 0 12px;
  border-radius: 11px;
  color: var(--text-2);
  font-size: 13.5px;
  transition: background-color 0.18s ease, color 0.18s ease, transform 0.18s var(--ease);
}

.nav:hover {
  background: var(--surface-3);
  color: var(--text);
  transform: translateX(2px);
}

/* 选中态：底色渐变 + 左侧实色指示条（去掉整圈光晕，更干净） */
.nav.active {
  background: linear-gradient(90deg, var(--brand-soft), transparent 85%);
  color: var(--brand);
  font-weight: 600;
}

.nav.active::before {
  content: '';
  position: absolute;
  left: -10px;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 20px;
  border-radius: 0 3px 3px 0;
  background: var(--brand);
}

.nav.active svg {
  color: var(--brand);
}

.side-scroll {
  flex: 1;
  overflow-y: auto;
  margin-top: 10px;
}

.room {
  position: relative;
  width: 100%;
  display: flex;
  align-items: center;
  gap: 6px;
  height: 36px;
  padding: 0 10px 0 4px;
  border-radius: 10px;
  color: var(--text-2);
  font-size: 13px;
  transition: background-color 0.16s ease, color 0.16s ease, transform 0.16s var(--ease);
}

.room:hover {
  background: var(--surface-3);
  color: var(--text);
  transform: translateX(2px);
}

.room.active {
  background: var(--brand-soft);
  color: var(--brand);
  font-weight: 600;
}

.room.active em {
  color: var(--brand);
  background: var(--surface);
  box-shadow: 0 1px 4px rgba(47, 109, 246, 0.18);
}

.room em {
  font-style: normal;
  font-size: 11px;
  color: var(--text-3);
  background: var(--surface-3);
  border-radius: 999px;
  padding: 1px 7px;
  min-width: 20px;
  text-align: center;
}

/* 房间名占满中间，名称长了省略而不是把计数挤出去 */
.room .room-name {
  flex: 1;
  min-width: 0;
  text-align: left;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* 拖动把手：只有它能拖，行本身的点击仍然是「筛选房间」 */
.room .grip {
  flex: none;
  width: 14px;
  height: 22px;
  display: grid;
  place-items: center;
  border-radius: 5px;
  color: var(--text-3);
  opacity: 0.3;
  cursor: grab;
  user-select: none;
  transition: opacity 0.15s ease, color 0.15s ease, background-color 0.15s ease;
}

.room:hover .grip {
  opacity: 0.75;
}

.room .grip:hover {
  opacity: 1;
  color: var(--brand);
  background: var(--brand-soft);
}

.room .grip:active {
  cursor: grabbing;
}

/* 「全屋」是固定入口，给个占位让文字和下面 rooms 对齐 */
.room .grip.ghost {
  opacity: 0;
  pointer-events: none;
}

.room.dragging {
  opacity: 0.35;
}

/* 拖到谁身上，谁就是插入位置 */
.room.drop-target {
  background: var(--brand-soft);
  color: var(--brand);
  box-shadow: inset 0 0 0 1px var(--brand);
}

.room-reset {
  margin-top: 4px;
  padding: 5px 10px;
  font-size: 11.5px;
  color: var(--text-3);
  text-align: left;
  border-radius: 8px;
  transition: color 0.15s ease, background-color 0.15s ease;
}

.room-reset:hover {
  color: var(--brand);
  background: var(--surface-3);
}

.side-hint {
  padding: 6px 12px;
  font-size: 12px;
  color: var(--text-3);
}

/* 后台常驻等应用级开关在设置页里，这里只留退出登录 */
.logout {
  margin-top: 8px;
  height: 36px;
  border-radius: 10px;
  color: var(--text-3);
  font-size: 12.5px;
  transition: background-color 0.15s ease, color 0.15s ease;
}

.logout:hover {
  background: rgba(229, 72, 77, 0.1);
  color: var(--danger);
}

.content {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  padding: 20px 24px 0;
  position: relative;
}

.content::before {
  content: '';
  position: absolute;
  inset: 0 0 auto 0;
  height: 220px;
  background: radial-gradient(680px 220px at 22% 0%, var(--brand-softer), transparent 72%);
  pointer-events: none;
}

.toolbar {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 18px;
}

h1 {
  margin: 0;
  font-size: 21px;
  font-weight: 700;
  letter-spacing: 0.2px;
  background: var(--grad-brand);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}

.count {
  margin: 4px 0 0;
  font-size: 12px;
  color: var(--text-2);
}

.tools {
  display: flex;
  align-items: center;
  gap: 10px;
}

.search {
  display: flex;
  align-items: center;
  gap: 7px;
  height: 38px;
  padding: 0 14px;
  width: 258px;
  border-radius: 12px;
  background: var(--surface);
  border: 1px solid var(--border);
  color: var(--text-3);
  transition: border-color 0.18s ease, box-shadow 0.18s ease, transform 0.18s var(--ease);
}

.search:focus-within {
  border-color: var(--brand);
  box-shadow: var(--ring);
  transform: translateY(-1px);
}

.search svg {
  flex: none;
  transition: color 0.18s ease;
}

.search:focus-within svg {
  color: var(--brand);
}

.search input {
  flex: 1;
  border: none;
  outline: none;
  background: transparent;
  color: var(--text);
  font-size: 13px;
}

.search input::placeholder {
  color: var(--text-3);
}

/* 设备页：外层滚动，里面按房间分段 */
.dev-scroll {
  flex: 1;
  overflow-y: auto;
  padding-bottom: 24px;
}

.dev-group + .dev-group {
  margin-top: 20px;
}

/* 房间小标题：名字 + 台数 + 一条拉到底的分隔线 */
.dg-head {
  display: flex;
  align-items: center;
  gap: 9px;
  margin: 0 2px 10px;
}

.dg-head::after {
  content: '';
  flex: 1;
  height: 1px;
  background: var(--border);
}

.dg-name {
  font-size: 12px;
  font-weight: 650;
  color: var(--text-2);
  letter-spacing: 0.6px;
}

.dg-count {
  font-size: 10.5px;
  color: var(--text-3);
  font-variant-numeric: tabular-nums;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(232px, 1fr));
  gap: 16px;
  align-content: start;
  min-width: 0;
}

.grid > * {
  animation: fade-up 0.28s var(--ease) both;
}

.scene-grid {
  flex: 1;
  overflow-y: auto;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 14px;
  align-content: start;
  padding-bottom: 24px;
}

.scene-grid > * {
  animation: fade-up 0.28s var(--ease) both;
}

.scene-card {
  padding: 18px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  overflow: hidden;
}

.scene-card:hover {
  transform: translateY(-2px);
  border-color: var(--brand);
  box-shadow: var(--shadow-md);
}

.scene-name {
  font-weight: 600;
  font-size: 14.5px;
}

/* ---------- 设置页 ---------- */
/* 只占内容区，侧栏保留（随时能切回环境/设备/场景）；
   滚动和留白交给 SettingsPage 自己，外层只负责撑满高度 */
.settings-wrap {
  flex: 1;
  min-height: 0;
  display: flex;
  overflow: hidden;
}

/* ---------- 环境页 ---------- */
.env-wrap {
  flex: 1;
  min-height: 0;
  /*
   * 外层不再滚动：环境页是「左仪表板 + 右控制中心」两栏，
   * 两栏各自滚自己的。外层一旦可滚，在控制中心上滚滚轮会把整页带走
   * （滚轮事件的滚动链冒泡到最近的可滚祖先），右栏内部的 overflow 永远不生效。
   * 底部留白下放到 .dash / .cc-scroll 各自的滚动区里。
   */
  overflow: hidden;
  /* 卡片不能贴着窗口底边；overflow:hidden 下 padding 仍然有效，
     而 .env 的 height:100% 只吃 content box，所以两栏会正好矮 24px */
  padding-bottom: 24px;
}

/* 窄屏退化单栏时恢复整页滚动（与 EnvironmentView 的 1180px 断点一致） */
@media (max-width: 1180px) {
  .env-wrap {
    overflow-y: auto;
    padding-bottom: 24px;
  }
}

/* 窄窗口：收紧留白、网格自然回流到更少列 */
@media (max-width: 760px) {
  .content {
    padding: 14px 14px 0;
  }
  .toolbar {
    gap: 10px;
    margin-bottom: 14px;
  }
  .search {
    width: 200px;
  }
  .grid {
    grid-template-columns: repeat(auto-fill, minmax(168px, 1fr));
    gap: 12px;
  }
  .scene-grid {
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 12px;
  }
}

.blank {
  grid-column: 1 / -1;
  padding: 60px 0;
  text-align: center;
  color: var(--text-3);
  font-size: 13px;
}

.toast {
  position: absolute;
  left: 50%;
  bottom: 28px;
  transform: translateX(-50%);
  padding: 10px 18px;
  border-radius: 12px;
  background: var(--text);
  color: var(--surface);
  font-size: 12.5px;
  box-shadow: var(--shadow-md);
  z-index: 40;
  animation: fade-up 0.22s var(--ease) both;
}

.toast-enter-active,
.toast-leave-active {
  transition: opacity 0.2s ease, transform 0.2s ease;
}

.toast-enter-from,
.toast-leave-to {
  opacity: 0;
  transform: translate(-50%, 6px);
}
</style>
