// SPDX-License-Identifier: GPL-3.0-or-later
// 极薄的 HTTP 封装：WinHTTP，同步调用，跑在后台任务线程上。
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vc::http {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
    int status = 0;
    std::string body;
    std::vector<std::string> set_cookies;
    std::string location;  // 3xx 跳转目标

    bool ok() const { return status >= 200 && status < 300; }
    bool redirect() const { return status >= 300 && status < 400 && !location.empty(); }
    // 从 Set-Cookie 里取某个 cookie 的值，取不到返回空串
    std::string cookie(std::string_view name) const;
};

class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // follow=false 时把 3xx 原样返回，便于逐跳收集 Set-Cookie
    Response get(const std::string& url, const Headers& headers, bool follow = true);
    Response post_form(const std::string& url, const std::string& body, const Headers& headers,
                       bool follow = true);

private:
    Response send(const std::wstring& verb, const std::string& url, const Headers& headers,
                 const std::string* body, bool follow);
    void* session_ = nullptr;
};

std::string utf8_of(const std::wstring& w);
std::wstring wide_of(const std::string& s);

}  // namespace vc::http
