// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include <stdexcept>

#pragma comment(lib, "winhttp.lib")

namespace vc::http {
namespace {

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool https = true;
};

UrlParts crack(const std::wstring& url) {
    UrlParts out;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4095;
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) {
        throw std::runtime_error("URL 解析失败");
    }
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength && uc.lpszExtraInfo) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    out.port = uc.nPort;
    out.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}  // namespace

std::string utf8_of(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring wide_of(const std::string& s) { return widen(s); }

std::string Response::cookie(std::string_view name) const {
    for (const auto& raw : set_cookies) {
        size_t eq = raw.find('=');
        if (eq == std::string::npos) continue;
        std::string key = raw.substr(0, eq);
        // 去掉前导空白
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
        if (key != name) continue;
        size_t end = raw.find(';', eq);
        return raw.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
    }
    return {};
}

Client::Client() {
    session_ = WinHttpOpen(L"Vicold.MIHome/0.1",
                           WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) throw std::runtime_error("初始化 HTTP 会话失败");
    // 解析/连接/发送/接收
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 15000, 15000, 30000, 130000);
    DWORD flags = WINHTTP_DISABLE_COOKIES;  // cookie 全部手工维护，避免被自动覆盖
    WinHttpSetOption(static_cast<HINTERNET>(session_), WINHTTP_OPTION_DISABLE_FEATURE, &flags, sizeof(flags));
}

Client::~Client() {
    if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

Response Client::get(const std::string& url, const Headers& headers, bool follow) {
    return send(L"GET", url, headers, nullptr, follow);
}

Response Client::post_form(const std::string& url, const std::string& body, const Headers& headers,
                           bool follow) {
    return send(L"POST", url, headers, &body, follow);
}

Response Client::send(const std::wstring& verb, const std::string& url, const Headers& headers,
                      const std::string* body, bool follow) {
    const UrlParts parts = crack(widen(url));
    Response resp;

    HINTERNET conn = WinHttpConnect(static_cast<HINTERNET>(session_), parts.host.c_str(), parts.port, 0);
    if (!conn) throw std::runtime_error("连接服务器失败: " + utf8_of(parts.host));
    struct ConnGuard {
        HINTERNET h;
        ~ConnGuard() { if (h) WinHttpCloseHandle(h); }
    } conn_guard{conn};

    DWORD open_flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, verb.c_str(), parts.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, open_flags);
    if (!req) throw std::runtime_error("创建请求失败");
    struct ReqGuard {
        HINTERNET h;
        ~ReqGuard() { if (h) WinHttpCloseHandle(h); }
    } req_guard{req};

    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

    DWORD redirect = follow ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
                            : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    std::wstring header_block;
    for (const auto& [k, v] : headers) {
        header_block += widen(k);
        header_block += L": ";
        header_block += widen(v);
        header_block += L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(
        req,
        header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
        header_block.empty() ? 0 : static_cast<DWORD>(-1L),
        body ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA,
        body ? static_cast<DWORD>(body->size()) : 0,
        body ? static_cast<DWORD>(body->size()) : 0, 0);
    if (!sent) throw std::runtime_error("发送请求失败: " + url);
    if (!WinHttpReceiveResponse(req, nullptr)) throw std::runtime_error("等待响应失败: " + url);

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<int>(status);

    // 3xx 的跳转目标：调用方可能自己跟随，以便逐跳收 Set-Cookie
    for (DWORD i = 0;; ++i) {
        DWORD size = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                            &size, &i);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                    buf.data(), &size, &i)) {
                resp.location = utf8_of(std::wstring(buf.c_str()));
            }
        }
        break;
    }

    // 收集所有 Set-Cookie
    for (DWORD i = 0;; ++i) {
        DWORD size = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                            &size, &i);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                    buf.data(), &size, &i)) {
                resp.set_cookies.push_back(utf8_of(std::wstring(buf.c_str())));
                continue;
            }
        }
        break;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
        chunk.resize(read);
        resp.body += chunk;
    }
    return resp;
}

}  // namespace vc::http
