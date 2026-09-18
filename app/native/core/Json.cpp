#include "Json.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace trinity::core {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Json parse() {
        skip_ws();
        Json value = parse_value(0);
        skip_ws();
        if (pos_ != text_.size()) fail("trailing characters after JSON value");
        return value;
    }

private:
    static constexpr int kMaxDepth = 128;

    void skip_ws() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("JSON parse error at offset ") +
                                 std::to_string(pos_) + ": " + message);
    }

    char peek() const {
        if (pos_ >= text_.size()) fail("unexpected end of input");
        return text_[pos_];
    }

    void expect(char c) {
        if (pos_ >= text_.size() || text_[pos_] != c) {
            std::string what = std::string("expected '") + c + "'";
            fail(what.c_str());
        }
        ++pos_;
    }

    Json parse_value(int depth) {
        if (depth > kMaxDepth) fail("maximum nesting depth exceeded");
        switch (peek()) {
            case '{': return parse_object(depth);
            case '[': return parse_array(depth);
            case '"': return Json(parse_string());
            case 't': return parse_literal("true", Json(true));
            case 'f': return parse_literal("false", Json(false));
            case 'n': return parse_literal("null", Json(nullptr));
            default: return parse_number();
        }
    }

    Json parse_literal(std::string_view literal, Json result) {
        if (text_.substr(pos_, literal.size()) != literal) fail("invalid literal");
        pos_ += literal.size();
        return result;
    }

    Json parse_object(int depth) {
        expect('{');
        JsonObject obj;
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            return Json(std::move(obj));
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            obj.emplace(std::move(key), parse_value(depth + 1));
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                break;
            }
            fail("expected ',' or '}' in object");
        }
        return Json(std::move(obj));
    }

    Json parse_array(int depth) {
        expect('[');
        JsonArray arr;
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            return Json(std::move(arr));
        }
        while (true) {
            skip_ws();
            arr.push_back(parse_value(depth + 1));
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                break;
            }
            fail("expected ',' or ']' in array");
        }
        return Json(std::move(arr));
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) fail("unterminated escape");
                char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': out.append(parse_unicode_escape()); break;
                    default: fail("invalid escape sequence");
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) fail("raw control character in string");
            out.push_back(c);
        }
        return out;
    }

    std::string parse_unicode_escape() {
        unsigned cp = parse_hex4();
        // Handle surrogate pairs.
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                pos_ += 2;
                unsigned low = parse_hex4();
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                    fail("invalid low surrogate");
                }
            } else {
                fail("unpaired high surrogate");
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("unpaired low surrogate");
        }
        return utf8_encode(cp);
    }

    unsigned parse_hex4() {
        if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid hex digit in \\u escape");
        }
        return value;
    }

    static std::string utf8_encode(unsigned cp) {
        std::string out;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    Json parse_number() {
        std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
            fail("invalid number");
        }
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                fail("digit expected after decimal point");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                fail("digit expected in exponent");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        std::string number(text_.substr(start, pos_ - start));
        return Json(std::strtod(number.c_str(), nullptr));
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

void escape_into(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void write_number(double d, std::string& out) {
    if (std::isnan(d) || std::isinf(d)) {
        out += "null";
        return;
    }
    if (d == static_cast<long long>(d) && std::fabs(d) < 9.0e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        out += buf;
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", d);
        out += buf;
    }
}

void write_value(const Json& value, std::string& out, int indent, int depth) {
    const bool pretty = indent > 0;
    const std::string pad = pretty ? std::string(static_cast<std::size_t>(indent * (depth + 1)), ' ') : "";
    const std::string pad_end = pretty ? std::string(static_cast<std::size_t>(indent * depth), ' ') : "";

    if (value.is_null()) { out += "null"; return; }
    if (value.is_bool()) { out += value.as_bool() ? "true" : "false"; return; }
    if (value.is_number()) { write_number(value.as_double(), out); return; }
    if (value.is_string()) { escape_into(value.as_string(), out); return; }

    if (value.is_array()) {
        const auto& arr = value.as_array();
        if (arr.empty()) { out += "[]"; return; }
        out += pretty ? "[\n" : "[";
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (pretty) out += pad;
            write_value(arr[i], out, indent, depth + 1);
            if (i + 1 < arr.size()) out += pretty ? ",\n" : ",";
        }
        out += pretty ? "\n" + pad_end + "]" : "]";
        return;
    }

    const auto& obj = value.as_object();
    if (obj.empty()) { out += "{}"; return; }
    out += pretty ? "{\n" : "{";
    std::size_t i = 0;
    for (const auto& [key, item] : obj) {
        if (pretty) out += pad;
        escape_into(key, out);
        out += pretty ? ": " : ":";
        write_value(item, out, indent, depth + 1);
        if (++i < obj.size()) out += pretty ? ",\n" : ",";
    }
    out += pretty ? "\n" + pad_end + "}" : "}";
}

}  // namespace

Json Json::parse(std::string_view text) {
    Parser parser(text);
    return parser.parse();
}

std::string Json::dump(int indent) const {
    std::string out;
    write_value(*this, out, indent, 0);
    return out;
}

}  // namespace trinity::core
