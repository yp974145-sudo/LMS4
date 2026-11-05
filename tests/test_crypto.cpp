#include <gtest/gtest.h>
#include "lms/crypto.h"

using namespace lms::crypto;

// ============================================
// SHA-256 测试
// ============================================

TEST(Sha256, EmptyString) {
    // SHA-256("") 的已知值
    auto hash = sha256("");
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, KnownValue_Hello) {
    // SHA-256("hello") 的已知值
    auto hash = sha256("hello");
    EXPECT_EQ(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(Sha256, KnownValue_Chinese) {
    // SHA-256("你好") 的已知值
    auto hash = sha256("你好");
    EXPECT_EQ(hash, "670d9743542cae3ea7ebe36af56bd5368b0b112f4cb1f1fb42d097088e013fcb");
}

TEST(Sha256, Deterministic) {
    // 同一输入应产生相同输出
    auto h1 = sha256("test");
    auto h2 = sha256("test");
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 64); // 64 hex chars = 256 bits
}

TEST(Sha256, Avalanche) {
    // 微小输入变化应导致输出完全不同
    auto h1 = sha256("abc");
    auto h2 = sha256("abd");
    EXPECT_NE(h1, h2);
    // 至少有 50% 的 bit 不同
    int diff = 0;
    for (size_t i = 0; i < h1.size(); ++i)
        if (h1[i] != h2[i]) ++diff;
    EXPECT_GT(diff, static_cast<int>(h1.size() / 2));
}

TEST(Sha256, LongInput) {
    // 长输入也应正常工作（> 512 bits）
    std::string long_str(1000, 'x');
    auto hash = sha256(long_str);
    EXPECT_EQ(hash.size(), 64);
}

// ============================================
// HMAC-SHA256 测试
// ============================================

TEST(HmacSha256, Rfc4231_Test1) {
    // RFC 4231 Test Case 1
    auto key = std::string(20, '\x0b');
    auto msg = "Hi There";
    auto mac = hmac_sha256(key, msg);
    EXPECT_EQ(mac, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST(HmacSha256, Rfc4231_Test2) {
    // RFC 4231 Test Case 2: key = "Jefe", data = "what do ya want for nothing?"
    auto mac = hmac_sha256("Jefe", "what do ya want for nothing?");
    EXPECT_EQ(mac, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(HmacSha256, Deterministic) {
    auto m1 = hmac_sha256("secret", "message");
    auto m2 = hmac_sha256("secret", "message");
    EXPECT_EQ(m1, m2);
}

TEST(HmacSha256, DifferentKeyDifferentOutput) {
    auto m1 = hmac_sha256("key1", "msg");
    auto m2 = hmac_sha256("key2", "msg");
    EXPECT_NE(m1, m2);
}
