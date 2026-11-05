#include <gtest/gtest.h>
#include "lms/jwt.h"

using namespace lms::jwt;

// ============================================
// JWT 创建与验证测试
// ============================================

static const std::string SECRET = "test-secret-key-2026";

TEST(Jwt, CreateAndVerify) {
    Claims claims{"admin", 0};
    auto token = create(claims, SECRET);

    // token 应包含两个点号（三段）
    EXPECT_NE(token.find('.'), std::string::npos);
    EXPECT_NE(token.rfind('.'), token.find('.'));

    auto verified = verify(token, SECRET);
    ASSERT_TRUE(verified.has_value());
    EXPECT_EQ(verified->subject, "admin");
    EXPECT_EQ(verified->role, 0);
}

TEST(Jwt, WrongSecret) {
    Claims claims{"admin", 0};
    auto token = create(claims, SECRET);

    auto verified = verify(token, "wrong-secret");
    EXPECT_FALSE(verified.has_value());
}

TEST(Jwt, TamperedToken) {
    auto token = create({"user1", 1}, SECRET);

    // 篡改 payload（修改最后一个字符）
    token[token.size() - 1] = (token[token.size() - 1] == 'A') ? 'B' : 'A';

    auto verified = verify(token, SECRET);
    EXPECT_FALSE(verified.has_value());
}

TEST(Jwt, ExtractBearer) {
    auto token = extract_bearer("Bearer eyJhbGciOiJIUzI1NiJ9.xxx.yyy");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "eyJhbGciOiJIUzI1NiJ9.xxx.yyy");
}

TEST(Jwt, ExtractBearer_NoPrefix) {
    auto token = extract_bearer("eyJhbGciOiJIUzI1NiJ9.xxx.yyy");
    EXPECT_FALSE(token.has_value());
}

TEST(Jwt, ExtractBearer_Empty) {
    auto token = extract_bearer("");
    EXPECT_FALSE(token.has_value());
}

TEST(Jwt, RolePreservation) {
    Claims claims{"storage", 1};
    auto token = create(claims, SECRET);
    auto verified = verify(token, SECRET);
    ASSERT_TRUE(verified.has_value());
    EXPECT_EQ(verified->role, 1);
}

TEST(Jwt, DifferentUsersDifferentTokens) {
    auto t1 = create({"admin", 0}, SECRET);
    auto t2 = create({"storage", 1}, SECRET);
    EXPECT_NE(t1, t2);

    auto v1 = verify(t1, SECRET);
    auto v2 = verify(t2, SECRET);
    EXPECT_EQ(v1->subject, "admin");
    EXPECT_EQ(v2->subject, "storage");
}
