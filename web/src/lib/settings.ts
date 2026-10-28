// 应用级设置（存在宿主的注册表 / settings.json 里，不放 localStorage）：
// 开机自启本质是写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run，
// 只有宿主能读写；前端这份只是镜像，靠 `autostart.changed` 事件与托盘菜单同步。

import { invoke } from './host'

export interface AppSettings {
  autostart: boolean
  closeToTray: boolean
}

/** 读当前设置。宿主没实现（老版本）时按全部关闭处理，不抛错。 */
export async function getSettings(): Promise<AppSettings> {
  try {
    return await invoke<AppSettings>('app.settings')
  } catch {
    return { autostart: false, closeToTray: true }
  }
}

/**
 * 开/关开机自启。
 * 返回宿主回读的真实状态 —— 写注册表可能失败（组策略、权限），
 * 以回读为准，否则界面会显示成「开了」但实际没写进去。
 */
export async function setAutostart(enable: boolean): Promise<boolean> {
  const r = await invoke<{ ok: boolean; autostart: boolean }>('app.autostart', { enable })
  return !!r.autostart
}

/**
 * 改「关闭窗口时最小化到托盘」。
 *
 * 托盘右键菜单也能改这一项，所以同样回读宿主的真实值再写回界面 ——
 * 两边改的是 settings.json 里同一个字段。
 */
export async function setCloseToTray(enable: boolean): Promise<boolean> {
  const r = await invoke<{ ok: boolean; closeToTray: boolean }>('app.closeToTray', { enable })
  return !!r.closeToTray
}
