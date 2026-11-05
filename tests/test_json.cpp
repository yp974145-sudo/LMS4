#include <gtest/gtest.h>
#include "lms/json.h"

using namespace lms;

// ============================================
// JSON 解析器测试
// ============================================

TEST(JsonParse, EmptyObject) {
    auto j = Json::parse("{}");
    EXPECT_TRUE(j.contains("non_existent") == false);
}

TEST(JsonParse, SimpleString) {
    auto j = Json::parse(R"({"name":"张三"})");
    EXPECT_EQ(j["name"].as_string(), "张三");
}

TEST(JsonParse, Integer) {
    auto j = Json::parse(R"({"age":25})");
    EXPECT_EQ(j["age"].as_int(), 25);
}

TEST(JsonParse, Double) {
    auto j = Json::parse(R"({"price":19.99})");
    EXPECT_DOUBLE_EQ(j["price"].as_double(), 19.99);
}

TEST(JsonParse, Boolean) {
    auto j = Json::parse(R"({"active":true,"deleted":false})");
    EXPECT_TRUE(j["active"].as_bool());
    EXPECT_FALSE(j["deleted"].as_bool());
}

TEST(JsonParse, Null) {
    auto j = Json::parse(R"({"data":null})");
    EXPECT_TRUE(j["data"].is_null());
}

TEST(JsonParse, NestedObject) {
    auto j = Json::parse(R"({"user":{"name":"李四","role":1}})");
    EXPECT_EQ(j["user"]["name"].as_string(), "李四");
    EXPECT_EQ(j["user"]["role"].as_int(), 1);
}

TEST(JsonParse, Array) {
    auto j = Json::parse(R"({"tags":["c++","backend","rest"]})");
    auto arr = j["tags"];
    EXPECT_EQ(arr.size(), 3);
    EXPECT_EQ(arr[0].as_string(), "c++");
    EXPECT_EQ(arr[1].as_string(), "backend");
    EXPECT_EQ(arr[2].as_string(), "rest");
}

TEST(JsonParse, UnicodeEscape) {
    auto j = Json::parse(R"({"emoji":"\u4f60\u597d"})");
    EXPECT_EQ(j["emoji"].as_string(), "你好");
}

TEST(JsonParse, ComplexNesting) {
    auto j = Json::parse(R"({
        "orders": [
            {"id": "WL001", "items": [{"name": "A", "qty": 2}]},
            {"id": "WL002", "items": [{"name": "B", "qty": 1}]}
        ],
        "total": 2
    })");
    EXPECT_EQ(j["orders"].size(), 2);
    EXPECT_EQ(j["orders"][0]["id"].as_string(), "WL001");
    EXPECT_EQ(j["orders"][0]["items"][0]["qty"].as_int(), 2);
    EXPECT_EQ(j["total"].as_int(), 2);
}

// ============================================
// JSON 序列化测试
// ============================================

TEST(JsonDump, SimpleObject) {
    auto j = Json::make_object({{"status", "ok"}});
    auto s = j.dump();
    EXPECT_NE(s.find("\"status\""), std::string::npos);
    EXPECT_NE(s.find("\"ok\""), std::string::npos);
}

TEST(JsonDump, ArrayRoundTrip) {
    auto j = Json::make_array({"a", "b", "c"});
    auto s = j.dump();
    auto parsed = Json::parse(s);
    EXPECT_EQ(parsed.size(), 3);
}

TEST(JsonDump, ObjectRoundTrip) {
    auto j = Json::make_object({
        {"name", "test"},
        {"count", 42},
        {"flag", true}
    });
    auto s = j.dump();
    auto parsed = Json::parse(s);
    EXPECT_EQ(parsed["name"].as_string(), "test");
    EXPECT_EQ(parsed["count"].as_int(), 42);
    EXPECT_TRUE(parsed["flag"].as_bool());
}

TEST(JsonParse, ErrorHandling) {
    // 格式错误的 JSON 应抛出异常
    EXPECT_THROW(Json::parse("{"), std::exception);
    EXPECT_THROW(Json::parse("["), std::exception);
    EXPECT_THROW(Json::parse("not json"), std::exception);
}
