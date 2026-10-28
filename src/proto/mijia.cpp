// SPDX-License-Identifier: GPL-3.0-or-later
#include "mijia.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>

#include "crypto.h"
#include "http.h"

namespace vc::mijia {
namespace {

constexpr char kApiBase[] = "https://api.mijia.tech/app";
constexpr char kLoginUrl[] = "https://account.xiaomi.com/longPolling/loginUrl";
constexpr char kServiceLogin[] =
    "https://account.xiaomi.com/pass/serviceLogin?_json=true&sid=mijia&_locale=zh_CN";
constexpr char kSpecUrl[] = "https://miot-spec.org/miot-spec-v2/instance";

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string strip_prefix(std::string body) {
    const std::string prefix = "&&&START&&&";
    if (body.starts_with(prefix)) body.erase(0, prefix.size());
    return body;
}

json parse_json(const std::string& text, const char* what) {
    try {
        return json::parse(strip_prefix(text));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string(what) + " 返回内容无法解析: " + e.what());
    }
}

std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out.push_back(static_cast<char>(hexval(s[i + 1]) * 16 + hexval(s[i + 2])));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& url) {
    std::map<std::string, std::string> out;
    size_t q = url.find('?');
    if (q == std::string::npos) return out;
    std::string query = url.substr(q + 1);
    size_t start = 0;
    while (start <= query.size()) {
        size_t amp = query.find('&', start);
        std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            out[percent_decode(pair.substr(0, eq))] = percent_decode(pair.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

std::string join_query(const std::map<std::string, std::string>& kv) {
    std::string out;
    for (const auto& [k, v] : kv) {
        if (!out.empty()) out += "&";
        out += crypto::url_encode(k) + "=" + crypto::url_encode(v);
    }
    return out;
}

std::string upper_hex(size_t n) {
    std::string s = crypto::random_hex(n);
    for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

// 客户端时区串：GMT+08:00 / GMT-05:00
std::string gmt_offset() {
    TIME_ZONE_INFORMATION tz{};
    DWORD r = GetTimeZoneInformation(&tz);
    LONG minutes = tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias);
    const int offset = static_cast<int>(-minutes);
    const int hh = (offset >= 0 ? offset : -offset) / 60;
    const int mm = (offset >= 0 ? offset : -offset) % 60;
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "GMT%c%02d:%02d", offset >= 0 ? '+' : '-', hh, mm);
    return buf;
}

bool is_daylight() {
    TIME_ZONE_INFORMATION tz{};
    return GetTimeZoneInformation(&tz) == TIME_ZONE_ID_DAYLIGHT;
}

// 把 Location 解析成绝对地址（相对路径、协议相对路径都要能处理）
std::string resolve_url(const std::string& base, const std::string& loc) {
    if (loc.starts_with("http://") || loc.starts_with("https://")) return loc;
    const size_t scheme_end = base.find("://");
    if (scheme_end == std::string::npos) return loc;
    const std::string scheme = base.substr(0, scheme_end);
    if (loc.starts_with("//")) return scheme + ":" + loc;
    const size_t host_end = base.find('/', scheme_end + 3);
    if (loc.starts_with('/')) {
        if (host_end == std::string::npos) return base + loc;
        return base.substr(0, host_end) + loc;
    }
    if (host_end == std::string::npos) return base + "/" + loc;
    const size_t slash = base.rfind('/');
    return base.substr(0, slash + 1) + loc;
}

// 云端字段类型不统一（uid/id 有时是字符串、有时是数字），统一容错取值
std::string jstr(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return {};
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    return {};
}

long long jll(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return 0;
    const auto& v = j[key];
    if (v.is_number_integer()) return v.get<long long>();
    if (v.is_number_unsigned()) return static_cast<long long>(v.get<unsigned long long>());
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

bool proto_debug() {
    static const bool on = [] {
        wchar_t buf[8]{};
        return GetEnvironmentVariableW(L"VICOLD_PROTO_DEBUG", buf, 8) > 0;
    }();
    return on;
}

bool jbool(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return false;
    const auto& v = j[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<long long>() != 0;
    if (v.is_string()) return v.get<std::string>() == "true" || v.get<std::string>() == "1";
    return false;
}

std::string spec_cache_name(const std::string& spec_type) {
    std::string s = spec_type;
    std::replace(s.begin(), s.end(), ':', '_');
    std::replace(s.begin(), s.end(), '\\', '_');
    std::replace(s.begin(), s.end(), '/', '_');
    return s + ".json";
}

}  // namespace

Client::Client(std::filesystem::path data_dir) : data_dir_(std::move(data_dir)) {
    std::filesystem::create_directories(data_dir_);
    auth_path_ = data_dir_ / "auth.json";
    load_auth();
}

void Client::load_auth() {
    if (!std::filesystem::exists(auth_path_)) return;
    try {
        std::ifstream in(auth_path_, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!text.empty()) auth_ = json::parse(text);
    } catch (...) {
        auth_ = json::object();  // 损坏就当作未登录，重新扫码
    }
}

void Client::save_auth() {
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    const auto tmp = auth_path_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << auth_.dump(2);
    }
    std::filesystem::rename(tmp, auth_path_, ec);
}

bool Client::authed() const {
    if (!auth_.is_object()) return false;
    for (const char* key : {"ssecurity", "userId", "cUserId", "serviceToken"}) {
        if (!auth_.contains(key) || auth_[key].get<std::string>().empty()) return false;
    }
    return true;
}

json Client::status() const {
    json out{{"authed", authed()}};
    if (authed()) {
        out["userId"] = auth_.value("userId", "");
        out["nickname"] = auth_.value("nickname", "");
    }
    return out;
}

void Client::logout() {
    auth_ = json::object();
    std::error_code ec;
    std::filesystem::remove(auth_path_, ec);
}

std::string Client::cookie_header() const {
    const std::string service_token = auth_.value("serviceToken", "");
    std::string s;
    s += "cUserId=" + auth_.value("cUserId", "") + ";";
    s += "yetAnotherServiceToken=" + service_token + ";";
    s += "serviceToken=" + service_token + ";";
    s += "timezone_id=" + auth_.value("timezone_id", "Asia/Shanghai") + ";";
    s += "timezone=" + gmt_offset() + ";";
    s += "is_daylight=" + std::string(is_daylight() ? "1" : "0") + ";";
    s += "dst_offset=" + std::to_string(is_daylight() ? 3600000 : 0) + ";";
    s += "channel=MI_APP_STORE;countryCode=CN;";
    s += "PassportDeviceId=" + auth_.value("deviceId", "") + ";";
    s += "locale=zh_CN";
    return s;
}

void Client::refresh_token() {
    if (service_login().value("ok", false)) {
        save_auth();
        return;
    }
    throw ApiError(-1, "登录已过期，请重新扫码");
}

json Client::service_login() {
    http::Client http;
    if (!auth_.contains("ua")) auth_["ua"] = "";
    if (auth_.value("ua", "").empty()) {
        const std::string ua_id1 = upper_hex(40), ua_id2 = upper_hex(32),
                          ua_id3 = upper_hex(32), ua_id4 = upper_hex(40);
        const std::string pass_o = auth_.value("pass_o", crypto::random_hex(16));
        auth_["pass_o"] = pass_o;
        auth_["ua"] = "Android-15-11.0.701-Xiaomi-23046RP50C-OS2.0.212.0.VMYCNXM-" + ua_id1 +
                      "-CN-" + ua_id3 + "-" + ua_id2 + "-SmartHome-MI_APP_STORE-" + ua_id1 + "|" +
                      ua_id4 + "|" + pass_o + "-64";
    }
    if (auth_.value("deviceId", "").empty()) {
        auth_["deviceId"] = crypto::random_hex(16);
    }

    const std::string cookie = "deviceId=" + auth_.value("deviceId", "") +
                               ";pass_o=" + auth_.value("pass_o", "") +
                               ";passToken=" + auth_.value("passToken", "") +
                               ";userId=" + auth_.value("userId", "") +
                               ";cUserId=" + auth_.value("cUserId", "") + ";uLocale=zh_CN;";
    http::Headers headers{
        {"User-Agent", auth_.value("ua", "")},
        {"Connection", "keep-alive"},
        {"Accept-Encoding", "identity"},
        {"Content-Type", "application/x-www-form-urlencoded"},
        {"Cookie", cookie},
    };
    auto resp = http.get(kServiceLogin, headers);
    json data = parse_json(resp.body, "登录服务");
    if (data.value("code", -1) != 0 || !data.contains("location")) return json{{"ok", false}};

    const std::string location = data.value("location", "");
    auto r2 = http.get(location, headers);
    if (r2.status == 200 && r2.body == "ok") {
        for (const auto& c : r2.set_cookies) {
            const size_t eq = c.find('=');
            if (eq == std::string::npos) continue;
            std::string key = c.substr(0, eq);
            size_t end = c.find(';', eq);
            if (end != std::string::npos) {
                std::string tail = c.substr(end);
                if (tail.find("Max-Age=0") != std::string::npos) continue;  // 过期指令
            }
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            auth_[key] = c.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
        }
        auth_["ssecurity"] = data.value("ssecurity", "");
        return json{{"ok", true}};
    }
    // 未登录：把 location 的 query 参数留给扫码流程
    return json{{"ok", false}, {"query", join_query(parse_query(location))}};
}

LoginStart Client::begin_login() {
    LoginStart start;
    auto probe = service_login();
    if (probe.value("ok", false)) {
        save_auth();
        start.refreshed = true;
        return start;
    }

    http::Client http;
    std::map<std::string, std::string> query;
    const std::string raw = probe.value("query", "");
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t amp = raw.find('&', pos);
        std::string pair = raw.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            query[percent_decode(pair.substr(0, eq))] = percent_decode(pair.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    query["theme"] = "";
    query["bizDeviceType"] = "";
    query["_hasLogo"] = "false";
    query["_qrsize"] = "240";
    query["_dc"] = std::to_string(now_ms());

    http::Headers headers{
        {"User-Agent", auth_.value("ua", "")},
        {"Accept-Encoding", "identity"},
        {"Content-Type", "application/x-www-form-urlencoded"},
        {"Connection", "keep-alive"},
    };
    auto resp = http.get(std::string(kLoginUrl) + "?" + join_query(query), headers);
    json data = parse_json(resp.body, "扫码登录");
    if (!data.contains("loginUrl")) throw ApiError(-1, "未取到登录二维码，请检查网络后重试");

    pending_login_ = data;
    start.login_url = data.value("loginUrl", "");
    return start;
}

void Client::wait_login() {
    if (!pending_login_.contains("lp")) throw ApiError(-1, "登录流程未初始化");
    http::Client http;
    http::Headers headers{
        {"User-Agent", auth_.value("ua", "")},
        {"Accept-Encoding", "identity"},
        {"Content-Type", "application/x-www-form-urlencoded"},
        {"Connection", "keep-alive"},
    };
    auto lp = http.get(pending_login_.value("lp", ""), headers);
    json lp_data = parse_json(lp.body, "扫码结果");
    if (jll(lp_data, "code") != 0) {
        throw ApiError(static_cast<int>(jll(lp_data, "code")),
                       lp_data.value("message", std::string("扫码失败")));
    }

    std::string cookie;
    for (const auto& c : lp.set_cookies) {
        const size_t eq = c.find('=');
        if (eq == std::string::npos) continue;
        size_t end = c.find(';', eq);
        std::string key = c.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        const std::string value =
            c.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
        cookie += key + "=" + value + ";";
        auth_[key] = value;
    }
    for (const char* key : {"psecurity", "nonce", "ssecurity", "passToken", "userId", "cUserId"}) {
        if (lp_data.contains(key) && lp_data[key].is_string()) auth_[key] = lp_data[key];
    }

    const std::string callback = lp_data.value("location", "");
    if (callback.empty()) throw ApiError(-1, "扫码结果缺少回调地址");

    // 回调链路会 302 多次，serviceToken 等 cookie 往往挂在中间那一跳上。
    // WinHTTP 自动跟随会只保留最后一跳的响应头，因此这里手动跟随并逐跳收 Set-Cookie。
    auto merge_cookies = [this, &cookie](const std::vector<std::string>& jars) {
        for (const auto& c : jars) {
            const size_t eq = c.find('=');
            if (eq == std::string::npos) continue;
            size_t end = c.find(';', eq);
            std::string key = c.substr(0, eq);
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            if (key.empty()) continue;
            const std::string value =
                c.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
            auth_[key] = value;
            if (cookie.find(key + "=") == std::string::npos) cookie += key + "=" + value + ";";
        }
    };

    std::string url = callback;
    for (int hop = 0; hop < 6; ++hop) {
        http::Headers h = headers;
        h.push_back({"Cookie", cookie});
        auto r = http.get(url, h, /*follow=*/false);
        merge_cookies(r.set_cookies);
        if (!r.redirect()) break;
        url = resolve_url(url, r.location);
    }

    auth_["expireTime"] = now_ms() + 30LL * 24 * 3600 * 1000;
    // 少数情况下服务端不在回调里下发 serviceToken，靠一次 token 刷新补齐
    if (!authed() && service_login().value("ok", false)) {
        // service_login 已经把 cookie 合并进 auth_
    }
    save_auth();
    if (!authed()) throw ApiError(-1, "登录未完成（缺少服务凭证），请重试");
}

json Client::request(const std::string& uri, json data) {
    if (!authed()) throw ApiError(401, "尚未登录");

    http::Client http;
    json result;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string nonce = crypto::gen_nonce();
        const std::string ssecurity = auth_.value("ssecurity", "");
        const std::string signed_n = crypto::signed_nonce(ssecurity, nonce);

        std::vector<std::pair<std::string, std::string>> params{
            {"data", data.dump()},
        };
        params.push_back({"rc4_hash__", crypto::signature(uri, "POST", signed_n, params)});
        for (auto& [k, v] : params) v = crypto::rc4_encrypt_b64(signed_n, v);
        const std::string sig = crypto::signature(uri, "POST", signed_n, params);
        params.push_back({"signature", sig});
        params.push_back({"ssecurity", ssecurity});
        params.push_back({"_nonce", nonce});

        std::string body;
        for (const auto& [k, v] : params) {
            if (!body.empty()) body += "&";
            body += crypto::url_encode(k) + "=" + crypto::url_encode(v);
        }

        http::Headers headers{
            {"User-Agent", auth_.value("ua", "")},
            {"accept-encoding", "identity"},
            {"Content-Type", "application/x-www-form-urlencoded"},
            {"miot-accept-encoding", "identity"},
            {"miot-encrypt-algorithm", "ENCRYPT-RC4"},
            {"x-xiaomi-protocal-flag-cli", "PROTOCAL-HTTP2"},
            {"Cookie", cookie_header()},
        };
        auto resp = http.post_form(std::string(kApiBase) + uri, body, headers);
        // 计数点：真实发出去的一次 HTTP POST。重试（token 失效后重发）算两次，
        // 因为对端确实收到了两次 —— 限流看的是这个数。
        // 前 30 个逐个记（启动那一段，用来核对「打开程序到出数据发了多少」），
        // 之后每 10 个一记（稳定运行时 5 分钟才一个，逐个记只会刷满日志）。
        const long long sent = ++api_total_;
        if (api_logger_ && (sent <= 30 || sent % 10 == 0)) api_logger_(stats(), uri);

        json payload;
        try {
            payload = json::parse(resp.body);
        } catch (...) {
            // 服务端偶尔回密文：RC4 解一次再解析
            const std::string plain = crypto::decrypt_payload(ssecurity, nonce, resp.body);
            try {
                payload = json::parse(plain);
            } catch (...) {
                ++api_errors_;
                throw ApiError(-1, "云端返回内容无法解析（HTTP " + std::to_string(resp.status) + "）");
            }
        }

        const int code = static_cast<int>(jll(payload, "code"));
        if (code == 0 && payload.contains("result")) return payload["result"];
        if (code == 0) return payload;

        if (attempt == 0) {  // 可能是 token 过期，刷新后重试一次
            refresh_token();
            continue;
        }
        ++api_errors_;
        throw ApiError(code, payload.value("message", payload.value("desc", "云端返回错误")) +
                                " [" + uri + "]");
    }
    throw ApiError(-1, "请求失败");
}

json Client::homes() {
    json data{{"fg", true},     {"fetch_share", true}, {"fetch_share_dev", true},
              {"fetch_cariot", true}, {"limit", 300},     {"app_ver", 7},
              {"plat_form", 0}};
    auto ret = request("/v2/homeroom/gethome_merged", data);
    return ret.value("homelist", json::array());
}

json Client::home_list() {
    // 房间必须来自家庭信息本身：从设备反推会漏掉没有设备的房间
    const auto homelist = homes();
    json out = json::array();
    for (const auto& home : homelist) {
        json rooms = json::array();
        if (home.contains("roomlist") && home["roomlist"].is_array()) {
            for (const auto& room : home["roomlist"]) {
                rooms.push_back(json{{"id", jstr(room, "id")}, {"name", jstr(room, "name")}});
            }
        }
        out.push_back(json{{"id", jstr(home, "id")},
                           {"name", jstr(home, "name")},
                           {"uid", jll(home, "uid")},
                           {"rooms", rooms}});
    }
    return out;
}

json Client::home_devices(const std::string& home_id, long long owner_uid) {
    json all = json::array();
    std::string start_did;
    for (int page = 0; page < 20; ++page) {
        json data{{"home_owner", owner_uid},
                  {"home_id", std::stoll(home_id)},
                  {"limit", 200},
                  {"start_did", start_did},
                  {"get_split_device", true},
                  {"support_smart_home", true},
                  {"get_cariot_device", true},
                  {"get_third_device", true}};
        auto ret = request("/home/home_device_list", data);
        if (!ret.contains("device_info") || !ret["device_info"].is_array()) break;
        for (auto& d : ret["device_info"]) all.push_back(d);
        const std::string max_did = ret.value("max_did", "");
        const bool more = ret.value("has_more", false);
        if (!more || max_did.empty() || max_did == start_did) break;
        start_did = max_did;
    }
    return all;
}

json Client::shared_devices() {
    json data{{"ssid", "<unknown ssid>"},
              {"bssid", "02:00:00:00:00:00"},
              {"getVirtualModel", true},
              {"getHuamiDevices", 1},
              {"get_split_device", true},
              {"support_smart_home", true},
              {"get_cariot_device", true},
              {"get_third_device", true},
              {"get_phone_device", true},
              {"get_miwear_device", true}};
    auto ret = request("/v2/home/device_list_page", data);
    json out = json::array();
    if (!ret.contains("list")) return out;
    for (auto& d : ret["list"]) {
        if (d.value("owner", false)) out.push_back(d);
    }
    return out;
}

json Client::devices() {
    const auto homelist = homes();
    // 设备本身不带房间字段；归属关系只出现在 roomlist[].dids 里（含 .sN 虚拟子设备）
    std::map<std::string, std::string> room_of_did;
    std::map<std::string, std::string> room_id_of_did;
    json list = json::array();
    for (const auto& home : homelist) {
        const std::string home_id = jstr(home, "id");
        const long long owner = jll(home, "uid");
        const std::string home_name = jstr(home, "name");
        if (home.contains("roomlist") && home["roomlist"].is_array()) {
            for (const auto& room : home["roomlist"]) {
                const std::string rid = jstr(room, "id");
                const std::string rname = jstr(room, "name");
                if (room.contains("dids") && room["dids"].is_array()) {
                    for (const auto& did : room["dids"]) {
                        if (!did.is_string()) continue;
                        const std::string key = did.get<std::string>();
                        room_of_did[key] = rname;
                        room_id_of_did[key] = rid;
                    }
                }
            }
        }
        auto items = home_devices(home_id, owner);
        for (auto& d : items) {
            d["home_id"] = home_id;
            d["home_name"] = home_name;
        }
        for (auto& d : items) list.push_back(d);
    }
    for (auto& d : shared_devices()) {
        d["home_id"] = "shared";
        d["home_name"] = "共享设备";
        list.push_back(d);
    }

    if (proto_debug()) {
        std::ofstream h(data_dir_ / "proto.log", std::ios::app | std::ios::binary);
        h << "\n=== homelist ===\n" << homelist.dump(2).substr(0, 3000) << "\n";
    }
    if (proto_debug() && !list.empty()) {
        std::ofstream log(data_dir_ / "proto.log", std::ios::app | std::ios::binary);
        log << "\n=== sample device ===\n" << list.front().dump(2).substr(0, 2000) << "\n";
        std::ofstream log2(data_dir_ / "proto.log", std::ios::app | std::ios::binary);
        log2 << "=== did -> room ===\n";
        for (const auto& [did, name] : room_of_did) log2 << did << " -> " << name << "\n";
        // 全量清单：排查「同一盏灯是否拆成多个设备/多条通道」时用
        std::ofstream log3(data_dir_ / "proto.log", std::ios::app | std::ios::binary);
        log3 << "=== all devices ===\n";
        for (const auto& d : list) {
            log3 << jstr(d, "did") << " | " << jstr(d, "name") << " | " << jstr(d, "model") << " | "
                 << jstr(d, "spec_type") << "\n";
        }
        // VICOLD_PROBE=<did> 时扫描该设备的全部 siid/piid，确认有没有规格外的通道
        char probe[256]{};
        if (GetEnvironmentVariableA("VICOLD_PROBE", probe, 256) > 0) {
            // 支持逗号分隔多个 did
            std::string all(probe);
            size_t pos = 0;
            while (pos <= all.size()) {
                const size_t comma = all.find(',', pos);
                const std::string one = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                probe_props(one, 12, 12);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
    }

    json out = json::array();
    for (auto& d : list) {
        const std::string did = jstr(d, "did");
        if (did.empty()) continue;
        std::string spec_type = jstr(d, "spec_type");
        if (spec_type.empty() && d.contains("extra") && d["extra"].is_object()) {
            spec_type = jstr(d["extra"], "spec_type");
        }
        json item{{"did", did},
                  {"name", jstr(d, "name")},
                  {"model", jstr(d, "model")},
                  {"spec_type", spec_type},
                  {"online", jbool(d, "isOnline")},
                  {"home_id", jstr(d, "home_id")},
                  {"home_name", jstr(d, "home_name")},
                  {"room_id", jstr(d, "room_id")},
                  {"room_name", jstr(d, "room_name")}};
        auto rit = room_of_did.find(did);
        if (rit != room_of_did.end()) {
            item["room_name"] = rit->second;
            item["room_id"] = room_id_of_did[did];
        }
        out.push_back(item);
    }
    return out;
}

json Client::device_spec(const std::string& spec_type) {
    if (spec_type.empty()) throw ApiError(-1, "设备缺少 spec_type，无法读取规格");

    const auto cache_dir = data_dir_ / "spec";
    const auto cache_file = cache_dir / spec_cache_name(spec_type);
    if (std::filesystem::exists(cache_file)) {
        try {
            std::ifstream in(cache_file, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return flatten_spec(json::parse(text));
        } catch (...) {
            // 缓存坏了就重新拉
        }
    }

    http::Client http;
    auto resp = http.get(std::string(kSpecUrl) + "?type=" + crypto::url_encode(spec_type),
                         {{"User-Agent", "Vicold.MIHome/0.1"}});
    if (!resp.ok()) throw ApiError(-1, "获取设备规格失败（HTTP " + std::to_string(resp.status) + "）");
    json content = parse_json(resp.body, "设备规格");
    if (!content.contains("services")) throw ApiError(-1, "设备规格内容异常");

    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    std::ofstream out(cache_file, std::ios::binary | std::ios::trunc);
    out << content.dump(2);

    return flatten_spec(content);
}

json Client::get_props(const json& items) {
    // 云端要求 {"params": [...], "datasource": 1}，直接传数组会返回 code -8
    json params = items.is_array() ? items : json::array({items});
    return request("/miotspec/prop/get", json{{"params", params}, {"datasource", 1}});
}

void Client::probe_props(const std::string& did, int siid_max, int piid_max) {
    if (did.empty()) return;
    json items = json::array();
    for (int s = 1; s <= siid_max; ++s) {
        for (int p = 1; p <= piid_max; ++p) {
            items.push_back({{"did", did}, {"siid", s}, {"piid", p}});
        }
    }
    json hits = json::array();
    try {
        auto ret = get_props(items);
        if (ret.is_array()) {
            for (auto& row : ret) {
                if (row.value("code", -1) == 0) hits.push_back(row);
            }
        }
    } catch (const std::exception& e) {
        hits.push_back(json{{"error", e.what()}});
    }
    std::ofstream log(data_dir_ / "proto.log", std::ios::app | std::ios::binary);
    log << "\n=== probe " << did << " (siid<=" << siid_max << ", piid<=" << piid_max << ") ===\n"
        << hits.dump(2) << "\n";
}

json Client::history_series(const std::string& did, int siid, int piid, long long from, long long to,
                            int max_pages) {
    // 云端每点都是 `[值]` 这种字符串（"[21]" / "[false]"），取第一个数字
    auto pick_number = [](const json& v) -> double {
        if (v.is_number()) return v.get<double>();
        if (v.is_array()) {
            if (!v.empty() && v[0].is_number()) return v[0].get<double>();
            return std::nan("");
        }
        if (v.is_string()) {
            const std::string s = v.get<std::string>();
            try {
                const json j = json::parse(s);
                if (j.is_number()) return j.get<double>();
                if (j.is_array() && !j.empty() && j[0].is_number()) return j[0].get<double>();
            } catch (...) {
                // 不是 JSON，退化成直接解析数字
            }
            try {
                return std::stod(s);
            } catch (...) {
            }
        }
        return std::nan("");
    };

    const std::string key = std::to_string(siid) + "." + std::to_string(piid);
    std::map<long long, double> by_ts;  // 翻页可能重叠，按时间戳收一遍
    std::string error;
    long long cursor = to;
    for (int page = 0; page < max_pages && cursor > from; ++page) {
        json rows;
        try {
            rows = request("/user/get_user_device_data",
                           json{{"did", did},
                                {"uid", auth_.value("userId", "")},
                                {"type", "prop"},
                                {"key", key},
                                {"time_start", from},
                                {"time_end", cursor},
                                {"limit", 1000}});
        } catch (const ApiError& e) {
            error = e.message();
            break;  // 某一段拿不到就保留已拿到的，回填不是关键路径
        }
        if (!rows.is_array() || rows.empty()) break;
        long long oldest = cursor;
        size_t usable = 0;
        for (const auto& row : rows) {
            if (!row.is_object() || !row.contains("time")) continue;
            const long long ts = row["time"].is_number() ? row["time"].get<long long>() : 0;
            if (ts <= 0) continue;
            const double v = pick_number(row.value("value", json()));
            if (!std::isfinite(v)) continue;
            by_ts[ts] = v;
            oldest = std::min(oldest, ts);
            ++usable;
        }
        // 没有有效点或没往前挪，翻页就到此为止，别死循环
        if (usable == 0 || oldest >= cursor) break;
        cursor = oldest - 1;
    }

    json points = json::array();
    for (const auto& [ts, v] : by_ts) {
        if (ts < from || ts > to) continue;
        points.push_back(json::array({ts, v}));
    }
    return json{{"points", std::move(points)}, {"error", error}};
}

json Client::set_prop(const std::string& did, int siid, int piid, json value) {
    json data{{"params", json::array({{{"did", did}, {"siid", siid}, {"piid", piid}, {"value", std::move(value)}}})}};
    return request("/miotspec/prop/set", data);
}
json Client::run_action(const std::string& did, int siid, int aiid, json params) {
    json item{{"did", did}, {"siid", siid}, {"aiid", aiid}};
    if (!params.is_null() && !(params.is_array() && params.empty())) item["value"] = params;
    return request("/miotspec/action", json{{"params", item}});
}

json Client::scenes() {
    const auto homelist = homes();
    json out = json::array();
    for (const auto& home : homelist) {
        const std::string home_id = home.value("id", "");
        json data{{"app_version", 12},
                  {"get_type", 2},
                  {"home_id", home_id},
                  {"owner_uid", jll(home, "uid")}};
        auto ret = request("/appgateway/miot/appsceneservice/AppSceneService/GetSimpleSceneList", data);
        if (!ret.contains("manual_scene_info_list")) continue;
        for (auto& s : ret["manual_scene_info_list"]) {
            s["home_id"] = home_id;
            s["owner_uid"] = jll(home, "uid");
            out.push_back(s);
        }
    }
    return out;
}

void Client::run_scene(const std::string& scene_id, const std::string& home_id, long long owner_uid) {
    json data{{"scene_id", scene_id},
              {"scene_type", 2},
              {"phone_id", "null"},
              {"home_id", home_id},
              {"owner_uid", owner_uid}};
    request("/appgateway/miot/appsceneservice/AppSceneService/NewRunScene", data);
}

// ----------------------------------------------------------------------------
// 规格展平：把 miot-spec 的 services/properties/actions 变成可直接渲染的列表
// ----------------------------------------------------------------------------
json flatten_spec(const json& spec) {
    json out{{"name", spec.value("description", "")},
             {"spec_type", spec.value("type", "")},
             {"properties", json::array()},
             {"actions", json::array()},
             {"services", json::array()}};
    if (!spec.contains("services") || !spec["services"].is_array()) return out;

    auto type_short = [](const std::string& urn) -> std::string {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= urn.size()) {
            size_t colon = urn.find(':', start);
            parts.push_back(urn.substr(start, colon == std::string::npos ? std::string::npos
                                                                        : colon - start));
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
        return parts.size() > 3 ? parts[3] : urn;
    };

    for (const auto& svc : spec["services"]) {
        const std::string svc_type = svc.value("type", "");
        std::vector<std::string> sp;
        {
            size_t start = 0;
            while (start <= svc_type.size()) {
                size_t colon = svc_type.find(':', start);
                sp.push_back(svc_type.substr(start, colon == std::string::npos ? std::string::npos
                                                                              : colon - start));
                if (colon == std::string::npos) break;
                start = colon + 1;
            }
        }
        if (sp.size() > 3 && sp[3] == "device-information") continue;

        const int siid = svc.value("iid", 0);
        // 服务（功能模块）的中英文名，界面按它分组显示
        const std::string svc_name = sp.size() > 3 ? sp[3] : svc_type;
        const std::string svc_zh = svc.value("comment", "");
        out["services"].push_back(json{{"siid", siid},
                                       {"name", svc_name},
                                       {"desc_zh", svc_zh},
                                       {"description", svc.value("description", "")}});

        if (svc.contains("properties") && svc["properties"].is_array()) {
            for (const auto& prop : svc["properties"]) {
                std::string format = prop.value("format", "");
                std::string type = format;
                if (format.starts_with("int")) type = "int";
                else if (format.starts_with("uint")) type = "uint";

                std::string rw;
                if (prop.contains("access") && prop["access"].is_array()) {
                    for (const auto& a : prop["access"]) {
                        const std::string s = a.get<std::string>();
                        if (s == "read") rw += "r";
                        if (s == "write") rw += "w";
                    }
                }
                // 官方 comment 字段是中文，description 是英文；两者分开存，
                // 前端优先用中文，缺失时再回退到前端兜底字典。
                const std::string desc_en = prop.value("description", "");
                const std::string desc_zh = prop.value("comment", "");
                std::string desc = desc_en;
                if (!desc_zh.empty()) desc += " / " + desc_zh;

                json item{{"name", type_short(prop.value("type", ""))},
                          {"base_name", type_short(prop.value("type", ""))},
                          {"description", desc},
                          {"desc_en", desc_en},
                          {"desc_zh", desc_zh},
                          {"service", svc_name},
                          {"service_zh", svc_zh},
                          {"type", type},
                          {"rw", rw},
                          {"range", prop.contains("value-range") ? prop["value-range"] : json(nullptr)},
                          {"value_list", nullptr},
                          {"siid", siid},
                          {"piid", prop.value("iid", 0)}};
                if (prop.contains("value-list") && prop["value-list"].is_array()) {
                    json vl = json::array();
                    for (const auto& v : prop["value-list"]) {
                        json e{{"value", v.value("value", 0)},
                               {"description", v.value("description", "")}};
                        if (v.contains("comment")) e["desc_zh"] = v["comment"];
                        vl.push_back(e);
                    }
                    item["value_list"] = vl;
                }
                out["properties"].push_back(item);
            }
        }
        if (svc.contains("actions") && svc["actions"].is_array()) {
            for (const auto& act : svc["actions"]) {
                const std::string desc_en = act.value("description", "");
                const std::string desc_zh = act.value("comment", "");
                std::string desc = desc_en;
                if (!desc_zh.empty()) desc += " / " + desc_zh;
                out["actions"].push_back(json{{"name", type_short(act.value("type", ""))},
                                              {"description", desc},
                                              {"desc_en", desc_en},
                                              {"desc_zh", desc_zh},
                                              {"service", svc_name},
                                              {"service_zh", svc_zh},
                                              {"siid", siid},
                                              {"aiid", act.value("iid", 0)}});
            }
        }
    }

    // 同名属性/动作加 siid 后缀去重（与上游一致）
    auto dedupe = [](json& items, const char* id_key) {
        std::map<std::string, int> counts;
        for (auto& it : items) counts[it.value("name", "")]++;
        for (auto& it : items) {
            if (counts[it.value("name", "")] > 1) {
                it["name"] = it.value("name", "") + "-" + std::to_string(it.value("siid", 0));
            }
        }
        std::map<std::string, int> counts2;
        for (auto& it : items) counts2[it.value("name", "")]++;
        for (auto& it : items) {
            if (counts2[it.value("name", "")] > 1) {
                it["name"] = it.value("name", "") + "-" + std::to_string(it.value(id_key, 0));
            }
        }
    };
    dedupe(out["properties"], "piid");
    dedupe(out["actions"], "aiid");
    return out;
}

}  // namespace vc::mijia
