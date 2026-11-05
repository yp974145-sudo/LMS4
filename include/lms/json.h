#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <cmath>

namespace lms {

// ========== JSON 类型 ==========
class Json {
public:
    enum Type { Null, Bool, Int, Double, String, Array, Object };

    using array_t  = std::vector<Json>;
    using object_t = std::map<std::string, Json>;

private:
    using value_t = std::variant<std::nullptr_t, bool, int64_t, double, std::string, array_t, object_t>;
    value_t value_;

public:
    // 构造
    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool v) : value_(v) {}
    Json(int v) : value_(static_cast<int64_t>(v)) {}
    Json(int64_t v) : value_(v) {}
    Json(double v) : value_(v) {}
    Json(const char* v) : value_(std::string(v)) {}
    Json(const std::string& v) : value_(v) {}
    Json(std::string&& v) : value_(std::move(v)) {}
    Json(const array_t& v) : value_(v) {}
    Json(array_t&& v) : value_(std::move(v)) {}
    Json(const object_t& v) : value_(v) {}
    Json(object_t&& v) : value_(std::move(v)) {}

    // 类型判断
    Type type() const {
        switch (value_.index()) {
            case 0: return Null;
            case 1: return Bool;
            case 2: return Int;
            case 3: return Double;
            case 4: return String;
            case 5: return Array;
            case 6: return Object;
            default: return Null;
        }
    }

    bool is_null()    const { return type() == Null; }
    bool is_bool()    const { return type() == Bool; }
    bool is_int()     const { return type() == Int; }
    bool is_double()  const { return type() == Double; }
    bool is_number()  const { return is_int() || is_double(); }
    bool is_string()  const { return type() == String; }
    bool is_array()   const { return type() == Array; }
    bool is_object()  const { return type() == Object; }

    // 取值
    bool as_bool() const { return std::get<bool>(value_); }
    int64_t as_int() const {
        if (is_int()) return std::get<int64_t>(value_);
        if (is_double()) return static_cast<int64_t>(std::get<double>(value_));
        throw std::runtime_error("JSON: not a number");
    }
    double as_double() const {
        if (is_double()) return std::get<double>(value_);
        if (is_int()) return static_cast<double>(std::get<int64_t>(value_));
        throw std::runtime_error("JSON: not a number");
    }
    const std::string& as_string() const { return std::get<std::string>(value_); }
    const array_t& as_array() const { return std::get<array_t>(value_); }
    const object_t& as_object() const { return std::get<object_t>(value_); }

    // 访问
    Json& operator[](size_t idx) { return std::get<array_t>(value_)[idx]; }
    const Json& operator[](size_t idx) const { return std::get<array_t>(value_)[idx]; }
    Json& operator[](const char* key) { return std::get<object_t>(value_)[std::string(key)]; }
    const Json& operator[](const char* key) const {
        static Json nil;
        auto& obj = std::get<object_t>(value_);
        auto it = obj.find(std::string(key));
        return it != obj.end() ? it->second : nil;
    }
    Json& operator[](const std::string& key) { return std::get<object_t>(value_)[key]; }
    const Json& operator[](const std::string& key) const {
        static Json nil;
        auto& obj = std::get<object_t>(value_);
        auto it = obj.find(key);
        return it != obj.end() ? it->second : nil;
    }

    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        return std::get<object_t>(value_).count(key) > 0;
    }

    // 序列化
    std::string dump() const;

    // 反序列化
    static Json parse(const std::string& src);

    // 便捷工厂
    static Json make_object(std::initializer_list<std::pair<const char*, Json>> items) {
        object_t obj;
        for (auto& [k, v] : items) obj[std::string(k)] = v;
        return Json(std::move(obj));
    }

    static Json make_array(std::initializer_list<Json> items) {
        return Json(array_t(items));
    }

    // push_back for arrays
    void push_back(const Json& item) {
        std::get<array_t>(value_).push_back(item);
    }

    // 隐式转换
    operator bool() const { return !is_null(); }

