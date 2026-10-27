// SPDX-License-Identifier: GPL-3.0-or-later
// 设备产品图缓存：从 home.miot-spec.com 的产品页取 og:image，落盘到 img/。
//
// 为什么要在宿主里干这件事：图片地址带签名（GalaxyAccessKeyId/Signature），
// 没法按型号拼出来，只能先抓产品页再从 HTML 里取；而产品页跨域，
// 网页里 fetch 会被 CORS 拦掉，所以走宿主的 WinHTTP。
#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>

#include "json.hpp"

namespace vc::images {

using json = nlohmann::json;
/// 抓到一张就回调（model, 文件名）：前端据此逐张点亮卡片
using ReadyFn = std::function<void(const std::string&, const std::string&)>;

/// 图片缓存目录：<data>/img
std::filesystem::path dir(const std::filesystem::path& data_dir);

class Store {
public:
    explicit Store(std::filesystem::path data_dir);

    /// 已缓存的图片文件名（<model>.png），没缓存返回空串
    std::string cached(const std::string& model) const;

    /// 确保某个型号的图已落盘，返回文件名（失败返回空）
    std::string ensure(const std::string& model);

    /**
     * 批量补齐：models 去重后并发抓取。
     *
     * on_ready 每成功一张就调一次（在抓取线程上），前端可以边抓边显示；
     * 最后的回包仍然给全量 {"images": {"<model>": "<file>"}, "missing": [...]}，
     * 万一事件丢了也能靠回包补齐。
     */
    json sync(const json& models, const ReadyFn& on_ready = {});

private:
    std::filesystem::path dir_;
    /// 本次运行判定为「网上也没有」的型号，避免每次刷新都重抓一遍
    mutable std::mutex mu_;
    std::set<std::string> missing_;
};

}  // namespace vc::images
