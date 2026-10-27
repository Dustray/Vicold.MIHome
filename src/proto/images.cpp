// SPDX-License-Identifier: GPL-3.0-or-later
#include "images.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <regex>
#include <thread>
#include <vector>

#include "http.h"

namespace vc::images {
namespace {

constexpr const char* kPagePrefix = "https://home.miot-spec.com/p/";
// 产品页是普通网页，不带浏览器 UA 会被挡
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0 Safari/537.36 Vicold.MIHome/0.1";
/// 一次最多处理多少个型号（防止前端传疯）
constexpr size_t kMaxModels = 300;
/// 并发抓取数：页面 + 图片都是几百 KB，对方是静态站，8 路能明显缩短首次填充时间
/// （实测瓶颈在产品页服务端渲染，单页 2~30s 不等，图床本身只要几十毫秒）
constexpr size_t kWorkers = 8;
/// 记录「产品库里没有这个型号」的清单：型号不会凭空出现，不用每次启动都去问一遍
constexpr const char* kMissingFile = ".missing";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// 型号做文件名：只留 [A-Za-z0-9._-]，其余替换成下划线
std::string sanitize(const std::string& model) {
    std::string out;
    out.reserve(model.size());
    for (char c : model) {
        const auto uc = static_cast<unsigned char>(c);
        const bool keep = std::isalnum(uc) || c == '.' || c == '-' || c == '_';
        out.push_back(keep ? c : '_');
    }
    return out;
}

/// HTML 属性里的 URL 会把 & 写成 &amp;
std::string html_unescape(std::string s) {
    static const std::pair<const char*, const char*> kTable[] = {
        {"&amp;", "&"}, {"&#38;", "&"}, {"&quot;", "\""}, {"&#39;", "'"},
        {"&lt;", "<"},  {"&gt;", ">"},  {"&#x2F;", "/"}, {"&apos;", "'"}};
    for (const auto& [from, to] : kTable) {
        const size_t fl = std::strlen(from);
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, fl, to);
            pos += std::strlen(to);
        }
    }
    return s;
}

/**
 * 从产品页 HTML 里取图片地址。
 *
 * 首选 `og:image`（服务端渲染时一定是主图，且属性顺序无所谓，两种写法都试）；
 * 取不到就扫全文所有图片地址，优先 iotweb-product-center / miio.files 这两个
 * 小米图床，跳过站标（raw.githubusercontent）和推荐滤芯之类的拼多多广告图。
 */
std::string pick_image_url(const std::string& html) {
    static const std::regex kMetaPropFirst(
        R"(<meta[^>]+property\s*=\s*["']og:image["'][^>]*?content\s*=\s*["']([^"']+)["'])",
        std::regex::icase);
    static const std::regex kMetaContentFirst(
        R"(<meta[^>]+content\s*=\s*["']([^"']+)["'][^>]*?property\s*=\s*["']og:image["'])",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(html, m, kMetaPropFirst) && m.size() > 1) return html_unescape(m[1].str());
    if (std::regex_search(html, m, kMetaContentFirst) && m.size() > 1) return html_unescape(m[1].str());

    static const std::regex kAnyImage(
        R"(https://[^"'\s<>\\]+?\.(?:png|jpe?g|webp|gif)(?:\?[^"'\s<>\\]*)?)", std::regex::icase);
    std::string fallback;
    for (std::sregex_iterator it(html.begin(), html.end(), kAnyImage), end; it != end; ++it) {
        const std::string url = html_unescape(it->str());
        const std::string u = lower(url);
        if (u.find("raw.githubusercontent") != std::string::npos) continue;
        if (u.find("img.pddpic") != std::string::npos) continue;
        if (u.find("iotweb-product-center") != std::string::npos ||
            u.find("miio.files") != std::string::npos) {
            return url;
        }
        if (fallback.empty()) fallback = url;
    }
    return fallback;
}

/// 按文件头判扩展名：小米图床两种都有（.png / 带签名的 .png），也见过 jpeg
std::string sniff_ext(const std::string& body) {
    const auto* p = reinterpret_cast<const unsigned char*>(body.data());
    if (body.size() > 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') return "png";
    if (body.size() > 3 && p[0] == 0xFF && p[1] == 0xD8) return "jpg";
    if (body.size() > 4 && body.compare(0, 4, "GIF8") == 0) return "gif";
    if (body.size() > 12 && body.compare(0, 4, "RIFF") == 0 && body.compare(8, 4, "WEBP") == 0) {
        return "webp";
    }
    return "";
}

constexpr const char* kExts[] = {"png", "jpg", "webp", "gif"};

/// 抓图很慢的时候总得知道慢在哪一段：页面还是图床。写 <data>/img.log
void append_log(const std::filesystem::path& dir, const std::string& line) {
    static std::mutex mu;
    std::lock_guard lk(mu);
    std::ofstream out(dir.parent_path() / "img.log", std::ios::app | std::ios::binary);
    out << line << "\n";
}

long long ms_since(std::chrono::steady_clock::time_point t0) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

}  // namespace

