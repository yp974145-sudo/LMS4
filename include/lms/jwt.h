#pragma once

#include "lms/crypto.h"
#include "lms/json.h"
#include <string>
#include <optional>
#include <ctime>
#include <array>

namespace lms::jwt {

// ============================================
// Base64URL 编解码（JWT 专用，无填充）
// ============================================
namespace {

constexpr char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

inline std::string b64url_encode(const std::string& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    uint32_t val = 0; int valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
    return out;
}

inline std::string b64url_decode(const std::string& data) {
    // 构建反向查找表
    static const auto rev = [] {
        std::array<int8_t, 256> table{};
        table.fill(-1);
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(b64_table[i])] = i;
        return table;
    }();

    std::string out;
    out.reserve((data.size() * 3) / 4);
    uint32_t val = 0; int valb = -8;
    for (uint8_t c : data) {
        int8_t idx = rev[c];
        if (idx == -1) throw std::invalid_argument("Invalid base64url");
        val = (val << 6) + idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

} // anonymous namespace

// ============================================
// JWT 令牌
// ============================================

struct Claims {
    std::string subject;   // 用户名
    int         role = 0;  // 角色 ID
    int64_t     iat  = 0;  // 签发时间 (unix)
    int64_t     exp  = 0;  // 过期时间 (unix)
};

// 创建 JWT（默认 24 小时过期）
inline std::string create(const Claims& claims, const std::string& secret) {
    auto now = static_cast<int64_t>(std::time(nullptr));

    // Header
    auto header = b64url_encode(R"({"alg":"HS256","typ":"JWT"})");

    // Payload
    Json payload = Json::make_object({
        {"sub", claims.subject},
        {"role", claims.role},
        {"iat", now},
        {"exp", now + 86400}  // 24 小时
    });
    auto payload_b64 = b64url_encode(payload.dump());

    // Signature: HMAC-SHA256(header.payload, secret) → raw bytes → base64url
    auto signing_input = header + "." + payload_b64;
    auto sig_raw = crypto::hmac_sha256_raw(
        reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
        reinterpret_cast<const uint8_t*>(signing_input.data()), signing_input.size());
    auto sig_b64 = b64url_encode(std::string(reinterpret_cast<char*>(sig_raw.data()), 32));

    return signing_input + "." + sig_b64;
}

// 验证并解析 JWT
inline std::optional<Claims> verify(const std::string& token, const std::string& secret) {
    // 分割 header.payload.signature
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    auto header_b64  = token.substr(0, dot1);
    auto payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    auto sig_b64     = token.substr(dot2 + 1);

    // 验证签名
    auto signing_input = header_b64 + "." + payload_b64;
    auto expected_raw = crypto::hmac_sha256_raw(
        reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
        reinterpret_cast<const uint8_t*>(signing_input.data()), signing_input.size());
    auto expected_b64 = b64url_encode(std::string(reinterpret_cast<char*>(expected_raw.data()), 32));

    if (sig_b64 != expected_b64) return std::nullopt;

    // 解析 payload JSON
    try {
        auto payload_json = b64url_decode(payload_b64);
        auto j = Json::parse(payload_json);

        Claims claims;
        claims.subject = j["sub"].as_string();
        claims.role    = static_cast<int>(j["role"].as_int());
        claims.iat     = j["iat"].as_int();
        claims.exp     = j["exp"].as_int();

        // 检查过期
        auto now = static_cast<int64_t>(std::time(nullptr));
        if (claims.subject.empty() || claims.role < 0 || claims.role > 3 || now >= claims.exp) return std::nullopt;

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

// 从 Authorization header 提取 Bearer token
inline std::optional<std::string> extract_bearer(const std::string& auth_header) {
    const std::string prefix = "Bearer ";
    if (auth_header.size() < prefix.size()) return std::nullopt;
    if (auth_header.substr(0, prefix.size()) != prefix) return std::nullopt;
    return auth_header.substr(prefix.size());
}

} // namespace lms::jwt
