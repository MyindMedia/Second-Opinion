#pragma once

// A deliberately small JSON reader and writer for the song builder's mood contract.
// No dependencies, no exceptions on bad input (parse returns nullopt), deterministic output.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace so::song::json
{

struct Value
{
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type { Type::Null };
    bool boolean { false };
    double number { 0.0 };
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;   // insertion order kept

    // Special members and factories are defined after the class. Clang with libstdc++ (C++20
    // constexpr containers) rejects the implicit ones, because they instantiate
    // std::vector<std::pair<std::string, Value>> while Value is still incomplete.
    Value();
    ~Value();
    Value (const Value&);
    Value (Value&&) noexcept;
    Value& operator= (const Value&);
    Value& operator= (Value&&) noexcept;

    static Value makeNumber (double d);
    static Value makeString (std::string s);
    static Value makeArray();
    static Value makeObject();

    bool isNumber() const noexcept { return type == Type::Number; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept  { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }

    /** Object member or nullptr. */
    const Value* find (std::string_view key) const;

    void set (std::string key, Value v);
};

inline Value::Value() = default;
inline Value::~Value() = default;
inline Value::Value (const Value&) = default;
inline Value::Value (Value&&) noexcept = default;
inline Value& Value::operator= (const Value&) = default;
inline Value& Value::operator= (Value&&) noexcept = default;

inline Value Value::makeNumber (double d) { Value v; v.type = Type::Number; v.number = d; return v; }
inline Value Value::makeString (std::string s) { Value v; v.type = Type::String; v.string = std::move (s); return v; }
inline Value Value::makeArray() { Value v; v.type = Type::Array; return v; }
inline Value Value::makeObject() { Value v; v.type = Type::Object; return v; }
inline void Value::set (std::string key, Value v) { object.emplace_back (std::move (key), std::move (v)); }

/** Parses one JSON document (leading and trailing whitespace allowed). Also accepts a document
    wrapped in a Markdown code fence, because language models often add one. */
std::optional<Value> parse (std::string_view text);

/** Serializes with 2-space indentation. Numbers print with up to 3 decimals, trailing zeros trimmed. */
std::string write (const Value& v, int indent = 0);

std::string escape (std::string_view s);

} // namespace so::song::json
