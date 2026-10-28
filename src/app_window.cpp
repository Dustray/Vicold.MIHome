// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_window.h"

#include "proto/http.h"

#include <WebView2EnvironmentOptions.h>

#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace vc {
namespace {

constexpr wchar_t kClassName[] = L"VicoldMIHomeWindow";
constexpr wchar_t kTitle[] = L"桌面米家";
constexpr wchar_t kVirtualHost[] = L"app.vicold.mihome";
// 设备产品图走第二个虚拟主机：图片缓存在 <data>/img，网页直接 <img src>，
// 不用把几 MB 的 base64 塞进桥接消息
constexpr wchar_t kImageHost[] = L"img.vicold.mihome";
constexpr UINT kMsgReply = WM_APP + 1;
constexpr UINT kMsgTray = WM_APP + 2;
// 后台线程 → 前端事件（产品图每抓到一张就推一条）
constexpr UINT kMsgEvent = WM_APP + 3;

// 托盘图标回调消息
constexpr UINT kTrayIdShow = 1001;
constexpr UINT kTrayIdToggle = 1002;
constexpr UINT kTrayIdExit = 1003;
constexpr UINT kTrayIdAutostart = 1004;

// 开机自启：写 HKCU\...\Run。用注册表而不是启动文件夹快捷方式，
// 是因为「任务管理器 → 启动」里能直接看到并禁用，用户有掌控感。
// 值名固定，别改：老版本写进去的值会变成孤儿项。
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Vicold.MIHome";

std::wstring exe_path() {
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::wstring(buf, n);
}

/** 自启命令：带 --tray，开机时静默进托盘，不弹窗打断用户 */
std::wstring autostart_command() { return L"\"" + exe_path() + L"\" --tray"; }

bool autostart_enabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LONG r = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool set_autostart(bool on) {
    if (!on) {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
            return false;  // 整条 Run 键都没有 → 本来就没开
        }
        const LONG r = RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
        return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring cmd = autostart_command();
    const LONG r = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(cmd.c_str()),
                                  static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

/** 命令行里有没有 --tray（开机自启走这条路，启动后直接待在托盘） */
bool wants_tray_start() {
    const std::wstring cmd = GetCommandLineW();
    return cmd.find(L"--tray") != std::wstring::npos;
}

// 宿主侧的采样心跳定时器：窗口隐藏到托盘时 Chromium 会节流网页定时器，
// 由宿主按分钟推 sample.tick，保证后台也能持续记录环境读数
constexpr UINT kTimerSample = 2;
constexpr UINT kSampleIntervalMs = 60 * 1000;

constexpr int kMinWidth = 1040;
constexpr int kMinHeight = 680;

/**
 * 运行时自绘应用图标（米家风：米家绿圆角方块 + 白色「M 房」标记）。
 *
 * 项目没有 .rc 资源文件，托盘图标如果用 IDI_APPLICATION 会是一块灰色方块，
 * 和界面配色完全不搭。这里在内存里拼一个标准 .ico（32bpp BGRA + alpha），
 * 再用 Windows 官方的"从内存加载图标"API 生成 HICON。
 *
 * 图形不是位图粘贴，而是按归一化坐标（0~1）算出来的：
 *  - 底色是米家绿 RGB(73,177,128)，取自米家官网 favicon；
 *  - 白色标记是一个圆角方环（底角圆到底，就是那个"圆底"），顶边正中往下
 *    压出一个 V —— 那道 V 就是米家"M"的尖。参数是照 32px 官方 favicon
 *    逐像素比着调的，16/24 这类小图标单独把线加粗、V 压浅一点。
 *
 * ⚠️ 两条踩坑记录（都因为本机 NuGet 版 Windows SDK 被裁剪过）：
 *  1) 不能用 CreateIconIndirect —— SDK 的 winuser.h 里没有 ICONDIR /
 *     ICONDIRENTRY 的定义，编译不过；
 *  2) 也不能用 CreateIcon —— SDK 只剩 winuser.h 里 7 参数的那个变体
 *     （BYTE cPlanes, BYTE cBitsPixel，AND 在前），实测画出来全透明。
 *     最终走 .ico 字节 + 标准加载 API，格式完全可控。
 */
namespace {

// 米家绿：官网 favicon 的主力填充色 RGB(73,177,128)
constexpr int kGreenR = 0x49;
constexpr int kGreenG = 0xB1;
constexpr int kGreenB = 0x80;

#ifndef LR_VERSION
#define LR_VERSION 0x00030000
#endif

/**
 * 圆角矩形有符号距离（<0 在内部）。
 * 用距离场算描边而不是"外框减内框"，转角处的线宽才和其它地方一样粗。
 */
double sdf_round_rect(double u, double v, double hw, double hh, double r) {
    const double dx = std::abs(u - 0.5) - hw + r;
    const double dy = std::abs(v - 0.5) - hh + r;
    const double ox = std::max(dx, 0.0);
    const double oy = std::max(dy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0) - r;
}

/**
 * 米家标记（归一化坐标 0~1）：圆角方环 + 顶边正中的 V。
 *
 * V 的实现是把采样点往上抬 dip —— 等价于把环顶那条边整段往下弯，
 * 抬得越多压得越深，中间最深、两侧线性收回到 0，正好是个 V 形。
 * 只对 v < 0.45 生效，否则圆底会跟着往下坠。
 */
bool in_mark(double u, double v, double stroke, double dip_a, double dip_w) {
    if (v < 0.45) {
        v -= dip_a * std::max(0.0, 1.0 - std::abs(u - 0.5) / dip_w);
    }
    return std::abs(sdf_round_rect(u, v, 0.200, 0.215, 0.112)) <= stroke;
}

/** 画一帧像素（自上而下存储的 BGRA），ss×ss 超采样做抗锯齿 */
std::vector<UINT32> render_app_pixels(int size, int ss) {
    // 小图标单独调参：托盘 16px 上 V 形会被采样糊掉，不如压浅、线加粗
    const bool tiny = size <= 24;
    const double stroke = tiny ? 0.055 : 0.048;
    const double dip_a = tiny ? 0.030 : 0.055;
    const double dip_w = tiny ? 0.075 : 0.085;
    // 方块自己的圆角：官方 favicon 是近乎方的超椭圆，0.17 已经看不出差别
    const double tile_r = 0.17;

    std::vector<UINT32> px(static_cast<size_t>(size) * size, 0);
    const int n = ss * ss;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int cov_tile = 0;
            int cov_white = 0;
            for (int sy = 0; sy < ss; sy++) {
                for (int sx = 0; sx < ss; sx++) {
                    const double u = (x + (sx + 0.5) / ss) / size;
                    const double v = (y + (sy + 0.5) / ss) / size;
                    if (sdf_round_rect(u, v, 0.5, 0.5, tile_r) > 0.0) continue;  // 圆角外
                    cov_tile++;
                    if (in_mark(u, v, stroke, dip_a, dip_w)) cov_white++;
                }
            }
            if (cov_tile == 0) continue;  // 完全透明

            // 白色覆盖率以"落在方块里的采样点"为分母：边缘像素才不会被圆角稀释
            const double w = static_cast<double>(cov_white) / cov_tile;
            const auto mix = [w](int base) {
                return static_cast<UINT32>(std::lround(base * (1.0 - w) + 255.0 * w));
            };
            const UINT32 a = static_cast<UINT32>(std::lround(255.0 * cov_tile / n));
            px[static_cast<size_t>(y) * size + x] =
                (a << 24) | (mix(kGreenR) << 16) | (mix(kGreenG) << 8) | mix(kGreenB);
        }
    }
    return px;
}

/** 打包成 .ico 里的单张图像：BITMAPINFOHEADER + XOR 位图 + AND 掩码 */
std::vector<BYTE> pack_icon_image(const std::vector<UINT32>& px, int size) {
    const size_t xorSize = static_cast<size_t>(size) * size * 4;
    const size_t andStride = (static_cast<size_t>(size) + 31) / 32 * 4;
    const size_t andSize = andStride * size;

    std::vector<BYTE> out(40 + xorSize + andSize, 0);
    auto* bh = reinterpret_cast<BITMAPINFOHEADER*>(out.data());
    bh->biSize = sizeof(BITMAPINFOHEADER);
    bh->biWidth = size;
    bh->biHeight = size * 2;  // .ico 里 XOR + AND 叠在一起
    bh->biPlanes = 1;
    bh->biBitCount = 32;
    bh->biCompression = BI_RGB;
    bh->biSizeImage = static_cast<DWORD>(xorSize + andSize);

    BYTE* xorPtr = out.data() + 40;
    for (int y = 0; y < size; y++) {
        const int row = size - 1 - y;  // .ico 里的位图是自下而上
        // ⚠️ 源指针是 UINT32*，指针算术会自己乘 4，这里**只能**用元素下标 y*size。
        //    写成 y*size*4 会让源地址偏 4 倍，y≥size/4 时越界读堆外内存 ——
        //    表现为启动时随机崩在 VCRUNTIME140（memcpy 内部）。
        std::memcpy(xorPtr + static_cast<size_t>(row) * size * 4,
                    px.data() + static_cast<size_t>(y) * size,
                    static_cast<size_t>(size) * 4);
    }
    // AND 掩码留 0 —— 32 位图标由 alpha 决定透明
    return out;
}

/**
 * 组装标准 .ico 内存镜像（多尺寸、全是 32bpp）。
 *
 * 一次给齐 16/24/32/48/64/128 六张，系统按用途各取所需（托盘要 16、
 * 任务栏和 Alt-Tab 要 32~48、高 DPI 往上拿），比只给一张 32px 再让
 * 系统缩放到处都清楚。
 */
std::vector<BYTE> build_ico(const int* sizes, int count) {
    struct Image {
        std::vector<BYTE> data;
        int size;
    };
    std::vector<Image> images;
    images.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        const int s = sizes[i];
        images.push_back({pack_icon_image(render_app_pixels(s, s <= 64 ? 4 : 2), s), s});
    }

