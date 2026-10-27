// SPDX-License-Identifier: GPL-3.0-or-later
#include "history.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace vc {

namespace {
// 保留 7 天；单条曲线最多 3000 点（超出按时间间隔抽稀）
// 3000 是给云端回填留的余量：云端点间隔约 37 秒，24 小时就有 ~2300 点，
// 上限设小了会把刚补回来的曲线又抽稀一遍。
constexpr long long kKeepSeconds = 7 * 24 * 3600;
constexpr size_t kMaxPoints = 3000;
// 同一设备同一指标，60 秒内的重复采样只保留最后一次
constexpr long long kMergeSeconds = 60;
}  // namespace

HistoryStore::HistoryStore(std::filesystem::path path) : path_(std::move(path)) {
    load();
}

void HistoryStore::load() {
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;
    try {
        json j = json::parse(in);
        for (const auto& [did, series] : j.items()) {
            if (!series.is_object()) continue;
            Series dev;
            for (const auto& [key, arr] : series.items()) {
                if (!arr.is_array()) continue;
                std::vector<Point> pts;
                for (const auto& p : arr) {
                    if (!p.is_array() || p.size() < 2) continue;
                    Point pt;
                    pt.ts = p[0].get<long long>();
                    pt.value = p[1].get<double>();
                    if (std::isfinite(pt.value)) pts.push_back(pt);
                }
                if (!pts.empty()) dev[key] = std::move(pts);
            }
            if (!dev.empty()) data_[did] = std::move(dev);
        }
    } catch (...) {
        // 文件坏了就当空库，别让历史功能拖垮整个应用
        data_.clear();
    }
}

void HistoryStore::prune_locked() {
    const long long now =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const long long cutoff = now - kKeepSeconds;
    for (auto& [did, dev] : data_) {
        for (auto& [key, pts] : dev) {
            if (!pts.empty() && pts.front().ts < cutoff) {
                pts.erase(std::remove_if(pts.begin(), pts.end(),
                                         [&](const Point& p) { return p.ts < cutoff; }),
                          pts.end());
            }
            // 超量时按时间间隔抽稀：每隔 step 个点保留一个
            if (pts.size() > kMaxPoints) {
                const size_t step = (pts.size() + kMaxPoints - 1) / kMaxPoints;
                std::vector<Point> kept;
                kept.reserve(kMaxPoints + 1);
                for (size_t i = 0; i < pts.size(); i += step) kept.push_back(pts[i]);
                if (!kept.empty() && kept.back().ts != pts.back().ts) kept.push_back(pts.back());
                pts = std::move(kept);
            }
        }
    }
}

void HistoryStore::append(const json& points) {
    if (!points.is_array() || points.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    const long long now =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    for (const auto& p : points) {
        if (!p.is_object()) continue;
        const std::string did = p.value("did", "");
        const std::string key = p.value("key", "");
        if (did.empty() || key.empty()) continue;
        double value = 0;
        if (p.contains("value") && p["value"].is_number()) value = p["value"].get<double>();
        else if (p.contains("value") && p["value"].is_string()) {
            try {
                value = std::stod(p["value"].get<std::string>());
            } catch (...) {
                continue;
            }
        } else {
            continue;
        }
        if (!std::isfinite(value)) continue;
        long long ts = p.contains("ts") && p["ts"].is_number_integer() ? p["ts"].get<long long>() : now;

        auto& dev = data_[did];
        auto& pts = dev[key];
        if (!pts.empty() && ts <= pts.back().ts + kMergeSeconds && ts >= pts.back().ts) {
            // 60 秒内的重复采样：覆盖最后一个点，不追加
            pts.back().value = value;
            pts.back().ts = ts;
        } else {
            pts.push_back(Point{ts, value});
        }
    }
    prune_locked();
    dirty_ = true;
    save_locked();
}

void HistoryStore::merge(const std::string& did, const std::string& key, const json& points) {
    if (did.empty() || key.empty() || !points.is_array() || points.empty()) return;
    std::vector<Point> incoming;
    incoming.reserve(points.size());
    for (const auto& p : points) {
        if (!p.is_array() || p.size() < 2) continue;
        if (!p[0].is_number_integer() && !p[0].is_number()) continue;
        if (!p[1].is_number()) continue;
        const auto ts = p[0].get<long long>();
        const double v = p[1].get<double>();
        if (ts <= 0 || !std::isfinite(v)) continue;
        incoming.push_back(Point{ts, v});
    }
    if (incoming.empty()) return;

    std::lock_guard<std::mutex> lock(mu_);
    auto& pts = data_[did][key];
    pts.insert(pts.end(), incoming.begin(), incoming.end());
    std::sort(pts.begin(), pts.end(),
              [](const Point& a, const Point& b) { return a.ts < b.ts; });
    // 同一秒只留一个（云端与本地采样撞上时以先到的为准，值本来同源）
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](const Point& a, const Point& b) { return a.ts == b.ts; }),
              pts.end());
    prune_locked();
    dirty_ = true;
    save_locked();
}

json HistoryStore::list(const std::vector<std::string>& dids, const std::string& key, long long since,
                        size_t limit) const {
    std::lock_guard<std::mutex> lock(mu_);
    json out = json::object();
    for (const auto& [d, dev] : data_) {
        if (!dids.empty() && std::find(dids.begin(), dids.end(), d) == dids.end()) continue;
        json devOut = json::object();
        for (const auto& [k, pts] : dev) {
            if (!key.empty() && k != key) continue;
            json arr = json::array();
            std::vector<const Point*> picked;
            picked.reserve(pts.size());
            for (const auto& p : pts) {
                if (since > 0 && p.ts < since) continue;
                picked.push_back(&p);
            }
            if (limit > 0 && picked.size() > limit) {
                // 均匀抽稀：首尾一定保留
                std::vector<const Point*> kept;
                kept.reserve(limit + 1);
                const size_t step = (picked.size() - 1) / (limit > 1 ? limit - 1 : 1);
                for (size_t i = 0; i < picked.size(); i += step) kept.push_back(picked[i]);
                if (kept.empty() || kept.back()->ts != picked.back()->ts) kept.push_back(picked.back());
                picked = std::move(kept);
            }
            for (const auto* p : picked) arr.push_back(json::array({p->ts, p->value}));
            if (!arr.empty()) devOut[k] = std::move(arr);
        }
        if (!devOut.empty()) out[d] = std::move(devOut);
    }
    return out;
}

void HistoryStore::clear(const std::string& did) {
    std::lock_guard<std::mutex> lock(mu_);
    if (did.empty()) {
        data_.clear();
    } else {
        data_.erase(did);
    }
    dirty_ = true;
    save_locked();
}

void HistoryStore::save_locked() {
    if (!dirty_) return;
    json out = json::object();
    for (const auto& [did, dev] : data_) {
        json devOut = json::object();
        for (const auto& [key, pts] : dev) {
            json arr = json::array();
            for (const auto& p : pts) arr.push_back(json::array({p.ts, p.value}));
            devOut[key] = std::move(arr);
        }
        out[did] = std::move(devOut);
    }
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream os(path_, std::ios::binary | std::ios::trunc);
    if (!os) return;
    os << out.dump();
    os.flush();
    dirty_ = false;
}

}  // namespace vc
