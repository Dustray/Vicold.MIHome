// SPDX-License-Identifier: GPL-3.0-or-later
// 无边框主窗口 + WebView2 宿主 + JS 桥 + 串行后台任务线程。
#pragma once

#include <windows.h>
// WIN32_LEAN_AND_MEAN 会挡掉 objbase.h，而 WebView2.h 依赖其中的 interface 宏
#include <objbase.h>
#include <shellapi.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <WebView2.h>
#include <wrl.h>

#include "json.hpp"
#include "history.h"
#include "proto/images.h"
#include "proto/mijia.h"

namespace vc {

/**
 * 启动期崩溃诊断。
 *
 * 本机没有完整 Windows SDK（也就没有调试器），双击启动崩溃时只能靠
 * Windows 事件日志看到「VCRUNTIME140.dll + 某个偏移」，定位不到代码。
 * 这里挂一个顶层异常过滤器，把**异常码 / 故障模块 / 模块内偏移**直接写进
 * app.log —— 下次崩溃就有现场了。
 *
 * ⚠️ 必须在 `wWinMain` 的第一行调用：崩溃可能发生在任何初始化之前。
 */
void install_crash_logger();

/** 启动阶段打点（写 app.log），配合崩溃记录判断走到了哪一步 */
void log_boot(const char* tag);

class AppWindow {
public:
    bool create(HINSTANCE instance, int show_cmd);
    int run();

private:
    using json = nlohmann::json;

    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void init_webview();
    void resize_webview();
    void probe_page();
    void on_bridge_message(const std::wstring& payload);

    // 边缘 resize 命中测试：返回 HTLEFT/HTRIGHT/... 或 HTCLIENT
    int edge_hit_test(LPARAM lp);
    // 启动原生缩放循环（方向 n/s/e/w/ne/nw/se/sw）
    void begin_native_resize(const std::string& dir);
    // 后台线程执行 → 结果回到 UI 线程
    void start_worker();
    void stop_worker();
    void post_job(std::function<json()> job, uint64_t id);
    json run_command(const std::string& cmd, const json& args);
    void reply(uint64_t id, bool ok, const json& data, const std::string& error);
    void eval_js(const std::wstring& script);
    // 把窗口状态（当前是否最大化）推给前端，用于最大化按钮图标同步
    void push_window_state();

    // ---- 系统托盘（关闭窗口后常驻后台，环境采样才能连续攒满 24 小时）----
    void create_tray();
    void destroy_tray();
    void show_main_window();
    void show_tray_menu();
    void hide_to_tray(bool quiet = false);
    void apply_tray_balloon();
    bool load_close_to_tray();
    void save_close_to_tray();
    // 宿主定时推 sample.tick：窗口隐藏时 Chromium 会节流网页定时器，
    // 由宿主驱动采样，保证后台也能持续记录
    void push_sample_tick();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken msg_token_{};

    NOTIFYICONDATAW nid_{};
    bool tray_ready_ = false;
    // 自绘的应用图标，窗口类与托盘共用（进程退出时由系统回收）。
    // icon_ 是大图标（32，窗口 / Alt-Tab），icon_small_ 是小图标（16，标题栏 / 托盘）
    HICON icon_ = nullptr;
    HICON icon_small_ = nullptr;
    // 关闭窗口时是否最小化到托盘（默认开），托盘右键菜单可切换
    bool close_to_tray_ = true;
    // 首次隐藏到托盘时弹一次气泡提示，告诉用户程序还在后台跑
    bool tray_hint_shown_ = false;

    std::unique_ptr<mijia::Client> client_;
    // 本地历史库（环境读数时间序列），给前端画折线图
    std::unique_ptr<HistoryStore> history_;
    // 设备产品图缓存（home.miot-spec.com 的 og:image），落盘后由虚拟主机给网页加载。
    // 用 shared_ptr 是因为抓图跑在独立线程上：窗口关掉时线程还持有它，不能跟着析构
    std::shared_ptr<images::Store> images_;

    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;

    std::wstring web_root_;
    bool maximized_ = false;
    // 上一次推送给前端的最大化状态，用于去重避免重复推送
    bool pushed_maximized_ = false;
};

}  // namespace vc