    const size_t headerSize = 6 + 16 * images.size();  // ICONDIR + N 条 ICONDIRENTRY
    size_t total = headerSize;
    for (const auto& im : images) total += im.data.size();

    std::vector<BYTE> ico(total, 0);
    auto* p = ico.data();
    auto put16 = [](BYTE* q, WORD v) {
        q[0] = static_cast<BYTE>(v & 0xFF);
        q[1] = static_cast<BYTE>((v >> 8) & 0xFF);
    };
    auto put32 = [](BYTE* q, DWORD v) {
        for (int i = 0; i < 4; i++) q[i] = static_cast<BYTE>((v >> (8 * i)) & 0xFF);
    };

    put16(p + 0, 0);  // idReserved
    put16(p + 2, 1);  // idType = 1 (icon)
    put16(p + 4, static_cast<WORD>(images.size()));  // idCount

    size_t offset = headerSize;
    for (size_t i = 0; i < images.size(); i++) {
        BYTE* e = p + 6 + i * 16;
        // ⚠️ 目录里宽高是单字节，256 只能存成 0。这里最大 128，不踩这个坑
        e[0] = static_cast<BYTE>(images[i].size);
        e[1] = static_cast<BYTE>(images[i].size);
        e[2] = 0;  // bColorCount = 0（真彩）
        e[3] = 0;  // bReserved
        put16(e + 4, 1);   // wPlanes
        put16(e + 6, 32);  // wBitCount
        put32(e + 8, static_cast<DWORD>(images[i].data.size()));  // dwBytesInRes
        put32(e + 12, static_cast<DWORD>(offset));                // 图像数据偏移
        std::memcpy(p + offset, images[i].data.data(), images[i].data.size());
        offset += images[i].data.size();
    }
    return ico;
}

}  // namespace

/**
 * 生成指定边长的 HICON（16 = 托盘小图标，32 = 窗口 / Alt-Tab）。
 *
 * 从多尺寸 .ico 里挑最接近的那张再交给 CreateIconFromResourceEx，
 * 而不是让它自己缩放 —— 我们自己画的分辨率是对齐过的，缩放反而糊。
 */
HICON make_app_icon(int want) {
    static const int kSizes[] = {16, 24, 32, 48, 64, 128};
    const auto ico = build_ico(kSizes, static_cast<int>(std::size(kSizes)));
    if (ico.empty()) return nullptr;

    const int count = static_cast<int>(ico[4] | (ico[5] << 8));
    int best = -1;
    int best_diff = INT_MAX;
    for (int i = 0; i < count; i++) {
        const int s = ico[6 + i * 16] ? ico[6 + i * 16] : 256;  // 目录里 0 表示 256
        const int diff = std::abs(s - want);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    if (best < 0) return nullptr;

    const BYTE* e = ico.data() + 6 + best * 16;
    DWORD len = 0;
    DWORD off = 0;
    std::memcpy(&len, e + 8, sizeof(len));
    std::memcpy(&off, e + 12, sizeof(off));
    if (static_cast<size_t>(off) + len > ico.size()) return nullptr;

    // ⚠️ CreateIconFromResourceEx 要的是「图像数据」指针，不是整个 .ico
    auto* raw = const_cast<BYTE*>(ico.data() + off);
    return CreateIconFromResourceEx(raw, len, TRUE, LR_VERSION, want, want, 0);
}

struct Reply {
    uint64_t id = 0;
    bool ok = false;
    std::string payload;  // 已序列化的 JS 对象字面量
};

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path data_dir() {
    PWSTR raw = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        out = std::filesystem::path(raw) / L"Vicold.MIHome";
        CoTaskMemFree(raw);
    } else {
        out = exe_dir() / L"data";
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    return out;
}

int dpi_of(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return fn ? static_cast<int>(fn(hwnd)) : 96;
}

/**
 * 校验上次存下来的窗口矩形还能不能用，并把它夹进那块屏幕的工作区。
 *
 * 拔掉副屏、改分辨率、换远程桌面尺寸之后，存下来的坐标很可能落在屏幕外面 ——
 * 那样窗口一启动就在看不见的地方，用户只会以为程序没起来。这里要求至少有一块
 * 120×80 的面积压在某块屏幕的工作区里，才算「看得见」。
 *
 * 顺手把尺寸和位置夹进那块工作区：换到更小的屏幕后，窗口不会比屏幕还大。
 */
bool fit_saved_rect(RECT& r) {
    HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
    if (!mon) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;

    RECT overlap{};
    if (!IntersectRect(&overlap, &r, &mi.rcWork)) return false;
    if (overlap.right - overlap.left < 120 || overlap.bottom - overlap.top < 80) return false;

    const int work_w = mi.rcWork.right - mi.rcWork.left;
    const int work_h = mi.rcWork.bottom - mi.rcWork.top;
    const int w = std::min(static_cast<int>(r.right - r.left), work_w);
    const int h = std::min(static_cast<int>(r.bottom - r.top), work_h);
    const int x = std::clamp(static_cast<int>(r.left), static_cast<int>(mi.rcWork.left),
                             static_cast<int>(mi.rcWork.right) - w);
    const int y = std::clamp(static_cast<int>(r.top), static_cast<int>(mi.rcWork.top),
                             static_cast<int>(mi.rcWork.bottom) - h);
    r = RECT{x, y, x + w, y + h};
    return true;
}

void log_line(const std::string& text) {
    static std::filesystem::path path = data_dir() / L"app.log";
    std::ofstream out(path, std::ios::app | std::ios::binary);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char ts[32]{};
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond);
    out << ts << text << "\n";
}