private:
    static void dump_value(const Json& j, std::ostringstream& oss);
    static std::string escape(const std::string& s);

    // 解析器
    struct Parser {
        const char* p;
        const char* end;

        explicit Parser(const std::string& src) : p(src.c_str()), end(p + src.size()) {}

        void skip_ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

        Json parse_value() {
            skip_ws();
            if (p >= end) throw std::runtime_error("JSON: unexpected EOF");
            switch (*p) {
                case '"': return parse_string();
                case '{': return parse_object();
                case '[': return parse_array();
                case 't': case 'f': return parse_bool();
                case 'n': return parse_null();
                default:  return parse_number();
            }
        }

        Json parse_string() {
            ++p; // skip opening "
            std::string result;
            while (p < end) {
                if (*p == '"') { ++p; return result; }
                if (*p == '\\') {
                    ++p;
                    if (p >= end) throw std::runtime_error("JSON: bad escape");
                    switch (*p) {
                        case '"':  result += '"';  break;
                        case '\\': result += '\\'; break;
                        case '/':  result += '/';  break;
                        case 'b':  result += '\b'; break;
                        case 'f':  result += '\f'; break;
                        case 'n':  result += '\n'; break;
                        case 'r':  result += '\r'; break;
                        case 't':  result += '\t'; break;
                        case 'u': {
                            // \uXXXX — 取低 16 位直接输出（简化处理，不转 UTF-8 到码点）
                            if (p + 4 >= end) throw std::runtime_error("JSON: bad unicode escape");
                            char hex[5] = {p[1], p[2], p[3], p[4], 0};
                            wchar_t cp = static_cast<wchar_t>(strtol(hex, nullptr, 16));
                            // 简单输出为 UTF-8
                            if (cp <= 0x7F) result += static_cast<char>(cp);
                            else if (cp <= 0x7FF) {
                                result += static_cast<char>(0xC0 | (cp >> 6));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (cp >> 12));
                                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                            p += 4;
                            break;
                        }
                        default: throw std::runtime_error("JSON: bad escape char");
                    }
                } else {
                    result += *p;
                }
                ++p;
            }
            throw std::runtime_error("JSON: unterminated string");
        }

        Json parse_number() {
            const char* start = p;
            if (*p == '-') ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
            bool is_double = false;
            if (p < end && *p == '.') { is_double = true; ++p; while (p < end && *p >= '0' && *p <= '9') ++p; }
            if (p < end && (*p == 'e' || *p == 'E')) { is_double = true; ++p; if (*p == '+' || *p == '-') ++p; while (p < end && *p >= '0' && *p <= '9') ++p; }
            std::string num(start, p - start);
            char* endp = nullptr;
            if (is_double) return Json(strtod(num.c_str(), &endp));
            return Json(static_cast<int64_t>(strtoll(num.c_str(), &endp, 10)));
        }

        Json parse_object() {
            ++p; // skip '{'
            object_t obj;
            skip_ws();
            if (p < end && *p == '}') { ++p; return obj; }
            while (true) {
                skip_ws();
                std::string key = parse_string().as_string();
                skip_ws();
                if (p >= end || *p != ':') throw std::runtime_error("JSON: expected ':'");
                ++p;
                obj[key] = parse_value();
                skip_ws();
                if (p >= end) throw std::runtime_error("JSON: unterminated object");
                if (*p == '}') { ++p; return obj; }
                if (*p != ',') throw std::runtime_error("JSON: expected ',' or '}'");
                ++p;
            }
        }

        Json parse_array() {
            ++p; // skip '['
            array_t arr;
            skip_ws();
            if (p < end && *p == ']') { ++p; return arr; }
            while (true) {
                arr.push_back(parse_value());
                skip_ws();
                if (p >= end) throw std::runtime_error("JSON: unterminated array");
                if (*p == ']') { ++p; return arr; }
                if (*p != ',') throw std::runtime_error("JSON: expected ',' or ']'");
                ++p;
            }
        }

        Json parse_bool() {
            if (*p == 't') { if (p + 3 >= end || p[1] != 'r' || p[2] != 'u' || p[3] != 'e') throw std::runtime_error("JSON: bad bool"); p += 4; return true; }
            if (p + 4 >= end || p[1] != 'a' || p[2] != 'l' || p[3] != 's' || p[4] != 'e') throw std::runtime_error("JSON: bad bool");
            p += 5; return false;
        }

        Json parse_null() {
            if (p + 3 >= end || p[1] != 'u' || p[2] != 'l' || p[3] != 'l') throw std::runtime_error("JSON: bad null");
            p += 4; return nullptr;
        }
    };
};

// ========== 序列化实现 ==========
inline std::string Json::escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

inline void Json::dump_value(const Json& j, std::ostringstream& oss) {
    switch (j.type()) {
        case Null:
            oss << "null"; break;
        case Bool:
            oss << (j.as_bool() ? "true" : "false"); break;
        case Int:
            oss << j.as_int(); break;
        case Double:
            oss << j.as_double(); break;
        case String:
            oss << '"' << escape(j.as_string()) << '"'; break;
        case Array: {
            oss << '[';
            auto& arr = j.as_array();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) oss << ',';
                dump_value(arr[i], oss);
            }
            oss << ']';
            break;
        }
        case Object: {
            oss << '{';
            auto& obj = j.as_object();
            bool first = true;
            for (auto& [k, v] : obj) {
                if (!first) oss << ',';
                first = false;
                oss << '"' << escape(k) << "\":";
                dump_value(v, oss);
            }
            oss << '}';
            break;
        }
    }
}

inline std::string Json::dump() const {
    std::ostringstream oss;
    dump_value(*this, oss);
    return oss.str();
}

inline Json Json::parse(const std::string& src) {
    Parser parser(src);
    Json result = parser.parse_value();
    parser.skip_ws();
    if (parser.p < parser.end)
        throw std::runtime_error("JSON: trailing garbage");
    return result;
}

} // namespace lms
