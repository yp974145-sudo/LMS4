#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace lms::crypto {

// ============================================
// SHA-256 — FIPS 180-4 兼容实现
// 纯头文件，零依赖，~200 行
// ============================================

namespace detail {

static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t bsig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t bsig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t ssig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t ssig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

// 核心 SHA-256 压缩（输入完整填充后的消息，输出 32 字节哈希）
inline void sha256_core(const uint8_t* msg, size_t msg_len, uint8_t out[32]) {
    uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t bi = 0; bi < msg_len; bi += 64) {
        uint32_t W[64] = {};
        for (int t = 0; t < 16; ++t) {
            W[t] = (static_cast<uint32_t>(msg[bi + t * 4]) << 24) |
                   (static_cast<uint32_t>(msg[bi + t * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[bi + t * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[bi + t * 4 + 3]));
        }
        for (int t = 16; t < 64; ++t)
            W[t] = ssig1(W[t-2]) + W[t-7] + ssig0(W[t-15]) + W[t-16];

        uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

        for (int t = 0; t < 64; ++t) {
            uint32_t T1 = h + bsig1(e) + ch(e,f,g) + K[t] + W[t];
            uint32_t T2 = bsig0(a) + maj(a,b,c);
            h = g; g = f; f = e; e = d + T1;
            d = c; c = b; b = a; a = T1 + T2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }

    for (int i = 0; i < 8; ++i) {
        out[i*4]   = static_cast<uint8_t>(H[i] >> 24);
        out[i*4+1] = static_cast<uint8_t>(H[i] >> 16);
        out[i*4+2] = static_cast<uint8_t>(H[i] >> 8);
        out[i*4+3] = static_cast<uint8_t>(H[i]);
    }
}

} // namespace detail

// ---- 公开 API ----

// SHA-256 输入字符串，返回 hex 字符串
inline std::string sha256(const std::string& input) {
    uint64_t bit_len = input.size() * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

    uint8_t hash[32];
    detail::sha256_core(msg.data(), msg.size(), hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) oss << std::setw(2) << static_cast<int>(hash[i]);
    return oss.str();
}

// SHA-256 输入原始字节，返回 32 字节 raw hash
inline std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    uint64_t bit_len = len * 8;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

    std::array<uint8_t, 32> hash{};
    detail::sha256_core(msg.data(), msg.size(), hash.data());
    return hash;
}

// ============================================
// HMAC-SHA256 — RFC 2104
// ============================================

// Raw 字节输出（用于 JWT 签名等）
inline std::array<uint8_t, 32> hmac_sha256_raw(const uint8_t* key, size_t key_len,
                                                 const uint8_t* msg, size_t msg_len) {
    constexpr size_t BLOCK_SIZE = 64;
    uint8_t key_block[BLOCK_SIZE] = {};

    if (key_len > BLOCK_SIZE) {
        auto kh = sha256_raw(key, key_len);
        std::memcpy(key_block, kh.data(), 32);
    } else {
        std::memcpy(key_block, key, key_len);
    }

    uint8_t ipad[BLOCK_SIZE], opad[BLOCK_SIZE];
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    std::vector<uint8_t> inner(ipad, ipad + BLOCK_SIZE);
    inner.insert(inner.end(), msg, msg + msg_len);
    auto inner_hash = sha256_raw(inner.data(), inner.size());

    std::vector<uint8_t> outer(opad, opad + BLOCK_SIZE);
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    return sha256_raw(outer.data(), outer.size());
}

// Hex 字符串输出
inline std::string hmac_sha256(const std::string& key, const std::string& message) {
    auto raw = hmac_sha256_raw(
        reinterpret_cast<const uint8_t*>(key.data()), key.size(),
        reinterpret_cast<const uint8_t*>(message.data()), message.size());

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : raw) oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

} // namespace lms::crypto