/**
 * 顶层异常过滤器：只记录现场，然后交还给 Windows 错误报告。
 *
 * 返回 EXCEPTION_CONTINUE_SEARCH 是故意的 —— 我们只要一份可读的现场，
 * 不要抢走 WER 的崩溃记录（两份对照着看才准）。
 */
LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
    auto* rec = ep ? ep->ExceptionRecord : nullptr;
    if (!rec) return EXCEPTION_CONTINUE_SEARCH;

    wchar_t path[MAX_PATH]{};
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(rec->ExceptionAddress), &mod);
    GetModuleFileNameW(mod, path, MAX_PATH);
    const auto off = reinterpret_cast<DWORD_PTR>(rec->ExceptionAddress) -
                     reinterpret_cast<DWORD_PTR>(mod);

    char buf[512]{};
    std::snprintf(buf, sizeof(buf), "!! CRASH code=0x%08lX addr=%p off=0x%llX module=%s",
                  static_cast<unsigned long>(rec->ExceptionCode), rec->ExceptionAddress,
                  static_cast<unsigned long long>(off),
                  http::utf8_of(std::wstring(path)).c_str());
    log_line(buf);

    // 访问冲突时 ExceptionInformation[1] 就是被读写的那个坏地址，信息量最大
    if (rec->NumberParameters > 1) {
        std::snprintf(buf, sizeof(buf), "!! CRASH 访问类型=%llu 坏地址=%p",
                      static_cast<unsigned long long>(rec->ExceptionInformation[0]),
                      reinterpret_cast<void*>(rec->ExceptionInformation[1]));
        log_line(buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_terminate() {
    log_line("!! std::terminate（未捕获的 C++ 异常或 noexcept 违约）");
    std::abort();
}

}  // namespace

// 这两个是给 main.cpp 用的，必须有外部链接，所以不能留在匿名命名空间里
void log_boot(const char* tag) {
    log_line(std::string("boot: ") + tag);
}

void install_crash_logger() {
    SetUnhandledExceptionFilter(&crash_filter);
    std::set_terminate(&on_terminate);
}

bool AppWindow::create(HINSTANCE instance, int show_cmd) {
    instance_ = instance;
    // 自绘图标：项目没有 .rc 资源，系统默认图标是一块灰色方块，和界面配色不搭。
    // 大图标给窗口 / Alt-Tab，小图标给标题栏和托盘，两者分开取才能都清晰
    icon_ = make_app_icon(GetSystemMetrics(SM_CXICON));
    icon_small_ = make_app_icon(GetSystemMetrics(SM_CXSMICON));
    if (!icon_small_) icon_small_ = icon_;
    log_boot("icon");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AppWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_ ? icon_ : LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = icon_small_ ? icon_small_ : wc.hIcon;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    log_boot("wndclass");

    // 设置要在窗口创建之前读：里面存着上次退出时的大小和位置
    load_settings();
    log_boot("settings");

    const int dpi = 96;
    const int w = MulDiv(1280, dpi, 96);
    const int h = MulDiv(820, dpi, 96);
    hwnd_ = CreateWindowExW(0, kClassName, kTitle,
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
                                WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    log_boot("hwnd");

    // 上次退出时的大小和位置。放在窗口创建之后还原：这时才拿得到目标屏幕的
    // DPI，最小尺寸（1040×680 是 96dpi 下的值）要按它缩放。
    // 此刻窗口还没 Show，挪过去不会闪一下。
    if (win_rect_ok_) restore_window_geometry();
    pending_maximize_ = start_maximized_;

    // Win11 原生圆角；Win10 上调用失败无副作用
    DWORD corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));

    client_ = std::make_unique<mijia::Client>(data_dir());
    // 云端请求计数写进 app.log：`api calls=13 errors=0 uri=/miotspec/prop/get`。
    // 想知道「启动到数据出来发了多少」「每小时多少」时直接 grep 这个前缀。
    client_->set_api_logger([](const mijia::ApiStats& s, const std::string& uri) {
        log_line("api calls=" + std::to_string(s.total) + " errors=" + std::to_string(s.errors) +
                 " uri=" + uri);
    });
    log_boot("client");
    history_ = std::make_unique<HistoryStore>(data_dir() / L"history.json");
    log_boot("history");
    images_ = std::make_shared<images::Store>(data_dir());
    log_boot("images");

    // 托盘常驻：关掉窗口后进程仍在，环境采样才能连续攒满 24 小时
    create_tray();

    // Vue 产物的落点：<exe>/web/dist
    web_root_ = (exe_dir() / L"web" / L"dist").wstring();
    log_boot("web-root");

    // 从控制台/后台启动时 nCmdShow 可能是 SW_HIDE，这里强制显示
    // （但 --tray 自启例外：那时就是不想让窗口出来）
    const bool tray_start = wants_tray_start();
    if (!tray_start) {
        if (show_cmd == SW_HIDE) show_cmd = SW_SHOWNORMAL;
        ShowWindow(hwnd_, pending_maximize_ ? SW_SHOWMAXIMIZED : show_cmd);
        pending_maximize_ = false;
        UpdateWindow(hwnd_);
        SetForegroundWindow(hwnd_);
        BringWindowToTop(hwnd_);
    }
    // 抢前台的定时器只在正常启动时挂：--tray 自启就是要窗口别出来，
    // 那个定时器里会 ShowWindow(SW_SHOW)，挂上等于把托盘启动废掉
    if (!tray_start) SetTimer(hwnd_, 1, 1500, nullptr);
    // 采样心跳：窗口隐藏到托盘时网页定时器会被节流，靠宿主推 tick
    SetTimer(hwnd_, kTimerSample, kSampleIntervalMs, nullptr);

    RECT wr{}, cr{};
    GetWindowRect(hwnd_, &wr);
    GetClientRect(hwnd_, &cr);
    log_line("window rect=" + std::to_string(wr.right - wr.left) + "x" + std::to_string(wr.bottom - wr.top) +
             " at " + std::to_string(wr.left) + "," + std::to_string(wr.top) +
             " client=" + std::to_string(cr.right) + "x" + std::to_string(cr.bottom));

    start_worker();
    init_webview();
    // 自启时不弹托盘气泡：每次开机都弹一次「仍在后台运行」太吵
    if (tray_start) hide_to_tray(/*quiet=*/true);
    return true;
}

