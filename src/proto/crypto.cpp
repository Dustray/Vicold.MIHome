// SPDX-License-Identifier: GPL-3.0-or-later
#include "crypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <chrono>
#include <cstring>
#include <random>

#pragma comment(lib, "bcrypt.lib")

namespace vc::crypto {
namespace {

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

Bytes hash_with(LPCWSTR alg, const Bytes& in, size_t out_len) {
    BCRYPT_ALG_HANDLE handle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&handle, alg, nullptr, 0))) return {};
    Bytes out(out_len);
    NTSTATUS st = BCryptHash(handle, nullptr, 0,
                             const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()),
                             out.data(), static_cast<ULONG>(out.size()));
    BCryptCloseAlgorithmProvider(handle, 0);
    if (!BCRYPT_SUCCESS(st)) out.clear();
    return out;
}

std::mt19937_64& rng() {
    static std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

}  // namespace

std::string b64enc(const Bytes& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back(kB64[(n >> 6) & 63]);
        out.push_back(kB64[n & 63]);
    }
    if (i + 1 == in.size()) {
        uint32_t n = in[i] << 16;
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t n = (in[i] << 16) | (in[i + 1] << 8);
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back(kB64[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string b64enc(std::string_view in) {
    return b64enc(Bytes(in.begin(), in.end()));
}

Bytes b64dec(std::string_view s) {
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        std::memset(table, -1, sizeof(table));
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(kB64[i])] = static_cast<int8_t>(i);
        init = true;
    }
    Bytes out;
    out.reserve(s.size() / 4 * 3);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        int8_t v = table[static_cast<uint8_t>(c)];
        if (v < 0) continue;  // '=' 与换行直接跳过
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

Bytes sha1(const Bytes& in) { return hash_with(BCRYPT_SHA1_ALGORITHM, in, 20); }
Bytes sha256(const Bytes& in) { return hash_with(BCRYPT_SHA256_ALGORITHM, in, 32); }

Bytes rc4_crypt(const Bytes& key, const Bytes& data, size_t drop) {
    uint8_t s[256];
    for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);
    if (!key.empty()) {
        uint8_t j = 0;
        for (int i = 0; i < 256; ++i) {
            j = static_cast<uint8_t>(j + s[i] + key[i % key.size()]);
            std::swap(s[i], s[j]);
        }
    }
    uint8_t i = 0, j = 0;
    auto next = [&]() -> uint8_t {
        i = static_cast<uint8_t>(i + 1);
        j = static_cast<uint8_t>(j + s[i]);
        std::swap(s[i], s[j]);
        return s[static_cast<uint8_t>(s[i] + s[j])];
    };
    for (size_t k = 0; k < drop; ++k) next();
    Bytes out(data.size());
    for (size_t k = 0; k < data.size(); ++k) out[k] = static_cast<uint8_t>(data[k] ^ next());
    return out;
}

std::string random_hex(size_t n) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string s(n, '0');
    for (size_t i = 0; i < n; ++i) s[i] = hex[rng()() % 16];
    return s;
}

std::string gen_nonce() {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    // 8 字节有符号随机数
    Bytes b(8);
    uint64_t r = rng()();
    for (int i = 7; i >= 0; --i) {
        b[i] = static_cast<uint8_t>(r & 0xFF);
        r >>= 8;
    }
    // 分钟数，按 bit_length 决定字节数（与上游一致）
    uint64_t part2 = static_cast<uint64_t>(millis / 60000);
    size_t len = 1;
    while (part2 >> (len * 8)) ++len;
    for (size_t i = len; i > 0; --i) b.push_back(static_cast<uint8_t>((part2 >> ((i - 1) * 8)) & 0xFF));
    return b64enc(b);
}

std::string signed_nonce(const std::string& ssecurity_b64, const std::string& nonce_b64) {
    Bytes data = b64dec(ssecurity_b64);
    Bytes n = b64dec(nonce_b64);
    data.insert(data.end(), n.begin(), n.end());
    return b64enc(sha256(data));
}

std::string rc4_encrypt_b64(const std::string& signed_nonce_b64, std::string_view plain) {
    Bytes key = b64dec(signed_nonce_b64);
    Bytes in(plain.begin(), plain.end());
    return b64enc(rc4_crypt(key, in, 1024));
}

std::string decrypt_payload(const std::string& ssecurity_b64, const std::string& nonce_b64,
                            std::string_view payload_b64) {
    Bytes key = b64dec(signed_nonce(ssecurity_b64, nonce_b64));
    Bytes data = b64dec(payload_b64);
    Bytes plain = rc4_crypt(key, data, 1024);
    return std::string(plain.begin(), plain.end());
}

std::string signature(const std::string& uri, const std::string& method,
                      const std::string& signed_nonce_b64,
                      const std::vector<std::pair<std::string, std::string>>& params) {
    std::string s;
    s.reserve(256);
    for (char c : method) s.push_back(static_cast<char>(toupper(static_cast<unsigned char>(c))));
    s += "&";
    s += uri;
    for (const auto& [k, v] : params) {
        s += "&";
        s += k;
        s += "=";
        s += v;
    }
    s += "&";
    s += signed_nonce_b64;
    return b64enc(sha1(Bytes(s.begin(), s.end())));
}

std::string url_encode(std::string_view s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

}  // namespace vc::crypto
