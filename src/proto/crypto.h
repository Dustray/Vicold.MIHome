// SPDX-License-Identifier: GPL-3.0-or-later
// Vicold.MIHome —— 米家桌面控制端（C++ / WebView2 / Vue）
// 标准件封装：base64、SHA1/SHA256、RC4、URL 编码。
// 不引入 OpenSSL：哈希走 Windows CNG（bcrypt），RC4 自实现（40 行）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vc::crypto {

using Bytes = std::vector<uint8_t>;

std::string b64enc(const Bytes& in);
std::string b64enc(std::string_view in);
Bytes b64dec(std::string_view in);

Bytes sha1(const Bytes& in);
Bytes sha256(const Bytes& in);

// ARC4；drop 为丢弃的初始密钥流字节数（小米协议固定丢弃 1024 字节）
Bytes rc4_crypt(const Bytes& key, const Bytes& data, size_t drop);

std::string random_hex(size_t n);

// ---- 小米签名协议 ----
// nonce = base64(8 字节有符号随机数 ‖ 由毫秒/60000 得到的变长整数)
std::string gen_nonce();
// signed_nonnce = base64(SHA256(base64dec(ssecurity) ‖ base64dec(nonce)))
std::string signed_nonce(const std::string& ssecurity_b64, const std::string& nonce_b64);
// 用 signedNonce 作 RC4 密钥加密，输出 base64
std::string rc4_encrypt_b64(const std::string& signed_nonce_b64, std::string_view plain);
// 解密服务端返回：RC4 → UTF-8
std::string decrypt_payload(const std::string& ssecurity_b64, const std::string& nonce_b64,
                            std::string_view payload_b64);
// base64(SHA1("METHOD&uri&k1=v1&...&signedNonce"))
std::string signature(const std::string& uri, const std::string& method,
                      const std::string& signed_nonce_b64,
                      const std::vector<std::pair<std::string, std::string>>& params);

std::string url_encode(std::string_view s);

}  // namespace vc::crypto