std::filesystem::path dir(const std::filesystem::path& data_dir) {
    return data_dir / "img";
}

Store::Store(std::filesystem::path data_dir) : dir_(dir(data_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    // 上次已经确认「产品库里没有」的型号，别每次启动都重抓
    std::ifstream in(dir_ / kMissingFile, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) missing_.insert(line);
    }
}

std::string Store::cached(const std::string& model) const {
    if (model.empty()) return "";
    const std::string stem = sanitize(model);
    for (const char* ext : kExts) {
        const std::string name = stem + "." + ext;
        std::error_code ec;
        if (std::filesystem::exists(dir_ / name, ec)) return name;
    }
    return "";
}

std::string Store::ensure(const std::string& model) {
    if (model.empty()) return "";
    if (const auto have = cached(model); !have.empty()) return have;
    {
        std::lock_guard lk(mu_);
        if (missing_.count(model)) return "";
    }

    // persist=true 表示「已经问过、确实没有这张图」，写进 .missing 下次跳过；
    // 网络错误不写（断网和"没有这个型号"是两回事，网络恢复后还得重试）
    auto give_up = [&](bool persist) -> std::string {
        std::lock_guard lk(mu_);
        missing_.insert(model);
        if (persist) {
            std::ofstream out(dir_ / kMissingFile, std::ios::app | std::ios::binary);
            out << model << "\n";
        }
        return "";
    };

    try {
        http::Client http;
        const auto t0 = std::chrono::steady_clock::now();
        const auto page = http.get(std::string(kPagePrefix) + model, {{"User-Agent", kUserAgent}});
        const long long page_ms = ms_since(t0);
        // 型号不在产品库里就是 404：记成 missing，别再重复抓
        if (!page.ok()) {
            append_log(dir_, model + " page=" + std::to_string(page.status) + " " +
                                  std::to_string(page_ms) + "ms");
            return give_up(page.status == 404);
        }

        const auto url = pick_image_url(page.body);
        if (url.empty()) {
            append_log(dir_, model + " no-image-url " + std::to_string(page_ms) + "ms");
            return give_up(true);
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto img = http.get(url, {{"User-Agent", kUserAgent},
                                        {"Referer", "https://home.miot-spec.com/"}});
        const long long img_ms = ms_since(t1);
        if (!img.ok() || img.body.size() < 256) {
            append_log(dir_, model + " img=" + std::to_string(img.status) + " " +
                                  std::to_string(img_ms) + "ms");
            return give_up(false);
        }

        const auto ext = sniff_ext(img.body);
        if (ext.empty()) return give_up(false);

        const std::string name = sanitize(model) + "." + ext;
        const auto path = dir_ / name;
        auto tmp = path;
        tmp += ".tmp";

        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return give_up(false);
            out.write(img.body.data(), static_cast<std::streamsize>(img.body.size()));
            if (!out) return give_up(false);
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return give_up(false);
        }
        append_log(dir_, model + " ok page=" + std::to_string(page_ms) + "ms img=" +
                              std::to_string(img_ms) + "ms size=" + std::to_string(img.body.size()));
        return name;
    } catch (...) {
        // 断网 / TLS 失败都算这次拿不到，但**不记 missing**：
        // 网络恢复后下次刷新还应该重试（网络问题和"型号没有图"是两回事）
        return "";
    }
}

json Store::sync(const json& models, const ReadyFn& on_ready) {
    std::vector<std::string> wanted;
    if (models.is_array()) {
        for (const auto& m : models) {
            if (!m.is_string()) continue;
            const std::string model = m.get<std::string>();
            if (model.empty()) continue;
            if (std::find(wanted.begin(), wanted.end(), model) == wanted.end()) wanted.push_back(model);
            if (wanted.size() >= kMaxModels) break;
        }
    }

    json images = json::object();
    std::vector<std::string> todo;
    for (const auto& model : wanted) {
        if (const auto have = cached(model); !have.empty()) {
            images[model] = have;
            if (on_ready) on_ready(model, have);
        } else {
            todo.push_back(model);
        }
    }

    if (!todo.empty()) {
        std::atomic<size_t> next{0};
        std::mutex out_mu;
        const size_t workers = std::min(kWorkers, todo.size());
        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (size_t i = 0; i < workers; ++i) {
            pool.emplace_back([&]() {
                for (;;) {
                    const size_t idx = next.fetch_add(1);
                    if (idx >= todo.size()) return;
                    const std::string model = todo[idx];
                    const std::string name = ensure(model);
                    if (name.empty()) continue;
                    {
                        std::lock_guard lk(out_mu);
                        images[model] = name;
                    }
                    // 回调放锁外：往 UI 线程投消息，速度慢一点也不该卡住别的 worker
                    if (on_ready) on_ready(model, name);
                }
            });
        }
        for (auto& t : pool) t.join();
    }

    json missing = json::array();
    for (const auto& model : wanted) {
        if (!images.contains(model)) missing.push_back(model);
    }
    return json{{"images", images}, {"missing", missing}};
}

}  // namespace vc::images
