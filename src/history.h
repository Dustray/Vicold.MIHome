// SPDX-License-Identifier: GPL-3.0-or-later
// 本地历史库：把环境读数（温度/湿度/PM2.5/甲醛/CO2…）按时间序列存到磁盘，
// 供前端画折线图。
//
// 两级数据源：
//  1) 本机采样（append）—— 程序没运行时没有数据；
//  2) 云端回填（merge）—— /user/get_user_device_data 按 siid.piid 保留了
//     至少 30 天的读数（米家 App「历史记录」走的就是它），
//     关程序/关机那段空白由它补。
// 注意设备规格里自带的 read-history-data / record-data 仍然读不到（-704010000），
// 那是另一套东西，别混。
#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vc {

using json = nlohmann::json;

class HistoryStore {
public:
    explicit HistoryStore(std::filesystem::path path);

    struct Point {
        long long ts = 0;  // Unix 秒
        double value = 0;
    };

    // 批量追加采样点。key 是指标名（temperature / humidity / pm25 …）。
    void append(const json& points);
    // 批量并入云端回填的点（[[ts, value], ...]）。
    // 与 append 的区别：不做「60 秒内覆盖最后一点」的合并 —— 那是给实时采样去重的，
    // 拿来回填会把 37 秒一个的云端点整片吃掉。这里只按时间戳去重。
    void merge(const std::string& did, const std::string& key, const json& points);
    // 查询：dids 为空表示全部设备，key 为空表示全部指标，since 为 0 表示不限，
    // limit > 0 时对每条曲线均匀抽稀到不超过该点数。
    json list(const std::vector<std::string>& dids, const std::string& key, long long since,
              size_t limit) const;
    // 清除：did 为空表示清空全部。
    void clear(const std::string& did);

private:
    using Series = std::map<std::string, std::vector<Point>>;  // 指标 -> 点
    using Device = std::map<std::string, Series>;              // did -> 指标

    void load();
    void save_locked();
    void prune_locked();

    std::filesystem::path path_;
    mutable std::mutex mu_;
    Device data_;
    bool dirty_ = false;
};

}  // namespace vc
