cmake_minimum_required(VERSION 3.20)
project(VicoldMIHome VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /Zc:__cplusplus 让 __cplusplus 反映真实标准版本（MSVC 默认恒为 199711L）
    add_compile_options(/Zc:__cplusplus)
endif()

if(NOT MSVC)
    message(FATAL_ERROR "此项目仅支持 MSVC（需要 WebView2 SDK 与 Windows SDK）")
endif()

# ---- WebView2 SDK ----
# 优先用 -DWEBVIEW2_SDK=<path>，否则从 NuGet 全局缓存里找最新的
if(NOT DEFINED WEBVIEW2_SDK)
    file(GLOB _wv2_candidates "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/*/build/native")
    if(_wv2_candidates)
        list(SORT _wv2_candidates)
        list(GET _wv2_candidates -1 WEBVIEW2_SDK)
    endif()
endif()
if(NOT WEBVIEW2_SDK OR NOT EXISTS "${WEBVIEW2_SDK}/include/WebView2.h")
    message(FATAL_ERROR "未找到 WebView2 SDK。请执行: nuget install Microsoft.Web.WebView2，或用 -DWEBVIEW2_SDK=... 指定 build/native 目录")
endif()
message(STATUS "WebView2 SDK: ${WEBVIEW2_SDK}")

add_executable(Vicold.MIHome WIN32
    src/main.cpp
    src/app_window.cpp
    src/history.cpp
    src/proto/crypto.cpp
    src/proto/http.cpp
    src/proto/images.cpp
    src/proto/mijia.cpp
)

target_include_directories(Vicold.MIHome PRIVATE
    src
    third_party
    third_party/nlohmann
    "${WEBVIEW2_SDK}/include"
)

target_link_directories(Vicold.MIHome PRIVATE "${WEBVIEW2_SDK}/x64")
target_link_libraries(Vicold.MIHome PRIVATE
    WebView2LoaderStatic
    winhttp
    bcrypt
    dwmapi
    shell32
    ole32
    user32
    comctl32
    advapi32
)

target_compile_definitions(Vicold.MIHome PRIVATE
    UNICODE UNICODE_ _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(Vicold.MIHome PRIVATE /utf-8 /W3 /EHsc /MP)

set_target_properties(Vicold.MIHome PROPERTIES
    OUTPUT_NAME "Vicold.MIHome"
)

# ---- 前端产物：每次构建都同步 web/dist 到输出目录 ----
# 不用 POST_BUILD：exe 没重新链接时它不会执行，容易留下上一次的前端产物
set(WEB_DIST "${CMAKE_CURRENT_SOURCE_DIR}/web/dist")
add_custom_target(copy_web ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:Vicold.MIHome>/web/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEB_DIST}" "$<TARGET_FILE_DIR:Vicold.MIHome>/web/dist"
    DEPENDS Vicold.MIHome
    COMMENT "同步 Vue 产物到输出目录"
)
