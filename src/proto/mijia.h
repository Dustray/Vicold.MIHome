// SPDX-License-Identifier: GPL-3.0-or-later
// 米家云端协议客户端（纯 C++ 实现，不依赖 mijiaAPI / Python）。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "json.hpp"

namespace vc::mijia {

using json = nlohmann::json;

class ApiError : public std::runtime_error {
public:
    ApiError(int code, std::string message)
        : std::runtime_error(message), code_(code), message_(std::move(message)) {}
    int code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    int code_;
    std::string message_;
};

struct LoginStart {
    bool refreshed = false;    // 凭据仍有效，无需扫码
    std::string login_url;     // 需要扫码时，前端据此渲染二维码
};

class Client {
public:
    explicit Client(std::filesystem::path data_dir);

    bool authed() const;
    json status() const;
    LoginStart begin_login();
    void wait_login();   // 阻塞长轮询（最长约 120s）
    void logout();

    json homes();                                   // 家庭列表（含房间）
    json home_list();                               // 归一化后的家庭 + 房间（含空房间）
    json devices();                                 // 全部设备（自有 + 共享）
    json device_spec(const std::string& spec_type); // miot-spec 规格（带磁盘缓存）
    json get_props(const json& items);
    // 调试：按 did 扫描一段 siid/piid，用来发现规格里没写、但设备其实支持的通道
    void probe_props(const std::string& did, int siid_max, int piid_max);
    // 云端读数历史（米家 App「历史记录」走的就是这个接口，
    // 与设备规格里那个读不到的 read-history-data 不是一回事）
    // 实测结论（2026-10-27）：
    //  · key 必须是 `siid.piid` 字符串，写属性名一律返回空数组；
    //  · time_start 会被忽略，接口固定返回「time_end 往前最近 1000 点」，
    //    单次上限就是 1000，传更大没用 —— 想取更早的只能把 time_end 往前提着翻页；
    //  · 实测能翻到 30 天前，点间隔约 37 秒（不同型号差异很大，蓝牙设备常在
    //    数值变化时才上报，几十分钟一个点也正常）。
    // 返回 {"points": [[ts, value], ...], "error": "..."}，points 按时间升序。
    // 用对象包一层是为了把「为什么没数据」带回前端/日志：一个是云端真没有，
    // 一个是接口报错，两者表现都是空数组，不区分就没法排查。
    json history_series(const std::string& did, int siid, int piid, long long from, long long to,
                        int max_pages = 4);
    json set_prop(const std::string& did, int siid, int piid, json value);
    json run_action(const std::string& did, int siid, int aiid, json params);
    json scenes();
    void run_scene(const std::string& scene_id, const std::string& home_id, long long owner_uid);

private:
    json request(const std::string& uri, json data);
    json service_login();                 // 尝试用已有凭据换 token
    void refresh_token();
    json home_devices(const std::string& home_id, long long owner_uid);
    json shared_devices();
    std::string cookie_header() const;
    void load_auth();
    void save_auth();

    std::filesystem::path data_dir_;
    std::filesystem::path auth_path_;
    json auth_ = json::object();
    json pending_login_ = json::object();   // begin_login 与 wait_login 之间传递
};

// 工具：把 spec 原始 JSON 展平成前端可直接渲染的属性/动作列表
json flatten_spec(const json& spec);

}  // namespace vc::mijia