int AppWindow::run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK AppWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = reinterpret_cast<AppWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(hwnd, msg, wp, lp);
}

LRESULT AppWindow::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // 去掉系统标题栏与边框，改由网页自绘；最大化时也保持正确边界
            if (wp) return 0;
            break;
        case WM_NCHITTEST:
            return edge_hit_test(lp);
        case WM_ENTERSIZEMOVE:
            return 0;
        case WM_EXITSIZEMOVE:
            // 用户刚拖完边框或拖动完标题栏：立刻把几何落盘。
            //
            // 只在关窗口时保存的话，任何「没走到 WM_CLOSE / WM_DESTROY 的退出」
            // 都会把这次改的大小丢掉 —— 被任务管理器结束、被 kill、崩溃，
            // 表现就是「位置记住了、大小没记住」（尺寸还是上次那份旧值）。
            // EXITSIZEMOVE 是拖拽结束的唯一可靠时机，改完就写，谁也丢不了。
            save_settings();
            return 0;
        case WM_SIZING:
            return TRUE;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const int dpi = dpi_of(hwnd);
            mmi->ptMinTrackSize.x = MulDiv(kMinWidth, dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(kMinHeight, dpi, 96);
            // 最大化时不要盖住任务栏
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            return 0;
        }
        case WM_SIZE:
            resize_webview();
            // 最大化状态可能变化（双击标题栏 / 用户拖到边缘 / 点按钮），推给前端同步图标
            if (maximized_ != (IsZoomed(hwnd) != FALSE)) {
                maximized_ = (IsZoomed(hwnd) != FALSE);
                push_window_state();
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd, 1);
                // 这期间用户可能已经点了关闭（窗口进了托盘），别再把它弹出来
                if (!IsWindowVisible(hwnd_)) return 0;
                log_line("window visible=" + std::to_string(IsWindowVisible(hwnd_) ? 1 : 0));
                if (controller_) {
                    controller_->put_IsVisible(TRUE);
                    resize_webview();
                }
                // 抢一次前台：从控制台/任务启动时系统可能不给焦点
                ShowWindow(hwnd_, SW_SHOW);
                SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetForegroundWindow(hwnd_);
            } else if (wp == kTimerSample) {
                // 后台（隐藏到托盘）时也要持续采样，否则历史曲线攒不满 24 小时
                push_sample_tick();
            }
            return 0;
        case kMsgTray: {
            const UINT code = LOWORD(lp);
            if (code == WM_LBUTTONUP || code == WM_LBUTTONDBLCLK) {
                show_main_window();
            } else if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
                show_tray_menu();
            }
            return 0;
        }
        case WM_CLOSE:
            // 关闭按钮：按设置最小化到托盘（后台继续采样），否则真退出。
            // 两条走法都先存一次 —— 进托盘那一路后面不会再走 WM_DESTROY
            // （save_settings 内部会先抓几何，这里不用再单独 capture）
            save_settings();
            if (close_to_tray_) {
                hide_to_tray();
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_QUERYENDSESSION:
            // 系统关机/注销：必须放行，不能被"隐藏到托盘"拦住
            return TRUE;
        case WM_ENDSESSION:
            if (wp) DestroyWindow(hwnd);
            return 0;
        case kMsgReply: {
            auto* reply = reinterpret_cast<Reply*>(lp);
            // payload 已 ensure_ascii 序列化，逐字节转宽字符即可
            std::wstring script = L"window.__vicold&&window.__vicold.resolve(";
            script.append(reply->payload.begin(), reply->payload.end());
            script += L")";
            eval_js(script);
            delete reply;
            return 0;
        }
        case kMsgEvent: {
            // 后台线程投过来的事件体（已是 ensure_ascii 的 JSON 字面量）
            auto* payload = reinterpret_cast<std::string*>(lp);
            std::wstring script = L"window.__vicold&&window.__vicold.event(";
            script.append(payload->begin(), payload->end());
            script += L")";
            eval_js(script);
            delete payload;
            return 0;
        }
        case WM_DESTROY:
            // 从托盘菜单「退出」、或者系统关机走的都是这一条，最后再存一次
            save_settings();
            // 本次运行一共往米家发了多少请求，收尾记一笔（10 的倍数那条不一定是准数）
            if (client_) {
                const auto st = client_->stats();
                log_line("api total=" + std::to_string(st.total) +
                         " errors=" + std::to_string(st.errors) + " (run end)");
            }
            destroy_tray();
            stop_worker();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- 边缘 resize

// 启动系统原生的缩放循环。
// 原理：把「非客户区左键按下」直接发给顶层窗口，DefWindowProc 会进入
// SC_SIZE 的模态循环 —— 系统自己抓鼠标、跟随光标改窗口尺寸、画双向箭头，
// 还自带边缘吸附。这正是普通窗口「拖边框改大小」走的同一条代码路径。
// 注意必须带上光标的屏幕坐标，否则系统算不出抓取偏移，窗口会跳一下。
void AppWindow::begin_native_resize(const std::string& dir) {
    if (!hwnd_ || IsZoomed(hwnd_)) return;
    int ht = 0;
    if (dir == "n") ht = HTTOP;
    else if (dir == "s") ht = HTBOTTOM;
    else if (dir == "e") ht = HTRIGHT;
    else if (dir == "w") ht = HTLEFT;
    else if (dir == "ne") ht = HTTOPRIGHT;
    else if (dir == "nw") ht = HTTOPLEFT;
    else if (dir == "se") ht = HTBOTTOMRIGHT;
    else if (dir == "sw") ht = HTBOTTOMLEFT;
    if (!ht) return;

    POINT pt{};
    GetCursorPos(&pt);
    ReleaseCapture();
    SendMessageW(hwnd_, WM_NCLBUTTONDOWN, static_cast<WPARAM>(ht), MAKELPARAM(pt.x, pt.y));
}

// 计算鼠标是否落在窗口最外圈的 resize 区，返回对应的 HT* 值。
// lParam 是屏幕坐标（WM_NCHITTEST 约定）。
int AppWindow::edge_hit_test(LPARAM lp) {
    if (!hwnd_ || IsZoomed(hwnd_)) return HTCLIENT;
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    const int border = MulDiv(8, dpi_of(hwnd_), 96);
    const bool left = pt.x < rc.left + border;
    const bool right = pt.x >= rc.right - border;
    const bool top = pt.y < rc.top + border;
    const bool bottom = pt.y >= rc.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

// ---------------------------------------------------------------- 边缘缩放
//
// 为什么不在 C++ 侧接管 WM_NCHITTEST：
// WebView2 的 Chromium 会创建一组子窗口（Chrome_WidgetWin_0/1、
// Chrome_RenderWidgetHostHWND、Intermediate D3D Window）铺满整个窗口。
// 命中测试先落到最深的那个子窗口，顶层窗口根本收不到 WM_NCHITTEST。
// 而这些子窗口属于 Chromium 自己的线程，SetWindowLongPtr(GWLP_WNDPROC)
// 与 SetWindowSubclass 都会返回 ERROR_ACCESS_DENIED，无法子类化。
//
// 所以走「前端检测边缘 → 桥命令 → 宿主原生缩放循环」这条路，
// 与标题栏拖动（window.drag）用的是同一套机制。
// 宿主侧仍然保留 WM_NCHITTEST，万一系统帧区被露出来也能原生工作。

void AppWindow::resize_webview() {
    if (!controller_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    controller_->put_Bounds(rc);
    controller_->put_IsVisible(TRUE);
    // 注意：这里不要再 log。它跟着每一次 WM_SIZE 跑，拖一下边框就是几百次调用，
    // 之前往里 dump 子窗口树（EnumChildWindows + 写文件）直接把 app.log 刷满了。
}

void AppWindow::eval_js(const std::wstring& script) {
    if (webview_) webview_->ExecuteScript(script.c_str(), nullptr);
}

void AppWindow::push_window_state() {
    if (!webview_) return;
    if (pushed_maximized_ == maximized_) return;
    pushed_maximized_ = maximized_;
    std::wstring script = L"window.__vicold&&window.__vicold.event({name:'window.state',payload:{maximized:";
    script += maximized_ ? L"true" : L"false";
    script += L"}});";
    eval_js(script);
}

// ---------------------------------------------------------------- 托盘

void AppWindow::create_tray() {
    if (tray_ready_) return;
    nid_ = NOTIFYICONDATAW{};
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = kMsgTray;
    nid_.hIcon = icon_small_ ? icon_small_ : (icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION));
    wcscpy_s(nid_.szTip, L"桌面米家");
    if (Shell_NotifyIconW(NIM_ADD, &nid_)) {
        tray_ready_ = true;
        log_line("tray created");
    } else {
        log_line("tray create failed");
    }
}

void AppWindow::destroy_tray() {
    if (!tray_ready_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    tray_ready_ = false;
    // 图标与窗口类共用，不在这里 DestroyIcon —— 进程退出时由系统回收
}

void AppWindow::show_main_window() {
    if (!hwnd_) return;
    // 开机自启进托盘那次窗口没显示过，最大化要等现在补上（见 pending_maximize_）
    ShowWindow(hwnd_, pending_maximize_ ? SW_SHOWMAXIMIZED : SW_SHOW);
    pending_maximize_ = false;
    if (controller_) controller_->put_IsVisible(TRUE);
    resize_webview();
    SetForegroundWindow(hwnd_);
    BringWindowToTop(hwnd_);
}

void AppWindow::hide_to_tray(bool quiet) {
    // 启动后 1.5 秒那个「抢前台」定时器会把窗口 ShowWindow(SW_SHOW) 一遍。
    // 用户手够快、启动后一秒内就关窗口的话，它会把刚藏起来的窗口又弹回来。
    // 藏窗口就顺带把这个一次性定时器掐掉。
    KillTimer(hwnd_, 1);
    ShowWindow(hwnd_, SW_HIDE);
    // 开机自启时安静进托盘：每次开机都弹一次「仍在后台运行」太吵，
    // 但那一次仍然算「已提示过」，之后用户手动关窗口时还是会提示一遍
    if (quiet) return;
    if (!tray_hint_shown_) {
        tray_hint_shown_ = true;
        apply_tray_balloon();
    }
}

void AppWindow::apply_tray_balloon() {
    if (!tray_ready_) return;
    NOTIFYICONDATAW n = nid_;
    n.uFlags |= NIF_INFO;
    wcscpy_s(n.szInfoTitle, L"仍在后台运行");
    wcscpy_s(n.szInfo,
             L"窗口已隐藏到托盘，程序继续在后台运行，温湿度等读数会照常更新。"
             L"左键点击托盘图标可唤回主界面，右键菜单里有「退出」。");
    n.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void AppWindow::show_tray_menu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kTrayIdShow, L"显示主界面");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (close_to_tray_ ? MF_CHECKED : MF_UNCHECKED), kTrayIdToggle,
                L"关闭窗口时最小化到托盘");
    AppendMenuW(menu, MF_STRING | (autostart_enabled() ? MF_CHECKED : MF_UNCHECKED), kTrayIdAutostart,
                L"开机自动启动（静默到托盘）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayIdExit, L"退出桌面米家");

    POINT pt{};
    GetCursorPos(&pt);
    // 菜单要在窗口前面弹出，且要吃掉本次鼠标激活，否则菜单点不动
    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0,
                                     hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == kTrayIdShow) {
        show_main_window();
    } else if (cmd == kTrayIdToggle) {
        close_to_tray_ = !close_to_tray_;
        save_settings();
    } else if (cmd == kTrayIdAutostart) {
        const bool on = !autostart_enabled();
        if (!set_autostart(on)) {
            log_line("autostart write failed");
        } else {
            log_line(std::string("autostart -> ") + (on ? "on" : "off"));
            // 托盘里改的，主界面上那个开关要跟着动
            const std::string body =
                json{{"name", "autostart.changed"}, {"payload", {{"enabled", on}}}}.dump(-1, ' ', true);
            std::wstring script = L"window.__vicold&&window.__vicold.event(";
            script.append(body.begin(), body.end());
            script += L")";
            eval_js(script);
        }
    } else if (cmd == kTrayIdExit) {
        DestroyWindow(hwnd_);
    }
}

// ---------------------------------------------------------------- 设置持久化

/**
 * settings.json 里存两样东西：关窗口是否进托盘、上次退出时的窗口几何。
 *
 * 一次读全、一次写全 —— 分开写会互相覆盖（以前只写 closeToTray，
 * 加了几何之后先存几何再切托盘开关，几何就丢了）。
 */
void AppWindow::load_settings() {
    close_to_tray_ = true;  // 默认关窗口进托盘：后台要继续采样
    std::ifstream in(data_dir() / L"settings.json", std::ios::binary);
    if (!in) return;
    try {
        json j = json::parse(in);
        if (j.contains("closeToTray") && j["closeToTray"].is_boolean()) {
            close_to_tray_ = j["closeToTray"].get<bool>();
        }
        if (j.contains("window") && j["window"].is_object()) {
            const json& w = j["window"];
            const int x = w.value("x", 0);
            const int y = w.value("y", 0);
            const int ww = w.value("w", 0);
            const int hh = w.value("h", 0);
            // 宽高有一个不对就当没存过，别用一堆 0 去摆窗口
            if (ww > 0 && hh > 0) {
                win_rect_ = RECT{x, y, x + ww, y + hh};
                win_rect_ok_ = true;
                start_maximized_ = w.value("maximized", false);
            }
        }
    } catch (...) {
        // 文件坏了就当没存过，全部用默认值
    }
}

void AppWindow::save_settings() {
    // 保存前先抓一次当前几何。
    //
    // 之前是靠调用方各自记得先 capture 再 save，结果「设置页切托盘开关」
    // 那条路（`app.closeToTray`）只调了 save_settings，写进去的是**启动时读到
    // 的旧矩形**：用户改完窗口大小再去切一下开关，刚改的大小就被旧值覆盖了。
    // 收口到这里，任何一条保存路径写出去的都是最新的窗口尺寸。
    capture_window_geometry();
    json j;
    j["closeToTray"] = close_to_tray_;
    if (win_rect_ok_) {
        j["window"] = json{{"x", win_rect_.left},
                           {"y", win_rect_.top},
                           {"w", win_rect_.right - win_rect_.left},
                           {"h", win_rect_.bottom - win_rect_.top},
                           {"maximized", start_maximized_}};
    }
    std::ofstream out(data_dir() / L"settings.json", std::ios::binary | std::ios::trunc);
    if (out) out << j.dump();
}

void AppWindow::capture_window_geometry() {
    if (!hwnd_) return;
    // 用 GetWindowPlacement 而不是 GetWindowRect：最大化时 GetWindowRect 给的是
    // 铺满屏幕的那个矩形，存下来下次「还原」就成了全屏大小；
    // rcNormalPosition 始终是还原态的矩形。
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd_, &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return;
    win_rect_ = r;
    win_rect_ok_ = true;
    // 窗口隐藏着的时候不要动最大化标记：开机自启直接进托盘那次，窗口从来没
    // 显示过，这时 IsZoomed 一定是 0，照写会把用户「上次是最大化」的偏好抹掉
    if (IsWindowVisible(hwnd_)) start_maximized_ = IsZoomed(hwnd_) != FALSE;
}

void AppWindow::restore_window_geometry() {
    RECT r = win_rect_;
    if (!fit_saved_rect(r)) {
        log_line("saved window rect not on any screen, keep default placement");
        return;
    }
    const int dpi = dpi_of(hwnd_);
    const int w = std::max(static_cast<int>(r.right - r.left), MulDiv(kMinWidth, dpi, 96));
    const int h = std::max(static_cast<int>(r.bottom - r.top), MulDiv(kMinHeight, dpi, 96));
    SetWindowPos(hwnd_, nullptr, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    log_line("restore window " + std::to_string(w) + "x" + std::to_string(h) + " at " +
             std::to_string(r.left) + "," + std::to_string(r.top) +
             (start_maximized_ ? " maximized" : ""));
}

void AppWindow::push_sample_tick() {
    eval_js(L"window.__vicold&&window.__vicold.event({name:'sample.tick',payload:{}});");
}


void AppWindow::init_webview() {
    const auto user_data = (data_dir() / L"WebView2").wstring();
    const std::wstring dist = web_root_;

    if (!std::filesystem::exists(std::filesystem::path(web_root_) / L"index.html")) {
        MessageBoxW(hwnd_,
                    L"没有找到界面文件（<exe 目录>\\web\\dist）。\n\n"
                    L"请确认安装包完整解压；如果是自行构建的版本，先运行 build.cmd 再启动。",
                    kTitle, MB_ICONERROR | MB_OK);
    }

    log_line("init_webview: user_data=" + http::utf8_of(user_data) + " dist=" + http::utf8_of(dist));

    auto handler = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, dist](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            log_line("env completed hr=" + std::to_string(static_cast<long>(hr)));
            if (FAILED(hr) || !env) {
                MessageBoxW(hwnd_,
                            L"WebView2 初始化失败：请确认已安装 Microsoft Edge WebView2 Runtime。",
                            kTitle, MB_ICONERROR | MB_OK);
                return hr;
            }
            env_ = env;
            auto ctrl_handler =
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, dist](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
                        log_line("controller completed hr=" + std::to_string(static_cast<long>(hr2)));
                        if (FAILED(hr2) || !controller) return hr2;
                        controller_ = controller;
                        controller_->get_CoreWebView2(&webview_);
                        log_line("webview ptr=" + std::to_string(webview_ ? 1 : 0));

                        Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                        if (SUCCEEDED(webview_->get_Settings(&settings)) && settings) {
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(TRUE);
                            settings->put_IsZoomControlEnabled(FALSE);
                        }
                        Microsoft::WRL::ComPtr<ICoreWebView2Controller2> c2;
                        if (SUCCEEDED(controller_.As(&c2)) && c2) {
                            COREWEBVIEW2_COLOR bg{255, 244, 247, 252};
                            c2->put_DefaultBackgroundColor(bg);
                        }

                        // SetVirtualHostNameToFolderMapping 在 ICoreWebView2_3 上
                        Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                        HRESULT qi = webview_->QueryInterface(IID_PPV_ARGS(&webview3));
                        log_line("QueryInterface(ICoreWebView2_3) hr=" + std::to_string(static_cast<long>(qi)));
                        if (SUCCEEDED(qi) && webview3) {
                            HRESULT map = webview3->SetVirtualHostNameToFolderMapping(
                                kVirtualHost, dist.c_str(),
                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            log_line("SetVirtualHostNameToFolderMapping hr=" + std::to_string(static_cast<long>(map)));
                            // 产品图目录：文件由 image.sync 落盘，这里只做映射
                            const auto img_dir = images::dir(data_dir()).wstring();
                            HRESULT map2 = webview3->SetVirtualHostNameToFolderMapping(
                                kImageHost, img_dir.c_str(),
                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            log_line("SetVirtualHostNameToFolderMapping(img) hr=" +
                                     std::to_string(static_cast<long>(map2)));
                        }

                        webview_->add_WebMessageReceived(
                            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                    -> HRESULT {
                                    LPWSTR raw = nullptr;
                                    if (FAILED(args->get_WebMessageAsJson(&raw)) || !raw) return S_OK;
                                    std::wstring text(raw);
                                    CoTaskMemFree(raw);
                                    on_bridge_message(text);
                                    return S_OK;
                                })
                                .Get(),
                            &msg_token_);

                        webview_->add_NavigationCompleted(
                            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args)
                                    -> HRESULT {
                                    BOOL ok = FALSE;
                                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    args->get_IsSuccess(&ok);
                                    args->get_WebErrorStatus(&status);
                                    log_line("navigation completed ok=" + std::to_string(ok ? 1 : 0) +
                                             " status=" + std::to_string(static_cast<int>(status)));
                                    return S_OK;
                                })
                                .Get(),
                            nullptr);

                        resize_webview();
                        const std::wstring url = std::wstring(L"https://") + kVirtualHost + L"/index.html";
                        HRESULT nav = webview_->Navigate(url.c_str());
                        log_line("navigate hr=" + std::to_string(static_cast<long>(nav)));
                        return S_OK;
                    });
            const HRESULT create = env->CreateCoreWebView2Controller(hwnd_, ctrl_handler.Get());
            log_line("CreateCoreWebView2Controller hr=" + std::to_string(static_cast<long>(create)));
            return S_OK;
        });
    // 兼容模式：部分安全软件会拦截 WebView2 渲染进程的沙箱，表现为窗口正常但内容
    // 一直是白屏/黑屏（连 60 行的最小 WebView2 程序也一样）。本程序只加载本地
    // 虚拟主机里的静态资源，不加载外部网页，因此默认关闭渲染进程沙箱以保证可用性。
    // 需要恢复沙箱时设置环境变量 VICOLD_SECURE=1。
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions> options;
    {
        std::wstring args = L"--no-sandbox";
        wchar_t buf[256]{};
        if (GetEnvironmentVariableW(L"VICOLD_SECURE", buf, 256) > 0 && std::wstring(buf) == L"1") {
            args.clear();
        }
        wchar_t extra[256]{};
        if (GetEnvironmentVariableW(L"VICOLD_WEBVIEW2_ARGS", extra, 256) > 0) {
            if (!args.empty()) args += L" ";
            args += extra;
        }
        if (!args.empty() &&
            SUCCEEDED(Microsoft::WRL::MakeAndInitialize<CoreWebView2EnvironmentOptions>(&options))) {
            options->put_AdditionalBrowserArguments(args.c_str());
        }
        log_line("browser args: " + http::utf8_of(args));
    }
    CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data.c_str(),
                                             options ? options.Get() : nullptr, handler.Get());
}

