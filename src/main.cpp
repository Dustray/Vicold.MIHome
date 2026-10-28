// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <objbase.h>

#include "app_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    // 崩溃可能发生在任何初始化之前，这两行必须最先执行（见 install_crash_logger 的注释）
    vc::install_crash_logger();
    vc::log_boot("wWinMain");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    vc::log_boot("dpi-aware");
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        MessageBoxW(nullptr, L"COM 初始化失败", L"桌面米家", MB_ICONERROR);
        return 1;
    }
    vc::log_boot("com-ready");

    vc::AppWindow window;
    if (!window.create(instance, show_cmd)) {
        MessageBoxW(nullptr, L"窗口创建失败", L"桌面米家", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    vc::log_boot("window-created");
    const int code = window.run();
    CoUninitialize();
    return code;
}
