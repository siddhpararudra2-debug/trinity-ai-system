// Trinity — minimal JSON value, parser and writer.
// Dependency-free; sufficient for IPC frames, engine payloads and DB blobs.
// Object keys are stored sorted (std::map) which also canonicalises payloads
// for hashing (mirrors the Python cache key policy).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace trinity::core {

class Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json>;  // sorted keys

class Json {
public:
    using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool b) : value_(b) {}
    Json(int i) : value_(static_cast<double>(i)) {}
    Json(std::int64_t i) : value_(static_cast<double>(i)) {}
    Json(std::size_t i) : value_(static_cast<double>(i)) {}
    Json(double d) : value_(d) {}
    Json(const char* s) : value_(std::string(s)) {}
    Json(std::string s) : value_(std::move(s)) {}
    Json(std::string_view s) : value_(std::string(s)) {}
    Json(JsonArray a) : value_(std::move(a)) {}
    Json(JsonObject o) : value_(std::move(o)) {}

    static Json object() { return Json(JsonObject{}); }
    static Json array() { return Json(JsonArray{}); }

    // ------------------------------------------------------------ accessors
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<JsonArray>(value_); }
    bool is_object() const { return std::holds_alternative<JsonObject>(value_); }

    bool as_bool(bool fallback = false) const {
        return is_bool() ? std::get<bool>(value_) : fallback;
    }
    double as_double(double fallback = 0.0) const {
        return is_number() ? std::get<double>(value_) : fallback;
    }
    std::int64_t as_int(std::int64_t fallback = 0) const {
        return is_number() ? static_cast<std::int64_t>(std::get<double>(value_)) : fallback;
    }
    const std::string& as_string() const {
        static const std::string empty;
        return is_string() ? std::get<std::string>(value_) : empty;
    }
    const JsonArray& as_array() const {
        static const JsonArray empty;
        return is_array() ? std::get<JsonArray>(value_) : empty;
    }
    const JsonObject& as_object() const {
        static const JsonObject empty;
        return is_object() ? std::get<JsonObject>(value_) : empty;
    }

    // ------------------------------------------------------------ object api
    const Json* find(std::string_view key) const {
        if (!is_object()) return nullptr;
        const auto& obj = std::get<JsonObject>(value_);
        auto it = obj.find(std::string(key));
        return it == obj.end() ? nullptr : &it->second;
    }
    Json& operator[](const std::string& key) {
        if (!is_object()) value_ = JsonObject{};
        return std::get<JsonObject>(value_)[key];
    }
    bool contains(std::string_view key) const { return find(key) != nullptr; }

    void push_back(Json v) {
        if (!is_array()) value_ = JsonArray{};
        std::get<JsonArray>(value_).push_back(std::move(v));
    }

    // --------------------------------------------------------------- parse
    // Throws std::runtime_error with a position-annotated message on failure.
    static Json parse(std::string_view text);

    // --------------------------------------------------------------- write
    std::string dump(int indent = 0) const;

private:
    Value value_;
};

}  // namespace trinity::core