void AppWindow::on_bridge_message(const std::wstring& payload) {
    json msg;
    try {
        msg = json::parse(http::utf8_of(payload));
    } catch (...) {
        return;
    }
    const std::string cmd = msg.value("cmd", "");
    const json args = msg.value("args", json::object());
    const uint64_t id = msg.value("id", 0ULL);

    // 窗口控制必须在 UI 线程立刻执行
    if (cmd == "window.drag") {
        // 原生窗口移动：系统会捕获鼠标并吞掉 mouseup/click，
        // 所以双击判定只能在 mousedown 阶段做（见 TitleBar.vue）。
        if (IsZoomed(hwnd_)) {
            // 最大化状态下拖标题栏：先还原，再让窗口跟随光标。
            // 系统默认行为是「还原后停在原位」，这里按比例还原光标的相对位置，
            // 拖动手感与未最大化时一致。
            POINT cur{};
            GetCursorPos(&cur);
            RECT maxRect{};
            GetWindowRect(hwnd_, &maxRect);
            ShowWindow(hwnd_, SW_RESTORE);
            RECT norm{};
            GetWindowRect(hwnd_, &norm);
            const int maxW = maxRect.right - maxRect.left;
            const int normW = norm.right - norm.left;
            const double fx = (maxW > 0 && normW > 0) ? double(normW) / double(maxW) : 1.0;
            // 横向：保持光标在标题栏内的相对比例；纵向：保持离窗口顶端的距离
            const int newLeft = cur.x - static_cast<int>((cur.x - maxRect.left) * fx);
            const int newTop = cur.y - (cur.y - maxRect.top);
            SetWindowPos(hwnd_, nullptr, newLeft, newTop, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        reply(id, true, json{{"ok", true}}, "");
        return;
    }
    if (cmd == "window.resize") {
        // 前端检测到鼠标落在窗口边缘 → 走系统原生缩放循环
        begin_native_resize(args.value("dir", ""));
        reply(id, true, json{{"ok", true}}, "");
        return;
    }
    if (cmd == "window.minimize") {
        ShowWindow(hwnd_, SW_MINIMIZE);
        reply(id, true, json{{"ok", true}}, "");
        return;
    }
    if (cmd == "window.maximize") {
        // 双击标题栏与按钮共用：已最大化则还原，否则最大化
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        maximized_ = IsZoomed(hwnd_) != FALSE;
        push_window_state();
        reply(id, true, json{{"ok", true}, {"maximized", maximized_}}, "");
        return;
    }
    if (cmd == "window.state") {
        maximized_ = IsZoomed(hwnd_) != FALSE;
        reply(id, true, json{{"ok", true}, {"maximized", maximized_}}, "");
        return;
    }
    if (cmd == "window.close") {
        // 关闭按钮：按设置隐藏到托盘（后台继续采样），否则真退出
        if (close_to_tray_) {
            hide_to_tray();
        } else {
            DestroyWindow(hwnd_);
        }
        return;
    }

    if (cmd == "image.sync") {
        // 抓图是慢活（每台设备一个 HTML 页面 + 一张图，冷缓存几十秒）：
        // 绝不能占串行 job 线程，否则同步期间设备开关、读数、场景全被堵死。
        // 自己开线程 + 共享指针持有缓存对象，窗口先关掉也不会踩空。
        auto store = images_;
        const HWND hwnd = hwnd_;
        const json models = args.value("models", json::array());
        std::thread([store, hwnd, models, id]() {
            // 每抓到一张就把 model→文件名 推给前端，卡片逐张点亮；
            // 全部抓完还有一次完整回包，事件丢了也能补齐
            auto notify = [hwnd](const std::string& model, const std::string& name) {
                json ev{{"name", "image.ready"}, {"payload", json{{"model", model}, {"name", name}}}};
                auto* payload = new std::string(ev.dump(-1, ' ', true));
                PostMessageW(hwnd, kMsgEvent, 0, reinterpret_cast<LPARAM>(payload));
            };
            json data{{"images", json::object()}, {"missing", json::array()}};
            std::string error;
            try {
                data = store->sync(models, notify);
            } catch (const std::exception& e) {
                error = e.what();
            }
            auto* r = new Reply;
            r->id = id;
            r->ok = error.empty();
            json envelope{{"id", id}, {"ok", r->ok}};
            if (r->ok) envelope["data"] = data;
            else envelope["error"] = error;
            r->payload = envelope.dump(-1, ' ', true);
            PostMessageW(hwnd, kMsgReply, 0, reinterpret_cast<LPARAM>(r));
        }).detach();
        return;
    }

    post_job([this, cmd, args]() { return run_command(cmd, args); }, id);
}

AppWindow::json AppWindow::run_command(const std::string& cmd, const json& args) {
    if (cmd == "app.info") {
        return json{{"version", "0.1.0"},
                    {"cpp", static_cast<long>(__cplusplus)},
                    {"data_dir", http::utf8_of(data_dir().wstring())},
                    {"web_root", http::utf8_of(std::filesystem::path(web_root_).wstring())}};
    }
    if (cmd == "auth.status") return client_->status();
    if (cmd == "auth.begin") {
        const auto start = client_->begin_login();
        return json{{"refreshed", start.refreshed}, {"login_url", start.login_url}};
    }
    if (cmd == "auth.wait") {
        client_->wait_login();
        return json{{"ok", true}};
    }
    if (cmd == "auth.logout") {
        client_->logout();
        return json{{"ok", true}};
    }
    if (cmd == "home.list") return client_->home_list();
    if (cmd == "device.list") {
        auto list = client_->devices();
        // 家庭数解释了「为什么 home_device_list 发了 N 次」：每个家庭各发一次
        log_line("device.list: " + std::to_string(client_->home_count()) + " homes, " +
                 std::to_string(list.is_array() ? list.size() : 0) + " devices");
        return list;
    }
    if (cmd == "device.spec") return client_->device_spec(args.value("spec_type", ""));
    if (cmd == "prop.get") {
        // 记一下这一批多少项：接口单次 40 条上限，看日志就知道前端有没有把批次攒满
        const json items = args.value("items", json::array());
        auto ret = client_->get_props(items);
        log_line("prop.get sent=" + std::to_string(items.size()) + " got=" +
                 std::to_string(ret.is_array() ? ret.size() : 0));
        return ret;
    }
    if (cmd == "prop.set") {
        return client_->set_prop(args.value("did", ""), args.value("siid", 0), args.value("piid", 0),
                                 args.value("value", json(nullptr)));
    }
    if (cmd == "action.run") {
        return client_->run_action(args.value("did", ""), args.value("siid", 0),
                                   args.value("aiid", 0), args.value("params", json::array()));
    }
    if (cmd == "scene.list") return client_->scenes();    if (cmd == "scene.run") {
        client_->run_scene(args.value("scene_id", ""), args.value("home_id", ""),
                           args.value("owner_uid", 0LL));
        return json{{"ok", true}};
    }
    // 设备产品图由 on_bridge_message 直接开线程处理（不能占串行 job 线程），
    // 所以 run_command 里没有 image.sync 分支。

    // ---- 应用设置 ----
    // 开机自启：带 enable 就是改，不带就是读当前状态
    if (cmd == "app.autostart") {
        if (args.contains("enable") && args["enable"].is_boolean()) {
            const bool want = args["enable"].get<bool>();
            if (!set_autostart(want)) {
                log_line("autostart write failed (from ui)");
                return json{{"ok", false}, {"autostart", autostart_enabled()}};
            }
            log_line(std::string("autostart -> ") + (want ? "on" : "off") + " (from ui)");
        }
        return json{{"ok", true}, {"autostart", autostart_enabled()}};
    }
    if (cmd == "app.settings") {
        return json{{"autostart", autostart_enabled()}, {"closeToTray", close_to_tray_}};
    }
    // 关闭窗口时最小化到托盘：设置页和托盘右键菜单改的是同一个开关，
    // 两处都回读真实值，界面不会出现「显示已开但实际没生效」
    if (cmd == "app.closeToTray") {
        if (args.contains("enable") && args["enable"].is_boolean()) {
            close_to_tray_ = args["enable"].get<bool>();
            save_settings();
            log_line(std::string("closeToTray -> ") + (close_to_tray_ ? "on" : "off") + " (from ui)");
        }
        return json{{"ok", true}, {"closeToTray", close_to_tray_}};
    }

    // 云端请求计数（本次运行累计）。设置页「关于」或排查限流时用；
    // 日志里每 10 个请求也会自己写一行，这个命令只是方便随时取当前值。
    if (cmd == "app.apiStats") {
        const auto st = client_->stats();
        return json{{"total", st.total},
                    {"errors", st.errors},
                    {"uptimeSec", static_cast<long long>(GetTickCount64() / 1000)}};
    }

    // ---- 本地历史库：环境读数时间序列，前端画折线图用 ----
    if (cmd == "history.append") {
        history_->append(args.value("points", json::array()));
        return json{{"ok", true}};
    }
    if (cmd == "history.list") {
        std::vector<std::string> dids;
        if (args.contains("dids") && args["dids"].is_array()) {
            for (const auto& d : args["dids"]) {
                if (d.is_string()) dids.push_back(d.get<std::string>());
            }
        }
        return history_->list(dids, args.value("key", ""), args.value("since", 0LL),
                              args.value("limit", 0ULL));
    }
    if (cmd == "history.clear") {
        history_->clear(args.value("did", ""));
        return json{{"ok", true}};
    }
    // 云端历史回填：程序没运行的那段空白，从云端补回来。
    // 一次只处理一条曲线（云端每次最多 1000 点，一条要翻 2~3 页），
    // 由前端串行发多条，这样每条都在串行 job 线程里短时间完成，
    // 不会像整批回填那样把设备开关/读数堵上一分钟。
    if (cmd == "history.backfill") {
        const std::string did = args.value("did", "");
        const std::string key = args.value("key", "");
        // 虚拟子设备的读数挂在父 did 的 siid 上，存盘仍按界面上的 did
        std::string cloud_did = args.value("cloud_did", "");
        if (cloud_did.empty()) cloud_did = did;
        const int siid = args.value("siid", 0);
        const int piid = args.value("piid", 0);
        const long long from = args.value("from", 0LL);
        const long long to = args.value("to", 0LL);
        if (did.empty() || key.empty() || siid <= 0 || piid <= 0 || to <= from) {
            return json{{"ok", false}, {"points", 0}};
        }
        const json res = client_->history_series(cloud_did, siid, piid, from, to);
        const json& pts = res["points"];
        if (!pts.empty()) history_->merge(did, key, pts);
        const std::string note = res.value("error", "");
        log_line("backfill " + cloud_did + " " + key + " " + std::to_string(siid) + "." +
                 std::to_string(piid) + " -> " + std::to_string(pts.size()) +
                 (note.empty() ? "" : (" (" + note + ")")));
        return json{{"ok", note.empty()}, {"points", pts.size()}, {"error", note}};
    }
    // 调试：只看云端有没有这条曲线，不写库（排查「某台设备补不到数据」用）
    if (cmd == "history.probe") {
        std::string cloud_did = args.value("cloud_did", "");
        if (cloud_did.empty()) cloud_did = args.value("did", "");
        const json res = client_->history_series(cloud_did, args.value("siid", 0),
                                                 args.value("piid", 0), args.value("from", 0LL),
                                                 args.value("to", 0LL));
        return json{{"count", res["points"].size()},
                    {"error", res.value("error", "")},
                    {"first", res["points"].empty() ? json(nullptr) : res["points"].front()},
                    {"last", res["points"].empty() ? json(nullptr) : res["points"].back()}};
    }
    throw mijia::ApiError(-1, "未知指令: " + cmd);
}

void AppWindow::start_worker() {
    worker_ = std::thread([this]() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();
        }
    });
}

void AppWindow::stop_worker() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AppWindow::post_job(std::function<json()> job, uint64_t id) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back([this, job = std::move(job), id]() {
            json data;
            bool ok = true;
            std::string error;
            try {
                data = job();
            } catch (const mijia::ApiError& e) {
                ok = false;
                error = e.message();
                log_line("cmd failed: " + error + " (code " + std::to_string(e.code()) + ")");
            } catch (const std::exception& e) {
                ok = false;
                error = e.what();
                log_line("cmd failed: " + error);
            }
            reply(id, ok, data, error);
        });
    }
    cv_.notify_one();
}

void AppWindow::reply(uint64_t id, bool ok, const json& data, const std::string& error) {
    auto* r = new Reply;
    r->id = id;
    r->ok = ok;
    json envelope{{"id", id}, {"ok", ok}};
    if (ok) envelope["data"] = data;
    else envelope["error"] = error;
    // ensure_ascii：避免 lone surrogate 之类的转义问题，直接变纯 ASCII
    r->payload = envelope.dump(-1, ' ', true);
    PostMessageW(hwnd_, kMsgReply, 0, reinterpret_cast<LPARAM>(r));
}

}  // namespace vc
